#!/usr/bin/env python3
"""Generate selected-only descriptors. Existing 25-theme geometry is unchanged."""
from pathlib import Path
import argparse
import json
import re
ROOT = Path(__file__).resolve().parents[1]
KINDS = ['CLASSIC', 'CARDS', 'TERMINAL', 'MINIMAL', 'HIGH_CONTRAST', 'FLUENT', 'FLUENT2', 'MATERIAL2', 'MATERIAL3', 'SURFACE', 'CUPERTINO', 'IOS_HIG', 'HARMONYOS', 'CLOVER', 'GLASS', 'AURORA', 'SOFT_UI', 'BENTO', 'NEON', 'NORD', 'METRO', 'ONE_UI', 'ADWAITA', 'HOLO', 'LVGL_DEFAULT', 'STAR_CHART']
LAYOUTS = ['LIST', 'GRID', 'BENTO', 'SPLIT', 'RIBBON', 'INSET']
MARKS = ['CURSOR', 'DOT', 'CHECK', 'RADIO', 'BAR', 'PILL', 'CORNERS']
BASE = {'enabled': 1, 'kind': 'MINIMAL', 'layout': 'LIST', 'mark': 'CHECK', 'max_width': 640, 'padding': 24, 'title_size': 28, 'row_size': 18, 'status_size': 14, 'row_height': 56, 'tile_height': 144, 'tile_width': 180, 'gap': 0, 'panel_radius': 0, 'row_radius': 0, 'boot_radius': 0, 'boot_width': 160, 'boot_height': 44, 'boot_align': 2, 'icon_size': 0, 'header': 0, 'panel_shadow': 0, 'row_shadow': 0, 'focus_outline': 0, 'focus_glow': 0, 'press_scale': 256, 'grid_two': 560, 'grid_three': 900, 'background': 0}
def preset(name, **values):
    item = dict(BASE, name=name, kind=name, title="Choose a system", action="Boot")
    item.update(values)
    return item

PRESETS = [
    preset('CLASSIC', mark='CURSOR', max_width=720, padding=20, title_size=18, row_size=17, status_size=13, row_height=38, boot_width=136, boot_height=38, header=1, title='Boot Manager'),
    preset('CARDS', layout='GRID', max_width=1000, padding=28, status_size=13, tile_height=136, gap=16, row_radius=12, boot_radius=8, boot_width=180, icon_size=32, row_shadow=6, focus_outline=2, press_scale=254, grid_two=520, grid_three=840, title='Choose a boot image'),
    preset('TERMINAL', mark='CURSOR', max_width=880, padding=20, title_size=20, row_size=16, row_height=36, boot_width=176, boot_height=36, boot_align=0, header=3, title='KSHIMC / BOOT SELECTOR', action='[ Boot ]'),
    preset('MINIMAL', mark='DOT', max_width=560, padding=20, title_size=34, row_size=20, status_size=13, gap=4, boot_width=132, boot_height=40, boot_align=0, press_scale=254, title='Start here'),
    preset('HIGH_CONTRAST', max_width=760, title_size=26, row_size=20, status_size=16, row_height=60, gap=8, boot_width=192, boot_height=48, focus_outline=2),
    preset('FLUENT', mark='BAR', max_width=760, padding=28, row_size=17, status_size=13, row_height=44, boot_width=152, boot_height=40, icon_size=20, header=3, press_scale=254, title='Choose an operating system'),
    preset('FLUENT_DARK', kind='FLUENT', mark='BAR', max_width=760, padding=28, row_size=17, status_size=13, row_height=44, boot_width=152, boot_height=40, icon_size=20, header=3, press_scale=254, title='Choose an operating system'),
    preset('FLUENT2', mark='PILL', max_width=760, padding=28, row_size=17, status_size=13, row_height=44, gap=4, panel_radius=12, row_radius=4, boot_radius=4, boot_width=152, boot_height=40, icon_size=20, panel_shadow=12, press_scale=254, title='Start your device'),
    preset('FLUENT2_DARK', kind='FLUENT2', mark='PILL', max_width=760, padding=28, row_size=17, status_size=13, row_height=44, gap=4, panel_radius=12, row_radius=4, boot_radius=4, boot_width=152, boot_height=40, icon_size=20, panel_shadow=12, press_scale=254, title='Start your device'),
    preset('MATERIAL2', mark='RADIO', title_size=24, row_size=16, row_height=48, panel_radius=4, boot_radius=4, boot_width=112, boot_height=36, icon_size=20, header=2, panel_shadow=8, title='Select system'),
    preset('MATERIAL2_DARK', kind='MATERIAL2', mark='RADIO', title_size=24, row_size=16, row_height=48, panel_radius=4, boot_radius=4, boot_width=112, boot_height=36, icon_size=20, header=2, panel_shadow=8, title='Select system'),
    preset('MATERIAL3', mark='RADIO', row_size=16, gap=4, panel_radius=28, row_radius=16, boot_radius=255, boot_height=40, icon_size=24),
    preset('SURFACE', layout='SPLIT', mark='BAR', max_width=1120, padding=32, title_size=30, row_size=17, row_height=44, boot_radius=2, boot_width=156, boot_height=40, icon_size=20, header=3, title='kShimC UEFI'),
    preset('SURFACE_DARK', kind='SURFACE', layout='SPLIT', mark='BAR', max_width=1120, padding=32, title_size=30, row_size=17, row_height=44, boot_radius=2, boot_width=156, boot_height=40, icon_size=20, header=3, title='kShimC UEFI'),
    preset('CUPERTINO', layout='GRID', max_width=920, padding=40, title_size=24, row_size=16, status_size=13, tile_height=160, gap=24, row_radius=10, boot_radius=8, boot_width=140, boot_height=36, boot_align=1, icon_size=72, focus_outline=2, press_scale=254, grid_two=460, grid_three=700, title='Startup Disk'),
    preset('IOS_HIG', layout='INSET', max_width=540, padding=20, title_size=34, row_size=17, status_size=13, row_height=48, boot_radius=255, boot_width=240, boot_align=1, title='Start up'),
    preset('HARMONYOS', layout='GRID', max_width=1040, padding=32, title_size=32, tile_height=156, gap=16, row_radius=24, boot_radius=255, boot_width=220, boot_align=1, icon_size=48, focus_outline=2, press_scale=254, title='Boot library'),
    preset('CLOVER', layout='RIBBON', mark='CORNERS', max_width=1200, title_size=24, row_size=16, status_size=13, tile_height=180, tile_width=176, gap=20, boot_radius=8, boot_width=180, boot_align=1, icon_size=96, press_scale=254, title='kShimC Boot Picker'),
    preset('GLASS', max_width=680, padding=28, title_size=30, gap=8, panel_radius=24, row_radius=12, boot_radius=12, boot_width=180, panel_shadow=16, press_scale=254, background=1),
    preset('AURORA', mark='DOT', max_width=720, padding=32, title_size=36, gap=12, panel_radius=24, row_radius=8, boot_radius=255, boot_width=180, press_scale=254, background=2, title='Choose your next start'),
    preset('SOFT_UI', max_width=720, padding=32, gap=16, panel_radius=24, row_radius=16, boot_radius=16, boot_width=184, row_shadow=10, press_scale=254, title='Start your device'),
    preset('BENTO', layout='BENTO', max_width=1040, padding=28, title_size=32, tile_height=140, gap=16, row_radius=20, boot_radius=12, boot_width=180, icon_size=32, focus_outline=2, press_scale=254, title='Boot library'),
    preset('NEON', mark='CORNERS', max_width=840, padding=28, title_size=26, status_size=13, row_height=52, gap=12, boot_width=176, icon_size=20, header=3, focus_glow=8, press_scale=254, title='BOOT / SELECT'),
    preset('NORD', mark='BAR', max_width=760, padding=28, row_size=17, status_size=13, row_height=44, gap=4, panel_radius=6, row_radius=2, boot_radius=4, boot_height=40, icon_size=20, press_scale=254, title='Boot manager'),
    preset('METRO', title_size=44, row_size=26, row_height=64, gap=4, boot_width=140, boot_align=0, title='operating systems', action='start'),
    preset('ONE_UI', layout='SPLIT', mark='PILL', max_width=1200, padding=28, title_size=36, row_size=20, status_size=22, row_height=64, gap=4, row_radius=28, boot_radius=255, boot_width=560, boot_height=52, boot_align=1, icon_size=32, title='Boot manager'),
    preset('ADWAITA', layout='INSET', max_width=680, title_size=20, row_size=17, row_height=52, panel_radius=12, boot_radius=8, boot_width=144, icon_size=20, header=4, title='Boot Images'),
    preset('LVGL_DEFAULT', enabled=0, title='Select OS to Boot'),
    preset('HOLO', mark='RADIO', padding=20, title_size=22, row_height=48, boot_radius=2, boot_width=140, header=5, title='Choose system'),
    preset('STAR_CHART', kind='STAR_CHART', layout='SPLIT', mark='CORNERS', max_width=1520, padding=40, title_size=42, row_size=28, status_size=20, row_height=70, gap=6, row_radius=20, boot_radius=20, boot_width=240, boot_height=68, icon_size=36, header=0, panel_shadow=0, row_shadow=0, focus_glow=0, focus_outline=0, press_scale=256, background=0, title='KShimC Launcher', action='Boot'),
]
ORDER = [item['name'] for item in PRESETS]
DATA = {item['name']: item for item in PRESETS}

def generate():
    lines = ['/* Generated by tools/generate_design_presets.py; only the selected descriptor is compiled. */',
             '#ifndef KSHIM_MENU_DESIGN_H','#define KSHIM_MENU_DESIGN_H','#include <stdint.h>','#include <stddef.h>']
    for prefix, names in [('KSHIM_DESIGN',KINDS),('KSHIM_LAYOUT',LAYOUTS),('KSHIM_MARK',MARKS)]:
        lines.append('enum { '+', '.join(f'{prefix}_{name}={i+1}' for i,name in enumerate(names))+' };')
    lines += ['typedef struct {','    const char *name, *title, *action;']
    for key in BASE:
        lines.append(f'    uint16_t {key};')
    lines.append('} kshim_menu_design_t;')
    for index, item in enumerate(PRESETS):
        name=item['name']
        lines.append(('#if' if index==0 else '#elif')+f' defined(CONFIG_KSHIM_MENU_STYLE_{name}) && CONFIG_KSHIM_MENU_STYLE_{name}')
        # Positional initializer follows the generated structure field order.
        values=[json.dumps(item[k]) for k in ('name','title','action')]
        for key in BASE:
            value=item[key]
            prefix={'kind':'KSHIM_DESIGN_', 'layout':'KSHIM_LAYOUT_', 'mark':'KSHIM_MARK_'}.get(key)
            values.append(prefix+value if prefix else str(value)+'U')
        lines.append('static const kshim_menu_design_t mMenuDesign = {'+', '.join(values)+'};')
    lines += ['#else','static const kshim_menu_design_t mMenuDesign = {.name="FLAT", .enabled=0U};','#endif','#endif']
    output='\n'.join(lines)+'\n'
    return re.sub(r'\n    (\.[^\n]+)\n    (\.[^\n]+)\n    (\.[^\n]+)',lambda m:'\n    '+' '.join(m.groups()),output)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    path=ROOT/'src/ui/menu_design.h'
    generated=generate()
    if args.check:
        if path.read_text(encoding='utf-8')!=generated:
            raise SystemExit('menu_design.h differs from its generator')
        print(f'PASS: {len(PRESETS)} selected-only descriptors match their generator')
    else:
        path.write_text(generated,encoding='utf-8')
        print('Generated',path)
if __name__=='__main__': main()
