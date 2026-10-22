#!/usr/bin/env python3
"""Evaluate one Kconfig tree once for CMake source selection and C headers."""
import argparse
from pathlib import Path
import kconfiglib
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', required=True, type=Path)
p.add_argument('--config', required=True, type=Path)
p.add_argument('--header', required=True, type=Path)
p.add_argument('--cmake', required=True, type=Path)
a = p.parse_args()
k = kconfiglib.Kconfig(str(a.root / 'Kconfig'))
if a.config.exists():
    k.load_config(str(a.config))
else:
    k.write_config(str(a.config))
a.header.parent.mkdir(parents=True, exist_ok=True)
k.write_autoconf(str(a.header))
lines = []
for name, sym in sorted(k.syms.items()):
    if not sym.nodes:
        continue
    value = str(int(sym.str_value != 'n')) if sym.type in (kconfiglib.BOOL, kconfiglib.TRISTATE) else sym.str_value
    lines.append(f'set(CONFIG_{name} [==[{value}]==])')
text = '\n'.join(lines) + '\n'
if not a.cmake.exists() or a.cmake.read_text() != text:
    a.cmake.write_text(text)
