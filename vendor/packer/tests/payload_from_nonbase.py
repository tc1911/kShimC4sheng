#!/usr/bin/env python3
"""Regression test for the *non-base* ``PayloadFrom`` (shared real Linux payload).

Companion to ``payload_from.py`` (which keeps the original base-alias contract:
``PayloadFrom=Image-00``, offset 0, ``EntryOffset=4``). This script covers the
extended contract: a ``PayloadFrom`` alias may point at a *defined, non-alias,
real-Path* ``Type=Linux`` **non-base** source. It is intentionally a RED test for
the current base-only implementation: the positive cases fail until the feature
is implemented, while the negative cases assert the rejection contract.

Contract under test (no manifest ABI change):

* Source: one real ``Type=Linux`` non-base entry with ``Path`` and NO ``CopyTo``;
  its ``EntryOffset`` is 0.
* Alias: ``PayloadFrom=<source section>`` with NO ``Path`` (not even empty), no
  ``CopyTo``/``CopySizeMax``, never ``BaseImage=true``, ``Type=Linux``.
* The source must be defined *before* the alias, must be unique, and must be a
  real (Path-backed) Linux entry: forward refs, self refs, alias chains, missing
  targets and DTB targets are rejected.
* Alias ``EntryOffset`` defaults to 0; an explicit 0 is accepted; 4/8 are rejected.
* The three Linux entries (source + two aliases) share one offset/size; the file
  stores exactly one copy of the real Linux payload.
* Each Linux role carries its own independent ``DeviceTreeBlob``.
* ``unpack`` extracts the real Linux once and rewrites the aliases as
  ``PayloadFrom=Image-01`` (the real entry, not ``Image-00``); the Android base is
  restored byte-for-byte; pack -> unpack -> repack round-trips byte-for-byte.

Requires python3 (stdlib) and an external ``dtc`` on PATH. Not a build driver;
the caller builds the binaries and runs this script.
"""

import argparse
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

DTC = "dtc"

MANIFEST_MAGIC = 0x464D504B
MANIFEST_PTR_OFFSET = 0x20
MANIFEST_HEADER_SIZE = 60

TYPE_LINUX = 1
TYPE_SHIM = 3
TYPE_DTB = 5

FLAG_BASE = 1
FLAG_COPY = 2
FLAG_EXEC = 4
FLAG_HAS_DTB = 8
FLAG_QC = 16

BASE_SIZE = 4096
LINUX_SIZE = 8192
SHIM_SIZE = 4 * 1024 * 1024
BASE_MARKER = b"ANDROID_BASE_MARKER_5b1e"
LINUX_MARKER = b"LINUX_PAYLOAD_MARKER_9d7c"
DT_ALIGN_MIN = 3

DEFAULT_ENTRY_INDEX = 1
DEVICE_TIMEOUT_MS = 8000

RESULTS = []
PACKER = None
UNPACKER = None


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)


def run(cmd, log_path=None, timeout=180):
    argv = [str(c) for c in cmd]
    if log_path is None:
        return subprocess.run(
            argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout
        )
    with open(str(log_path), "wb") as log:
        return subprocess.run(
            argv, stdout=log, stderr=subprocess.STDOUT, timeout=timeout
        )


def case(name, fn):
    try:
        fn()
    except Exception as exc:  # noqa: BLE001 - report every independent case
        RESULTS.append((name, False, str(exc)))
        print("FAIL %s: %s" % (name, exc))
    else:
        RESULTS.append((name, True, ""))
        print("PASS %s" % name)


# --------------------------------------------------------------------------- #
# Fixtures
# --------------------------------------------------------------------------- #
def write_dts(path, model, bootargs):
    path.write_text(
        "/dts-v1/;\n/\n{\n"
        '    model = "%s";\n'
        '    chosen { bootargs = "%s"; };\n'
        "};\n" % (model, bootargs)
    )


def compile_dtb(dts_path, dtb_path):
    proc = run([DTC, "-I", "dts", "-O", "dtb", "-o", dtb_path, dts_path], timeout=60)
    check(
        proc.returncode == 0,
        "dtc failed on %s: %s" % (dts_path, proc.stdout.decode("utf-8", "replace")),
    )


def build_image(path, size, marker):
    """Valid 64-bit arm64 Linux Image of ``size`` bytes carrying ``marker``."""
    buf = bytearray(size)
    struct.pack_into("<I", buf, 0, 0xD503201F)   # code0: NOP
    struct.pack_into("<I", buf, 4, 0x14000005)   # code1: branch
    struct.pack_into("<Q", buf, 8, 0)            # text_offset
    struct.pack_into("<Q", buf, 16, size)        # image_size
    struct.pack_into("<I", buf, 56, 0x644D5241)  # magic "ARM\x64"
    buf[0x200:0x200 + len(marker)] = marker
    path.write_bytes(bytes(buf))


def build_shim(path):
    path.write_bytes(bytes(SHIM_SIZE))


# --------------------------------------------------------------------------- #
# Manifest parsing
# --------------------------------------------------------------------------- #
def parse_manifest(data):
    ptr = struct.unpack_from("<Q", data, MANIFEST_PTR_OFFSET)[0]
    check(ptr + MANIFEST_HEADER_SIZE <= len(data), "manifest offset out of range")
    check(
        struct.unpack_from("<I", data, ptr)[0] == MANIFEST_MAGIC, "bad manifest magic"
    )
    header = {
        "entryCount": struct.unpack_from("<I", data, ptr + 8)[0],
        "defaultEntry": struct.unpack_from("<I", data, ptr + 12)[0],
        "timeoutMs": struct.unpack_from("<I", data, ptr + 16)[0],
        "shimOffset": struct.unpack_from("<Q", data, ptr + 52)[0],
    }
    entries = []
    cur = ptr + MANIFEST_HEADER_SIZE
    for _ in range(header["entryCount"]):
        e = {}
        e["offset"] = struct.unpack_from("<Q", data, cur + 0)[0]
        e["size"] = struct.unpack_from("<Q", data, cur + 8)[0]
        e["type"] = struct.unpack_from("<I", data, cur + 16)[0]
        e["flags"] = struct.unpack_from("<I", data, cur + 20)[0]
        e["load"] = struct.unpack_from("<Q", data, cur + 24)[0]
        e["entryoffset"] = struct.unpack_from("<I", data, cur + 32)[0]
        e["dtindex"] = struct.unpack_from("<I", data, cur + 36)[0]
        e["alignment"] = struct.unpack_from("<I", data, cur + 40)[0]
        e["copysizemax"] = struct.unpack_from("<Q", data, cur + 44)[0]
        namelen = struct.unpack_from("<I", data, cur + 52)[0]
        e["name"] = data[cur + 56:cur + 56 + namelen].decode("utf-8", "replace")
        entries.append(e)
        cur += (56 + namelen + 3) & ~3
    return header, entries


def entry_bytes(data, entry):
    return data[entry["offset"]:entry["offset"] + entry["size"]]


# --------------------------------------------------------------------------- #
# Config building
# --------------------------------------------------------------------------- #
def compose(shim, out_bin, default, timeout, sections):
    lines = [
        "[Pack]",
        "Shim=%s" % shim,
        "Output=%s" % out_bin,
        "Default=%s" % default,
        "Timeout=%d" % timeout,
        "",
    ]
    for header, body in sections:
        lines.append("[%s]" % header)
        lines.extend(body)
        lines.append("")
    return "\n".join(lines)


def image(header, name, path=None, type_="Linux", base="false",
          payload_from=None, entryoffset=None, dtb=None, extra=()):
    body = ["Name=%s" % name]
    if path is not None:
        body.append("Path=%s" % path)
    if payload_from is not None:
        body.append("PayloadFrom=%s" % payload_from)
    body.append("Type=%s" % type_)
    body.append("BaseImage=%s" % base)
    if dtb is not None:
        body.append("DeviceTreeBlob=%s" % dtb)
    if entryoffset is not None:
        body.append("EntryOffset=%s" % entryoffset)
    body.extend(extra)
    return (header, body)


def good_alias_tf():
    # EntryOffset omitted -> default; source is the real non-base Linux Image-01.
    return image("Image-TF", "TF", payload_from="Image-01", dtb="Image-DT-TF")


def good_alias_diag():
    # EntryOffset explicit 0.
    return image(
        "Image-Diag", "Diag", payload_from="Image-01", entryoffset="0x0",
        dtb="Image-DT-Diag",
    )


def std_sections(base_path, linux_path, dtb_linux, dtb_tf, dtb_diag,
                 alias_tf=None, alias_diag=None):
    return [
        image("Image-00", "Android", path=base_path, base="true"),
        image("Image-01", "Payload", path=linux_path, dtb="Image-DT-Linux"),
        alias_tf if alias_tf is not None else good_alias_tf(),
        alias_diag if alias_diag is not None else good_alias_diag(),
        image("Image-DT-Linux", "DTLinux", path=dtb_linux, type_="DTB"),
        image("Image-DT-TF", "DTTF", path=dtb_tf, type_="DTB"),
        image("Image-DT-Diag", "DTDiag", path=dtb_diag, type_="DTB"),
        ("Manifest", ["Type=Manifest"]),
    ]


# --------------------------------------------------------------------------- #
# Positive case
# --------------------------------------------------------------------------- #
def verify_positive(data, dtb_linux_bytes, dtb_tf_bytes, dtb_diag_bytes):
    header, entries = parse_manifest(data)
    check(len(entries) == 8, "expected 8 manifest entries, got %d" % len(entries))

    base, linux, tf, diag, shim = (
        entries[0], entries[1], entries[2], entries[3], entries[7],
    )

    # Android base: Linux at offset 0, no custom DT.
    check(base["type"] == TYPE_LINUX and base["offset"] == 0,
          "base must be Linux at offset 0")
    check(base["size"] == BASE_SIZE, "base size %d != %d" % (base["size"], BASE_SIZE))
    check(base["flags"] == (FLAG_BASE | FLAG_EXEC),
          "base flags 0x%X != BASE|EXEC (Android base has no custom DT)" % base["flags"])

    # Source: real non-base Linux with its own Path/region, no COPY, EntryOffset 0.
    check(linux["type"] == TYPE_LINUX, "source must be a Linux entry")
    check(linux["offset"] > 0, "source must be a real appended payload, not offset 0")
    check(linux["size"] == LINUX_SIZE,
          "source size %d != %d" % (linux["size"], LINUX_SIZE))
    check(linux["entryoffset"] == 0,
          "source EntryOffset %d != 0" % linux["entryoffset"])
    check(not (linux["flags"] & (FLAG_BASE | FLAG_COPY | FLAG_QC)),
          "source must not carry BASE/COPY/QC flags (0x%X)" % linux["flags"])
    check(linux["flags"] & FLAG_HAS_DTB, "source must reference its own DT")
    check(linux["load"] == 0 and linux["copysizemax"] == 0,
          "source load/copySizeMax must be 0")

    # Aliases: default EntryOffset and explicit 0 both land at 0; share the source
    # region exactly; no Path (implied), no COPY/BASE/QC.
    for label, alias in (("TF", tf), ("Diag", diag)):
        check(alias["type"] == TYPE_LINUX, "%s alias must be Linux" % label)
        check(alias["entryoffset"] == 0,
              "%s alias EntryOffset %d != 0" % (label, alias["entryoffset"]))
        check(alias["offset"] == linux["offset"] and alias["size"] == linux["size"],
              "%s alias must reuse the source region exactly (off %d sz %d != off %d sz %d)"
              % (label, alias["offset"], alias["size"], linux["offset"], linux["size"]))
        check(alias["flags"] == (FLAG_EXEC | FLAG_HAS_DTB),
              "%s alias flags 0x%X != EXEC|HAS_DTB" % (label, alias["flags"]))
        check(alias["load"] == 0 and alias["copysizemax"] == 0,
              "%s alias load/copySizeMax must be 0" % label)
        check(alias["alignment"] == linux["alignment"],
              "%s alias alignment %d != source alignment %d"
              % (label, alias["alignment"], linux["alignment"]))

    # Only one copy of the real Linux payload in the file.
    check(data.count(LINUX_MARKER) == 1,
          "real Linux marker occurs %d times (duplicate payload stored)"
          % data.count(LINUX_MARKER))
    check(data.count(BASE_MARKER) == 1, "base marker duplicated")
    check(entry_bytes(data, tf) == entry_bytes(data, linux),
          "TF alias region must equal source region")
    check(entry_bytes(data, diag) == entry_bytes(data, linux),
          "Diag alias region must equal source region")

    # Device default resolves to manifest index 1; timeout is the device 8s value.
    check(header["defaultEntry"] == DEFAULT_ENTRY_INDEX,
          "defaultEntry %d != %d" % (header["defaultEntry"], DEFAULT_ENTRY_INDEX))
    check(entries[header["defaultEntry"]]["name"] == "Payload",
          "defaultEntry name %r != 'Payload'" % entries[header["defaultEntry"]]["name"])
    check(header["timeoutMs"] == DEVICE_TIMEOUT_MS,
          "timeoutMs %d != %d" % (header["timeoutMs"], DEVICE_TIMEOUT_MS))

    # Three independent DTBs, one per Linux role; base has none.
    check(base["dtindex"] == 0, "base must not reference a DT")
    check(len({linux["dtindex"], tf["dtindex"], diag["dtindex"]}) == 3,
          "each Linux role needs its own DT reference")
    for entry in (linux, tf, diag):
        target = entries[entry["dtindex"]]
        check(target["type"] == TYPE_DTB,
              "Linux role DT target must be a DTB entry")
    check(entry_bytes(data, entries[linux["dtindex"]]) == dtb_linux_bytes,
          "source DT payload mismatch")
    check(entry_bytes(data, entries[tf["dtindex"]]) == dtb_tf_bytes,
          "TF alias DT payload mismatch")
    check(entry_bytes(data, entries[diag["dtindex"]]) == dtb_diag_bytes,
          "Diag alias DT payload mismatch")
    for index in (4, 5, 6):
        dt = entries[index]
        check(dt["type"] == TYPE_DTB and dt["alignment"] >= DT_ALIGN_MIN,
              "DT entry %d alignment %d < %d" % (index, dt["alignment"], DT_ALIGN_MIN))
        check(dt["offset"] % (1 << dt["alignment"]) == 0,
              "DT entry %d offset not aligned" % index)

    check(shim["type"] == TYPE_SHIM and shim["size"] == SHIM_SIZE,
          "attached shim layout wrong")


def run_positive(name, cfg_path, bin_path, sections, out):
    def body():
        if bin_path.exists():
            bin_path.unlink()
        cfg_path.write_text(
            compose(out / "shim.bin", bin_path, "Image-01", DEVICE_TIMEOUT_MS, sections)
        )
        log_path = out / ("%s.log" % name)
        rc = run([PACKER, cfg_path], log_path).returncode
        check(rc == 0, "packer exit %d\n%s" % (rc, log_path.read_text(errors="replace")))
        check(bin_path.is_file(), "no output produced")
        data = bin_path.read_bytes()
        verify_positive(
            data,
            (out / "dt_linux.dtb").read_bytes(),
            (out / "dt_tf.dtb").read_bytes(),
            (out / "dt_diag.dtb").read_bytes(),
        )
    case(name, body)


# --------------------------------------------------------------------------- #
# Negative cases
# --------------------------------------------------------------------------- #
def run_negative(name, sections, out, default="Image-01"):
    def body():
        bin_path = out / ("%s.bin" % name)
        if bin_path.exists():
            bin_path.unlink()
        cfg_path = out / ("%s.cfg" % name)
        cfg_path.write_text(
            compose(out / "shim.bin", bin_path, default, DEVICE_TIMEOUT_MS, sections)
        )
        rc = run([PACKER, cfg_path], out / ("%s.log" % name)).returncode
        check(rc != 0, "expected nonzero exit for %s, got 0" % name)
        check(not bin_path.exists(), "output produced despite rejection")
    case(name, body)


def negative_cases(base_path, linux_path, dtb_linux, dtb_tf, dtb_diag):
    """Each case yields (name, sections, default)."""
    def std(alias_tf=None, alias_diag=None):
        return std_sections(
            base_path, linux_path, dtb_linux, dtb_tf, dtb_diag,
            alias_tf=alias_tf, alias_diag=alias_diag,
        )

    cases = [
        # Alias EntryOffset must be 0 (default or explicit); 4 and 8 are rejected.
        ("neg_alias_entryoffset4", std(
            alias_tf=image("Image-TF", "TF", payload_from="Image-01",
                           entryoffset="0x4", dtb="Image-DT-TF"))),
        ("neg_alias_entryoffset8", std(
            alias_tf=image("Image-TF", "TF", payload_from="Image-01",
                           entryoffset="0x8", dtb="Image-DT-TF"))),
        # Missing target section.
        ("neg_missing_target", std(
            alias_tf=image("Image-TF", "TF", payload_from="Image-Nope",
                           dtb="Image-DT-TF"))),
        # Self reference.
        ("neg_self_target", std(
            alias_tf=image("Image-TF", "TF", payload_from="Image-TF",
                           dtb="Image-DT-TF"))),
        # Alias chain: the second alias targets the first alias, not the source.
        ("neg_alias_chain", std(
            alias_diag=image("Image-Diag", "Diag", payload_from="Image-TF",
                             entryoffset="0x0", dtb="Image-DT-Diag"))),
        # DTB target is not a real Linux source.
        ("neg_dtb_target", std(
            alias_tf=image("Image-TF", "TF", payload_from="Image-DT-Linux",
                           dtb="Image-DT-TF"))),
        # Alias may not carry CopyTo/CopySizeMax.
        ("neg_copy_to", std(
            alias_tf=image("Image-TF", "TF", payload_from="Image-01",
                           dtb="Image-DT-TF",
                           extra=["CopyTo=0x80000", "CopySizeMax=0x400000"]))),
        # Alias may not carry a Path (not even empty).
        ("neg_path_mixed", std(
            alias_tf=image("Image-TF", "TF", path=base_path,
                           payload_from="Image-01", dtb="Image-DT-TF"))),
        ("neg_empty_path_mixed", std(
            alias_tf=image("Image-TF", "TF", path="",
                           payload_from="Image-01", dtb="Image-DT-TF"))),
    ]
    for name, sections in cases:
        yield (name, sections, "Image-01")

    # Forward reference: the alias is declared before its real Linux target.
    forward = [
        image("Image-00", "Android", path=base_path, base="true"),
        image("Image-TF", "TF", payload_from="Image-Late", dtb="Image-DT-TF"),
        image("Image-Late", "Payload", path=linux_path, dtb="Image-DT-Linux"),
        image("Image-DT-Linux", "DTLinux", path=dtb_linux, type_="DTB"),
        image("Image-DT-TF", "DTTF", path=dtb_tf, type_="DTB"),
        ("Manifest", ["Type=Manifest"]),
    ]
    yield ("neg_forward_target", forward, "Image-Late")


# --------------------------------------------------------------------------- #
# Unpack / repack round-trip
# --------------------------------------------------------------------------- #
def roundtrip(packed_path, base_path, linux_path, shim_path, dtb_paths, out):
    unpacked = out / "unpacked"
    if unpacked.exists():
        shutil.rmtree(unpacked)
    unpacked.mkdir(parents=True)
    log = out / "unpack.log"
    rc = run([UNPACKER, packed_path, unpacked], log).returncode
    check(rc == 0, "unpacker exit %d\n%s" % (rc, log.read_text(errors="replace")))

    cfg = unpacked / "config.cfg"
    check(cfg.is_file(), "unpacker did not produce config.cfg")
    text = cfg.read_text(errors="replace")

    # The real Linux is the source entry (Image-01); both aliases resolve to it,
    # never to Image-00.
    check(text.count("PayloadFrom=Image-01") == 2,
          "both aliases must resolve to PayloadFrom=Image-01, config:\n%s" % text)
    check("PayloadFrom=Image-00" not in text,
          "aliases must not fall back to the base image")

    files = sorted(p.name for p in unpacked.iterdir())
    check("Image-01-Linux" in files,
          "the real Linux payload must be extracted once: %s" % files)
    check("Image-02-Linux" not in files and "Image-03-Linux" not in files,
          "aliases must not be extracted as duplicate payloads: %s" % files)

    # Base Android restored byte-for-byte, plus Linux/shim/DT identical to input.
    check((unpacked / "BaseImage").read_bytes() == base_path.read_bytes(),
          "restored Android base differs from the input image")
    check((unpacked / "Image-01-Linux").read_bytes() == linux_path.read_bytes(),
          "restored real Linux payload differs from the input image")
    check((unpacked / "Shim.bin").read_bytes() == shim_path.read_bytes(),
          "restored shim differs from the input")
    for dtb_path in dtb_paths:
        name = "Image-%02d-DTB" % (4 + dtb_paths.index(dtb_path))
        check((unpacked / name).read_bytes() == dtb_path.read_bytes(),
              "restored %s differs from the input DT" % name)

    # pack -> unpack -> repack must reproduce the packed image byte-for-byte.
    rc = run([PACKER, cfg], out / "repack.log").returncode
    check(rc == 0, "repack exit %d\n%s"
          % (rc, (out / "repack.log").read_text(errors="replace")))
    repacked = unpacked / "RepackedKernel"
    check(repacked.is_file(), "repack did not produce RepackedKernel")
    check(repacked.read_bytes() == packed_path.read_bytes(),
          "repacked image differs from the original pack byte-for-byte")


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #
def build_fixtures(out):
    base = out / "base_image.bin"
    linux = out / "linux_payload.bin"
    shim = out / "shim.bin"
    build_image(base, BASE_SIZE, BASE_MARKER)
    build_image(linux, LINUX_SIZE, LINUX_MARKER)
    build_shim(shim)
    dtb_paths = []
    for stem, model, bootargs in (
        ("dt_linux", "armada-linux-payload", "armada-root=payload"),
        ("dt_tf", "armada-tf", "armada-root=tf"),
        ("dt_diag", "armada-diag", "armada-root=diag"),
    ):
        dts = out / ("%s.dts" % stem)
        dtb = out / ("%s.dtb" % stem)
        write_dts(dts, model, bootargs)
        compile_dtb(dts, dtb)
        dtb_paths.append(dtb)
    return base, linux, shim, dtb_paths


def run_all(out):
    base, linux, shim, dtb_paths = build_fixtures(out)
    dtb_linux, dtb_tf, dtb_diag = dtb_paths

    # Positive: Android base + real non-base Linux + two aliases + 3 DTs + shim.
    run_positive(
        "pos_nonbase_aliases",
        out / "nonbase.cfg",
        out / "nonbase.bin",
        std_sections(base, linux, dtb_linux, dtb_tf, dtb_diag),
        out,
    )

    # Negative contract.
    for name, sections, default in negative_cases(base, linux, dtb_linux, dtb_tf, dtb_diag):
        run_negative(name, sections, out, default)

    # Round-trip only if a valid package was produced.
    def do_roundtrip():
        packed = out / "nonbase.bin"
        check(packed.is_file(), "no valid non-base package to round-trip")
        roundtrip(packed, base, linux, shim, dtb_paths, out)
    case("roundtrip_nonbase", do_roundtrip)


def main():
    global PACKER, UNPACKER
    ap = argparse.ArgumentParser(description="PayloadFrom non-base regression test")
    ap.add_argument("--packer", required=True)
    ap.add_argument("--unpacker", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    PACKER = Path(args.packer).resolve()
    UNPACKER = Path(args.unpacker).resolve()
    out = Path(args.out).resolve()

    if not PACKER.exists():
        print("FAIL: packer not found: %s" % PACKER, file=sys.stderr)
        return 1
    if not UNPACKER.exists():
        print("FAIL: unpacker not found: %s" % UNPACKER, file=sys.stderr)
        return 1
    out.mkdir(parents=True, exist_ok=True)

    try:
        run_all(out)
    except Exception as exc:  # fixture/driver failure
        case("driver", lambda: (_ for _ in ()).throw(exc))

    failed = [name for name, ok, _ in RESULTS if not ok]
    (out / "summary.json").write_text(json.dumps(
        {"passed": [n for n, ok, _ in RESULTS if ok],
         "failed": [{"case": n, "error": e} for n, ok, e in RESULTS if not ok]},
        indent=2))
    if failed:
        print("FAILED %d case(s): %s" % (len(failed), ", ".join(failed)),
              file=sys.stderr)
        return 1
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
