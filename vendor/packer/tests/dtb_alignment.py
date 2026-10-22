#!/usr/bin/env python3
"""Standalone regression test for MultiBootKernelPatcher DTB alignment.

Requires python3 (stdlib) and an external ``dtc`` on PATH.

Reproduces the original 4-byte DTB alignment bug: the payload DTB compiles to
a 163-byte blob whose length % 8 == 3, which previously produced wrongly
aligned DTB entries. Verifies that:

* DTB alignment defaults to exponent 3 (8-byte) when no Align key is given,
* explicit Align=3 and Align=4 are accepted and recorded,
* explicit Align=2 is rejected with a message containing ``8-byte`` and
  produces no output,
* non-DTB manifest entries keep the default exponent 2 (4-byte),
* DTB payload bytes in the packed image match the input DTBs in declaration
  order,
* unpack -> repack round-trips byte-for-byte with identical DTB alignment.

This is not a test-framework driver and not the compilation driver; the caller
compiles and runs it.
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
TYPE_DTB = 5

DEFAULT_DTB_ALIGN = 3
DEFAULT_OTHER_ALIGN = 2

PAYLOAD_MODEL = "kshim-payload-fixture"
PAYLOAD_BOOTARGS = "linux-only"
BASE_MODEL = "kshim-base-fixture"
BASE_BOOTARGS = "android-base-only"

PASSED = []


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)


def run(cmd, log_path=None):
    argv = [str(c) for c in cmd]
    if log_path is None:
        return subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with open(str(log_path), "wb") as log:
        return subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT)


def write_dts(path, model, bootargs):
    path.write_text(
        '/dts-v1/;\n'
        '/ {\n'
        '    model = "%s";\n'
        '    chosen { bootargs = "%s"; };\n'
        '};\n' % (model, bootargs)
    )


def compile_dtb(dts_path, dtb_path):
    proc = subprocess.run(
        [DTC, "-I", "dts", "-O", "dtb", "-o", str(dtb_path), str(dts_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    check(
        proc.returncode == 0,
        "dtc failed on %s: %s" % (dts_path, proc.stdout.decode("utf-8", "replace")),
    )


def build_base_image(path):
    buf = bytearray(4096)
    struct.pack_into("<I", buf, 0, 0x14000010)   # code0
    struct.pack_into("<I", buf, 4, 0xD503201F)   # code1
    struct.pack_into("<Q", buf, 8, 0)            # text_offset
    struct.pack_into("<Q", buf, 16, 4096)        # image_size
    struct.pack_into("<I", buf, 56, 0x644D5241)  # magic
    path.write_bytes(bytes(buf))


def build_shim(path):
    path.write_bytes(bytes(4 * 1024 * 1024))


def build_config(cfg_path, shim, out_bin, base, dtb1, dtb2, align):
    lines = [
        "[Pack]",
        "Shim=%s" % shim,
        "Output=%s" % out_bin,
        "Default=Image-A",
        "Timeout=0",
        "",
        "[Image-A]",
        "Name=Base",
        "Path=%s" % base,
        "Type=Linux",
        "BaseImage=true",
        "DeviceTreeBlob=Image-DTB2",
        "",
        "[Image-DTB1]",
        "Name=First",
        "Path=%s" % dtb1,
        "Type=DTB",
        "BaseImage=false",
    ]
    if align is not None:
        lines.append("Align=%d" % align)
    lines.extend([
        "",
        "[Image-DTB2]",
        "Name=Second",
        "Path=%s" % dtb2,
        "Type=DTB",
        "BaseImage=false",
    ])
    if align is not None:
        lines.append("Align=%d" % align)
    lines.extend([
        "",
        "[Manifest]",
        "Type=Manifest",
        "",
    ])
    cfg_path.write_text("\n".join(lines))


def parse_manifest(data):
    ptr = struct.unpack_from("<Q", data, MANIFEST_PTR_OFFSET)[0]
    check(ptr + MANIFEST_HEADER_SIZE <= len(data), "manifest offset out of range")
    magic = struct.unpack_from("<I", data, ptr)[0]
    check(magic == MANIFEST_MAGIC, "bad manifest magic 0x%08X" % magic)
    count = struct.unpack_from("<I", data, ptr + 8)[0]
    entries = []
    cur = ptr + MANIFEST_HEADER_SIZE
    for _ in range(count):
        offset = struct.unpack_from("<Q", data, cur + 0)[0]
        size = struct.unpack_from("<Q", data, cur + 8)[0]
        etype = struct.unpack_from("<I", data, cur + 16)[0]
        flags = struct.unpack_from("<I", data, cur + 20)[0]
        loadaddress = struct.unpack_from("<Q", data, cur + 24)[0]
        entryoffset = struct.unpack_from("<I", data, cur + 32)[0]
        dtindex = struct.unpack_from("<I", data, cur + 36)[0]
        alignment = struct.unpack_from("<I", data, cur + 40)[0]
        copysizemax = struct.unpack_from("<Q", data, cur + 44)[0]
        namelen = struct.unpack_from("<I", data, cur + 52)[0]
        name = data[cur + 56:cur + 56 + namelen].decode("utf-8", "replace")
        entries.append({
            "offset": offset,
            "size": size,
            "type": etype,
            "flags": flags,
            "loadaddress": loadaddress,
            "entryoffset": entryoffset,
            "dtindex": dtindex,
            "alignment": alignment,
            "copysizemax": copysizemax,
            "name": name,
        })
        cur += (56 + namelen + 3) & ~3
    return entries


def run_all(packer, unpacker, out):
    dts1 = out / "payload.dts"
    dts2 = out / "base.dts"
    dtb1 = out / "payload.dtb"
    dtb2 = out / "base.dtb"
    base_img = out / "base_image.bin"
    shim = out / "shim.bin"

    write_dts(dts1, PAYLOAD_MODEL, PAYLOAD_BOOTARGS)
    write_dts(dts2, BASE_MODEL, BASE_BOOTARGS)
    compile_dtb(dts1, dtb1)
    compile_dtb(dts2, dtb2)
    build_base_image(base_img)
    build_shim(shim)

    dtb1_bytes = dtb1.read_bytes()
    dtb2_bytes = dtb2.read_bytes()

    check(
        len(dtb1_bytes) % 8 == 3,
        "expected first DTB length %% 8 == 3, got %d (%d bytes)"
        % (len(dtb1_bytes) % 8, len(dtb1_bytes)),
    )
    check(
        len(dtb1_bytes) == 163,
        "expected known 163-byte payload DTB, got %d bytes" % len(dtb1_bytes),
    )

    cases = [
        {"name": "align_absent", "align": None, "expect_align": DEFAULT_DTB_ALIGN, "ok": True, "bin": "defaultcase.bin"},
        {"name": "align_3", "align": 3, "expect_align": 3, "ok": True, "bin": "align3.bin"},
        {"name": "align_4", "align": 4, "expect_align": 4, "ok": True, "bin": "align4.bin"},
        {"name": "align_2", "align": 2, "expect_align": None, "ok": False, "bin": "align2.bin"},
    ]

    for case in cases:
        cfg = out / ("%s.cfg" % case["name"])
        bin_path = out / case["bin"]
        log_path = out / ("%s.log" % case["name"])
        if bin_path.exists():
            bin_path.unlink()
        build_config(cfg, shim, bin_path, base_img, dtb1, dtb2, case["align"])
        rc = run([packer, cfg], log_path).returncode
        log_text = log_path.read_text(errors="replace")

        if case["ok"]:
            check(rc == 0, "case %s: packer exit %d\n%s" % (case["name"], rc, log_text))
            check(bin_path.is_file(), "case %s: no output produced" % case["name"])
            data = bin_path.read_bytes()
            entries = parse_manifest(data)
            dtb_entries = [e for e in entries if e["type"] == TYPE_DTB]
            check(
                len(dtb_entries) == 2,
                "case %s: expected 2 DTB entries, got %d" % (case["name"], len(dtb_entries)),
            )
            mod = 1 << case["expect_align"]
            for entry in dtb_entries:
                check(
                    entry["alignment"] == case["expect_align"],
                    "case %s: DTB entry %r alignment %d != %d"
                    % (case["name"], entry["name"], entry["alignment"], case["expect_align"]),
                )
                check(
                    entry["offset"] % mod == 0,
                    "case %s: DTB entry %r offset %d not %% %d"
                    % (case["name"], entry["name"], entry["offset"], mod),
                )
            for entry in entries:
                if entry["type"] != TYPE_DTB:
                    check(
                        entry["alignment"] == DEFAULT_OTHER_ALIGN,
                        "case %s: non-DTB entry %r alignment %d != %d"
                        % (case["name"], entry["name"], entry["alignment"], DEFAULT_OTHER_ALIGN),
                    )
            check(
                data[dtb_entries[0]["offset"]:dtb_entries[0]["offset"] + dtb_entries[0]["size"]] == dtb1_bytes,
                "case %s: first DTB payload bytes mismatch" % case["name"],
            )
            check(
                data[dtb_entries[1]["offset"]:dtb_entries[1]["offset"] + dtb_entries[1]["size"]] == dtb2_bytes,
                "case %s: second DTB payload bytes mismatch" % case["name"],
            )
            PASSED.append(case["name"])
        else:
            check(rc != 0, "case %s: expected failure but packer succeeded" % case["name"])
            check(
                "8-byte" in log_text,
                "case %s: expected '8-byte' in log, got:\n%s" % (case["name"], log_text),
            )
            check(
                not bin_path.exists(),
                "case %s: output produced despite rejection" % case["name"],
            )
            PASSED.append(case["name"])

    default_bin = out / "defaultcase.bin"
    check(default_bin.is_file(), "defaultcase.bin missing; align_absent case did not run")
    unpacked_dir = out / "unpacked"
    unpacked_dir.mkdir(parents=True, exist_ok=True)
    unpack_log = out / "unpack.log"
    rc = run([unpacker, default_bin, unpacked_dir], unpack_log).returncode
    check(rc == 0, "unpacker exit %d\n%s" % (rc, unpack_log.read_text(errors="replace")))

    unpacked_cfg = unpacked_dir / "config.cfg"
    check(unpacked_cfg.is_file(), "unpacker did not produce config.cfg")

    repack_log = out / "repack.log"
    rc = run([packer, unpacked_cfg], repack_log).returncode
    check(rc == 0, "repack exit %d\n%s" % (rc, repack_log.read_text(errors="replace")))

    repacked = unpacked_dir / "RepackedKernel"
    check(repacked.is_file(), "repack did not produce RepackedKernel")

    orig_data = default_bin.read_bytes()
    repack_data = repacked.read_bytes()
    check(orig_data == repack_data, "repacked image differs from original pack")

    orig_dtb = [e for e in parse_manifest(orig_data) if e["type"] == TYPE_DTB]
    repack_dtb = [e for e in parse_manifest(repack_data) if e["type"] == TYPE_DTB]
    check(len(orig_dtb) == len(repack_dtb), "DTB entry count changed after repack")
    for a, b in zip(orig_dtb, repack_dtb):
        check(
            a["alignment"] == b["alignment"],
            "DTB alignment changed after repack: %d vs %d" % (a["alignment"], b["alignment"]),
        )
    PASSED.append("unpack_repack")


def main():
    ap = argparse.ArgumentParser(
        description="MultiBootKernelPatcher DTB alignment regression test"
    )
    ap.add_argument("--packer", required=True)
    ap.add_argument("--unpacker", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    packer = Path(args.packer).resolve()
    unpacker = Path(args.unpacker).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    check(packer.exists(), "packer not found: %s" % packer)
    check(unpacker.exists(), "unpacker not found: %s" % unpacker)
    check(out.is_dir(), "out is not a directory: %s" % out)

    try:
        run_all(packer, unpacker, out)
    except Exception as exc:
        (out / "summary.json").write_text(
            json.dumps({"status": "fail", "passed": PASSED, "error": str(exc)}, indent=2)
        )
        raise

    summary = {
        "status": "pass",
        "passed": PASSED,
        "cases": [
            "align_absent",
            "align_3",
            "align_4",
            "align_2",
            "unpack_repack",
        ],
        "payload_dtb_length": len((out / "payload.dtb").read_bytes()),
        "base_dtb_length": len((out / "base.dtb").read_bytes()),
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2))
    for name in PASSED:
        print("PASS %s" % name)
    print("ALL PASS")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        sys.exit(1)
