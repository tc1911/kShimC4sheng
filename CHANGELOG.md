# Changelog

**English** | [简体中文](CHANGELOG.zh-CN.md)

## 0.0.5 — unreleased

### Version
- Bumped to `0.0.5`; the menu/device subtitle now comes from `KSHIM_BUILD_VERSION`
  instead of a hard-coded string, and the charger bitmap shows `kShimC v0.0.5`.

### Menu
- Refactored into the six-page Atlas layout: Boot environment, Advanced boot,
  Settings, Language, Boot delay, Device info.
- Fixed left navigation with a blue focus bar; cards/grids on the right; the
  old STAR_CHART and gold decorations were removed.
- Added 56×56 A8 line icons (Android/Linux/Advanced/Settings/Info/Restart/Power
  and more).
- The footer shows the countdown, cancelled auto-boot state and device name;
  Device info is now a two-column field grid.
- Fixed language/device-info hangs: `info_values` pointer cast and a dangling
  countdown label.
- Font subset grew to 741 glyphs for the new strings.

### Language and documentation
- The menu now falls back to English when no language has been saved; an
  explicit choice from the Language page still wins.
- English is the default documentation language on GitHub; Simplified Chinese
  versions live at `*.zh-CN.md` with a language switch on every page.
- Added English UI previews (`docs/ui-preview/pages-overview-en.png`,
  `docs/ui-preview/pages-en/`, `docs/images/menu-*-en.png`); the English
  README uses them.

## 0.0.4 — 2026-10-29

### Menu
- Added a device line under the left title:
  `Xiaomi Pad 6S Pro · sheng · v0.0.4`.
- Reworked the right side into the Atlas layout: overline, 48 px title,
  28 px description, gold tag pills; removed the large illustration/feather
  logo.
- Removed the two gold title underlines and the bottom wave/mountain
  decorations.
- Grew the menu font to 732 glyphs and fixed a device-info crash caused by an
  oversized LVGL font cache.

### Charger screen
- Built-in charger screen is bilingual and follows the menu language.
- Bottom line shows `kShimC v0.0.4`; Chinese text uses embedded LXGW bitmaps
  and does not depend on the menu font.
- Holding power for 2 seconds still opens the normal menu.

## 0.0.3 — 2026-10-27

### Built-in charger mode
- When powered off and plugged in, detect ABL's `androidboot.mode=charger` and
  skip the menu to show the built-in charger screen.
- Detection covers `/chosen/bootargs`, Android bootconfig / initrd tail
  parameters, and a PMIC boot-reason fallback.
- The charger screen is two English lines: `CHARGER CONNECTED` /
  `HOLD POWER TO BOOT`; hold power for 2 seconds to open the normal menu.
- No dependency on Debian, systemd or any distro userspace; only the bare-metal
  framebuffer and keys.
- Note: there is no battery driver in bare metal, so no percentage is shown;
  panel backlight is controlled by KTZ8866 and is not turned off by the shim.

### Settings persistence
- SDAM records upgraded to v2, storing last boot entry, menu language and boot
  delay in one record.
- Settings gained a "Boot delay" submenu with the same interaction as language:
  `No auto-boot / 3 s / 5 s / 8 s / 10 s / 15 s / 30 s`.
- Language and delay survive reboot and power-off; `0` is shown as
  "No auto-boot".
- Device info keeps read-only fields only; the boot diagnostic row was removed.

### Fixes and polish
- Fixed bootconfig trailer size being read in host byte order (arm64 is
  little-endian); it previously reported `bc=-15`.
- Fixed charger-screen init order: create the PON key descriptor before
  `menu_build` to avoid `c=-11`.
- Grew the menu font to 721 glyphs for the new settings strings and removed
  missing-glyph boxes.
- Removed the unused ADC battery experiment; kept the simplified charger screen.

## 0.0.2 — 2026-10-24

- The menu remembers the last booted system (PMIC SDAM `KSET` record + CRC32,
  with a backup copy).
- Shows "Starting, please wait" after selection to avoid a false hang.
- Settings simplified: device is fixed to sheng; device selection and the
  default-boot entry were removed.
- Version, credits and boot hints updated; menu font grew to 708 glyphs.
- Windows one-click build: pure-Python FDT editing, bundled
  `MultiBootKernelPatcher.exe`, no dtc/fdtput needed.

## 0.0.1 — 2026-10-23

- First sheng port: a `boot_a` boot menu replacing `fastboot set_active` /
  WoaManager flash-based switching.
- SM8550 display uses the `cont_splash_region` fallback; keys, device info and
  Advanced boot (Fastboot/Recovery/EDL) work.
- Packs Debian/distro boot.img; Android + Debian dual boot verified on hardware.
