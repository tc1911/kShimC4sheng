#!/usr/bin/env python3
"""Exercise Manifest handoff after the real LVGL framebuffer menu."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time


FRAMEBUFFER_ADDRESS = 0x44000000
FRAMEBUFFER_PIXELS = 320 * 240
FREEEXEC_ADDRESS = 0x47000000
BASE_IMAGE_ADDRESS = 0x40200000


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def payload_source(identity, expected_address):
    # A/B keep the original fixed-address identity check byte-for-byte. The
    # DIAGNOSTIC verifier additionally proves the dispatch landed inside the
    # real packed image range: _start must be strictly above BASE_IMAGE_ADDRESS
    # and strictly below BASE_IMAGE_ADDRESS + the packed ARM64 image_size that
    # the packer left at base+0x10 (the header is not restored before handoff).
    if identity == "DIAGNOSTIC":
        identity_label = "packed-linux"
        identity_check = (
            f"    adr x1, _start\n"
            f"    ldr x2, ={expected_address:#x}\n"
            f"    cmp x1, x2\n"
            f"    b.ls fail_identity\n"
            f"    ldr x3, [x2, #16]\n"
            f"    add x3, x2, x3\n"
            f"    cmp x1, x3\n"
            f"    b.hs fail_identity\n"
        )
    else:
        identity_label = "in-place" if identity == "A" else "copied"
        identity_check = (
            f"    adr x1, _start\n"
            f"    ldr x2, ={expected_address:#x}\n"
            f"    cmp x1, x2\n"
            f"    b.ne fail_identity\n"
        )
    success = (
        f"PAYLOAD_{identity}_PASS dtb=ok mmu=off cpus=off "
        f"framebuffer=varied identity="
        f"{identity_label}\\n"
    )
    return f'''/* Position-independent handoff verifier. */
.global _start
.text
_start:
    b start
    nop
    .quad 0
    .quad 4096
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .ascii "ARM\\x64"
    .long 0

start:
    mov x19, x0
    cbz x19, fail_dtb
    ldr w1, [x19]
    mov w2, #0x0dd0
    movk w2, #0xedfe, lsl #16
    cmp w1, w2
    b.ne fail_dtb

{identity_check}
    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #1
    b.eq read_sctlr_el1
    cmp x1, #2
    b.ne fail_el
    mrs x2, sctlr_el2
    b check_sctlr
read_sctlr_el1:
    mrs x2, sctlr_el1
check_sctlr:
    tbnz x2, #0, fail_mmu

    mov x20, #1
check_cpus:
    mov x0, #4
    movk x0, #0xc400, lsl #16
    mov x1, x20
    mov x2, #0
    hvc #0
    cmp x0, #1
    b.ne fail_cpus
    add x20, x20, #1
    cmp x20, #4
    b.lo check_cpus

    ldr x3, ={FRAMEBUFFER_ADDRESS:#x}
    ldr w4, [x3]
    cmp w4, #0
    cset w7, ne
    mov w6, #0
    mov x5, #1
    ldr x8, ={FRAMEBUFFER_PIXELS}
check_framebuffer:
    ldr w9, [x3, x5, lsl #2]
    cmp w9, #0
    cset w10, ne
    orr w7, w7, w10
    cmp w9, w4
    cset w10, ne
    orr w6, w6, w10
    add x5, x5, #1
    cmp x5, x8
    b.lo check_framebuffer
    cbz w7, fail_framebuffer_blank
    cbz w6, fail_framebuffer_flat

    adr x4, message_success
    b print
fail_dtb:
    adr x4, message_dtb
    b print
fail_identity:
    adr x4, message_identity
    b print
fail_el:
    adr x4, message_el
    b print
fail_mmu:
    adr x4, message_mmu
    b print
fail_cpus:
    adr x4, message_cpus
    b print
fail_framebuffer_blank:
    adr x4, message_framebuffer_blank
    b print
fail_framebuffer_flat:
    adr x4, message_framebuffer_flat

print:
    ldr x5, =0x09000000
1:
    ldrb w6, [x4], #1
    cbz w6, power_off
    str w6, [x5]
    b 1b
power_off:
    mov x0, #8
    movk x0, #0x8400, lsl #16
    hvc #0
2:
    wfe
    b 2b

    .ltorg
message_success: .asciz "{success}"
message_dtb: .asciz "PAYLOAD_{identity}_FAIL dtb\\n"
message_identity: .asciz "PAYLOAD_{identity}_FAIL identity\\n"
message_el: .asciz "PAYLOAD_{identity}_FAIL exception-level\\n"
message_mmu: .asciz "PAYLOAD_{identity}_FAIL mmu-enabled\\n"
message_cpus: .asciz "PAYLOAD_{identity}_FAIL secondary-cpu-online\\n"
message_framebuffer_blank: .asciz "PAYLOAD_{identity}_FAIL framebuffer-blank\\n"
message_framebuffer_flat: .asciz "PAYLOAD_{identity}_FAIL framebuffer-flat\\n"
'''


def build_payload(out, identity, expected_address):
    assembly = out / f"payload-{identity}.S"
    elf = out / f"payload-{identity}.elf"
    binary = out / f"payload-{identity}.bin"
    assembly.write_text(payload_source(identity, expected_address))
    run([
        "aarch64-linux-gnu-gcc", "-nostdlib", "-static",
        "-Wl,--build-id=none", "-Wl,-Ttext=0", assembly, "-o", elf,
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    run(["aarch64-linux-gnu-objcopy", "-O", "binary", elf, binary])
    return binary


def write_pack_config(path, runtime, output, payload_a, payload_b, default):
    path.write_text(f"""[Pack]
Shim={runtime}
Output={output}
Default=Image-{default}
Timeout=1500

[Image-A]
Name=Linux A
Path={payload_a}
Type=Linux
BaseImage=true

[Image-B]
Name=FreeExec B
Path={payload_b}
Type=FreeExec
BaseImage=false
CopyTo={FREEEXEC_ADDRESS:#x}
CopySizeMax=0x10000

[Manifest]
Type=Manifest
""")


def write_diagnostic_pack_config(path, runtime, output, payload_a,
                                 payload_diagnostic, payload_dtb):
    # Diagnostic-only manifest. Default points at the non-copy diagnostic
    # Linux entry, whose custom DTB marks /chosen armada,boot-stage=maintenance
    # so the real session resolver recognises it as a board-safe diagnostic.
    # Base A stays the ordinary base fallback. The diagnostic Linux entry
    # includes DeviceTreeBlob and sets no CopyTo or CopySizeMax.
    path.write_text(f"""[Pack]
Shim={runtime}
Output={output}
Default=Image-Diagnostic
Timeout=1500

[Image-A]
Name=Linux A
Path={payload_a}
Type=Linux
BaseImage=true

[Image-Diagnostic]
Name=Linux Diagnostic
Path={payload_diagnostic}
Type=Linux
BaseImage=false
EntryOffset=0
Align=21
DeviceTreeBlob=Image-Diagnostic-DTB

[Image-Diagnostic-DTB]
Name=Diagnostic DTB
Path={payload_dtb}
Type=DTB
BaseImage=false
Align=3

[Manifest]
Type=Manifest
""")


def config_symbol_enabled(text, symbol):
    pattern = re.compile(
        r"^#define[ \t]+" + re.escape(symbol) + r"[ \t]+1[ \t]*$",
        re.MULTILINE)
    return pattern.search(text) is not None


def validate_diagnostic_config(out):
    # The generated header is authoritative: a legacy runtime profile would
    # still produce a valid-looking pass, so this fails before QEMU rather than
    # trusting the requested .config alone.
    config_h = out / "generated/include/config.h"
    if not config_h.is_file():
        raise RuntimeError(f"Generated configuration header missing: {config_h}")
    text = config_h.read_text()
    evidence = {}
    for symbol in ("CONFIG_KSHIM_MENU_SESSION_SETTINGS",
                   "CONFIG_KSHIM_MENU_ADVANCED_ACTIONS"):
        if not config_symbol_enabled(text, symbol):
            raise RuntimeError(
                f"{config_h} does not enable {symbol}; the legacy runtime "
                "profile would report a false GREEN")
        evidence[symbol] = True
    return config_h, evidence


def build_runtime_fixture(out):
    # Real QEMU runtime DT for the exact execution machine, then only the two
    # synthetic board compatible strings are appended. No PMIC/hardware nodes
    # are invented; QEMU's own nodes are preserved untouched.
    dtb = out / "runtime-diagnostic.dtb"
    run([
        "qemu-system-aarch64", "-machine",
        f"virt,gic-version=3,virtualization=off,dumpdtb={dtb}",
        "-cpu", "cortex-a76", "-smp", "4", "-m", "256M",
        "-display", "none", "-monitor", "none", "-no-reboot",
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    existing = subprocess.check_output(
        ["fdtget", "-t", "s", str(dtb), "/", "compatible"]).decode().split()
    compatible = existing + ["lenovo,elden", "qcom,sm8750"]
    run(["fdtput", "-t", "s", str(dtb), "/", "compatible", *compatible])
    # Deterministic fixture: drop QEMU's per-boot random seeds if present so a
    # pre-fix and fixed runtime compare equal; functional nodes are not touched.
    present = subprocess.run(
        ["fdtget", "-p", str(dtb), "/chosen"], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False)
    if present.returncode == 0:
        properties = present.stdout.decode(errors="replace").split()
        for seed in ("rng-seed", "kaslr-seed"):
            if seed in properties:
                run(["fdtput", "-d", str(dtb), "/chosen", seed])
    return dtb


def build_payload_dtb(out):
    # Synthetic identity fixture only: the two board compatible strings plus the
    # maintenance boot-stage. No /memory node, so the real dt_memory_apply sees
    # the actual QEMU runtime memory snapshot instead of a fabricated one.
    dts = out / "payload-diagnostic.dts"
    dtb = out / "payload-diagnostic.dtb"
    dts.write_text(
        "/dts-v1/;\n"
        "\n"
        "/ {\n"
        "    #address-cells = <2>;\n"
        "    #size-cells = <2>;\n"
        '    compatible = "lenovo,elden", "qcom,sm8750";\n'
        "\n"
        "    chosen {\n"
        '        armada,boot-stage = "maintenance";\n'
        "    };\n"
        "};\n")
    run(["dtc", "-I", "dts", "-O", "dtb", "-o", dtb, dts])
    return dtb


def run_diagnostic_case(out, runtime, packer, timeout):
    config_h, config_symbols = validate_diagnostic_config(out)

    runtime_dtb = build_runtime_fixture(out)
    payload_dtb = build_payload_dtb(out)
    payload_a = build_payload(out, "A", BASE_IMAGE_ADDRESS)
    payload_diagnostic = build_payload(out, "DIAGNOSTIC", BASE_IMAGE_ADDRESS)

    name = "default-diagnostic"
    config = out / f"{name}.cfg"
    packed = out / f"{name}.bin"
    write_diagnostic_pack_config(config, runtime, packed, payload_a,
                                 payload_diagnostic, payload_dtb)
    with (out / f"{name}-pack.log").open("w") as log:
        run([packer, config], stdout=log, stderr=subprocess.STDOUT)

    command = [
        "qemu-system-aarch64", "-machine",
        "virt,gic-version=3,virtualization=off", "-accel",
        "tcg,thread=multi", "-cpu", "cortex-a76", "-smp", "4",
        "-m", "256M", "-display", "none", "-monitor", "none",
        "-serial", "stdio", "-no-reboot", "-kernel", packed,
        "-dtb", runtime_dtb,
    ]
    started = time.monotonic()
    timed_out = False
    returncode = None
    try:
        result = subprocess.run(
            [str(item) for item in command], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=timeout, check=False)
        output = result.stdout.decode(errors="replace")
        returncode = result.returncode
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or b"").decode(errors="replace")
        timed_out = True

    expected = (
        "PAYLOAD_DIAGNOSTIC_PASS dtb=ok mmu=off cpus=off "
        "framebuffer=varied identity=packed-linux"
    )
    # Only the exact diagnostic success marker counts; a base PAYLOAD_A_PASS
    # fallback and any explicit DIAGNOSTIC_FAIL both stay red.
    passed = (not timed_out and returncode == 0 and expected in output
              and "PAYLOAD_DIAGNOSTIC_FAIL" not in output)
    (out / f"{name}.log").write_text(output)
    print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)
    return [{
        "case": name,
        "default": "Image-Diagnostic",
        "packed_sha256": hashlib.sha256(packed.read_bytes()).hexdigest(),
        "runtime_dtb": str(runtime_dtb),
        "runtime_dtb_sha256":
            hashlib.sha256(runtime_dtb.read_bytes()).hexdigest(),
        "payload_dtb": str(payload_dtb),
        "payload_dtb_sha256":
            hashlib.sha256(payload_dtb.read_bytes()).hexdigest(),
        "config": {
            "generated_config_h": str(config_h),
            "generated_config_h_sha256":
                hashlib.sha256(config_h.read_bytes()).hexdigest(),
            "required_symbols": config_symbols,
        },
        "expected": expected,
        "command": [str(item) for item in command],
        "returncode": returncode,
        "timed_out": timed_out,
        "seconds": time.monotonic() - started,
        "passed": passed,
    }]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=None)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--case",
                        choices=("default-a", "default-b", "default-diagnostic"))
    parser.add_argument("--diagnostic-config", type=Path)
    args = parser.parse_args()

    diagnostic = args.case == "default-diagnostic"
    if args.diagnostic_config is not None and not diagnostic:
        parser.error(
            "--diagnostic-config is only valid with --case default-diagnostic")
    if diagnostic and not args.no_build and args.diagnostic_config is None:
        parser.error(
            "--case default-diagnostic with a build requires --diagnostic-config")

    root = Path(__file__).resolve().parents[1]
    packer_root = root.parent / "MultiBootKernelPatcher"
    if args.build is None:
        args.build = Path("build/qemu-manifest-ui-diagnostic" if diagnostic
                          else "build/qemu-manifest-ui")
    out = args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)

    if not args.no_build:
        config_source = (args.diagnostic_config.resolve() if diagnostic
                         else root / "configs/qemu_manifest_ui_defconfig")
        shutil.copy2(config_source, out / ".config")
        with (out / "configure.log").open("w") as log:
            run([
                "cmake", "-S", root, "-B", out, "-G", "Ninja",
                "-DKSHIM_STANDALONE=OFF",
            ], stdout=log, stderr=subprocess.STDOUT)
        with (out / "build.log").open("w") as log:
            run(["cmake", "--build", out, "-j2" if diagnostic else "-j8"],
                stdout=log, stderr=subprocess.STDOUT)

    runtime = out / "kShimC.bin"
    if runtime.stat().st_size != 4 * 1024 * 1024:
        raise RuntimeError("Manifest ABI requires an exact 4 MiB runtime slot")

    packer_build = out / "packer"
    with (out / "packer-build.log").open("w") as log:
        run(["cmake", "-S", packer_root, "-B", packer_build,
             "-DBUILD_SHIMS=OFF"], stdout=log, stderr=subprocess.STDOUT)
        run(["cmake", "--build", packer_build, "-j2" if diagnostic else "-j4"],
            stdout=log, stderr=subprocess.STDOUT)
    packer = packer_build / "MultiBootKernelPatcher"

    if diagnostic:
        results = run_diagnostic_case(out, runtime, packer, args.timeout)
    else:
        payload_a = build_payload(out, "A", BASE_IMAGE_ADDRESS)
        payload_b = build_payload(out, "B", FREEEXEC_ADDRESS)
        results = []

        for identity in ("A", "B"):
            name = f"default-{identity.lower()}"
            if args.case is not None and name != args.case:
                continue
            config = out / f"{name}.cfg"
            packed = out / f"{name}.bin"
            write_pack_config(config, runtime, packed, payload_a, payload_b,
                              identity)
            with (out / f"{name}-pack.log").open("w") as log:
                run([packer, config], stdout=log, stderr=subprocess.STDOUT)

            command = [
                "qemu-system-aarch64", "-machine",
                "virt,gic-version=3,virtualization=off", "-accel",
                "tcg,thread=multi", "-cpu", "cortex-a76", "-smp", "4",
                "-m", "256M", "-display", "none", "-monitor", "none",
                "-serial", "stdio", "-no-reboot", "-kernel", packed,
            ]
            started = time.monotonic()
            timed_out = False
            returncode = None
            try:
                result = subprocess.run(
                    [str(item) for item in command], stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    timeout=args.timeout, check=False)
                output = result.stdout.decode(errors="replace")
                returncode = result.returncode
            except subprocess.TimeoutExpired as error:
                output = (error.stdout or b"").decode(errors="replace")
                timed_out = True

            expected = (
                f"PAYLOAD_{identity}_PASS dtb=ok mmu=off cpus=off "
                f"framebuffer=varied identity="
                f"{'in-place' if identity == 'A' else 'copied'}"
            )
            passed = not timed_out and returncode == 0 and expected in output
            (out / f"{name}.log").write_text(output)
            results.append({
                "case": name,
                "default": f"Image-{identity}",
                "packed_sha256": hashlib.sha256(packed.read_bytes()).hexdigest(),
                "command": [str(item) for item in command],
                "returncode": returncode,
                "timed_out": timed_out,
                "seconds": time.monotonic() - started,
                "passed": passed,
            })
            print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)

    report = {
        "runtime_sha256": hashlib.sha256(runtime.read_bytes()).hexdigest(),
        "runtime_size": runtime.stat().st_size,
        "qemu": subprocess.check_output(
            ["qemu-system-aarch64", "--version"], text=True).splitlines()[0],
        "framebuffer": {
            "address": f"{FRAMEBUFFER_ADDRESS:#x}",
            "width": 320,
            "height": 240,
            "stride": 1280,
            "format": "ARGB8888",
        },
        "tests": results,
        "passed": all(item["passed"] for item in results),
    }
    (out / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
