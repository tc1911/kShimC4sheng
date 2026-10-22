#!/usr/bin/env python3
"""Run the real ARM64 PSCI/MMU worker self-test in QEMU TCG."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build/qemu-smp"))
    parser.add_argument("--cpus", default="1,2,4,8")
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not args.no_build:
        shutil.copy2(root / "configs/qemu_smp_defconfig", out / ".config")
        with (out / "configure.log").open("w") as log:
            subprocess.run(["cmake", "-S", root, "-B", out, "-G", "Ninja",
                            "-DKSHIM_STANDALONE=ON"], stdout=log, stderr=subprocess.STDOUT, check=True)
        with (out / "build.log").open("w") as log:
            subprocess.run(["cmake", "--build", out, "-j8"], stdout=log,
                           stderr=subprocess.STDOUT, check=True)
    image = out / "kShimC.bin"
    results = []
    for virt in (False, True):
        for count in (int(x) for x in args.cpus.split(",")):
            for repeat in range(args.repeat):
                name = f"{'el2-smc' if virt else 'el1-hvc'}-{count}cpu-{repeat}"
                command = ["qemu-system-aarch64", "-machine",
                           f"virt,gic-version=3,virtualization={'on' if virt else 'off'}",
                           "-accel", "tcg,thread=multi", "-cpu", "cortex-a76",
                           "-smp", str(count), "-m", "256M", "-display", "none",
                           "-monitor", "none", "-serial", "stdio", "-no-reboot",
                           "-kernel", str(image)]
                start = time.monotonic()
                try:
                    result = subprocess.run(command, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, timeout=45)
                    output = result.stdout.decode(errors="replace")
                    match = re.search(
                        r"LVGL_SMP: expected=(\d+) active=(\d+) peak=(\d+) "
                        r"mask=0x([0-9a-fA-F]+) flushes=(\d+) pixels=(\d+) "
                        r"varied=(\d+) shutdown=(\w+)", output)
                    expected = min(max(count - 1, 0), 7)
                    evidence = None
                    if match:
                        evidence = {
                            "expected": int(match.group(1)),
                            "active": int(match.group(2)),
                            "peak": int(match.group(3)),
                            "worker_mask": int(match.group(4), 16),
                            "flushes": int(match.group(5)),
                            "pixels": int(match.group(6)),
                            "varied": int(match.group(7)),
                            "shutdown": match.group(8),
                        }
                    lvgl_passed = evidence is not None and all((
                        evidence["expected"] == expected,
                        evidence["active"] == expected,
                        evidence["peak"] == expected,
                        bin(evidence["worker_mask"]).count("1") == expected,
                        evidence["flushes"] > 0,
                        evidence["pixels"] >= 64 * 48,
                        evidence["varied"] == 1,
                        evidence["shutdown"] == "clean",
                    ))
                    passed = (result.returncode == 0 and
                              "SMP_SELFTEST PASS (0)" in output and lvgl_passed)
                    error = None if lvgl_passed else "invalid LVGL SMP evidence"
                except subprocess.TimeoutExpired as exc:
                    output = (exc.stdout or b"").decode(errors="replace")
                    passed, error, evidence = False, "timeout", None
                (out / (name + ".log")).write_text(output)
                results.append({"name": name, "command": command, "passed": passed,
                                "error": error, "lvgl": evidence,
                                "seconds": time.monotonic() - start})
                print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)
    report = {"image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
              "qemu": subprocess.check_output(["qemu-system-aarch64", "--version"], text=True).splitlines()[0],
              "tests": results, "passed": all(x["passed"] for x in results)}
    (out / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
