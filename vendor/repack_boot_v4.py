#!/usr/bin/env python3
"""bootv4 narrow repacker: replace kernel in an unsigned v4 boot image."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
import zlib

HDR = 4096
HDR_SIZE = 1584
MAGIC = b"ANDROID!"
KERN_MAGIC = b"ARM\x64"

# Fixed DT-probe diagnostic carrier. The cmdline field is Android boot header
# bytes [44:1580] (1536 bytes): NUL-terminated text followed by zero padding.
CMDLINE_START = 44
CMDLINE_END = 1580
CMDLINE_LEN = CMDLINE_END - CMDLINE_START
PROBE_VALUE = "pending" + "_" * 73          # 80 ASCII bytes
PROBE_PARAM_PROBE = "kshimreport.kshim_probe"
PROBE_PARAM_S0 = "kshimreport.kshim_s0"
# Exactly two ordinary cmdline parameters, single-space separated: 206 bytes.
PROBE_SUFFIX = (PROBE_PARAM_PROBE + "=" + PROBE_VALUE + " " +
                PROBE_PARAM_S0 + "=" + PROBE_VALUE).encode("ascii")
PROBE_CONFLICT_KEYS = frozenset((
    b"kshimreport.kshim_probe",
    b"kshimreport.kshim_s0",
    b"androidboot.kshim_probe",
    b"androidboot.kshim_s0",
))
PROBE_CAVEAT = (
    "DT-only diagnostic carrier: the ordinary kshimreport prefix is converted "
    "in-place by runtime into an equal-length androidboot prefix; real hardware "
    "report availability is unverified. It does not fix the boot menu or hardware."
)


def die(msg):
    raise SystemExit("error: " + msg)


def build_probe_field(field):
    """Validate the original cmdline field and return (new_field, original_text).

    Diagnostic mode only. Never normalises the original bytes: the text up to
    the first NUL is preserved verbatim (including whitespace) and the fixed
    206-byte suffix is appended with a single separator when text is nonempty.
    """
    if len(field) != CMDLINE_LEN:
        die("cmdline field length %d != %d" % (len(field), CMDLINE_LEN))
    nul = field.find(b"\x00")
    if nul < 0:
        die("original cmdline has no NUL terminator")
    if any(field[nul + 1:]):
        die("original cmdline has nonzero bytes after the first NUL")
    orig_text = bytes(field[:nul])
    for token in orig_text.split():
        key = token.split(b"=", 1)[0]
        if key in PROBE_CONFLICT_KEYS:
            die("original cmdline already defines %s"
                % key.decode("ascii", "replace"))
    new_text = (orig_text + b" " + PROBE_SUFFIX) if orig_text else PROBE_SUFFIX
    if len(new_text) > CMDLINE_LEN - 1:
        die("resulting cmdline text %d exceeds %d bytes"
            % (len(new_text), CMDLINE_LEN - 1))
    new_field = new_text + b"\x00" + b"\x00" * (CMDLINE_LEN - len(new_text) - 1)
    return new_field, orig_text


def probe_report_slots():
    return {
        "slots": {
            PROBE_PARAM_PROBE: PROBE_VALUE,
            PROBE_PARAM_S0: PROBE_VALUE,
        },
        "value_bytes": 80,
        "allowed_header_changes": [[8, 12], [CMDLINE_START, CMDLINE_END]],
        "hardware_tested": False,
        "caveat": PROBE_CAVEAT,
    }


def align(n, a=4096):
    return (n + a - 1) // a * a


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_file(path, what):
    if not os.path.isfile(path):
        die("%s not found: %s" % (what, path))
    with open(path, "rb") as f:
        return f.read()


def load_avbtool(path):
    if not os.path.isfile(path):
        die("avbtool not found: %s" % path)
    spec = importlib.util.spec_from_file_location("_avbtool_under_test", path)
    if spec is None or spec.loader is None:
        die("cannot load avbtool module from %s" % path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    saved = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(mod)
    except Exception as e:
        die("failed to import avbtool from %s: %s" % (path, e))
    finally:
        sys.dont_write_bytecode = saved
    return mod


def run_avbtool(avbtool, args):
    cmd = [sys.executable, "-B", avbtool] + list(args)
    return subprocess.run(cmd, capture_output=True, text=True)


def parse_avb_source(mod, image_path):
    handler = None
    try:
        handler = mod.ImageHandler(image_path, read_only=True)
        return mod.Avb()._parse_image(handler)
    except Exception as e:
        die("failed to parse AVB metadata in %s: %s" % (image_path, e))
    finally:
        if handler is not None:
            handler._image.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--original", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--avbtool", default=None)
    ap.add_argument("--dt-probe-report-slots", action="store_true")
    a = ap.parse_args()
    diagnostic = a.dt_probe_report_slots

    if os.path.realpath(a.output) == os.path.realpath(a.report):
        die("output and report paths are the same")

    for p, what in ((a.output, "output"), (a.report, "report")):
        if os.path.exists(p):
            die("refusing to overwrite existing %s: %s" % (what, p))

    orig = read_file(a.original, "original")
    kern = read_file(a.kernel, "kernel")

    if len(orig) < HDR:
        die("original smaller than 4096 bytes")
    if orig[0:8] != MAGIC:
        die("original magic is not ANDROID!")
    ks = struct.unpack_from("<I", orig, 8)[0]
    rs = struct.unpack_from("<I", orig, 12)[0]
    hs = struct.unpack_from("<I", orig, 20)[0]
    ver = struct.unpack_from("<I", orig, 40)[0]
    ss = struct.unpack_from("<I", orig, 1580)[0]
    if ks == 0:
        die("original kernel_size is zero")
    if rs != 0:
        die("original ramdisk_size is %d, expected 0" % rs)
    if hs != HDR_SIZE:
        die("original header_size is %d, expected %d" % (hs, HDR_SIZE))
    if ver != 4:
        die("original header version is %d, expected 4" % ver)
    if ss != 0:
        die("original signature_size is %d, expected 0" % ss)
    if HDR + ks > len(orig):
        die("original kernel exceeds image bounds")

    probe_field = None
    probe_orig_text = None
    if diagnostic:
        probe_field, probe_orig_text = build_probe_field(
            orig[CMDLINE_START:CMDLINE_END])

    footer_present = len(orig) >= 64 and orig[-64:-60] == b"AVBf"
    if diagnostic and not footer_present:
        die("--dt-probe-report-slots requires an AVB footer in the original")
    avb = {"footer_present": footer_present}
    limit = len(orig)
    avb_source = None
    mod = None
    hash_cls = None
    prop_cls = None

    if footer_present:
        _m, maj, mnr, ois, vbo, vbs, _r = struct.unpack_from(
            ">4sIIQQQ28s", orig, len(orig) - 64)
        if maj != 1:
            die("AVB footer major version %d, expected 1" % maj)
        need = HDR + align(ks)
        if ois < need:
            die("AVB original_image_size %d below required %d" % (ois, need))
        if vbo < ois:
            die("AVB vbmeta_offset %d below original_image_size %d" % (vbo, ois))
        if vbs == 0:
            die("AVB vbmeta_size is zero")
        if vbo + vbs > len(orig) - 64:
            die("AVB vbmeta range exceeds footer start")
        avb.update({
            "major": maj,
            "minor": mnr,
            "original_image_size": ois,
            "vbmeta_offset": vbo,
            "vbmeta_size": vbs,
        })

        if not a.avbtool:
            die("original image has an AVB footer; --avbtool <path> is required")

        mod = load_avbtool(a.avbtool)
        if not hasattr(mod, "Avb") or not hasattr(mod, "ImageHandler"):
            die("--avbtool does not expose Avb/ImageHandler: %s" % a.avbtool)
        hash_cls = getattr(mod, "AvbHashDescriptor", None)
        prop_cls = getattr(mod, "AvbPropertyDescriptor", None)
        if hash_cls is None or prop_cls is None:
            die("--avbtool missing AvbHashDescriptor/AvbPropertyDescriptor")

        src_footer, src_header, src_descs, _src_total = parse_avb_source(mod, a.original)
        if src_footer is None:
            die("official avbtool did not find an AVB footer in the original")
        if getattr(src_header, "flags", None) != 0:
            die("original AVB header flags %r, expected 0"
                % getattr(src_header, "flags", None))

        hash_descs = [d for d in src_descs if isinstance(d, hash_cls)]
        if len(hash_descs) != 1:
            die("original must carry exactly one AvbHashDescriptor, found %d"
                % len(hash_descs))
        old_hash = hash_descs[0]
        if getattr(old_hash, "partition_name", None) != "boot":
            die("original hash descriptor partition_name %r, expected 'boot'"
                % getattr(old_hash, "partition_name", None))
        old_flags = getattr(old_hash, "flags", 0)
        if old_flags not in (0, 1):
            die("original hash descriptor flags %d unsupported" % old_flags)
        if not old_hash.digest:
            die("original hash descriptor digest is empty")

        props = []
        for d in src_descs:
            if isinstance(d, hash_cls):
                continue
            if not isinstance(d, prop_cls):
                die("unsupported AVB descriptor in original: %s" % type(d).__name__)
            key = d.key
            if not isinstance(key, str):
                die("original property key is not a string")
            val = d.value
            if not isinstance(val, (bytes, bytearray)):
                die("original property %s value is not bytes" % key)
            if b"\x00" in bytes(val):
                die("original property %s value contains a null byte" % key)
            try:
                decoded = bytes(val).decode("utf-8")
            except UnicodeDecodeError:
                die("original property %s value is not valid utf-8" % key)
            props.append((key, decoded))

        avb_source = {
            "hash_algorithm": old_hash.hash_algorithm,
            "salt": bytes(old_hash.salt),
            "descriptor_flags": old_flags,
            "rollback_index": getattr(src_header, "rollback_index", 0),
            "rollback_index_location": getattr(src_header, "rollback_index_location", 0),
            "props": props,
        }
        avb["source"] = {
            "hash_algorithm": old_hash.hash_algorithm,
            "salt": bytes(old_hash.salt).hex(),
            "descriptor_flags": old_flags,
            "rollback_index": avb_source["rollback_index"],
            "rollback_index_location": avb_source["rollback_index_location"],
            "properties": [k for k, _ in props],
        }
    else:
        if a.avbtool:
            die("--avbtool was provided but the original has no AVB footer "
                "(unspecified layout)")

    if kern[:2] == b"\x1f\x8b":
        try:
            plain = gzip.decompress(kern)
        except (OSError, EOFError, zlib.error) as e:
            die("new kernel is not a complete gzip stream: %s" % e)
        if len(plain) < 64:
            die("new kernel gzip payload smaller than 64 bytes")
        if plain[56:60] != KERN_MAGIC:
            die("new kernel gzip payload missing ARM64 magic at offset 56")
        if struct.unpack_from("<Q", plain, 16)[0] != len(plain):
            die("new kernel gzip payload image_size %d != decompressed length %d"
                % (struct.unpack_from("<Q", plain, 16)[0], len(plain)))
    else:
        if len(kern) < 64:
            die("new kernel smaller than 64 bytes")
        if kern[56:60] != KERN_MAGIC:
            die("new kernel missing ARM64 magic at offset 56")

    new_len = HDR + len(kern)
    aligned = align(new_len)
    if aligned > limit:
        die("repacked image %d exceeds partition limit %d" % (aligned, limit))

    header = bytearray(orig[:HDR])
    struct.pack_into("<I", header, 8, len(kern))
    if diagnostic:
        header[CMDLINE_START:CMDLINE_END] = probe_field
    pad = aligned - new_len
    raw = bytes(header) + kern + b"\x00" * pad

    if len(raw) != aligned:
        die("internal length mismatch")
    if raw[:HDR] != bytes(header):
        die("output header does not match expected header")
    if diagnostic:
        if (raw[:8] != orig[:8] or raw[12:CMDLINE_START] != orig[12:CMDLINE_START]
                or raw[CMDLINE_END:HDR] != orig[CMDLINE_END:HDR]):
            die("output header differs outside kernel_size/cmdline fields")
        if raw[CMDLINE_START:CMDLINE_END] != probe_field:
            die("output cmdline field does not match the diagnostic field")
        if probe_orig_text and raw[CMDLINE_START:CMDLINE_START + len(probe_orig_text)] != probe_orig_text:
            die("output cmdline lost the original prefix")
    elif raw[:8] != orig[:8] or raw[12:HDR] != orig[12:HDR]:
        die("output header differs beyond kernel_size field")
    if raw[HDR:HDR + len(kern)] != kern:
        die("output kernel payload mismatch")
    if raw[HDR + len(kern):] != b"\x00" * pad:
        die("output padding is not zero")
    if len(raw) >= 64 and raw[-64:-60] == b"AVBf":
        die("output unexpectedly ends with an AVB footer")

    if avb_source is not None:
        max_run = run_avbtool(a.avbtool, [
            "add_hash_footer",
            "--partition_size", str(len(orig)),
            "--calc_max_image_size",
        ])
        if max_run.returncode != 0:
            die("avbtool --calc_max_image_size failed: %s"
                % (max_run.stderr.strip() or max_run.stdout.strip()))
        try:
            max_payload = int(max_run.stdout.strip().splitlines()[-1].strip())
        except (ValueError, IndexError):
            die("could not parse avbtool --calc_max_image_size output: %r"
                % max_run.stdout)
        if aligned > max_payload:
            die("repacked image %d exceeds avbtool max payload %d"
                % (aligned, max_payload))

    owned = False
    final_len = len(raw)
    try:
        with open(a.output, "xb") as f:
            f.write(raw)
        owned = True

        if avb_source is not None:
            args = [
                "add_hash_footer",
                "--image", a.output,
                "--partition_size", str(len(orig)),
                "--partition_name", "boot",
                "--algorithm", "NONE",
                "--hash_algorithm", avb_source["hash_algorithm"],
                "--salt", avb_source["salt"].hex(),
                "--rollback_index", str(avb_source["rollback_index"]),
                "--rollback_index_location", str(avb_source["rollback_index_location"]),
            ]
            if avb_source["descriptor_flags"] & 1:
                args.append("--do_not_use_ab")
            for key, value in avb_source["props"]:
                args += ["--prop", "%s:%s" % (key, value)]

            run = run_avbtool(a.avbtool, args)
            if run.returncode != 0:
                die("avbtool add_hash_footer failed: %s"
                    % (run.stderr.strip() or run.stdout.strip()))

            final = read_file(a.output, "output")
            final_len = len(final)
            if final_len != len(orig):
                die("final output length %d != original partition length %d"
                    % (final_len, len(orig)))
            if final[:len(raw)] != raw:
                die("final output data prefix differs from expected raw image")

            fin_footer, fin_header, fin_descs, _fin_total = parse_avb_source(
                mod, a.output)
            if fin_footer is None:
                die("final output has no AVB footer")
            if fin_footer.original_image_size != aligned:
                die("final footer original_image_size %d != %d"
                    % (fin_footer.original_image_size, aligned))
            fin_hashes = [d for d in fin_descs if isinstance(d, hash_cls)]
            if len(fin_hashes) != 1:
                die("final output must carry exactly one hash descriptor, found %d"
                    % len(fin_hashes))
            if fin_hashes[0].partition_name != "boot":
                die("final hash descriptor partition_name %r != 'boot'"
                    % fin_hashes[0].partition_name)
            if getattr(fin_header, "algorithm_type", None) != 0:
                die("final header algorithm_type %r != NONE(0)"
                    % getattr(fin_header, "algorithm_type", None))
            if getattr(fin_header, "flags", None) != 0:
                die("final header flags %r != 0" % getattr(fin_header, "flags", None))
            fin_props = sorted((d.key, d.value) for d in fin_descs
                               if isinstance(d, prop_cls))
            exp_props = sorted((k, v.encode("utf-8")) for k, v in avb_source["props"])
            if fin_props != exp_props:
                die("final property descriptors differ from original")

        report = {
            "source": a.original,
            "kernel": a.kernel,
            "output": a.output,
            "original_length": len(orig),
            "old_kernel_size": ks,
            "new_kernel_length": len(kern),
            "output_length": final_len,
            "partition_limit": limit,
            "sha256_original": sha256(orig),
            "sha256_new_kernel": sha256(kern),
            "sha256_output": sha256_file(a.output),
            "old_avb": avb,
            "header_version": 4,
            "unsigned": True,
            "no_device_validation": True,
            "avb": {
                "footer_present": avb_source is not None,
                "metadata_algorithm": "NONE" if avb_source is not None else None,
            },
            "warning": "requires unlocked bootloader; hardware untested",
        }
        if diagnostic:
            report["dt_probe_report_slots"] = probe_report_slots()
        with open(a.report, "x", encoding="utf-8") as f:
            json.dump(report, f, indent=2, sort_keys=True)
            f.write("\n")
    except BaseException:
        if owned:
            try:
                os.remove(a.output)
            except OSError:
                pass
        raise

    print("wrote %s (%d bytes)" % (a.output, final_len))
    print("wrote %s" % a.report)


if __name__ == "__main__":
    main()
