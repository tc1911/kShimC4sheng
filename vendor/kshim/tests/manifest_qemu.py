#!/usr/bin/env python3
"""Exercise the sibling packer's actual Manifest ABI with the ARM64 runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct

from qemu_menu import run_with_label


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, **kwargs)


def escape_incbin(path):
    return path.as_posix().replace("\\", "\\\\").replace('"', '\\"')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=Path("build/manifest/kShimC.bin"))
    parser.add_argument("--out", type=Path, default=Path("build/manifest-regression"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    packer_root = root.parent / "MultiBootKernelPatcher"
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    runtime = args.runtime.resolve()
    assert runtime.stat().st_size == 4 * 1024 * 1024, "Manifest ABI requires the 4 MiB slot"
    with (out / "packer-build.log").open("w") as log:
        run(["cmake", "-S", packer_root, "-B", out / "packer", "-DBUILD_SHIMS=OFF"],
            stdout=log, stderr=subprocess.STDOUT)
        run(["cmake", "--build", out / "packer", "-j4"], stdout=log, stderr=subprocess.STDOUT)

    payload_dts = out / "payload.dts"
    payload_dtb = out / "payload.dtb"
    payload_dts.write_text('/dts-v1/; / { #address-cells=<2>; #size-cells=<2>; '
                           'model="kshim-payload-fixture"; '
                           'chosen { bootargs="linux-only"; }; };\n')
    run(["dtc", "-I", "dts", "-O", "dtb", "-o", payload_dtb, payload_dts])

    base_dts = out / "base.dts"
    base_dtb = out / "base.dtb"
    base_dts.write_text('/dts-v1/; / { #address-cells=<2>; #size-cells=<2>; '
                        'model="kshim-base-fixture"; '
                        'chosen { bootargs="android-base-only"; }; };\n')
    run(["dtc", "-I", "dts", "-O", "dtb", "-o", base_dtb, base_dts])

    simple_template = '''.global _start
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
    cbz x0, fail
    ldr w1, [x0]
    mov w2, #0x0dd0
    movk w2, #0xedfe, lsl #16
    cmp w1, w2
    b.ne fail
    adr x4, message
    b print
fail:
    adr x4, error
print:
    mov x5, #0x09000000
1:  ldrb w6, [x4], #1
    cbz w6, finish
    str w6, [x5]
    b 1b
finish:
    mov x0, #8
    movk x0, #0x8400, lsl #16
    hvc #0
    b .
message: .asciz "PAYLOAD_NAME_PASS\\n"
error: .asciz "PAYLOAD_BAD_DTB\\n"
'''

    dtb_template = '''.global _start
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
    cbz x0, fail
    ldr w1, [x0]
    mov w2, #0x0dd0
    movk w2, #0xedfe, lsl #16
    cmp w1, w2
    b.ne fail
    mov x19, x0
    ldr w20, [x19, #4]
    rev w20, w20
    cmp x20, #40
    b.lo fail
    mov x9, #0x200000
    cmp x20, x9
    b.hi fail
    adr x7, expected_model
    mov x8, #(model_end - expected_model)
    bl find_pattern
    cbz x0, fail
    adr x7, expected_bootargs
    mov x8, #(bootargs_end - expected_bootargs)
    bl find_pattern
    cbz x0, fail
    adr x7, expected_ram
    mov x8, #16
    bl find_pattern
    cbz x0, fail
    adr x4, message
    b print
find_pattern:
    add x10, x19, x20
    sub x10, x10, x8
    add x10, x10, #1
    mov x9, x19
    cmp x9, x10
    b.hs 8f
6:  mov x11, #0
    mov x12, x9
    mov x13, x7
7:  cmp x11, x8
    b.hs 9f
    ldrb w14, [x12, x11]
    ldrb w15, [x13, x11]
    cmp w14, w15
    b.ne 8f
    add x11, x11, #1
    b 7b
8:  add x9, x9, #1
    cmp x9, x10
    b.lo 6b
    mov x0, #0
    ret
9:  mov x0, #1
    ret
fail:
    adr x4, error
print:
    mov x5, #0x09000000
2:  ldrb w6, [x4], #1
    cbz w6, finish
    str w6, [x5]
    b 2b
finish:
    mov x0, #8
    movk x0, #0x8400, lsl #16
    hvc #0
    b .
message: .asciz "PAYLOAD_NAME_PASS\\n"
error: .asciz "PAYLOAD_BAD_DTB\\n"
.balign 8
expected_model:
.asciz "MODEL"
model_end:
expected_bootargs:
.asciz "BOOTARGS"
bootargs_end:
.balign 8
expected_ram:
.byte 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00
.byte 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00
'''

    dtb_fixtures = {
        "D": (payload_dtb, "kshim-payload-fixture", "linux-only"),
        "E": (base_dtb, "kshim-base-fixture", "android-base-only"),
    }
    for name in ("A", "B", "D", "E"):
        assembly = out / (name + ".S")
        if name not in dtb_fixtures:
            assembly.write_text(simple_template.replace("NAME", name))
        else:
            dtb_path, model, bootargs = dtb_fixtures[name]
            assembly.write_text(
                dtb_template.replace("NAME", name)
                .replace("MODEL", model)
                .replace("BOOTARGS", bootargs))
        run(["aarch64-linux-gnu-gcc", "-nostdlib", "-static", "-Wl,--build-id=none",
             "-Wl,-Ttext=0", assembly, "-o", out / (name + ".elf")], capture_output=True)
        run(["aarch64-linux-gnu-objcopy", "-O", "binary", out / (name + ".elf"),
             out / (name + ".bin")])

    # The default-A case stays automatic. The payload/copy/DT cases below boot
    # the generic "Alternate B" payload, which under SESSION_SETTINGS=y is
    # rendered in the menu but is deliberately not an automatic candidate, so
    # they select it by label like select-linux does. The distinct-base-dtb
    # fixture also selects explicitly: its base DT variant has a generic
    # UNKNOWN role, so the production menu renders Android 原厂, Linux A and
    # Alternate B and waits rather than running the automatic Android
    # placeholder, so fixture E is reached by choosing "Linux A" by label.
    cases = [
        {"name": "default", "default": "Image-A", "key": b"", "copied": False, "dtb": False, "new": False},
        {"name": "select-linux", "default": "Image-A", "key": b"", "label": "Alternate B", "copied": False, "dtb": False, "new": False},
        {"name": "copy-freeexec", "default": "Image-B", "key": b"", "label": "Alternate B", "copied": True, "dtb": False, "new": False},
        {"name": "payload-dtb", "default": "Image-B", "key": b"", "label": "Alternate B", "copied": False, "dtb": True, "new": False},
        {"name": "short-dtb-entry", "default": "Image-B", "key": b"", "copied": False, "dtb": True, "new": False},
        {"name": "bad-dtb-structure", "default": "Image-B", "key": b"", "copied": False, "dtb": True, "new": False},
        {"name": "distinct-base-dtb", "default": "Image-A", "key": b"", "label": "Linux A", "copied": False, "dtb": False, "new": True},
        {"name": "distinct-alt-dtb", "default": "Image-B", "key": b"", "label": "Alternate B", "copied": False, "dtb": False, "new": True},
        {"name": "freeexec-dtb", "default": "Image-B", "key": b"", "label": "Alternate B", "copied": False, "dtb": True, "new": False},
        {"name": "base-only-android", "default": "Image-A", "key": b"", "copied": False, "dtb": False, "new": False, "base_only": True},
    ]
    expected_map = {
        "default": "PAYLOAD_A_PASS",
        "select-linux": "PAYLOAD_B_PASS",
        "copy-freeexec": "PAYLOAD_B_PASS",
        "payload-dtb": "PAYLOAD_D_PASS",
        "short-dtb-entry": "PAYLOAD_A_PASS",
        "bad-dtb-structure": "PAYLOAD_A_PASS",
        "distinct-base-dtb": "PAYLOAD_E_PASS",
        "distinct-alt-dtb": "PAYLOAD_D_PASS",
        "freeexec-dtb": "PAYLOAD_D_PASS",
        "base-only-android": "PAYLOAD_A_PASS",
    }

    results = []
    for case in cases:
        name = case["name"]
        default = case["default"]
        key = case["key"]
        with_dtb = case["dtb"]
        is_new = case["new"]
        base_only = case.get("base_only", False)
        rejected = name in ("short-dtb-entry", "bad-dtb-structure")
        config = out / (name + ".cfg")
        packed = out / (name + ".bin")

        extra = "Type=FreeExec\n" if case["name"] == "freeexec-dtb" else ("Type=FreeExec\nCopyTo=0x47000000\nCopySizeMax=0x10000\n" if case["copied"] else "Type=Linux\n")

        base_path = out / "E.bin" if is_new else out / "A.bin"
        alt_path = out / "D.bin" if (with_dtb or is_new) else out / "B.bin"
        base_extra = "DeviceTreeBlob=Image-Base-DTB\n" if is_new else ""
        dtb_section = ""
        if with_dtb:
            extra += "DeviceTreeBlob=Image-DTB\n"
            dtb_section = f"[Image-DTB]\nName=Payload DT\nType=DTB\nPath={payload_dtb}\nBaseImage=false\n"
        elif is_new:
            extra += "DeviceTreeBlob=Image-DTB\n"
            dtb_section = (f"[Image-DTB]\nName=Payload DT\nType=DTB\nPath={payload_dtb}\nBaseImage=false\n"
                           f"[Image-Base-DTB]\nName=Base DT\nType=DTB\nPath={base_dtb}\nBaseImage=false\n")

        image_b_block = "" if base_only else (
            f"[Image-B]\nName=Alternate B\nPath={alt_path}\nBaseImage=false\n"
            f"{extra}\n{dtb_section}\n")

        config.write_text(f"""[Pack]
Shim={runtime}
Output={packed}
Default={default}
Timeout=1500
[Image-A]
Name=Linux A
Path={base_path}
Type=Linux
BaseImage=true
{base_extra}
{image_b_block}[Manifest]
Type=Manifest
""")
        with (out / (name + "-pack.log")).open("w") as log:
            run([out / "packer/MultiBootKernelPatcher", config], stdout=log, stderr=subprocess.STDOUT)
        if base_only:
            # Base-only layout: one base Image plus the auto-added Attached Shim.
            data = packed.read_bytes()
            manifest = struct.unpack_from("<Q", data, 0x20)[0]
            assert struct.unpack_from("<I", data, manifest + 8)[0] == 2, "base-only manifest count != 2"
            assert struct.unpack_from("<I", data, manifest + 12)[0] == 0, "base-only manifest default != 0"
        if rejected:
            data = bytearray(packed.read_bytes())
            manifest = struct.unpack_from("<Q", data, 0x20)[0]
            cursor = manifest + 60
            for _ in range(struct.unpack_from("<I", data, manifest + 8)[0]):
                offset, size, entry_type = struct.unpack_from("<QQI", data, cursor)
                if entry_type == 5:
                    if name == "short-dtb-entry":
                        struct.pack_into("<Q", data, cursor + 8, size - 1)
                    else:
                        structure = struct.unpack_from(">I", data, offset + 8)[0]
                        struct.pack_into(">I", data, offset + structure, 0xffffffff)
                    break
                cursor += (56 + struct.unpack_from("<I", data, cursor + 52)[0] + 3) & ~3
            else:
                raise AssertionError("No DTB entry in packed fixture")
            packed.write_bytes(data)
        command = ["qemu-system-aarch64", "-machine", "virt,gic-version=3",
                   "-accel", "tcg", "-cpu", "cortex-a76", "-smp", "1", "-m", "256M",
                   "-display", "none", "-monitor", "none", "-serial", "stdio",
                   "-no-reboot", "-kernel", packed]
        try:
            if case.get("label"):
                result = run_with_label(command, case["label"], 20)
                # Keep the check=True contract of the no-selection path.
                result.check_returncode()
            else:
                result = run(command, input=key, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=20)
        except subprocess.SubprocessError as error:
            (out / (name + ".log")).write_bytes(error.stdout or b"")
            raise
        output = result.stdout.decode(errors="replace")
        (out / (name + ".log")).write_text(output)
        expected = expected_map[name]
        assert expected in output, output
        if rejected:
            assert "Manifest not found, booting base image" in output, output
        if base_only:
            assert "Manifest not found, booting base image" not in output, output
        results.append({"case": name, "expected": expected, "passed": True})
        print(name + ": PASS", flush=True)
    (out / "validation.json").write_text(json.dumps({
        "runtime_sha256": hashlib.sha256(runtime.read_bytes()).hexdigest(),
        "case_count": len(results),
        "dtb_scope": ("Content-marker and RAM-region byte-pattern checks over the DTB slot (bounded scan "
                      "within the BE total-size prefix, plus ARM64 header magic) for the QEMU 256M fixture; "
                      "not a byte-for-byte DTB identity check. Separate host tests verify semantic FDT "
                      "properties."),
        "tests": results}, indent=2) + "\n")


if __name__ == "__main__":
    main()
