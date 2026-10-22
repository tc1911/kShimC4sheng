#!/usr/bin/env python3
"""Generate hardware facts from the hash-verified Android source snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sm8150", type=Path, required=True)
parser.add_argument("--sm8250", type=Path, required=True)
parser.add_argument("--crdk", type=Path, required=True)
args = parser.parse_args()
out = args.crdk / "Library/AndroidBindingsLib"
out.mkdir(parents=True, exist_ok=True)
header = '''/* SPDX-License-Identifier: MIT */
#pragma once
#include <oskal/cr_stdint.h>
struct CrAndroidPin { uint32_t offset; const char *functions[10]; };
struct CrAndroidClock { uint32_t id, vote, mask, halt, rcg; };
const struct CrAndroidPin *CrAndroidPin(unsigned soc, unsigned pin);
const struct CrAndroidClock *CrAndroidClock(unsigned soc, unsigned id);
'''
(args.crdk / "Include/Library/cr_android.h").write_text(header)
source = '''/* SPDX-License-Identifier: MIT
 * Generated numeric hardware facts and mux names. Source commits and hashes
 * are recorded in the integration SM8150-SOURCES.json / SM8250-SOURCES.json.
 */
#include <Library/cr_android.h>
'''
for soc, directory, name in [(8150, args.sm8150, "sm8150"), (8250, args.sm8250, "kona")]:
    manifest = json.loads((root / f"ports/crdk/SM{soc}-SOURCES.json").read_text())
    def read(path):
        p = directory / path
        if hashlib.sha256(p.read_bytes()).hexdigest() != manifest["files"][path]:
            raise ValueError(f"Unexpected source hash: {path}")
        return p.read_text()
    pins = read(f"drivers/pinctrl/qcom/pinctrl-{name}.c")
    tiles = {m[0]: int(m[1], 16) for m in re.findall(r"#define\s+(NORTH|SOUTH|WEST|EAST)\s+(0x[0-9A-Fa-f]+)", pins)}
    entries = re.findall(r"\[\d+\]\s*=\s*PINGROUP\((\d+),\s*(\w+),\s*([^)]*)\)", pins)
    source += f"static const struct CrAndroidPin pins_{soc}[] = {{\n"
    for number, tile, functions in entries:
        mux = ["gpio"] + [f.strip() for f in functions.split(",")][:9]
        mux = ["NULL" if m == "NA" else json.dumps(m) for m in mux]
        source += f"    [{number}] = {{0x{tiles[tile] + int(number) * 4096:x}, {{{', '.join(mux)}}}}},\n"
    source += "};\n"
    clocks = read(f"drivers/clk/qcom/gcc-{name}.c")
    ids = dict(re.findall(r"#define\s+(GCC_\w+)\s+(\d+)", read(f"include/dt-bindings/clock/qcom,gcc-{name}.h")))
    rcgs = dict(re.findall(r"static struct clk_rcg2\s+(\w+)\s*=\s*\{\s*\.cmd_rcgr\s*=\s*(0x[0-9a-fA-F]+)", clocks))
    source += f"static const struct CrAndroidClock clocks_{soc}[] = {{\n"
    for clock, body in re.findall(r"static struct clk_branch\s+(gcc_qupv3_\w+)\s*=\s*\{(.*?)\n};", clocks, re.S):
        if clock.upper() not in ids: continue
        vote = re.search(r"\.enable_reg\s*=\s*(0x[0-9a-fA-F]+)", body)
        mask = re.search(r"\.enable_mask\s*=\s*BIT\((\d+)\)", body)
        halt = re.search(r"\.halt_reg\s*=\s*(0x[0-9a-fA-F]+)", body)
        if not (vote and mask and halt): raise ValueError(f"Incomplete {clock}")
        source += f"    {{{ids[clock.upper()]}, {vote[1]}, 1U << {mask[1]}, {halt[1]}, {rcgs.get(clock + '_src', '0')}}}, /* {clock} */\n"
    source += "};\n"
source += '''
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
const struct CrAndroidPin *CrAndroidPin(unsigned soc, unsigned pin)
{
    if (soc == 8150 && pin < COUNT(pins_8150)) return &pins_8150[pin];
    if (soc == 8250 && pin < COUNT(pins_8250)) return &pins_8250[pin];
    return NULL;
}
const struct CrAndroidClock *CrAndroidClock(unsigned soc, unsigned id)
{
    const struct CrAndroidClock *table;
    size_t count;
    if (soc == 8150) { table = clocks_8150; count = COUNT(clocks_8150); }
    else if (soc == 8250) { table = clocks_8250; count = COUNT(clocks_8250); }
    else return NULL;
    for (size_t i = 0; i < count; ++i) if (table[i].id == id) return table + i;
    return NULL;
}
'''
(out / "android_bindings.c").write_text(source)
print("Generated Android clock ID and TLMM layout tables from verified sources")
