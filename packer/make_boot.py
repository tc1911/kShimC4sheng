#!/usr/bin/env python3
"""Build a boot_a image carrying the kShimC menu + Android + Linux payloads.

Layout produced by MultiBootKernelPatcher:
  entry 0  stock Android kernel   (BaseImage, ABL supplies initrd/DT)
  entry 1  Linux Image            (raw ARM64 Image)
  entry 2  DTB                    (sheng.dtb, /chosen bootargs rewritten)
  entry 3  initrd                 (optional Blob)
  entry N  Shim                   (4 MiB runtime, appended by the packer)
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bootimg  # noqa: E402
import manifest  # noqa: E402
import fdt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_PACKER = os.environ.get("KSHIM_PACKER") or os.path.join(
    ROOT, "build", "packer",
    "MultiBootKernelPatcher" + (".exe" if os.name == "nt" else ""))
DEFAULT_PARTITION = 201326592  # sheng boot_a/b, 192 MiB
AVB_RESERVE = 64 * 1024
DTB_MAGIC = b"\xd0\x0d\xfe\xed"


def die(msg):
    print("error: %s" % msg, file=sys.stderr)
    raise SystemExit(1)


def tool(name):
    path = shutil.which(name)
    if not path:
        die("required tool not found: %s (install device-tree-compiler)" % name)
    return path


def read(path, label):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        die("%s: %s" % (label, e))
    if not data:
        die("%s is empty: %s" % (label, path))
    return data


def patch_dtb(src, dst, bootargs, initrd_index):
    """Edit /chosen with the pure-Python FDT code (works on Windows too)."""
    with open(src, "rb") as f:
        blob = f.read()
    try:
        tree = fdt.Fdt.parse(blob)
    except fdt.FdtError as e:
        die("%s: %s" % (src, e))
    tree.set("/chosen", "bootargs", bootargs.encode("utf-8") + b"\0")
    # sheng's Linux DTB does not carry armada,root-medium. Tag the packaged
    # Linux entry as internal so the menu's default-target resolver can
    # address it (otherwise its role stays UNKNOWN and is not a candidate).
    if tree.get("/chosen", "armada,root-medium") is None:
        tree.set("/chosen", "armada,root-medium", b"internal\0")
    if initrd_index is not None:
        tree.set("/chosen", "kshim,initrd-entry", int(initrd_index).to_bytes(4, "big"))
    else:
        tree.delete("/chosen", "kshim,initrd-entry")
    for prop in ("linux,initrd-start", "linux,initrd-end"):
        tree.delete("/chosen", prop)
    tree.write(dst)

    with open(dst, "rb") as f:
        check = fdt.Fdt.parse(f.read())
    got = check.get("/chosen", "bootargs")
    if got != bootargs.encode("utf-8") + b"\0":
        die("DTB /chosen bootargs mismatch: %r" % got)


def write_config(path, args, staged):
    blocks = [
        "[Pack]",
        "Shim = %s" % args.runtime,
        "Output = %s" % staged["packed"],
        "Default = %s" % args.default_section,
        "Timeout = %d" % args.timeout_ms,
        "",
        "[Image-00]",
        "Name = Android",
        "Path = %s" % staged["android"],
        "Type = Linux",
        "BaseImage = true",
        "Align = 21",
        "",
        "[Image-01]",
        "Name = Linux",
        "Path = %s" % staged["linux"],
        "Type = Linux",
        "BaseImage = false",
        "Align = 21",
    ]
    if staged.get("dtb"):
        blocks += ["DeviceTreeBlob = Image-02", ""]
        blocks += [
            "[Image-02]",
            "Name = sheng-dtb",
            "Path = %s" % staged["dtb"],
            "Type = DTB",
            "BaseImage = false",
            "Align = 3",
            "",
        ]
    if staged.get("initrd"):
        blocks += [
            "[Image-03]",
            "Name = initrd",
            "Path = %s" % staged["initrd"],
            "Type = Blob",
            "BaseImage = false",
            "Align = 12",
            "",
        ]
    blocks += ["[Manifest]", "Type = Manifest", ""]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(blocks))


def verify(packed, stock, staged, args):
    m = manifest.parse(packed)
    if m["image_size"] != len(packed):
        die("manifest image_size %d != packed size %d" % (m["image_size"], len(packed)))
    base = m["base_image"]
    # The manifest backs up the *stock* header slots it overwrites.
    for field, off in (("instructions", 0x00), ("image_size", 0x10),
                       ("manifest_offset", 0x20), ("manifest_size", 0x28),
                       ("shim_offset", 0x30)):
        want = bootimg.u64(stock["kernel"], off)
        if base[field] != want:
            die("base image backup %s 0x%x != stock header 0x%x"
                % (field, base[field], want))
    if bootimg.u64(stock["kernel"], 0x20) or bootimg.u64(stock["kernel"], 0x28) \
            or bootimg.u64(stock["kernel"], 0x30):
        die("stock kernel header slots 0x20/0x28/0x30 are not zero; "
            "extract the kernel from a factory boot image")

    exp = ["Android", "Linux"]
    if staged.get("dtb"):
        exp.append("sheng-dtb")
    if staged.get("initrd"):
        exp.append("initrd")
    names = [e["name"] for e in m["entries"]]
    if names[:len(exp)] != exp:
        die("manifest entries %r != expected %r" % (names, exp))
    if len(m["entries"]) != len(exp) + 1:
        die("expected %d entries (incl. Shim), got %d" % (len(exp) + 1, len(m["entries"])))

    android, linux = m["entries"][:2]
    if android["type_name"] != "Linux" or "BASE_IMAGE" not in android["flag_names"]:
        die("entry 0 is not a Linux base image")
    if "EXECUTABLE" not in android["flag_names"]:
        die("entry 0 is not executable")
    if linux["type_name"] != "Linux":
        die("entry 1 is not a Linux image")
    if staged.get("dtb"):
        if "HAS_DTB" not in linux["flag_names"]:
            die("entry 1 has no DTB reference")
        if linux["dtb_entry"] != 2:
            die("entry 1 DTB reference %d != 2" % linux["dtb_entry"])
        dtb_entry = m["entries"][2]
        if dtb_entry["type_name"] != "DTB":
            die("entry 2 is not a DTB")
        if packed[dtb_entry["offset"]:dtb_entry["offset"] + 4] != DTB_MAGIC:
            die("entry 2 payload is not an FDT")
        if dtb_entry["size"] != os.path.getsize(staged["dtb"]):
            die("DTB size mismatch")
    elif "HAS_DTB" in linux["flag_names"]:
        die("entry 1 unexpectedly carries a DTB")
    if staged.get("initrd"):
        initrd_entry = m["entries"][len(exp) - 1]
        if initrd_entry["type_name"] != "Blob" or initrd_entry["size"] != os.path.getsize(staged["initrd"]):
            die("initrd entry mismatch")

    shim = m["entries"][-1]
    if shim["type_name"] != "Shim" or "EXECUTABLE" not in shim["flag_names"]:
        die("last entry is not an executable Shim")
    if shim["offset"] != m["shim_offset"]:
        die("shim offset mismatch")
    if shim["size"] != os.path.getsize(args.runtime):
        die("shim size %d != runtime %d" % (shim["size"], os.path.getsize(args.runtime)))
    if m["timeout_ms"] != args.timeout_ms:
        die("timeout mismatch")
    if m["default_entry"] != (0 if args.default_section == "Image-00" else 1):
        die("default entry mismatch")
    for e in m["entries"]:
        if e["offset"] + e["size"] > m["manifest_offset"]:
            die("entry %s overlaps manifest" % e["name"])
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stock-boot", required=True, help="stock boot_a image/dump")
    ap.add_argument("--distro-boot", help="community Linux boot image (Image.gz + DTB [+ initrd])")
    ap.add_argument("--linux-image", help="raw ARM64 Linux Image")
    ap.add_argument("--dtb", help="sheng DTB (required unless --no-dtb)")
    ap.add_argument("--no-dtb", action="store_true",
                    help="diagnostic: omit the DTB entry and pass the ABL DT through")
    ap.add_argument("--initrd", help="optional initramfs/initrd image")
    ap.add_argument("--runtime", required=True, help="4 MiB kShimC runtime")
    ap.add_argument("--out", required=True, help="output boot image path")
    ap.add_argument("--default", choices=("android", "linux"), default="android")
    ap.add_argument("--timeout-ms", type=int, default=8000)
    ap.add_argument("--bootargs", default="root=PARTLABEL=linux rw rootwait")
    ap.add_argument("--partition-size", type=int, default=DEFAULT_PARTITION)
    ap.add_argument("--packer", default=DEFAULT_PACKER)
    ap.add_argument("--work", default=os.path.join(ROOT, "out", "work"))
    ap.add_argument("--pad-to", type=int, help="pad the boot image to this size")
    ap.add_argument("--keep-work", action="store_true")
    args = ap.parse_args()
    for name in ("stock_boot", "distro_boot", "linux_image", "dtb", "initrd", "runtime",
                 "packer", "out", "work"):
        value = getattr(args, name)
        if value:
            setattr(args, name, os.path.abspath(value))

    if args.distro_boot and (args.linux_image or args.dtb or args.initrd):
        die("--distro-boot cannot be combined with --linux-image/--dtb/--initrd")
    distro_tmp = None
    if args.distro_boot:
        import extract
        tmp = tempfile.mkdtemp(prefix="kshim-distro-")
        distro_tmp = tmp
        try:
            got = extract.extract(args.distro_boot, tmp, "linux")
        except Exception as e:
            die("cannot extract %s: %s" % (args.distro_boot, e))
        if "dtb" not in got:
            die("distro boot image has no appended DTB; pass --linux-image/--dtb instead")
        args.linux_image = got["kernel"]["path"]
        args.dtb = got["dtb"]["path"]
        if "initrd" in got:
            args.initrd = got["initrd"]["path"]
    if not args.linux_image:
        die("--linux-image is required unless --distro-boot")
    if args.no_dtb and args.dtb:
        die("--dtb and --no-dtb are mutually exclusive")
    if not args.no_dtb and not args.dtb:
        die("--dtb is required unless --no-dtb")
    if args.initrd and args.no_dtb:
        die("--initrd requires a DTB (kshim,initrd-entry lives there)")
    args.default_section = "Image-00" if args.default == "android" else "Image-01"
    if not os.path.exists(args.packer):
        die("packer not built: %s (run packer/build_packer.sh)" % args.packer)
    if args.timeout_ms <= 0:
        die("--timeout-ms must be positive")

    stock_data = read(args.stock_boot, "stock boot")
    try:
        stock = bootimg.parse(stock_data)
        bootimg.assert_raw_arm64_image(stock["kernel"], "stock kernel")
    except bootimg.BootImageError as e:
        die(str(e))
    if stock["version"] not in (3, 4):
        die("expected a GKI boot v3/v4 stock image, got v%d" % stock["version"])
    linux_image = read(args.linux_image, "Linux Image")
    if linux_image[:2] == b"\x1f\x8b":
        # Accept a kernel-build Image.gz directly; --dtb is still explicit.
        try:
            d = zlib.decompressobj(16 + zlib.MAX_WBITS)
            linux_image = d.decompress(linux_image) + d.flush()
        except zlib.error as e:
            die("cannot decompress Linux Image.gz: %s" % e)
        if not d.eof:
            die("truncated gzip stream in Linux Image.gz")
    try:
        bootimg.assert_raw_arm64_image(linux_image, "Linux Image")
    except bootimg.BootImageError as e:
        die(str(e))
    dtb = None
    if args.dtb:
        dtb = read(args.dtb, "DTB")
        if dtb[:4] != DTB_MAGIC:
            die("%s is not a DTB" % args.dtb)
    runtime = read(args.runtime, "runtime")
    if len(runtime) != 4 * 1024 * 1024:
        die("runtime must be exactly 4 MiB, got %d" % len(runtime))
    initrd = read(args.initrd, "initrd") if args.initrd else None

    work = args.work
    os.makedirs(work, exist_ok=True)
    staged = {
        "android": os.path.join(work, "stock-kernel.img"),
        "linux": os.path.join(work, "linux-Image"),
        "packed": os.path.join(work, "packed-kernel.bin"),
    }
    for src, dst in ((stock["kernel"], staged["android"]), (linux_image, staged["linux"])):
        with open(dst, "wb") as f:
            f.write(src)
    if dtb:
        staged["dtb"] = os.path.join(work, "sheng.dtb")
    if initrd:
        staged["initrd"] = os.path.join(work, "initrd.img")
        with open(staged["initrd"], "wb") as f:
            f.write(initrd)

    initrd_index = 3 if initrd else None
    if dtb:
        patch_dtb(args.dtb, staged["dtb"], args.bootargs, initrd_index)
    config = os.path.join(work, "pack.ini")
    write_config(config, args, staged)

    proc = subprocess.run([args.packer, config], capture_output=True, text=True)
    sys.stderr.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        die("MultiBootKernelPatcher failed (%d)" % proc.returncode)
    packed = read(staged["packed"], "packed kernel")
    m = verify(packed, stock, staged, args)

    total = stock["page_size"] + len(packed)
    limit = args.partition_size - AVB_RESERVE
    if total > limit:
        die("boot image %d B exceeds partition budget %d B (partition %d - AVB reserve %d)"
            % (total, limit, args.partition_size, AVB_RESERVE))
    pad = args.pad_to
    if pad is not None and pad < total:
        die("--pad-to %d smaller than image %d" % (pad, total))
    out = bootimg.build(stock, packed, pad_to=pad)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    report = {
        "output": os.path.abspath(args.out),
        "size": len(out),
        "partition_size": args.partition_size,
        "free": args.partition_size - len(out),
        "header_version": stock["version"],
        "page_size": stock["page_size"],
        "packed_kernel_size": len(packed),
        "sha256": hashlib.sha256(out).hexdigest(),
        "default": args.default,
        "timeout_ms": args.timeout_ms,
        "bootargs": args.bootargs if dtb else None,
        "initrd_index": initrd_index,
        "manifest": m,
    }
    with open(args.out + ".json", "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    if not args.keep_work:
        shutil.rmtree(work, ignore_errors=True)
    if distro_tmp:
        shutil.rmtree(distro_tmp, ignore_errors=True)
    print(json.dumps({k: report[k] for k in
                      ("output", "size", "free", "header_version", "sha256")},
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
