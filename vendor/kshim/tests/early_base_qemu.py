#!/usr/bin/env python3
"""QEMU boot test for the y700-tb323fu offline image.

By default the test exercises entry only, using the base and alternate
fixtures that cover EL1/EL2, a valid FDT, and the expected EL and
MMU-off state. The optional --expect-console-init flag points QEMU at
a virt device tree supplied via --dtb and requires UART_CR 0x301, so
the PL011 is probed in that mode. The QEMU check does not validate
physical Qualcomm resources or display. Resource mode makes no
before-any-probes claim.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ASM = r'''
    .section ".text", "ax"
    .globl entry
    nop                       /* code0: patcher overwrites with a BL shim */
    b    entry                /* code1: MUST branch, else fall through hdr */
    .quad 0                   /* text_offset */
    .quad 0x26d0000           /* image_size (stock slot) */
    .quad 0                   /* flags */
    .quad 0                   /* res2 */
    .quad 0                   /* res3 */
    .quad 0                   /* res4 */
    .long 0x644d5241          /* ARM x64 magic */
    .long 0                   /* res5 */
entry:
    cbz  x0, fail             /* FDT pointer must be present */
    tst  x0, #7               /* ... and 8-byte aligned */
    b.ne fail
    ldr  w4, [x0]             /* FDT big-endian magic as native word */
    movz w5, #0x0dd0
    movk w5, #0xedfe, lsl #16
    cmp  w4, w5
    b.ne fail
    orr  x6, x1, x2           /* x1..x3 must all be zero */
    orr  x6, x6, x3
    cbnz x6, fail
    mrs  x7, CurrentEL
    cmp  x7, #%(cel)d
    b.ne fail
    mrs  x8, sctlr_el%(el)d
    tbnz x8, #0, fail         /* SCTLR_ELx.M must be 0 */
%(diag)s
    adr  x10, ok
    b    out
fail:
    adr  x10, bad
out:
    movz x9, #0x0900, lsl #16 /* PL011 UART0 @ 0x09000000 */
1:  ldrb w11, [x10], #1
    cbz  w11, 2f
    strb w11, [x9]
    b    1b
2:  movz w11, #10
    strb w11, [x9]
    movz x0, #0x20            /* SYS_EXIT_EXTENDED */
    adr  x1, blk
    hlt  #0xf000
    b    .
    .balign 8
blk:
    .quad 0x20026
    .quad 0
ok:
    .asciz "%(ok)s"
bad:
    .asciz "EARLY_BASE_FAIL"
'''

# Console-init checkpoint: prove console_init enabled the PL011 UART before
# handoff.  pl011_init() writes BIT9|BIT8|BIT0 (0x301) to UART_CR and masks
# IRQs at base+0x38; console_quiesce() does not reset the PL011, so UARTEN
# stays observable.  The reset default is 0x300 (TXE|RXE, UARTEN clear), so an
# entry-only diagnostic that never runs console_init leaves 0x300 and fails.
DIAG_CONSOLE_INIT = (
    "    movz x12, #0x0900, lsl #16  /* PL011 UART0 @ 0x09000000 */\n"
    "    movk x12, #0x0030           /* UART_CR @ base+0x30 */\n"
    "    ldr  w13, [x12]\n"
    "    movz w14, #0x0301           /* UARTEN|TXE|RXE set by console_init */\n"
    "    cmp  w13, w14\n"
    "    b.ne fail\n"
)

INI = """[Pack]
Shim=runtime
Output=packed
Default=Image-B
Timeout=100
[Image-A]
Name=Base
Path=A.bin
Type=Linux
BaseImage=true
Align=21
[Image-B]
Name=Alt
Path=B.bin
Type=Linux
BaseImage=false
Align=21
[Manifest]
Type=Manifest
"""

TRACE_BAD = ("Invalid read", "Invalid write", "Data Abort", "Undefined Instruction")
OUT_BAD = ("EARLY_BASE_FAIL", "WRONG_ALTERNATE", "KShim")


def sh(cmd, cwd, timeout, log):
    argv = [str(c) for c in cmd]
    try:
        p = subprocess.run(argv, cwd=str(cwd), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        data, rc = p.stdout or b"", p.returncode
    except subprocess.TimeoutExpired as exc:
        data, rc = (exc.stdout or b""), None
    Path(log).write_bytes(data)
    return rc, data.decode("utf-8", "replace")


def build(cc, oc, work, tag, el, ok, diag=""):
    src, obj = work / (tag + ".s"), work / (tag + ".o")
    elf, binf = work / (tag + ".elf"), work / (tag + ".bin")
    src.write_text(ASM % {"el": el, "cel": el << 2, "ok": ok, "diag": diag})
    steps = (
        [cc, "-c", "-o", obj, src],
        [cc, "-nostdlib", "-nostartfiles", "-static", "-Wl,--build-id=none",
         "-Wl,-Ttext=0", "-Wl,-e,entry", "-o", elf, obj],
        [oc, "-O", "binary", elf, binf],
    )
    for i, step in enumerate(steps):
        rc, _ = sh(step, work, 60, work / (tag + "." + str(i) + ".log"))
        if rc != 0:
            raise SystemExit("fixture build failed (" + tag + ", step " + str(i) + ")")
    return binf


def check(rc, text, trace):
    problems = []
    if rc is None:
        problems.append("timeout")
    elif rc != 0:
        problems.append("returncode=" + str(rc))
    passes = text.count("EARLY_BASE_PASS")
    if passes == 0:
        problems.append("missing EARLY_BASE_PASS")
    elif passes != 1:
        problems.append("expected exactly one EARLY_BASE_PASS, got " + str(passes))
    problems += ["unexpected " + t for t in OUT_BAD if t in text]
    problems += ["trace: " + t for t in TRACE_BAD if t in trace]
    return problems


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--dtb", required=True, type=Path)
    ap.add_argument("--packer", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--expect-console-init", action="store_true",
                    help="require observable PL011 UART_CR==0x301 evidence "
                         "that console_init ran before handoff")
    a = ap.parse_args()

    # Resolve every path so relative inputs/outputs work regardless of CWD.
    a.runtime = a.runtime.resolve()
    a.dtb = a.dtb.resolve()
    a.packer = a.packer.resolve()
    a.out = a.out.resolve()

    # The runtime must fill exactly the 4 MiB runtime slot; anything else is a
    # wrong artefact and would invalidate the diagnostic result.
    runtime_size = a.runtime.stat().st_size
    if runtime_size != 4 * 1024 * 1024:
        raise SystemExit("runtime size mismatch: expected 4194304 bytes, got " +
                         str(runtime_size))

    cc = os.environ.get("AARCH64_CC", "aarch64-linux-gnu-gcc")
    oc = os.environ.get("AARCH64_OBJCOPY", "aarch64-linux-gnu-objcopy")
    out = a.out
    out.mkdir(parents=True, exist_ok=True)

    runs, packed_hashes, failures = [], {}, []

    for el in (1, 2):
        w = out / ("el" + str(el))
        w.mkdir(exist_ok=True)
        diag = DIAG_CONSOLE_INIT if a.expect_console_init else ""
        build(cc, oc, w, "A", el, "EARLY_BASE_PASS", diag)
        build(cc, oc, w, "B", el, "WRONG_ALTERNATE", diag)
        (w / "runtime").write_bytes(a.runtime.read_bytes())
        (w / "pack.ini").write_text(INI)

        rc, _ = sh([a.packer, w / "pack.ini"], w, 100, w / "packer.log")
        if rc != 0 or not (w / "packed").exists():
            failures.append("EL" + str(el) + ": packer failed rc=" + str(rc))
            runs.append({"el": el, "status": "fail",
                         "problems": ["packer rc=" + str(rc)], "trace_head": []})
            continue

        packed_hashes["el" + str(el)] = sha(w / "packed")
        machine = "virt,gic-version=3" + (",virtualization=on" if el == 2 else "")
        qemu = ["qemu-system-aarch64", "-M", machine, "-cpu", "cortex-a76",
                "-accel", "tcg", "-smp", "1", "-m", "256M", "-display", "none",
                "-monitor", "none", "-serial", "stdio", "-no-reboot",
                "-semihosting-config", "enable=on,target=native",
                "-kernel", "packed", "-dtb", str(a.dtb),
                "-d", "int,guest_errors", "-D", "trace.txt"]
        rc, text = sh(qemu, w, 3, w / "qemu.log")

        tpath = w / "trace.txt"
        raw = tpath.read_bytes() if tpath.exists() else b""
        trace_bytes = len(raw)
        # Validate against the full trace, then trim the on-disk artefact.
        problems = check(rc, text, raw.decode("utf-8", "replace"))
        tpath.write_bytes(raw[:4096])
        tb = raw[:4096].decode("utf-8", "replace")
        runs.append({"el": el, "status": "fail" if problems else "pass",
                     "returncode": rc, "problems": problems,
                     "trace_bytes": trace_bytes,
                     "trace_head": tb.splitlines()[:8]})
        if problems:
            failures.append("EL" + str(el) + ": " + "; ".join(problems))

    scope = ("resources-checkpoint-qemu-console-evidence"
             if a.expect_console_init
             else "early-base-handoff-before-peripheral-probes")
    validation = {
        "status": "fail" if failures else "pass",
        "scope": scope,
        "expectConsoleInit": a.expect_console_init,
        "expectedEL": [1, 2],
        "hashes": {"runtime": sha(a.runtime), "dtb": sha(a.dtb),
                   "packed": packed_hashes},
        "runs": runs,
        "failures": failures,
    }
    (out / "validation.json").write_text(json.dumps(validation, indent=2))

    if failures:
        sys.stderr.write("EARLY-BASE REGRESSION FAILURES: " + " | ".join(failures))
        sys.stderr.write(os.linesep)
        return 1
    print("early-base handoff regression: PASS (EL1+EL2)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
