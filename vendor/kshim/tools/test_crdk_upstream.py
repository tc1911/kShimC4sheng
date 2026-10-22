#!/usr/bin/env python3
"""Test the materialized CrDK patch set with native EDK2 and AArch64 headers."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edk2", type=Path, required=True)
    parser.add_argument("--build", type=Path, default=Path("build/crdk-upstream"))
    parser.add_argument("--cross", default="aarch64-linux-gnu-gcc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source, edk = out / "source", args.edk2.resolve()
    subprocess.run([sys.executable, root / "ports/crdk/prepare.py",
                    "--source", root / "lib/CrDK", "--output", source], check=True)
    results = {}
    for suite in ("geni_transport", "clock", "rpmh", "mu_bus"):
        with (out / (suite + ".log")).open("w") as log:
            subprocess.run([sys.executable, source / "Tests" / ("run_" + suite + "_tests.py"),
                            "--edk2", edk], stdout=log, stderr=subprocess.STDOUT, check=True)
        results[suite] = "passed"
        print(suite + ": PASS", flush=True)
    files = ["Library/GeniSeLib/geni_fifo.c", "Library/GeniSeLib/geni_uefi.c",
             "Library/GeniSeLib/geni_resources.c", "Library/ClockLib/clock_poll.c",
             "Library/RpmhLib/rpmh_poll.c", "Library/CmdDBLib/cmddb_bounded.c",
             "Library/DebugUartLib/debug_uart_poll.c", "Library/DisplayLib/splash.c",
             "Driver/I2CCrDxe/I2CCrDxe.c", "Target/Sm8450/CrQupTarget.c"]
    cmd = [args.cross, "-std=gnu11", "-fshort-wchar", "-ffreestanding", "-fsyntax-only",
           "-Werror=implicit-function-declaration", "-I" + str(source / "Include"),
           "-I" + str(edk / "MdePkg/Include"), "-I" + str(edk / "MdePkg/Include/AArch64"),
           "-I" + str(edk / "MdeModulePkg/Include")]
    with (out / "uefi-syntax.log").open("w") as log:
        for name in files:
            subprocess.run(cmd + [source / name], stdout=log, stderr=subprocess.STDOUT, check=True)
            log.write("PASS " + name + "\n")
            log.flush()
    results["aarch64_uefi_syntax"] = files
    results["limits"] = "Host and syntax checks only; no full UEFI/WDK build or hardware execution."
    results["patches"] = json.loads((root / "ports/crdk/UPSTREAM.json").read_text())["patches"]
    (out / "validation.json").write_text(json.dumps(results, indent=2) + "\n")
    print("AArch64 UEFI syntax: PASS (10 translation units)")


if __name__ == "__main__":
    main()
