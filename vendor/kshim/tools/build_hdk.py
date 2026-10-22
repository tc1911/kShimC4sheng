#!/usr/bin/env python3
"""Build a kShimC menu with an existing HDK8150 or HDK8450 UEFI payload.

No flashing. The raw UEFI FD is extracted from the device-specific .img;
the shared Waipio build/FV directory may belong to another device.
"""
import argparse
import ast
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib


BOARDS = {"hdk8150": ("SurfaceDuo1Pkg", 0), "hdk8450": ("WaipioPkg", 3),
          "andromeda": ("SurfaceDuo1Pkg", 0)}


def device_name(board):
    return ("xiaomi-" if board == "andromeda" else "qcom-") + board


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_andromeda_bundle(path):
    """Recheck the DT builder's artifacts before replacing the UEFI base trees."""
    from build_andromeda_dt import BASES, BOARD_IDS, REVISIONS, REVISION, parse_bundle

    path = Path(path).resolve()
    manifest_path = path.with_name("manifest.json")
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    repository = "https://github.com/MiCode/Xiaomi_Kernel_OpenSource"
    if (not isinstance(manifest, dict) or manifest.get("repository") != repository or
            manifest.get("revision") != REVISION):
        raise ValueError("DT bundle must come from the pinned official Andromeda kernel")
    data = path.read_bytes()
    members = parse_bundle(data)
    if len(members) != len(BASES):
        raise ValueError("Expected the four validated SM8150 base DTBs")
    if manifest.get("bundle_sha256") != hashlib.sha256(data).hexdigest():
        raise ValueError("DT bundle SHA256 differs from its manifest")
    hashes = manifest.get("bases")
    if not isinstance(hashes, dict) or set(hashes) != set(BASES):
        raise ValueError("DT manifest must identify all four SM8150 base trees")
    for base, member in zip(BASES, members):
        if hashes[base] != hashlib.sha256(member).hexdigest():
            raise ValueError(f"DT member SHA256 differs for {base}")

    expected = {
        (base, "andromeda-sm8150" + revision + "-overlay.dtbo"): (revision, board)
        for base in BASES for revision, board in zip(REVISIONS, BOARD_IDS)
    }
    checked = manifest.get("validated_combinations")
    if not isinstance(checked, list) or len(checked) != len(expected):
        raise ValueError("DT manifest must contain all 32 validated base/overlay combinations")
    records = {}
    for record in checked:
        if not isinstance(record, dict):
            raise ValueError("Invalid DT combination record")
        base, overlay = record.get("base"), record.get("overlay")
        if not isinstance(base, str) or not isinstance(overlay, str):
            raise ValueError("Invalid DT combination identity")
        key = (base, overlay)
        if key not in expected or key in records:
            raise ValueError("Missing, duplicated or unexpected DT combination")
        board_id = record.get("board_id")
        if (not isinstance(board_id, list) or len(board_id) != 2 or
                any(not isinstance(x, str) or not re.fullmatch(r"[0-9a-fA-F]+", x)
                    for x in board_id) or
                [int(x, 16) for x in board_id] != [expected[key][1], 1] or
                record.get("overlay_model") != "SDX50M ANDROMEDA" or
                not isinstance(record.get("sha256"), str) or
                not re.fullmatch(r"[0-9a-f]{64}", record["sha256"])):
            raise ValueError("Invalid Andromeda overlay validation record")
        records[key] = record

    def get_cells(tree, prop):
        values = subprocess.check_output(
            ["fdtget", "-t", "x", str(tree), "/", prop], text=True).split()
        return [int(value, 16) for value in values]

    identities = {}
    overlay_hashes = {}
    qcom = path.parent / "obj/arch/arm64/boot/dts/qcom"
    with tempfile.TemporaryDirectory(prefix="kshim-dtb-check-") as temporary:
        temporary = Path(temporary)
        for index, (base, member) in enumerate(zip(BASES, members)):
            tree = temporary / (base + ".dtb")
            tree.write_bytes(member)
            soc = [339 if index < 2 else 361, 0x10000 if index % 2 == 0 else 0x20000]
            if get_cells(tree, "qcom,msm-id") != soc or get_cells(tree, "qcom,board-id") != [0, 0]:
                raise ValueError(f"Wrong SoC/version/board identity in {base}")
            identities[base] = {"msm_id": soc, "board_id": [0, 0], "sha256": hashes[base]}

        for (base, name), (revision, board) in expected.items():
            overlay = qcom / name
            if name not in overlay_hashes:
                if get_cells(overlay, "qcom,board-id") != [board, 1]:
                    raise ValueError(f"Wrong Andromeda overlay board identity in {name}")
                model = subprocess.check_output(["fdtget", str(overlay), "/", "model"],
                                                text=True).strip()
                if model != "SDX50M ANDROMEDA":
                    raise ValueError(f"Wrong Andromeda overlay model in {name}")
                overlay_hashes[name] = digest(overlay)
            merged = temporary / "merged.dtb"
            subprocess.run(["fdtoverlay", "-i", str(temporary / (base + ".dtb")),
                            "-o", str(merged), str(overlay)], check=True)
            expected_hash = records[(base, name)]["sha256"]
            if (digest(merged) != expected_hash or
                    digest(path.parent / "merged" / (base + revision + ".dtb")) != expected_hash):
                raise ValueError(f"DT overlay replay SHA256 differs for {base}/{name}")
            compatible = subprocess.check_output(
                ["fdtget", str(merged), "/soc/i2c@0xc80000/fts@49", "compatible"], text=True).strip()
            if compatible != "st,fts":
                raise ValueError(f"Andromeda touch node is absent in {base}/{name}")

    provenance = {
        "repository": repository, "revision": REVISION,
        "source_manifest": str(manifest_path),
        "manifest_snapshot": "dtb-build-manifest.json",
        "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "source_archive_sha256": manifest.get("source_archive_sha256"),
        "bases": identities, "overlay_sha256": overlay_hashes,
        "validated_combinations": len(records),
        "validation": "fdtget SoC/board identity and 32 fdtoverlay replays with matching hashes",
    }
    return data, provenance, manifest_bytes


def region(text, name):
    match = re.search(r'\{\s*"' + re.escape(name) +
                      r'"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)', text)
    if not match:
        raise ValueError(f"Missing memory region: {name}")
    return tuple(int(x, 16) for x in match.groups())


def read_boot(data, expected_version):
    if len(data) < 1632 or data[:8] != b"ANDROID!":
        raise ValueError("Expected an Android boot image")
    version = struct.unpack_from("<I", data, 40)[0]
    if version != expected_version:
        raise ValueError(f"Expected boot v{expected_version}, got v{version}")
    kernel_size = struct.unpack_from("<I", data, 8)[0]
    if version == 3:
        ramdisk_size, _, header_size = struct.unpack_from("<3I", data, 12)
        if header_size != 1580:
            raise ValueError("Invalid boot v3 header size")
        page = 4096
    else:
        ramdisk_size = struct.unpack_from("<I", data, 16)[0]
        if struct.unpack_from("<I", data, 24)[0] != 0:
            raise ValueError("Second-stage boot payloads are not supported")
        page = struct.unpack_from("<I", data, 36)[0]
        if page not in (2048, 4096, 8192, 16384):
            raise ValueError("Invalid boot v0 page size")
    ramdisk_offset = page + (kernel_size + page - 1) // page * page
    if kernel_size == 0 or len(data) < ramdisk_offset + ramdisk_size:
        raise ValueError("Truncated boot image")
    return (data[:page], page, data[page:page + kernel_size],
            data[ramdisk_offset:ramdisk_offset + ramdisk_size])


def pack_boot(template, page, version, kernel, ramdisk):
    # Retain the source board's load offsets, cmdline, OS version and page size.
    header = bytearray(template)
    struct.pack_into("<I", header, 8, len(kernel))
    struct.pack_into("<I", header, 12 if version == 3 else 16, len(ramdisk))
    if version == 0:
        identity = hashlib.sha1()
        for section in (kernel, ramdisk, b""):
            identity.update(section)
            identity.update(struct.pack("<I", len(section)))
        header[576:608] = identity.digest().ljust(32, b"\0")
    return (bytes(header) + kernel + bytes(-len(kernel) % page) +
            ramdisk + bytes(-len(ramdisk) % page))


def read_uefi_build(directory, board, fd, require_debug_uart):
    """Tie build settings to the exact FD extracted from the source image."""
    directory = directory.resolve()
    soc = "8450" if board == "hdk8450" else "8150"
    if (directory / "FV" / f"SM{soc}_EFI.fd").read_bytes() != fd:
        raise ValueError("UEFI build directory contains a different FD from the source image")
    report = (directory / "BUILD_REPORT.TXT").read_text()
    options = (directory / "BuildOptions").read_text()
    defines_line = next(line for line in options.splitlines()
                        if line.startswith("gCommandLineDefines: "))
    defines = ast.literal_eval(defines_line.split(": ", 1)[1])
    if defines.get("TARGET_DEVICE") != device_name(board):
        raise ValueError("UEFI build report belongs to a different target device")
    target = re.search(r"^Target:\s+(\S+)", report, re.M).group(1)

    def pcd(name):
        match = re.search(r"\b" + name + r"\s+:.*?=\s*(0x[0-9a-fA-F]+|[0-9]+)", report)
        return int(match.group(1), 0) if match else 0

    geni = "QcomGeniSerialPortLib/QcomGeniSerialPortLib.inf" in report
    enabled = geni and bool(pcd("PcdDebugPropertyMask") & 2) and bool(pcd("PcdDebugPrintErrorLevel"))
    if require_debug_uart and (target != "DEBUG" or not enabled):
        raise ValueError("The matching UEFI payload is not DEBUG with GENI UART debug output enabled")
    secure = re.search(r"-DSECURE_BOOT=([01])\b", report)
    return {
        "directory": str(directory), "target": target,
        "target_device": defines["TARGET_DEVICE"],
        "uart_debug_enabled": enabled,
        "serial_port_library": "QcomGeniSerialPortLib" if geni else "not verified",
        "uart_base": hex(pcd("PcdUartSerialBase")),
        "debug_property_mask": hex(pcd("PcdDebugPropertyMask")),
        "debug_print_error_level": hex(pcd("PcdDebugPrintErrorLevel")),
        "secure_boot_build": bool(int(secure.group(1))) if secure else None,
        "build_report_sha256": digest(directory / "BUILD_REPORT.TXT"),
        "build_options_sha256": digest(directory / "BuildOptions"),
    }


def main(default_board="hdk8450"):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, default=default_board)
    parser.add_argument("--aloha", type=Path, required=True, help="Explicit platform metadata workspace for packaging an existing UEFI image")
    parser.add_argument("--uefi-boot", type=Path, help="Board-specific UEFI boot image to extract")
    parser.add_argument("--uefi-build-dir", type=Path,
                        help="Matching mu_aloha build directory containing FV and BUILD_REPORT.TXT")
    parser.add_argument("--require-debug-uart", action="store_true",
                        help="Require matching build evidence for DEBUG with GENI UART output")
    parser.add_argument("--out", type=Path, help="Output directory (default: build/<board>)")
    parser.add_argument("--jobs", type=int, default=12)
    parser.add_argument("--dtb-bundle", type=Path,
                        help="Andromeda bundle from build_andromeda_dt, with sibling manifest and build artifacts")
    args = parser.parse_args()
    if args.require_debug_uart and not args.uefi_build_dir:
        parser.error("--require-debug-uart needs --uefi-build-dir")
    root = Path(__file__).resolve().parents[1]
    aloha = args.aloha.resolve()
    board = args.board
    package, version = BOARDS[board]
    out = (args.out or root / "build" / board).resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / "tmp").mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(out / "tmp"))
    device = aloha / "Platforms" / package / "Device" / device_name(board)
    memory = device / "Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c"
    base, reserved = region(memory.read_text(), "Kernel")
    uefi_base, uefi_size = region(memory.read_text(), "UEFI FD")

    source = (args.uefi_boot or aloha / "Build" / package / (device_name(board) + ".img")).resolve()
    data = source.read_bytes()
    template, page, source_kernel, ramdisk = read_boot(data, version)
    stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
    payload = stream.decompress(source_kernel) + stream.flush()
    if not stream.eof or len(payload) < 64:
        raise ValueError("Missing or truncated gzip kernel")
    dtb_data = stream.unused_data
    dtb_path = device / "DeviceTreeBlob/Android" / f"android-{board}.dtb"
    if version == 0:
        if not dtb_data or dtb_data != dtb_path.read_bytes():
            raise ValueError("Appended DTB differs from the selected board's Android DTB")
    elif dtb_data:
        raise ValueError("Unexpected trailing data after boot v3 gzip kernel")
    original_dtb_sha256 = hashlib.sha256(dtb_data).hexdigest() if dtb_data else None
    dtb_provenance = None
    if args.dtb_bundle:
        if board != "andromeda":
            raise ValueError("A replacement base-DTB bundle requires the andromeda profile")
        dtb_path = args.dtb_bundle.resolve()
        dtb_data, dtb_provenance, dtb_manifest = validate_andromeda_bundle(dtb_path)
        (out / "dtb-build-manifest.json").write_bytes(dtb_manifest)
    old_base, old_size = struct.unpack_from("<QQ", payload, 8)
    if payload[56:60] != b"ARM\x64" or (old_base, old_size) != (uefi_base, uefi_size):
        raise ValueError("UEFI BootShim header disagrees with the selected board's MemoryMap")
    fd = payload[-uefi_size:]
    if len(fd) != uefi_size or fd[40:44] != b"_FVH":
        raise ValueError("Missing UEFI firmware volume")
    uefi_build = None
    if args.uefi_build_dir:
        uefi_build = read_uefi_build(args.uefi_build_dir, board, fd, args.require_debug_uart)
        for name in ("BUILD_REPORT.TXT", "BuildOptions"):
            (out / ("uefi-" + name)).write_bytes((args.uefi_build_dir / name).read_bytes())
    (out / "source-uefi-boot.img").write_bytes(data)
    if dtb_data:
        (out / "android-dtbs.bin").write_bytes(dtb_data)
    fd_path = out / f"{board}-uefi.fd"
    fd_path.write_bytes(fd)

    config = (root / "configs" / f"{board}_defconfig").read_text()
    values = {"KSHIM_UEFI_LOAD_ADDRESS": hex(uefi_base), "KSHIM_UEFI_MAX_SIZE": hex(uefi_size)}
    for key, value in values.items():
        config = re.sub(r"^CONFIG_" + key + r"=.*$", f"CONFIG_{key}={value}", config, flags=re.M)
    firmware = out / "firmware"
    firmware.mkdir(exist_ok=True)
    (firmware / ".config").write_text(config)
    (out / f"{board}_defconfig").write_text(config)
    hardware_provenance = None
    if board == "andromeda":
        hardware_snapshot = out / "board-hardware-provenance.json"
        hardware_snapshot.write_bytes((root / "configs/andromeda-provenance.json").read_bytes())
        hardware_provenance = {"snapshot": hardware_snapshot.name,
                               "sha256": digest(hardware_snapshot)}

    def run(command, name):
        print(name, flush=True)
        with (out / (name + ".log")).open("w") as log:
            subprocess.run([str(arg) for arg in command], cwd=root, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)

    run(["cmake", "-S", root, "-B", firmware, "-G", "Ninja",
         "-DKSHIM_STANDALONE=ON", "-DKSHIM_RUNTIME_SLOT_SIZE=0x02800000",
         f"-DKSHIM_LINK_ADDRESS={hex(base)}", f"-DKSHIM_UEFI_PAYLOAD={fd_path}"], "configure")
    run(["cmake", "--build", firmware, "--parallel", args.jobs], "build")
    elf = firmware / "kShimC.elf"
    binary = firmware / "kShimC.bin"
    undefined = subprocess.check_output(["aarch64-linux-gnu-nm", "-u", elf], text=True)
    if undefined.strip():
        raise ValueError("Undefined symbols: " + undefined)
    symbols = {}
    for line in subprocess.check_output(["aarch64-linux-gnu-nm", "-n", elf], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    used = symbols["__image_end_address__"] - base
    if symbols["_start"] != base or used > reserved - 4096 or len(binary.read_bytes()) > used:
        raise ValueError("Shim does not fit its Kernel reservation")
    offset = symbols["kshim_uefi_payload_start"] - base
    if binary.read_bytes()[offset:offset + uefi_size] != fd:
        raise ValueError("Embedded UEFI payload mismatch")
    run(["aarch64-linux-gnu-gcc", "-nostdlib", "-static", "-Wl,-Ttext=0",
         "-Wl,--build-id=none", f"-DSHIM_LOAD_ADDRESS={hex(base)}",
         f"-DSHIM_FILE_SIZE={binary.stat().st_size}", f"-DSHIM_MEMORY_SIZE={used}",
         f"-DSHIM_SCRATCH_ADDRESS={hex(base + reserved - 4096)}",
         root / "tools/boot_entry.S", "-o", out / "boot-entry.elf"], "entry-build")
    run(["aarch64-linux-gnu-objcopy", "-O", "binary", out / "boot-entry.elf",
         out / "boot-entry.bin"], "entry-objcopy")
    kernel = (out / "boot-entry.bin").read_bytes() + binary.read_bytes()
    (out / "kernel.bin").write_bytes(kernel)
    compressed = gzip.compress(kernel, compresslevel=9, mtime=0)
    (out / "kernel.gz").write_bytes(compressed)
    image = out / f"kshimc-{board}-boot.img"
    image.write_bytes(pack_boot(template, page, version, compressed + dtb_data, ramdisk))
    _, _, packed_kernel, packed_ramdisk = read_boot(image.read_bytes(), version)
    if (packed_kernel != compressed + dtb_data or packed_ramdisk != ramdisk or
            gzip.decompress(packed_kernel[:len(compressed)]) != kernel):
        raise ValueError("Boot image round-trip failed")
    manifest = {
        "board": device_name(board), "menu": ["UEFI A", "UEFI B"],
        "ui": "flat", "libc": "picolibc", "runtime_mode": "standalone",
        "same_uefi_payload_for_both": True,
        "source_uefi_boot": str(source), "source_uefi_boot_sha256": hashlib.sha256(data).hexdigest(),
        "source_snapshot": "source-uefi-boot.img",
        "uefi_build": uefi_build,
        "hardware_provenance": hardware_provenance,
        "uefi_fd_sha256": digest(fd_path),
        "uefi_entry": hex(uefi_base), "uefi_bytes": uefi_size,
        "shim_entry": hex(base), "shim_memory_bytes": used,
        "shim_reserved_bytes": reserved, "scratch_address": hex(base + reserved - 4096),
        "framebuffer": {"source": "DT reservation and validated live SDE scanout"},
        "memorymap": {"path": str(memory), "sha256": digest(memory)},
        "boot_header_version": version, "page_bytes": page, "compression": "gzip",
        "ramdisk_bytes": len(ramdisk),
        "appended_dtb": {"bytes": len(dtb_data),
                         "sha256": hashlib.sha256(dtb_data).hexdigest() if dtb_data else None,
                         "source_uefi_dtb_sha256": original_dtb_sha256,
                         "source": str(dtb_path) if dtb_data else None,
                         "provenance": dtb_provenance},
        "artifacts": {p.name: {"bytes": p.stat().st_size, "sha256": digest(p)}
                      for p in (image, elf, binary, fd_path)},
        "validation": "Image and ELF checks passed; not hardware tested",
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (out / "SHA256SUMS").write_text(f"{digest(image)}  {image.name}\n")
    print(f"Created {image}\nSHA256 {digest(image)}")


if __name__ == "__main__":
    main()
