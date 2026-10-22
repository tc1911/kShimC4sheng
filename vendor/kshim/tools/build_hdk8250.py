#!/usr/bin/env python3
"""Build the HDK8250 Manifest runtime; a board-matched UEFI is not bundled."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/hdk8250"))
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.out.resolve()
    firmware = out / "firmware"
    firmware.mkdir(parents=True, exist_ok=True)
    config = (root / "configs/hdk8250_defconfig").read_text()
    (firmware / ".config").write_text(config)
    (out / "hdk8250_defconfig").write_text(config)
    for name, command in (
        ("configure", ["cmake", "-S", root, "-B", firmware, "-G", "Ninja",
                       "-DKSHIM_STANDALONE=OFF", "-DKSHIM_RUNTIME_SLOT_SIZE=0x400000"]),
        ("build", ["cmake", "--build", firmware, "-j", args.jobs]),
    ):
        with (out / (name + ".log")).open("w") as log:
            subprocess.run([str(x) for x in command], stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = firmware / "kShimC.bin"
    if binary.stat().st_size != 0x400000:
        raise ValueError("Manifest slot must remain exactly 4 MiB")
    undefined = subprocess.check_output(["aarch64-linux-gnu-nm", "-u", firmware / "kShimC.elf"], text=True)
    if undefined.strip():
        raise ValueError("Runtime has unresolved symbols: " + undefined)
    report = {
        "board": "qcom-hdk8250", "runtime_mode": "manifest", "libc": "picolibc",
        "ui": "flat", "bytes": binary.stat().st_size,
        "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "framebuffer": {"source": "DT reservation and validated live SDE scanout"},
        "touch": "DT-selected ST FTM5 with chip-ID verification",
        "display_prerequisite": "A supported active continuous splash layout",
        "uefi": "No UEFI payload is bundled",
        "hardware_tested": False,
    }
    (out / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Built {binary}\nSHA256 {report['sha256']}")


if __name__ == "__main__":
    main()
