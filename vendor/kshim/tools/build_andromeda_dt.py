#!/usr/bin/env python3
"""Build the pinned OEM SM8150 base trees and validate andromeda overlays.

The output bundle contains base DTBs only. ABL applies the device's existing
DTBO partition; this tool never writes a device or modifies mu_aloha assets.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import urllib.request

REVISION = "74347f5521fb444936f5986ff0937264307d78fe"
BASES = ("sm8150", "sm8150-v2", "sm8150p", "sm8150p-v2")
REVISIONS = ("", "-p1", "-p1_1", "-p2", "-p2_1", "-p2_2", "-p2_21", "-p2_22")
BOARD_IDS = (0x27, 0x28, 0x2b, 0x29, 0x2c, 0x2d, 0x2e, 0x2f)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_bundle(data):
    """Validate every concatenated DTB, rather than inspecting only its first root."""
    offset, members = 0, []
    while offset < len(data):
        if len(data) - offset < 40:
            raise ValueError("Truncated appended FDT header")
        magic, size = struct.unpack_from(">II", data, offset)
        if magic != 0xD00DFEED or size < 40 or size > len(data) - offset:
            raise ValueError(f"Invalid appended FDT at {offset:#x}")
        members.append(data[offset:offset + size])
        offset += size
    return members


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/andromeda-dt"))
    parser.add_argument("--source", type=Path)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tmp = out / "tmp"
    tmp.mkdir(exist_ok=True)
    source = args.source.resolve() if args.source else out / ("Xiaomi_Kernel_OpenSource-" + REVISION)
    archive = out / "kernel-source.tar.gz"
    if not source.exists() or (not args.source and not (source / ".kshim-source-complete").exists()):
        if args.source:
            raise ValueError("Source directory does not exist")
        if not archive.exists():
            url = f"https://codeload.github.com/MiCode/Xiaomi_Kernel_OpenSource/tar.gz/{REVISION}"
            print("Downloading pinned MiCode kernel source", flush=True)
            partial = archive.with_suffix(".partial")
            with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as target:
                shutil.copyfileobj(response, target)
            partial.replace(archive)
        def source_filter(member, destination):
            # Vendor audio headers include links into a separate kernel checkout.
            # They are not needed for DTB builds and must not escape this cache.
            try:
                return tarfile.data_filter(member, destination)
            except tarfile.LinkOutsideDestinationError:
                return None
        with tarfile.open(archive) as stream:
            stream.extractall(out, filter=source_filter)
        (source / ".kshim-source-complete").write_text(REVISION + "\n")
    obj = out / "obj"
    obj.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(tmp))

    def run(command, log):
        with (out / (log + ".log")).open("w") as output:
            subprocess.run([str(x) for x in command], env=env, stdout=output,
                           stderr=subprocess.STDOUT, check=True)

    make = ["make", "-C", source, f"O={obj}", "ARCH=arm64",
            "CROSS_COMPILE=aarch64-linux-gnu-", "CC=aarch64-linux-gnu-gcc",
            "HOSTCFLAGS=-O2 -fcommon", "CONFIG_SECURITY_SELINUX=n",
            "DTC_EXT=" + shutil.which("dtc")]
    run(make + ["andromeda_user_defconfig"], "defconfig")
    subprocess.run([str(source / "scripts/config"), "--file", str(obj / ".config"),
                    "--enable", "BUILD_ARM64_DT_OVERLAY"], check=True)
    run(make + ["olddefconfig"], "olddefconfig")
    run(make + [f"-j{args.jobs}", "dtbs"], "dtbs")
    qcom = obj / "arch/arm64/boot/dts/qcom"
    bundle = b"".join((qcom / (name + ".dtb")).read_bytes() for name in BASES)
    if len(parse_bundle(bundle)) != 4:
        raise ValueError("Expected four SM8150 base DTBs")
    (out / "android-andromeda-dtbs.bin").write_bytes(bundle)
    merged = out / "merged"
    merged.mkdir(exist_ok=True)
    checked = []
    for base in BASES:
        for revision, board_id in zip(REVISIONS, BOARD_IDS):
            overlay = qcom / ("andromeda-sm8150" + revision + "-overlay.dtbo")
            result = merged / (base + revision + ".dtb")
            run(["fdtoverlay", "-i", qcom / (base + ".dtb"), "-o", result, overlay],
                "merge-" + base + (revision or "-default"))
            # Qualcomm keeps selection metadata on the overlay root, outside
            # its fragments. fdtoverlay correctly does not copy that metadata
            # to the base tree. Validate identity and applied nodes separately.
            ids = subprocess.check_output(["fdtget", "-t", "x", overlay, "/", "qcom,board-id"], text=True).split()
            if [int(x, 16) for x in ids] != [board_id, 1]:
                raise ValueError("Overlay has wrong board ID")
            model = subprocess.check_output(["fdtget", overlay, "/", "model"], text=True).strip()
            dts = subprocess.check_output(["dtc", "-q", "-I", "dtb", "-O", "dts", result], text=True)
            if 'compatible = "st,fts"' not in dts or "SDX50M ANDROMEDA" not in model:
                raise ValueError("Merged tree is missing the andromeda touch profile")
            checked.append({"base": base, "overlay": overlay.name,
                            "board_id": ids, "overlay_model": model,
                            "sha256": sha(result)})
    manifest = {"repository": "https://github.com/MiCode/Xiaomi_Kernel_OpenSource",
                "revision": REVISION, "config": "andromeda_user_defconfig + CONFIG_BUILD_ARM64_DT_OVERLAY=y",
                "host_compatibility": "Direct GCC and system DTC, HOSTCFLAGS=-fcommon; skip unused SELinux host tools for DT-only build",
                "dtc": subprocess.check_output(["dtc", "--version"], text=True).strip(),
                "source_archive_sha256": sha(archive) if archive.exists() else None,
                "bundle_sha256": sha(out / "android-andromeda-dtbs.bin"),
                "bases": {base: sha(qcom / (base + ".dtb")) for base in BASES},
                "validated_combinations": checked,
                "device_dtbo": "ABL uses the existing compatible device DTBO partition; never flashed here"}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Validated {len(checked)} base/overlay combinations: {out}")


if __name__ == "__main__":
    main()
