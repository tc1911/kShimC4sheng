#!/usr/bin/env python3
"""Verify the actual ARM64 boot entry and both UEFI handoffs with Unicorn.

Usage: python boot_emulation.py build/hdk8150 (or build/hdk8450; requires unicorn)
This does not emulate the display controller, PMIC, or UEFI itself.
"""
from pathlib import Path
import json
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_BLOCK
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_LR, UC_ARM64_REG_PC, UC_ARM64_REG_SP


out = Path(sys.argv[1])
manifest = json.loads((out / "manifest.json").read_text())
symbols = {}
for line in subprocess.check_output(["aarch64-linux-gnu-nm", "-n",
                                     out / "firmware/kShimC.elf"], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
base = symbols["_start"]
kernel = (out / "kernel.bin").read_bytes()
firmware = (out / "firmware/kShimC.bin").read_bytes()
board = manifest["board"].removeprefix("qcom-").removeprefix("xiaomi-")
fd = (out / (board + "-uefi.fd")).read_bytes()
uefi_entry = int(manifest["uefi_entry"], 16)
scratch = int(manifest["scratch_address"], 16)
args = (0x90001000, 0x11223344, 0x55667788, 0x99aabbcc)

for source in (base, base - 0x100000, base + 0x100000, base + 0x4000000):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    return_address = base + 0xf00000
    ranges = sorted((start & ~0xfff, (end + 0xfff) & ~0xfff) for start, end in (
        (base, symbols["__image_end_address__"]), (source, source + len(kernel)),
        (scratch, scratch + 4096), (uefi_entry, uefi_entry + len(fd)),
        (return_address, return_address + 4096)))
    merged = []
    for start, end in ranges:
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    for start, end in merged:
        uc.mem_map(start, end - start)
    uc.mem_write(source, kernel)
    for index, value in enumerate(args):
        uc.reg_write(UC_ARM64_REG_X0 + index, value)
    hits = [0]

    def at_destination(machine, address, size, data):
        hits[0] += 1
        if source != base or hits[0] > 1:
            machine.emu_stop()

    hook = uc.hook_add(UC_HOOK_BLOCK, at_destination, begin=base, end=base)
    uc.emu_start(source, 0, timeout=30000000, count=100000000)
    uc.hook_del(hook)
    assert uc.reg_read(UC_ARM64_REG_PC) == base, hex(uc.reg_read(UC_ARM64_REG_PC))
    assert bytes(uc.mem_read(base, len(firmware))) == firmware
    assert tuple(uc.reg_read(UC_ARM64_REG_X0 + index) for index in range(4)) == args
    # Poison BSS to ensure the real startup clears every byte.
    bss, bss_end = symbols["__bss_start__"], symbols["__bss_end__"]
    uc.mem_write(bss, bytes([0xa5]) * (bss_end - bss))
    uc.emu_start(base, symbols["kshim_main"], timeout=30000000, count=5000000)
    assert uc.reg_read(UC_ARM64_REG_PC) == symbols["kshim_main"]
    assert bytes(uc.mem_read(bss, bss_end - bss)) == bytes(bss_end - bss)
    assert uc.reg_read(UC_ARM64_REG_SP) == symbols["__stack_top__"]
    assert tuple(uc.reg_read(UC_ARM64_REG_X0 + index) for index in range(4)) == args
    for address in range(symbols["__reladyn_start__"], symbols["__reladyn_end__"], 24):
        where, info, addend = struct.unpack("<QQQ", uc.mem_read(address, 24))
        assert info & 0xffffffff == 1027  # R_AARCH64_RELATIVE
        assert struct.unpack("<Q", uc.mem_read(where, 8))[0] == addend
    print(f"Boot copy from {source:#x}, arguments, BSS, stack and relocations passed", flush=True)

    if source != base:
        continue
    uc.reg_write(UC_ARM64_REG_LR, return_address)
    uc.emu_start(symbols["kshim_boot_set_args"], return_address, count=1000)
    for action in (1, 2):
        uc.mem_write(uefi_entry, bytes([0xa5]) * len(fd))
        uc.reg_write(UC_ARM64_REG_X0, action)
        uc.reg_write(UC_ARM64_REG_LR, return_address)
        uc.emu_start(symbols["kshim_menu_activate"], uefi_entry,
                     timeout=30000000, count=100000000)
        assert uc.reg_read(UC_ARM64_REG_PC) == uefi_entry
        assert bytes(uc.mem_read(uefi_entry, len(fd))) == fd
        assert tuple(uc.reg_read(UC_ARM64_REG_X0 + index) for index in range(4)) == args
        print(f"UEFI option {action}: identical FD copied, entry and arguments passed", flush=True)

# A loader placing a large payload over the reserved relocator page must be
# rejected before any source bytes are changed.
uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
source = scratch - 0x100000
uc.mem_map(source & ~0xfff, (len(kernel) + 0x1fff) & ~0xfff)
uc.mem_write(source, kernel)
before = bytes(uc.mem_read(scratch, 4096))
uc.emu_start(source, 0, count=1000)
assert bytes(uc.mem_read(scratch, 4096)) == before
assert source <= uc.reg_read(UC_ARM64_REG_PC) < source + 256
print("Source overlapping the relocator page: safely refused before writes")
