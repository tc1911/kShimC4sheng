#!/usr/bin/env python3
"""Compile/run pure geometry and new-theme contracts for all selected presets."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',default='gcc'); p.add_argument('--sanitize',action='store_true')
    a=p.parse_args()
    header=(ROOT/'src/ui/menu_design.h').read_text(encoding='utf-8')
    names=['FLAT']+re.findall(r'^#(?:if|elif) defined\(CONFIG_KSHIM_MENU_STYLE_([A-Z0-9_]+)\)',header,re.M)
    if len(names)!=30 or len(set(names))!=30: raise SystemExit('Expected 30 unique presets')
    with tempfile.TemporaryDirectory(prefix='kshim-design-') as td:
        for name in names:
            tests=['menu_design_contract_test.c']
            if name=='FLAT': tests+=['menu_legacy_marks_test.c']
            if name=='ONE_UI': tests+=['menu_one_ui_identity_test.c']
            if name in ('METRO','ONE_UI','ADWAITA','HOLO'): tests+=['menu_new_themes_contract_test.c']
            for test in tests:
                exe=Path(td)/name
                cmd=[a.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-pedantic',
                     f'-DCONFIG_KSHIM_MENU_STYLE_{name}=1',f'-DEXPECTED_STYLE="{name}"',str(ROOT/'tests'/test),'-o',str(exe)]
                if a.sanitize: cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
                subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
    print(f'PASS: {len(names)} compile-time presets and four new-theme contracts ({a.cc}, sanitizers={a.sanitize})')
if __name__=='__main__':main()
