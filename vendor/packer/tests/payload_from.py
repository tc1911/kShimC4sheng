#!/usr/bin/env python3
"""Standalone regression test for the PayloadFrom (aliased internal Linux) contract.

Requires python3 (stdlib) and an external ``dtc`` on PATH. This is a contract
regression test for the ``PayloadFrom`` config key: its positive cases fail
unless the feature is implemented, while the negative cases assert the
rejection contract. It is not a build driver; the caller builds and runs it.

Contract under test (no manifest ABI change):

* One Linux base image (``Image-TF``) declared with ``Path``.
* One Linux aliased image (``Image-Internal``) declared with
  ``PayloadFrom=Image-TF`` and NO ``Path``; it has its own name and its own
  ``DeviceTreeBlob``.
* ``PayloadFrom`` accepts ONLY a unique Linux section with ``BaseImage=true``.
* The alias may not be the base, may not use ``CopyTo``/``CopySizeMax``, may not
  target a missing/self/another-alias/non-base/DTB section, and may not carry a
  ``Path`` (not even an empty one).
* ``EntryOffset`` absent defaults to 4, explicit 4 is allowed, 0/8 are rejected.
* The alias reuses the base payload region (offset 0, size == base size); no
  second copy of the kernel bytes is stored.
* Unpack resolves the alias through ``PayloadFrom`` (not a duplicated ``Path``)
  and pack -> unpack -> repack round-trips byte-for-byte.
* A malformed alias record in an otherwise valid packed image is rejected by the
  actual UnpackTool.
"""

import argparse
import json
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
SHIM_SIZE = 4 * 1024 * 1024
BODY_MARKER = b"ARMADA_PAYLOAD_BODY_7f3a2c"
DT_ALIGN_MIN = 3

BASE_DTS_MODEL = "armada-tf-base"
BASE_DTS_BOOTARGS = "armada-root=tf"
ALIAS_DTS_MODEL = "armada-internal"
ALIAS_DTS_BOOTARGS = "armada-root=internal"

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


def build_base_image(path):
    """4096-byte valid arm64 Linux Image: code0 NOP, code1 branch."""
    buf = bytearray(BASE_SIZE)
    struct.pack_into("<I", buf, 0, 0xD503201F)   # code0: NOP
    struct.pack_into("<I", buf, 4, 0x14000005)   # code1: branch
    struct.pack_into("<Q", buf, 8, 0)            # text_offset
    struct.pack_into("<Q", buf, 16, BASE_SIZE)   # image_size
    struct.pack_into("<I", buf, 56, 0x644D5241)  # magic
    buf[512:512 + len(BODY_MARKER)] = BODY_MARKER
    path.write_bytes(bytes(buf))


def build_shim(path):
    path.write_bytes(bytes(SHIM_SIZE))


# --------------------------------------------------------------------------- #
# Manifest / image parsing
# --------------------------------------------------------------------------- #
def parse_manifest(data):
    ptr = struct.unpack_from("<Q", data, MANIFEST_PTR_OFFSET)[0]
    check(ptr + MANIFEST_HEADER_SIZE <= len(data), "manifest offset out of range")
    check(
        struct.unpack_from("<I", data, ptr)[0] == MANIFEST_MAGIC,
        "bad manifest magic",
    )
    header = {
        "entryCount": struct.unpack_from("<I", data, ptr + 8)[0],
        "defaultEntry": struct.unpack_from("<I", data, ptr + 12)[0],
        "timeoutMs": struct.unpack_from("<I", data, ptr + 16)[0],
        "instructions": struct.unpack_from("<Q", data, ptr + 20)[0],
        "shimOffset": struct.unpack_from("<Q", data, ptr + 52)[0],
    }
    entries = []
    cur = ptr + MANIFEST_HEADER_SIZE
    for _ in range(header["entryCount"]):
        e = {"rec": cur}
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


def image_span(data):
    return (
        struct.unpack_from("<Q", data, 0x20)[0],  # manifest offset
        struct.unpack_from("<Q", data, 0x28)[0],  # manifest size
        struct.unpack_from("<Q", data, 0x30)[0],  # shim offset
    )


def entry_bytes(data, entry):
    return data[entry["offset"]:entry["offset"] + entry["size"]]


# --------------------------------------------------------------------------- #
# Config building
# --------------------------------------------------------------------------- #
def compose(shim, out_bin, default, sections):
    lines = [
        "[Pack]",
        "Shim=%s" % shim,
        "Output=%s" % out_bin,
        "Default=%s" % default,
        "Timeout=0",
        "",
    ]
    for header, body in sections:
        lines.append("[%s]" % header)
        lines.extend(body)
        lines.append("")
    return "\n".join(lines)


def alias_body(name="Internal", type_="Linux", base="false", path=None,
               payload_from="Image-TF", entryoffset=None,
               dtb="Image-DT-Internal", extra=()):
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
    return body


def base_section(base):
    return ("Image-TF", [
        "Name=TF",
        "Path=%s" % base,
        "Type=Linux",
        "BaseImage=true",
        "DeviceTreeBlob=Image-DT-Base",
    ])


def std_sections(base, dtb_base, dtb_int, alias=None):
    return [
        base_section(base),
        ("Image-Internal", alias if alias is not None else alias_body()),
        ("Image-DT-Base", ["Name=DTBase", "Path=%s" % dtb_base, "Type=DTB", "BaseImage=false"]),
        ("Image-DT-Internal", ["Name=DTInternal", "Path=%s" % dtb_int, "Type=DTB", "BaseImage=false"]),
        ("Manifest", ["Type=Manifest"]),
    ]


# --------------------------------------------------------------------------- #
# Positive case verification
# --------------------------------------------------------------------------- #
def verify_positive(data, dtb_base_bytes, dtb_int_bytes, expect_default_name):
    header, entries = parse_manifest(data)
    check(len(entries) == 5, "expected 5 manifest entries, got %d" % len(entries))

    base, alias, shim = entries[0], entries[1], entries[4]

    check(base["type"] == TYPE_LINUX and base["offset"] == 0,
          "base must be Linux at offset 0")
    check(base["size"] == BASE_SIZE, "base size %d != %d" % (base["size"], BASE_SIZE))
    check(base["flags"] == (FLAG_BASE | FLAG_EXEC | FLAG_HAS_DTB),
          "base flags 0x%X != BASE|EXEC|HAS_DTB" % base["flags"])
    check(header["instructions"] == 0x14000005d503201f,
          "header instructions 0x%X != 0x14000005D503201F" % header["instructions"])
    check(struct.unpack_from("<I", data, 4)[0] == 0x14000005,
          "base code1 branch word 0x%08X != 0x14000005"
          % struct.unpack_from("<I", data, 4)[0])

    check(alias["type"] == TYPE_LINUX, "alias must be a Linux entry")
    check(alias["offset"] == 0 and alias["size"] == BASE_SIZE,
          "alias must reuse base region exactly (offset 0, size %d)" % BASE_SIZE)
    check(alias["flags"] == (FLAG_EXEC | FLAG_HAS_DTB),
          "alias flags 0x%X != EXEC|HAS_DTB" % alias["flags"])
    check(not (alias["flags"] & (FLAG_BASE | FLAG_COPY | FLAG_QC)),
          "alias must not carry BASE/COPY/QC flags")
    check(alias["entryoffset"] == 4,
          "alias entryOffset %d != 4" % alias["entryoffset"])
    check(alias["load"] == 0 and alias["copysizemax"] == 0,
          "alias load/copySizeMax must be 0")
    check(alias["alignment"] == base["alignment"],
          "alias alignment %d != base alignment %d" % (alias["alignment"], base["alignment"]))

    check(shim["type"] == TYPE_SHIM and shim["offset"] == BASE_SIZE
          and shim["size"] == SHIM_SIZE,
          "attached shim layout wrong (offset %d size %d)" % (shim["offset"], shim["size"]))
    check(header["shimOffset"] == 0,
          "saved original shimOffset %d != 0" % header["shimOffset"])

    # Default must resolve to the right entry *by name*.
    check(0 <= header["defaultEntry"] < len(entries), "defaultEntry out of range")
    check(entries[header["defaultEntry"]]["name"] == expect_default_name,
          "defaultEntry %d name %r != %r"
          % (header["defaultEntry"], entries[header["defaultEntry"]]["name"], expect_default_name))

    # Two real DTs, each referenced by exactly one of base/alias, payload exact.
    dtbs = [e for e in entries if e["type"] == TYPE_DTB]
    check(len(dtbs) == 2, "expected 2 DTB entries, got %d" % len(dtbs))
    check(base["dtindex"] != alias["dtindex"], "base and alias must reference distinct DTs")
    check(entry_bytes(data, entries[base["dtindex"]]) == dtb_base_bytes,
          "base DT payload bytes mismatch")
    check(entry_bytes(data, entries[alias["dtindex"]]) == dtb_int_bytes,
          "alias DT payload bytes mismatch")
    for dt in dtbs:
        check(dt["alignment"] >= DT_ALIGN_MIN,
              "DT alignment %d < %d" % (dt["alignment"], DT_ALIGN_MIN))
        check(dt["offset"] % (1 << dt["alignment"]) == 0, "DT offset not aligned")

    # Aggregate layout span + no duplicated kernel.
    mo, ms, shim_off = image_span(data)
    check(mo + ms == len(data), "packed length %d != span %d" % (len(data), mo + ms))
    check(mo >= BASE_SIZE + SHIM_SIZE, "manifest offset %d before payload end" % mo)
    check(shim_off == BASE_SIZE, "image shimOffset %d != %d" % (shim_off, BASE_SIZE))
    for dt in dtbs:
        check(dt["offset"] >= BASE_SIZE + SHIM_SIZE and dt["offset"] < mo,
              "DT offset %d not in payload region" % dt["offset"])
    check(data.count(BODY_MARKER) == 1,
          "base body marker occurs %d times (duplicate kernel stored)" % data.count(BODY_MARKER))
    check(entry_bytes(data, alias) == entry_bytes(data, base),
          "alias region must equal base region exactly")


def run_positive(name, cfg_path, bin_path, default, alias, sections, base,
                 dtb_base, dtb_int, dtb_base_bytes, dtb_int_bytes, expect_name, out):
    def body():
        if bin_path.exists():
            bin_path.unlink()
        cfg_path.write_text(compose(out / "shim.bin", bin_path, default, sections))
        rc = run([PACKER, cfg_path], out / ("%s.log" % name)).returncode
        check(rc == 0, "packer exit %d\n%s"
              % (rc, (out / ("%s.log" % name)).read_text(errors="replace")))
        check(bin_path.is_file(), "no output produced")
        data = bin_path.read_bytes()
        verify_positive(data, dtb_base_bytes, dtb_int_bytes, expect_name)
    case(name, body)


# --------------------------------------------------------------------------- #
# Negative config cases
# --------------------------------------------------------------------------- #
def run_negative(name, sections, default, out):
    def body():
        bin_path = out / ("%s.bin" % name)
        if bin_path.exists():
            bin_path.unlink()
        cfg_path = out / ("%s.cfg" % name)
        cfg_path.write_text(compose(out / "shim.bin", bin_path, default, sections))
        rc = run([PACKER, cfg_path], out / ("%s.log" % name)).returncode
        check(rc != 0, "expected nonzero exit for %s, got 0" % name)
        check(not bin_path.exists(), "output produced despite rejection")
    case(name, body)


def negative_cases(base, dtb_base, dtb_int):
    def variant(alias, default="Image-TF"):
        return std_sections(base, dtb_base, dtb_int, alias=alias), default

    yield ("neg_missing_target", *variant(alias_body(payload_from="Image-Nope")))
    yield ("neg_self", *variant(alias_body(payload_from="Image-Internal")))
    yield ("neg_nonbase_target", *variant(alias_body(payload_from="Image-DT-Base")))
    yield ("neg_path_plus_payload", *variant(alias_body(path=base)))
    yield ("neg_empty_path_plus_payload", *variant(alias_body(path="")))
    yield ("neg_alias_base_true", *variant(alias_body(base="true")))
    yield ("neg_copy_to_and_size",
           *variant(alias_body(extra=["CopyTo=0x80000", "CopySizeMax=0x400000"])))
    yield ("neg_copy_size_only", *variant(alias_body(extra=["CopySizeMax=0x400000"])))
    yield ("neg_type_dtb", *variant(alias_body(type_="DTB", dtb=None)))
    yield ("neg_type_freeexec", *variant(alias_body(type_="FreeExec", dtb=None)))
    yield ("neg_entryoffset_0", *variant(alias_body(entryoffset="0x0")))
    yield ("neg_entryoffset_8", *variant(alias_body(entryoffset="0x8")))

    # Alias chain: a second alias targets the first alias instead of the base.
    chain = [
        base_section(base),
        ("Image-Internal", alias_body(payload_from="Image-TF")),
        ("Image-Internal2", alias_body(name="Internal2", payload_from="Image-Internal")),
        ("Image-DT-Base", ["Name=DTBase", "Path=%s" % dtb_base, "Type=DTB", "BaseImage=false"]),
        ("Image-DT-Internal", ["Name=DTInternal", "Path=%s" % dtb_int, "Type=DTB", "BaseImage=false"]),
        ("Manifest", ["Type=Manifest"]),
    ]
    yield ("neg_alias_chain", chain, "Image-TF")

    # Alias targets a valid non-base Linux section (own Path=base, Type=Linux,
    # BaseImage=false) rather than the unique base — rejected, keeping the real
    # base and both DT sections present.
    other_linux = [
        base_section(base),
        ("Image-Internal", alias_body(payload_from="Image-Other")),
        ("Image-Other", ["Name=Other", "Path=%s" % base, "Type=Linux", "BaseImage=false"]),
        ("Image-DT-Base", ["Name=DTBase", "Path=%s" % dtb_base, "Type=DTB", "BaseImage=false"]),
        ("Image-DT-Internal", ["Name=DTInternal", "Path=%s" % dtb_int, "Type=DTB", "BaseImage=false"]),
        ("Manifest", ["Type=Manifest"]),
    ]
    yield ("neg_nonbase_linux_target", other_linux, "Image-TF")


# --------------------------------------------------------------------------- #
# Round-trip and malformed-alias unpack rejection
# --------------------------------------------------------------------------- #
def roundtrip(packed_path, out):
    unpacked = out / "roundtrip"
    check(not unpacked.exists(), "roundtrip dir unexpectedly exists")
    unpacked.mkdir(parents=True)
    rc = run([UNPACKER, packed_path, unpacked], out / "roundtrip_unpack.log").returncode
    check(rc == 0, "unpacker exit %d\n%s"
          % (rc, (out / "roundtrip_unpack.log").read_text(errors="replace")))

    cfg = unpacked / "config.cfg"
    check(cfg.is_file(), "unpacker did not produce config.cfg")
    text = cfg.read_text(errors="replace")
    check("PayloadFrom=" in text, "alias must resolve through PayloadFrom, config:\n%s" % text)
    check("Path=Image-01-Linux" not in text,
          "alias emitted as a duplicate Path instead of PayloadFrom")

    files = sorted(p.name for p in unpacked.iterdir())
    check("Image-01-Linux" not in files, "duplicate alias payload extracted: %s" % files)
    check(len(files) == 5, "expected 5 unpacked files, got %s" % files)
    for required in ("BaseImage", "Shim.bin", "config.cfg"):
        check(required in files, "missing unpacked file %s" % required)

    rc = run([PACKER, cfg], out / "roundtrip_repack.log").returncode
    check(rc == 0, "repack exit %d\n%s"
          % (rc, (out / "roundtrip_repack.log").read_text(errors="replace")))
    repacked = unpacked / "RepackedKernel"
    check(repacked.is_file(), "repack did not produce RepackedKernel")
    check(repacked.read_bytes() == packed_path.read_bytes(),
          "repacked image differs from original pack byte-for-byte")


def write_mutation(orig, alias_rec, field_off, value, width, path):
    buf = bytearray(orig)
    struct.pack_into("<%s" % ("Q" if width == 8 else "I"), buf, alias_rec + field_off, value)
    path.write_bytes(bytes(buf))


def mutation_cases(orig, alias_rec, out):
    muts = [
        ("mut_alias_offset4", 0, 4, 8),        # partial overlap with base
        ("mut_alias_size4092", 8, 4092, 8),    # not exactly base size
        ("mut_alias_entryoffset0", 32, 0, 4),
        ("mut_alias_entryoffset8", 32, 8, 4),
        ("mut_alias_copy_flag", 20, FLAG_EXEC | FLAG_HAS_DTB | FLAG_COPY, 4),
        ("mut_alias_qc_flag", 20, FLAG_EXEC | FLAG_HAS_DTB | FLAG_QC, 4),
    ]
    for name, field_off, value, width in muts:
        def body(field_off=field_off, value=value, width=width, name=name):
            bin_path = out / ("%s.bin" % name)
            write_mutation(orig, alias_rec, field_off, value, width, bin_path)
            dest = out / name
            check(not dest.exists(), "%s dir unexpectedly exists" % name)
            rc = run([UNPACKER, bin_path, dest], out / ("%s.log" % name)).returncode
            check(rc != 0, "UnpackTool accepted malformed alias %s" % name)
        case(name, body)


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #
def build_fixtures(out):
    dts_base = out / "tf.dts"
    dts_int = out / "internal.dts"
    dtb_base = out / "tf.dtb"
    dtb_int = out / "internal.dtb"
    base = out / "base_image.bin"
    shim = out / "shim.bin"
    write_dts(dts_base, BASE_DTS_MODEL, BASE_DTS_BOOTARGS)
    write_dts(dts_int, ALIAS_DTS_MODEL, ALIAS_DTS_BOOTARGS)
    compile_dtb(dts_base, dtb_base)
    compile_dtb(dts_int, dtb_int)
    build_base_image(base)
    build_shim(shim)
    return dtb_base, dtb_int


def run_all(out):
    dtb_base, dtb_int = build_fixtures(out)
    base = out / "base_image.bin"
    dtb_base_bytes = dtb_base.read_bytes()
    dtb_int_bytes = dtb_int.read_bytes()

    # --- positive: base default, alias default, reordered + explicit EntryOffset.
    run_positive(
        "pos_tf_default", out / "tf_default.cfg", out / "tf_default.bin",
        "Image-TF", None, std_sections(base, dtb_base, dtb_int),
        base, dtb_base, dtb_int, dtb_base_bytes, dtb_int_bytes, "TF", out,
    )
    run_positive(
        "pos_internal_default", out / "internal_default.cfg", out / "internal_default.bin",
        "Image-Internal", None, std_sections(base, dtb_base, dtb_int),
        base, dtb_base, dtb_int, dtb_base_bytes, dtb_int_bytes, "Internal", out,
    )
    reordered = [
        ("Image-Internal", alias_body(entryoffset="0x4")),
        base_section(base),
        ("Image-DT-Base", ["Name=DTBase", "Path=%s" % dtb_base, "Type=DTB", "BaseImage=false"]),
        ("Image-DT-Internal", ["Name=DTInternal", "Path=%s" % dtb_int, "Type=DTB", "BaseImage=false"]),
        ("Manifest", ["Type=Manifest"]),
    ]
    run_positive(
        "pos_reordered_explicit_entry", out / "reordered.cfg", out / "reordered.bin",
        "Image-Internal", None, reordered,
        base, dtb_base, dtb_int, dtb_base_bytes, dtb_int_bytes, "Internal", out,
    )

    # --- negative config cases.
    for name, sections, default in negative_cases(base, dtb_base, dtb_int):
        run_negative(name, sections, default, out)

    # --- round-trip + malformed-alias unpack rejection from a valid alias pack.
    valid_bin = out / "internal_default.bin"

    def do_roundtrip():
        check(valid_bin.is_file(), "no valid alias package to round-trip")
        roundtrip(valid_bin, out)
    case("roundtrip_alias", do_roundtrip)

    def do_mutations():
        check(valid_bin.is_file(), "no valid alias package to mutate")
        orig = valid_bin.read_bytes()
        _, entries = parse_manifest(orig)
        alias = entries[1]
        check(alias["offset"] == 0 and alias["size"] == BASE_SIZE, "alias record not found")
        dest = out / "valid_unpack_probe"
        check(not dest.exists(), "probe dir unexpectedly exists")
        rc = run([UNPACKER, valid_bin, dest], out / "valid_unpack_probe.log").returncode
        check(rc == 0, "valid exact alias must be accepted (rc %d)" % rc)
        mutation_cases(orig, alias["rec"], out)
    case("unpack_valid_alias_allowed", do_mutations)


def main():
    global PACKER, UNPACKER
    ap = argparse.ArgumentParser(description="PayloadFrom alias regression test")
    ap.add_argument("--packer", required=True)
    ap.add_argument("--unpacker", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    PACKER = Path(args.packer).resolve()
    UNPACKER = Path(args.unpacker).resolve()
    out = Path(args.out).resolve()

    if out.exists():
        print("FAIL: out directory must not preexist: %s" % out, file=sys.stderr)
        return 1
    if not PACKER.exists():
        print("FAIL: packer not found: %s" % PACKER, file=sys.stderr)
        return 1
    if not UNPACKER.exists():
        print("FAIL: unpacker not found: %s" % UNPACKER, file=sys.stderr)
        return 1
    out.mkdir(parents=True)

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
        print("FAILED %d case(s): %s" % (len(failed), ", ".join(failed)), file=sys.stderr)
        return 1
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
