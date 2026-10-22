#!/usr/bin/env python3
"""QEMU regression for the KSHIM diagnostic runtimes (DT_ONLY / RESOURCES_ONLY).

SYNTHETIC ONLY.  This test executes a locally built diagnostic runtime (the
real supplied 4 MiB artefact, embedded verbatim) under QEMU's ``virt`` machine
in EL1 and EL2.  It makes no claim about real hardware, physical Qualcomm
resources, an actual device tree, or deployment.  It never touches a device.

Two modes are selected by ``--mode`` (default ``dt``); the requested mode is
recorded in validation.json and the human output:

  dt         KSHIM_DIAGNOSTIC_DT_ONLY: the two report values are ``dt1:...``
             strings.
  resources  KSHIM_DIAGNOSTIC_RESOURCES_ONLY: the first report value is the
             ``r14:...`` splash-trace string and the second is the ``res1:...``
             resources status/timing string.

Both modes run the same three cases per exception level:

  valid    /chosen:bootargs carries the two source placeholders
           (kshimreport.kshim_probe= / kshimreport.kshim_s0=, each exactly
           "pending" + 73 underscores, separated by one space).  The runtime
           must convert them in place; the returned DT is scanned for the two
           mode-specific androidboot target tokens and the remaining source
           prefixes are rejected.
  missing  /chosen:bootargs has no source slots.  The report API refuses and
           the DT byte checksum must be unchanged afterwards (before/after byte
           sum), which is a checksum comparison, not full byte-equality proof.
  corrupt  x0 is an aligned 40-byte invalid FDT header (magic 0, big-endian
           totalsize 40); the runtime must skip reporting, forward the exact
           original pointer, and leave the local header checksum unchanged.

Every case additionally verifies the same test-side invariants: the exact
returned DT pointer, the preserved x1..x3 sentinels, the expected CurrentEL,
SCTLR_ELx.M == 0, and that PL011 UART_CR still reads its reset value 0x300.
DT_ONLY does not probe any peripheral.  RESOURCES_ONLY executes the runtime's
resource-probing path, but the synthetic QEMU ``virt`` fixture exposes no real
Qualcomm resources, so this asserts nothing about hardware MMIO and is not a
hardware resource proof.  The UART reset check is performed by the test-side
payload and is not evidence that the runtime itself drives the UART.
PASS/FAIL is reported only through semihosting SYS_WRITE0 followed by
SYS_EXIT_EXTENDED; no UART output is produced.

The harness is a raw ARM64 Image fixture (built here, not by the production
packer): code0 branches to the invoke payload, code1 (offset 4) branches to
the verify payload, the ordinary header metadata at 0x20/0x28 is zero and the
magic is present, and image_size is patched to the whole fixture.  The
supplied runtime is embedded at a 4096-aligned offset; the runtime computes
linux_base = LR-4 and the diagnostic handoff at linux_base+4 lands back on
code1 without consulting the manifest.

All generated fixtures and logs are written under --out.  The external tools
(aarch64 C compiler, objcopy, qemu-system-aarch64) run only on the remote
Docker host when the parent invokes this script; it is never run locally.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

# QEMU's int,guest_errors trace must never mention these.
TRACE_BAD = ("Invalid read", "Invalid write", "Data Abort",
             "Undefined Instruction")

# One source placeholder value: "pending" + 73 underscores (80 bytes).
PH = "pending" + "_" * 73
APPEND_VALID = ("kshimreport.kshim_probe=" + PH + " " +
                "kshimreport.kshim_s0=" + PH)
APPEND_OTHER = "other=1"

# Per-mode valid-case target patterns and semihost marker.  Both modes share
# the same two source placeholders; only the expected androidboot target
# values differ (DT_ONLY writes dt1:, RESOURCES_ONLY writes r14: then res1:).
MODES = {
    "dt": {
        "marker": "DT_ONLY",
        "pat_probe": "androidboot.kshim_probe=dt1:",
        "pat_s0": "androidboot.kshim_s0=dt1:",
    },
    "resources": {
        "marker": "RESOURCES_ONLY",
        "pat_probe": "androidboot.kshim_probe=r14:",
        "pat_s0": "androidboot.kshim_s0=res1:",
    },
}

LINKER = """\
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(4096);
    .runtime : { *(.runtime) }
}
"""

ASM = r'''
    .section ".text", "ax"
    .balign 4096
    .globl _start
_start:
    b    invoke
code1_label:
    b    returned
    .quad 0                       /* text_offset */
    .quad 0                       /* image_size (patched to whole fixture) */
    .quad 0                       /* flags */
    .quad 0                       /* reserved metadata @0x20 */
    .quad 0                       /* reserved metadata @0x28 */
    .quad 0                       /* reserved metadata @0x30 */
    .long 0x644d5241              /* ARM64 magic @0x38 */
    .long 0                       /* reserved5 */

invoke:
    adrp x9, harness_stack_top
    add  x9, x9, :lo12:harness_stack_top
    mov  sp, x9
%(setup)s
    mov  x19, x0                  /* the pointer the runtime must return */
    adrp x9, saved_dt
    add  x9, x9, :lo12:saved_dt
    str  x19, [x9]
    mov  x0, x19
    bl   fdt_len                  /* x1 = fdt totalsize (big-endian u32 @4) */
    mov  x0, x19
    bl   sum_bytes                /* x2 = byte sum before the runtime */
    adrp x9, saved_sum
    add  x9, x9, :lo12:saved_sum
    str  x2, [x9]
    mov  x0, x19
    movz x1, #0x1122              /* nonzero sentinels forwarded unchanged */
    movz x2, #0x3344
    movz x3, #0x5566
    adr  lr, code1_label          /* runtime computes linux_base = LR - 4 */
    adrp x9, runtime_base
    add  x9, x9, :lo12:runtime_base
    br   x9

returned:
    adrp x9, harness_stack_top
    add  x9, x9, :lo12:harness_stack_top
    mov  sp, x9
    adrp x9, saved_dt             /* exact returned pointer FIRST */
    add  x9, x9, :lo12:saved_dt
    ldr  x10, [x9]
    cmp  x0, x10
    b.ne fail
    mov  x19, x0
    movz x10, #0x1122             /* x1..x3 forwarded unchanged */
    cmp  x1, x10
    b.ne fail
    movz x10, #0x3344
    cmp  x2, x10
    b.ne fail
    movz x10, #0x5566
    cmp  x3, x10
    b.ne fail
    mrs  x10, CurrentEL           /* expected exception level */
    adrp x9, expected_cel
    add  x9, x9, :lo12:expected_cel
    ldr  x11, [x9]
    cmp  x10, x11
    b.ne fail
    cmp  x11, #4                  /* SCTLR_ELx.M must still be 0 (MMU off) */
    b.eq 1f
    mrs  x10, sctlr_el2
    b    2f
1:  mrs  x10, sctlr_el1
2:  tbnz x10, #0, fail
    movz x12, #0x0900, lsl #16    /* PL011 UART_CR @ 0x09000030 */
    movk x12, #0x0030
    ldr  w13, [x12]               /* must remain the reset value 0x300 */
    movz w14, #0x0300
    cmp  w13, w14
    b.ne fail
%(verify)s
pass:
    movz x0, #0x04                /* SYS_WRITE0 */
    adr  x1, msg_pass
    hlt  #0xf000
    b    exit_ok
fail:
    movz x0, #0x04                /* SYS_WRITE0 */
    adr  x1, msg_fail
    hlt  #0xf000
exit_ok:
    movz x0, #0x20                /* SYS_EXIT_EXTENDED */
    adr  x1, blk
    hlt  #0xf000
    b    .
    .balign 8
blk:
    .quad 0x20026
    .quad 0

/* x0 = fdt ptr -> x1 = big-endian u32 at offset 4 (zero extended). */
fdt_len:
    ldrb w1, [x0, #4]
    ldrb w2, [x0, #5]
    ldrb w3, [x0, #6]
    ldrb w4, [x0, #7]
    lsl  w1, w1, #24
    lsl  w2, w2, #16
    lsl  w3, w3, #8
    orr  w1, w1, w2
    orr  w1, w1, w3
    orr  w1, w1, w4
    ret

/* x0 = ptr, x1 = len -> x2 = sum of bytes. */
sum_bytes:
    mov  x2, xzr
    cbz  x1, 2f
1:  ldrb w3, [x0], #1
    add  x2, x2, x3
    subs x1, x1, #1
    b.ne 1b
2:  ret

/* x0 = hay, x1 = haylen, x2 = NUL-terminated needle -> x0 = 1 found / 0 not. */
contains:
    mov  x9, x2
    mov  x10, xzr
1:  ldrb w3, [x9, x10]
    cbz  w3, 2f
    add  x10, x10, #1
    b    1b
2:  cbz  x10, nf
    cmp  x1, x10
    b.lo nf
    sub  x11, x1, x10
    mov  x12, xzr
outer:
    mov  x13, xzr
inner:
    cmp  x13, x10
    b.eq fnd
    add  x14, x12, x13
    ldrb w15, [x0, x14]
    ldrb w16, [x2, x13]
    cmp  w15, w16
    b.ne nxt
    add  x13, x13, #1
    b    inner
nxt:
    cmp  x12, x11
    b.eq nf
    add  x12, x12, #1
    b    outer
fnd:
    mov  x0, #1
    ret
nf:
    mov  x0, xzr
    ret

    .section ".rodata", "a"
msg_pass:   .asciz "%(marker)s_PASS\n"
msg_fail:   .asciz "%(marker)s_FAIL\n"
pat_tprobe: .asciz "%(pat_probe)s"
pat_ts0:    .asciz "%(pat_s0)s"
pat_sprobe: .asciz "kshimreport.kshim_probe="
pat_ss0:    .asciz "kshimreport.kshim_s0="

    .section ".data", "aw"
    .balign 8
saved_dt:     .quad 0
saved_sum:    .quad 0
expected_cel: .quad %(cel)d
case_mode:    .quad %(mode)d
    .balign 8
corrupt_hdr:
    .byte 0,0,0,0                 /* magic = 0 (not an FDT) */
    .byte 0,0,0,40                /* big-endian totalsize = 40 */
    .zero 32
    .balign 16
harness_stack:
    .zero 512
harness_stack_top:

    .section ".runtime", "a"
    .balign 4096
runtime_base:
    .incbin "%(runtime)s"
'''

# Corrupt case: overwrite the incoming x0 with the local invalid header.
SETUP_CORRUPT = (
    "    adrp x0, corrupt_hdr\n"
    "    add  x0, x0, :lo12:corrupt_hdr\n"
)

# Valid case: the runtime must have produced both androidboot target tokens
# and no source token may remain.
VERIFY_VALID = (
    "    mov  x0, x19\n"
    "    bl   fdt_len\n"
    "    adrp x2, pat_tprobe\n"
    "    add  x2, x2, :lo12:pat_tprobe\n"
    "    bl   contains\n"
    "    cbz  x0, fail\n"
    "    mov  x0, x19\n"
    "    bl   fdt_len\n"
    "    adrp x2, pat_ts0\n"
    "    add  x2, x2, :lo12:pat_ts0\n"
    "    bl   contains\n"
    "    cbz  x0, fail\n"
    "    mov  x0, x19\n"
    "    bl   fdt_len\n"
    "    adrp x2, pat_sprobe\n"
    "    add  x2, x2, :lo12:pat_sprobe\n"
    "    bl   contains\n"
    "    cbnz x0, fail\n"
    "    mov  x0, x19\n"
    "    bl   fdt_len\n"
    "    adrp x2, pat_ss0\n"
    "    add  x2, x2, :lo12:pat_ss0\n"
    "    bl   contains\n"
    "    cbnz x0, fail\n"
)

# Missing / corrupt cases: the DT checksum (byte sum) must be unchanged; this
# is a checksum comparison, not a full byte-equality proof.
VERIFY_SUM = (
    "    mov  x0, x19\n"
    "    bl   fdt_len\n"
    "    mov  x0, x19\n"
    "    bl   sum_bytes\n"
    "    adrp x9, saved_sum\n"
    "    add  x9, x9, :lo12:saved_sum\n"
    "    ldr  x10, [x9]\n"
    "    cmp  x2, x10\n"
    "    b.ne fail\n"
)

# name, mode, setup, -append, verify block
CASES = (
    ("valid",   0, "",             APPEND_VALID, VERIFY_VALID),
    ("missing", 1, "",             APPEND_OTHER, VERIFY_SUM),
    ("corrupt", 2, SETUP_CORRUPT,  APPEND_OTHER, VERIFY_SUM),
)


def sh(cmd, cwd, timeout, log):
    """Run cmd, tee output to log, return (returncode, text). None = timeout."""
    argv = [str(c) for c in cmd]
    try:
        p = subprocess.run(argv, cwd=str(cwd), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        data, rc = p.stdout or b"", p.returncode
    except subprocess.TimeoutExpired as exc:
        data, rc = (exc.stdout or b""), None
    Path(log).write_bytes(data)
    return rc, data.decode("utf-8", "replace")


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def build_fixture(cc, oc, work, tag, runtime_s, ld, cel, mode, setup, verify,
                  pat):
    src = work / (tag + ".s")
    obj = work / (tag + ".o")
    elf = work / (tag + ".elf")
    binf = work / (tag + ".bin")
    src.write_text(ASM % {"cel": cel, "mode": mode, "setup": setup,
                          "verify": verify, "runtime": runtime_s,
                          "marker": pat["marker"],
                          "pat_probe": pat["pat_probe"],
                          "pat_s0": pat["pat_s0"]})
    steps = (
        [cc, "-c", "-o", str(obj), str(src)],
        [cc, "-nostdlib", "-nostartfiles", "-static", "-Wl,--build-id=none",
         "-Wl,-e,_start", "-Wl,-T," + str(ld), "-o", str(elf), str(obj)],
        [oc, "-O", "binary", str(elf), str(binf)],
    )
    for i, step in enumerate(steps):
        rc, out = sh(step, work, 120, work / (tag + "." + str(i) + ".log"))
        if rc != 0:
            raise SystemExit("fixture build failed (%s step %d):\n%s"
                             % (tag, i, out[-2000:]))
    data = bytearray(binf.read_bytes())
    if len(data) < 0x40:
        raise SystemExit("fixture too small: " + tag)
    # image_size covers the entire fixture (little-endian u64 at offset 0x10).
    struct.pack_into("<Q", data, 0x10, len(data))
    binf.write_bytes(data)
    return binf


def check(rc, text, trace, marker):
    pass_token = marker + "_PASS"
    fail_token = marker + "_FAIL"
    problems = []
    if rc is None:
        problems.append("timeout")
    elif rc != 0:
        problems.append("returncode=" + str(rc))
    passes = text.count(pass_token)
    if passes == 0:
        problems.append("missing " + pass_token)
    elif passes != 1:
        problems.append("expected exactly one " + pass_token + ", got " +
                        str(passes))
    if fail_token in text:
        problems.append(fail_token + " marker")
    problems += ["trace: " + t for t in TRACE_BAD if t in trace]
    return problems


def run_case(cc, oc, out, ld, rt, dtb, el, case, pat):
    name, mode, setup, append, verify = case
    tag = "el%d_%s" % (el, name)
    w = out / tag
    w.mkdir(exist_ok=True)
    binf = build_fixture(cc, oc, w, tag, rt, ld, el << 2, mode,
                         setup, verify, pat)
    machine = "virt,gic-version=3" + (",virtualization=on" if el == 2 else "")
    trace = w / (tag + ".trace")
    # PASS/FAIL is reported only through semihosting SYS_WRITE0 on an explicit
    # stdio chardev; the PL011 has no backend and the runtime never drives it.
    qemu = ["qemu-system-aarch64", "-M", machine, "-cpu", "cortex-a76",
            "-accel", "tcg", "-smp", "1", "-m", "256M", "-display", "none",
            "-monitor", "none", "-serial", "none", "-no-reboot",
            "-chardev", "stdio,id=semihost",
            "-semihosting-config", "enable=on,target=native,chardev=semihost",
            "-kernel", str(binf), "-dtb", str(dtb), "-append", append,
            "-d", "int,guest_errors", "-D", str(trace)]
    rc, text = sh(qemu, w, 10, w / (tag + ".qemu.log"))
    raw = trace.read_bytes() if trace.exists() else b""
    trace_bytes = len(raw)
    # Validate against the full trace, then keep a bounded artefact.
    problems = check(rc, text, raw.decode("utf-8", "replace"), pat["marker"])
    trace.write_bytes(raw[:4096])
    return {
        "el": el, "case": name, "status": "fail" if problems else "pass",
        "returncode": rc, "problems": problems, "trace_bytes": trace_bytes,
        "trace_head": raw[:4096].decode("utf-8", "replace").splitlines()[:8],
        "fixture_sha256": sha(binf),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--dtb", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--mode", choices=("dt", "resources"), default="dt")
    a = ap.parse_args()
    pat = MODES[a.mode]

    # Resolve every path so relative inputs/outputs work regardless of CWD.
    a.runtime = a.runtime.resolve()
    a.dtb = a.dtb.resolve()
    a.out = a.out.resolve()

    runtime_size = a.runtime.stat().st_size
    if runtime_size != 4 * 1024 * 1024:
        raise SystemExit("runtime size mismatch: expected 4194304 bytes, got " +
                         str(runtime_size))

    cc = os.environ.get("AARCH64_CC", "aarch64-linux-gnu-gcc")
    oc = os.environ.get("AARCH64_OBJCOPY", "aarch64-linux-gnu-objcopy")
    out = a.out
    out.mkdir(parents=True, exist_ok=True)
    ld = out / "fixture.ld"
    ld.write_text(LINKER)
    rt = str(a.runtime).replace("\\", "/")

    runs, failures = [], []
    for el in (1, 2):
        for case in CASES:
            res = run_case(cc, oc, out, ld, rt, a.dtb, el, case, pat)
            runs.append(res)
            if res["problems"]:
                failures.append("EL%d %s: %s"
                                % (el, case[0], "; ".join(res["problems"])))

    if a.mode == "resources":
        scope = ("synthetic-only: RESOURCES_ONLY diagnostic runtime regression "
                 "under QEMU virt; resource probing runs against the synthetic "
                 "virt fixture, which has no real Qualcomm resources and this "
                 "is not a hardware MMIO proof (no hardware, no device tree "
                 "deployment)")
    else:
        scope = ("synthetic-only: DT_ONLY diagnostic runtime regression under "
                 "QEMU virt; DT_ONLY probes no peripherals (no hardware, no "
                 "device tree deployment, no physical Qualcomm resource "
                 "claim)")

    validation = {
        "status": "fail" if failures else "pass",
        "mode": a.mode,
        "scope": scope,
        "expectedEL": [1, 2],
        "casesPerEL": [c[0] for c in CASES],
        "hashes": {"runtime": sha(a.runtime), "dtb": sha(a.dtb)},
        "runs": runs,
        "failures": failures,
    }
    (out / "validation.json").write_text(json.dumps(validation, indent=2))

    if failures:
        sys.stderr.write(pat["marker"] + " REGRESSION FAILURES: " +
                         " | ".join(failures) + os.linesep)
        return 1
    print("%s diagnostic regression (%s mode): PASS "
          "(EL1+EL2, valid/missing/corrupt)" % (pat["marker"], a.mode))
    return 0


if __name__ == "__main__":
    sys.exit(main())
