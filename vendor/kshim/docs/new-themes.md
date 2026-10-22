# Metro, One UI, Adwaita and Holo

This is the implementation record for these preset families and the later
native LVGL option. Counts and comparison commits below refer to those
development stages. Current Y700 profiles select STAR_CHART; see the
[Y700 menu guide](tb323fu-boot-menu.md) for device defaults.

Four additional compile-time presets extend the existing 25 choices. They use
the real pinned LVGL renderer, existing CJK font and shared boot/input state
machine. No native toolkit, proprietary font, OS logo or inactive control is
imported. These are design-language adaptations, not official implementations.

| Choice | Structural identity |
| --- | --- |
| METRO | Top-anchored unframed black page, 44-unit title / 26-unit row type, accent text and drawn selection check, local outlined action |
| ONE_UI | Centered portrait viewing region over a continuous lower interaction surface, leading media icons and inset focus pill, wide Boot action; long lists expand upward; sufficiently wide landscape uses navigation/detail columns |
| ADWAITA | Centered header surface, continuous boxed list, symbolic media icons, neutral separators and independent suggested action |
| HOLO | Dark action-bar surface with cyan bottom rule, flat continuous separated rows, radio marks and restrained rectangular action |

Use one `CONFIG_KSHIM_MENU_STYLE_<NAME>=y`. Existing choices retain their
colors, numeric descriptor values, motion and geometry. FLAT continues to bypass
the new design layer. The editable descriptor generator now reproduces the actual
committed old descriptors, including Terminal's 14-unit status text (the previous
generator had drifted to 13); enum values are preserved and new kinds appended.

`menu_new_themes_render.h` contains presentation-only hooks. One UI's layout is
computed from viewport and entry count, not selected index. Focus movement never
moves the fixed Boot control. All four use opaque surfaces and no new background
cache. Explicit design scale and reduced-effects settings remain available.

## Validation

The CI matrix covers 30 default host builds and 29 non-FLAT ARM64 builds. The
four additional themes and native LVGL also run the full host suite at explicit scale 256 with reduced
effects. New pure geometry tests cover ten sizes, five entry counts and five
scales; real LVGL tests verify CJK, selection pixels, long labels, fixed chrome,
49-item rebuilds, independent confirmation and framebuffer guards. Existing
keyboard, touch-cancellation and lifecycle assertions are not removed.

All 28 unaffected presets render both original resolutions and four stages
again using the previous 5167858 presentation headers. CI compares every PPM
byte to the new build's output. FLAT additionally retains its older renderer
comparison. A workflow configuration is not a passing result: consult the actual
run and `previews/compatibility.txt` artifacts for completed comparisons.

The original seven-entry preview fixture is unchanged. A successful matrix
produces 240 authentic PNGs and an offline `theme-review-html` artifact. Current
artifacts carry their exact checkout SHA; no image is relabelled with another
commit. The old/new gallery's optional baseline mode still requires equal theme
sets: a 25-theme historical matrix cannot pretend to contain the four new themes.

## Boundaries

The reference scale is a design unit, not measured physical DPI. Compact displays
may hide title/status and clip long text to keep the Boot control usable. This
work does not measure device frame rate, startup latency, physical touch-target
size or whole-product accessibility. Native appearance and target performance
remain subject to visual and hardware review.

Reference directions: Microsoft's typography/content-led Windows Phone Metro,
Samsung One UI's viewing/interaction regions, GNOME HIG boxed lists and header
bars, and Android's original Holo theme family. The new preset names do not
imply vendor certification or native API use.

## In-place One UI replacement and native LVGL

`ONE_UI` replaces its previous inset-group design under the same Kconfig symbol.
There is no `ONE_UI_V2` or compatibility preset. `IOS_HIG` is not changed. At
640x360 and larger suitable landscape sizes, the two-column layout keeps the
second row reachable without colliding with Boot; the geometry test guards this.
Very narrow or small screens retain the safe compact fallback.

`LVGL_DEFAULT` enables the actual default theme in the pinned LVGL library and
uses native container, list, list-button and button classes. It bypasses the
custom palette/drawing/layout and transform-animation paths. Geometry uses the
native flex layout; no copied palette, radius, border or shadow is installed.
The group selection activates the native FOCUS_KEY style, including for pointer
selection, rather than drawing a replacement highlight. The default Latin font
is Montserrat 14; a copy retains its glyphs and metrics and adds the existing CJK
font only as a missing-glyph fallback. With no scratch memory it uses the built-in
font alone. Native UI intentionally does not use the custom design-scale option.
At tiny framebuffer sizes, only container/Boot padding and height are reduced to
keep the input surface usable; native state colors/radius/borders remain intact.

Native preview entry and ready frames are identical because no custom entrance
animation is installed. Its confirmation sample uses a real pointer press and
native pressed feedback; it releases after sampling and verifies that only the
release confirms. Other presets retain the original preview input sequence.

The runtime native tests compare actual widget properties with newly-created,
untouched LVGL widgets across resting, focused and pressed states, exercise
2/49/2 entry rebuilding, CJK fallback, Boot position and confirmation. Existing
physical-key, touch-cancel, batched-tap, framebuffer-format, teardown and long-label
tests also run. Native behavior is checked explicitly instead of requiring a
custom animation that this preset intentionally does not have.

The 28 unaffected presets compare both resolutions and all four full-frame
samples to `35e9ad38529bcd6ff4222619a344a951d0366120`. One UI is deliberately excluded
from that equality check because its design is replaced. A configured CI matrix
is not evidence of success; consult the corresponding run and retained logs.
