#!/usr/bin/env python3
"""QEMU-only Linux/embedded init handoff validation for the TB323FU kShim.

Builds nothing. Drives an existing packer over a manifest_qemu.py A.bin
fixture, boots the produced image under qemu-system-aarch64, and checks the
kShim diagnostic handoff for three cases. No hardware, no publish, no cleanup.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd, log=None, timeout=None, check=False, cwd=None):
    cmd = [str(c) for c in cmd]
    print("+", " ".join(cmd), flush=True)
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, cwd=cwd)
    except subprocess.TimeoutExpired as e:
        if log:
            Path(log).write_bytes(e.stdout or b"")
        raise
    data = p.stdout or b""
    if log:
        Path(log).write_bytes(data)
    if check and p.returncode != 0:
        raise SystemExit("command failed (%d): %s" % (p.returncode, " ".join(cmd)))
    return p.returncode, data


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description="QEMU Linux handoff validation")
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--kernel", required=True, type=Path)
    ap.add_argument("--packer", required=True, type=Path)
    ap.add_argument("--base", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    runtime = args.runtime.resolve()
    kernel = args.kernel.resolve()
    packer = args.packer.resolve()
    base = args.base.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    for p in (runtime, kernel, packer, base):
        if not p.is_file():
            raise SystemExit("missing input: %s" % p)
    if runtime.stat().st_size != 4 * 1024 * 1024:
        raise SystemExit("runtime must be exactly 4 MiB")

    dtb = out / "qemu.dtb"
    run(["qemu-system-aarch64", "-machine", "virt,gic-version=3,dumpdtb=" + str(dtb),
         "-cpu", "cortex-a76", "-smp", "1",
         "-m", "1024M", "-display", "none", "-monitor", "none",
         "-serial", "none"],
        log=out / "qemu-dumpdtb.log", timeout=30, check=True)

    _rc, reg = run(["fdtget", "-t", "x", str(dtb), "/memory@40000000", "reg"])
    if reg.decode().strip() != "0 40000000 0 40000000":
        raise SystemExit("unexpected QEMU root RAM node: %r" % reg.decode().strip())

    kernel_sha = sha256(kernel)
    runtime_sha = sha256(runtime)
    results = {}
    failed = False

    for case in ("inherit-ram", "inherit-ram-automatic", "reject-bad-target"):
        dt = out / (case + ".dtb")
        shutil.copyfile(dtb, dt)
        run(["fdtput", "-t", "s", str(dt), "/chosen", "bootargs",
             "console=ttyAMA0 earlycon rdinit=/init "
             "kshim.diag_poweroff=1 loglevel=5"], check=True)
        if case != "inherit-ram-automatic":
            run(["fdtput", str(dt), "/chosen", "kshim,inherit-runtime-memory"], check=True)
        run(["fdtput", "-t", "x", str(dt), "/memory@40000000", "reg",
             "0", "0", "0", "0"], check=True)
        run(["fdtput", "-t", "x", str(dt), "/chosen", "linux,initrd-start",
             "dead0000"], check=True)
        run(["fdtput", "-t", "x", str(dt), "/chosen", "linux,initrd-end",
             "dead1000"], check=True)
        if case == "reject-bad-target":
            run(["fdtput", "-t", "x", str(dt), "/", "#address-cells", "1"], check=True)

        cfg = out / (case + ".cfg")
        cfg.write_text("\n".join([
            "[Pack]",
            "Shim=" + str(runtime),
            "Output=" + case + ".bin",
            "Default=Image-Linux",
            "Timeout=1",
            "[Image-Base]",
            "Name=Fallback",
            "Path=" + str(base),
            "Type=Linux",
            "BaseImage=true",
            "[Image-Linux]",
            "Name=Linux diagnostic",
            "Path=" + str(kernel),
            "Type=Linux",
            "BaseImage=false",
            "Align=21",
            "DeviceTreeBlob=Image-DTB",
            "[Image-DTB]",
            "Name=Linux DT",
            "Path=" + case + ".dtb",
            "Type=DTB",
            "BaseImage=false",
            "[Manifest]",
            "Type=Manifest",
            "",
        ]))

        run([str(packer), str(cfg)], log=out / (case + "-pack.log"),
            check=True, cwd=str(out))
        image = out / (case + ".bin")
        if not image.is_file():
            raise SystemExit("packer did not produce %s" % image)

        rc, data = run(["qemu-system-aarch64", "-machine", "virt,gic-version=3",
            "-accel", "tcg", "-cpu", "cortex-a76", "-smp", "1", "-m", "1024M",
            "-display", "none", "-monitor", "none", "-serial", "stdio",
            "-no-reboot", "-kernel", str(image),
            "-append", "console=ttyAMA0 rdinit=/wrong-source-init "
                       "kshim.diag_poweroff=0"],
            log=out / (case + ".log"), timeout=45)
        text = data.decode("utf-8", "replace")

        if case.startswith("inherit-ram"):
            ok = (rc == 0
                  and "TB323FU_DIAG_INIT_REACHED" in text
                  and "KSHIM_DIAG_QEMU_PASS" in text
                  and "rdinit=/init" in text
                  and "/wrong-source-init" not in text)
        else:
            ok = (rc == 0
                  and "PAYLOAD_A_PASS" in text
                  and "KSHIM_DIAG_QEMU_PASS" not in text
                  and "Memory handoff failed; booting base image" in text)

        results[case] = {"status": "PASS" if ok else "FAIL", "returncode": rc}
        print("PASS" if ok else "FAIL", case, flush=True)
        if not ok:
            failed = True

    (out / "validation.json").write_text(json.dumps({
        "cases": results,
        "kernel_sha256": kernel_sha,
        "runtime_sha256": runtime_sha,
        "scope": "QEMU only; no TB323FU hardware validation",
    }, indent=2) + "\n")

    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
