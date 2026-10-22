#!/usr/bin/env python3
"""Prove that an LVGL worker-delete failure stops before draw-unit free."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path,
                        default=Path("build/qemu-lvgl-failstop"))
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / "configs/qemu_smp_defconfig", out / ".config")

    with (out / "configure.log").open("w") as log:
        subprocess.run([
            "cmake", "-S", root, "-B", out, "-G", "Ninja",
            "-DKSHIM_STANDALONE=ON",
            "-DCMAKE_C_FLAGS=-DKSHIM_LVGL_TEST_FORCE_DELETE_FAILURE=1",
        ], stdout=log, stderr=subprocess.STDOUT, check=True)
    with (out / "build.log").open("w") as log:
        subprocess.run(["cmake", "--build", out, "-j8"], stdout=log,
                       stderr=subprocess.STDOUT, check=True)

    image = out / "kShimC.bin"
    command = [
        "qemu-system-aarch64", "-machine",
        "virt,gic-version=3,virtualization=off", "-accel", "tcg,thread=multi",
        "-cpu", "cortex-a76", "-smp", "2", "-m", "256M", "-display",
        "none", "-monitor", "none", "-serial", "stdio", "-no-reboot",
        "-kernel", str(image),
    ]
    started = time.monotonic()
    timed_out = False
    returncode = None
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=args.timeout)
        output = result.stdout.decode(errors="replace")
        returncode = result.returncode
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or b"").decode(errors="replace")
        timed_out = True

    armed = "LVGL_FAILSTOP: delete failure armed" in output
    returned = "LVGL_FAILSTOP: ERROR deinit returned" in output
    passed = timed_out and armed and not returned and "SMP_SELFTEST PASS" not in output
    (out / "qemu.log").write_text(output)
    report = {
        "image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        "qemu": subprocess.check_output(
            ["qemu-system-aarch64", "--version"], text=True).splitlines()[0],
        "command": command,
        "timeout_seconds": args.timeout,
        "timed_out": timed_out,
        "armed": armed,
        "deinit_returned": returned,
        "returncode": returncode,
        "seconds": time.monotonic() - started,
        "passed": passed,
    }
    (out / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print("lvgl-delete-failstop: " + ("PASS" if passed else "FAIL"))
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
