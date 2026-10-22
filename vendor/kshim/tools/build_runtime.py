#!/usr/bin/env python3
"""Build a 4 MiB Manifest runtime from any capability profile, without UEFI."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    root, out = Path(__file__).resolve().parents[1], args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    config = args.config.read_text()
    config = re.sub(r"^CONFIG_KSHIM_UEFI_TEST_MENU=.*$", "# CONFIG_KSHIM_UEFI_TEST_MENU is not set",
                    config, flags=re.M)
    (out / ".config").write_text(config)
    for label, command in (
        ("configure", ["cmake", "-S", root, "-B", out, "-G", "Ninja",
                       "-DKSHIM_STANDALONE=OFF", "-DKSHIM_RUNTIME_SLOT_SIZE=0x400000",
                       "-DKSHIM_UEFI_PAYLOAD=", "-DCMAKE_BUILD_TYPE=Release"]),
        ("build", ["cmake", "--build", out, "-j", str(args.jobs)]),
    ):
        with (out / (label + ".log")).open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = out / "kShimC.bin"
    if binary.stat().st_size != 0x400000:
        raise ValueError("Manifest runtime must occupy exactly 4 MiB")
    undefined = subprocess.check_output(["aarch64-linux-gnu-nm", "-u", out / "kShimC.elf"], text=True)
    if undefined.strip():
        raise ValueError("Unresolved symbols: " + undefined)
    report = {"profile": args.config.name, "mode": "manifest", "bytes": binary.stat().st_size,
              "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "undefined_symbols": [],
              "hardware_tested": False}
    (out / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{args.config.name}: PASS (4 MiB; no undefined symbols)")


if __name__ == "__main__":
    main()
