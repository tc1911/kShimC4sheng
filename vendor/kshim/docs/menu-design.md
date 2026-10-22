# Non-FLAT boot menu design adaptation

This document records the earlier 25-preset design layer. Its preset counts
and validation matrix describe that development stage. Current choices live
in `src/Kconfig` and `src/ui/styles/`; the Y700 profiles use STAR_CHART as
described in the [Y700 menu guide](tb323fu-boot-menu.md).

This branch implements a presentation layer on the existing LVGL menu. It is not
an import of UIKit, Material, Fluent, HarmonyOS or Clover toolkits, and passing
host tests is not a claim of official design conformance or hardware acceptance.

## Scope and compatibility

All 24 non-FLAT Kconfig choices select a design descriptor. Existing palette
fields remain in `menu_style.h` / `styles/*.h`; sizes, layout, typography and
selection-symbol rules live in `menu_design.h`. `FLAT` selects a disabled
descriptor and keeps its original geometry, fonts, animation and texture path.
The shared entry array, 49-entry limit, physical key order, pointer cancellation,
Boot-only touch confirmation, countdown and teardown remain in `lvgl_port.c`.

The source snapshot preserves these design descriptors alongside later themes.

## Theme identities

| Family | Presentation |
|---|---|
| CLASSIC | Square framed firmware menu, title band, compact rectangular action |
| CARDS | Responsive independent media cards, restrained elevation and selected outline |
| TERMINAL | Fixed-advance ASCII adapter, numbered prompt column, text-like action |
| MINIMAL | No outer panel, short text measure, small selection dot |
| HIGH_CONTRAST | Opaque monochrome surfaces, boundaries and a shape-based selected mark |
| FLUENT / DARK | Straight geometry, continuous selection rail, local action |
| FLUENT2 / DARK | Small-radius rows, inset short selection marker, separate action |
| MATERIAL2 / DARK | Header surface, continuous radio-selection list, compact raised-container composition |
| MATERIAL3 | Tonal container, role-specific shapes, radio selection and pill action |
| SURFACE / DARK | Wide navigation/detail split, deterministic narrow fallback |
| CUPERTINO | macOS-inspired startup-disk grid, media icons and independent action |
| IOS_HIG | Traditional inset group, title hierarchy, trailing drawn checkmark |
| HARMONYOS | Organized rounded media cards with distinct icon/text hierarchy |
| CLOVER | Horizontal media picker, icon-local selection frame and selected name below |
| GLASS | Static softened background under a translucent, highlighted content surface |
| AURORA | Large background color regions with quiet content surfaces |
| SOFT_UI | Paired light/dark edges and inset selected/pressed feedback |
| BENTO | Stable module grid; every fifth entry spans two columns when space permits |
| NEON | Dark technical frame with selected corners and bounded glow |
| NORD | Restrained utility layout using the existing Nord palette roles |

Light/dark siblings intentionally share geometry. Icons are generic media, not
OS logos inferred from entry names. Drawn icons and selection marks add no extra
focus-group entries or independent pointer targets.

The terminal adapter fixes ASCII advances over the existing glyph outlines and
uses the original font as a CJK fallback. It is not a newly bundled monospace
font. No proprietary or host-system font assets are added.

## Files and regeneration

`tools/generate_design_presets.py` is the editable descriptor source. Run it to
regenerate `src/ui/menu_design.h`, or use `--check` to verify exact reproduction.
`menu_design_layout.h` computes pure bounded geometry and label reservations.
`menu_design_port.h` binds it to LVGL and draws the style-specific surfaces.
`menu_symbols.h`, `menu_design_font.h` and `menu_design_background.h` own their
individual drawing/font/background concerns. `menu_layout_dispatch.h` isolates
the original FLAT fallback from the new layout dispatch.

## Scaling and resources

`CONFIG_KSHIM_MENU_SCALE_Q8=0` chooses a 1x–2x automatic scale from the short
framebuffer edge. An explicit nonzero value is clamped to 96–512 in Q8 units.
This is a design-unit scale, not physical dp, pt or measured DPI. Small displays
fall back to a bounded single list; they cannot promise full long-text display.

`CONFIG_KSHIM_MENU_REDUCED_EFFECTS=y` excludes the Glass/Aurora cached background
and removes extra shadows/glow. The cache is generated once at initialization:
120×120 ARGB pixels plus one scanline use 58,080 bytes, below 64 KiB. FLAT does
not use this cache and retains its own existing scratch allocation.

## Validation and authentic previews

```sh
python3 tools/generate_design_presets.py --check
python3 tools/test_theme_design.py --cc gcc --sanitize
python3 tools/test_theme_design.py --cc clang
cmake -S tests -B build-menu -DKSHIM_TEST_MENU_STYLE=IOS_HIG
cmake --build build-menu --parallel 2
ctest --test-dir build-menu --output-on-failure
mkdir -p previews
./build-menu/ui_preview_test previews
python3 tools/preview_manifest.py previews
```

Host overrides are `KSHIM_TEST_MENU_SCALE_Q8` and
`KSHIM_TEST_MENU_REDUCED_EFFECTS`. Firmware uses the Kconfig options instead.
The CI builds the complete host suite for 25 presets and ARM64 runtime for all
24 redesigned presets. Its actual run result is authoritative; configuration of
a job does not mean the job passed. The FLAT job compares both resolutions and
all four keyframes to the previous renderer with byte-for-byte PPM comparison.

Preview PNGs are rendered by the actual pinned LVGL build, with unchanged sample
entries. Each artifact includes `source-commit.txt`, the original image manifest
and test/render logs. HTML must display these frames, not redraw the menu in CSS.
These are host-rendered frames, not photographs of a device. Device frame rate,
boot latency, resource limits and final visual design acceptance need separate
measurement/review; no such acceptance is implied by this implementation.
