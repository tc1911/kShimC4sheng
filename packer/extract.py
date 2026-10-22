#!/usr/bin/env python3
"""Extract kernel / DTB / initrd from an Android boot image.

Handles the two payload styles used by Xiaomi Pad 6S Pro Linux builds:
  * stock GKI boot v4: raw ARM64 Image (kernel only)
  * community mkbootimg v0: Image.gz + appended DTB, optional initrd
"""

import argparse
import hashlib
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bootimg import BootImageError, assert_raw_arm64_image, parse  # noqa: E402

FDT_MAGIC = b"\xd0\x0d\xfe\xed"


def find_fdt(blob, at_start=False, at_end=False):
    """Return (offset, size) of a plausible FDT, optionally anchored."""
    best = None
    start = 0
    while True:
        i = blob.find(FDT_MAGIC, start)
        if i < 0:
            break
        if i + 8 <= len(blob):
            total = struct.unpack_from(">I", blob, i + 4)[0]
            ok = 0x30 <= total <= len(blob) - i
            if ok and at_start and i != 0:
                ok = False
            if ok and at_end and i + total != len(blob):
                ok = False
            if ok:
                best = (i, total)
        start = i + 1
    return best


def gunzip(blob):
    """Decompress the first gzip member; return (data, trailing_bytes)."""
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    out = d.decompress(blob)
    out += d.flush()
    if not d.eof:
        raise BootImageError("truncated gzip stream in kernel blob")
    return out, d.unused_data


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)
    return {"path": path, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def extract(boot_path, out_dir, prefix, stock=False):
    with open(boot_path, "rb") as f:
        image = parse(f.read())

    result = {"source": boot_path, "header_version": image["version"],
              "page_size": image["page_size"], "cmdline": image["cmdline"].decode("utf-8", "replace")}
    os.makedirs(out_dir, exist_ok=True)

    blob = image["kernel"]
    if stock:
        data = blob
        assert_raw_arm64_image(data, "stock kernel")
        result["kernel"] = write(os.path.join(out_dir, prefix + "-kernel.img"), data)
        end = image["kernel_offset"] + image["kernel_size"]
        if image["ramdisk_size"]:
            end = image["ramdisk_offset"] + image["ramdisk_size"]
        result["boot"] = write(os.path.join(out_dir, prefix + "-boot.img"),
                               image["raw"][:end])
    else:
        trailing = b""
        if blob[:2] == b"\x1f\x8b":
            data, trailing = gunzip(blob)
            result["compression"] = "gzip"
        else:
            data = blob
            result["compression"] = "none"
        # A DTB appended after a gzip member lands at the start of unused_data.
        # For a raw Image it must end exactly at the end of the kernel blob.
        fdt = find_fdt(trailing, at_start=True) if trailing else None
        if fdt is None and result["compression"] == "none":
            fdt = find_fdt(data, at_end=True)
        if fdt:
            off, size = fdt
            src = trailing if trailing else data
            result["dtb"] = write(os.path.join(out_dir, prefix + ".dtb"),
                                  src[off:off + size])
            if src is data:
                data = data[:off]
        assert_raw_arm64_image(data, "distro kernel")
        result["kernel"] = write(os.path.join(out_dir, prefix + "-Image"), data)

    if image["ramdisk_size"]:
        result["initrd"] = write(os.path.join(out_dir, prefix + "-initrd.img"),
                                 image["ramdisk"])
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--boot", required=True, help="boot image (or full partition dump)")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--prefix", default="payload", help="output file prefix")
    ap.add_argument("--stock", action="store_true",
                    help="stock kernel: require a raw ARM64 Image, no DTB split")
    args = ap.parse_args()
    try:
        print(json.dumps(extract(args.boot, args.out, args.prefix, args.stock),
                         indent=2, ensure_ascii=False))
    except (BootImageError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
