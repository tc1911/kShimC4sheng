# kShimC source patches

**English** | [简体中文](README.zh-CN.md)

`vendor/kshim` is the fifth-generation kShimC snapshot. The sheng patches
below are already applied in this repository; this file records what changed
and what is still optional.

## 1. `src/dt/splash.c`: SM8550 display topology

Applied:

- read the SM8550 `sde` node and `mdp_phys` register from the live DT;
- accept 10 SSPPs and up to 8 mixer slots (6 real mixers + 2 `0xf0f`
  placeholders);
- keep the legacy CTL/SSPP decoder for non-Sun/Canoe platforms.

If SDE decoding still fails, `CONFIG_KSHIM_SPLASH_FALLBACK` reuses the ABL
`cont_splash_region` as an XRGB8888 framebuffer (see `src/dt/resources.c`).
SDE routing itself remains undecoded.

## 2. `src/dt/device_info.c`: sheng detection

Applied:

- recognize `qcom,kalama` as SM8550 and `xiaomi,sheng` (or a model string
  containing `sheng`) as the board;
- skip the zero-size placeholder bank in `/memory`;
- report `Xiaomi Pad 6S Pro` / `sheng (SM8550)` in the UI and settings.

## 3. `src/drivers/qcom_reboot.c`: SPMI v7

Applied: accept SPMI v5/v7/v8/8.5 instead of only v8/8.5. sheng reports v7, so
Recovery/Fastboot/EDL would otherwise fail with `KSHIM_QCOM_REBOOT_UNSUPPORTED`.

## 4. Settings and Atlas UI

Applied:

- SDAM `KSET` v2 records at `0x8400/0x8500` for last boot, language and delay;
- Atlas six-page UI (`src/ui/menu_atlas.h`) and 56×56 A8 icons
  (`src/ui/atlas_icons.h`).

## 5. Optional: touch

sheng uses a Novatek NT36532E panel with the userspace `thp` stack; kShimC only
supports ST FTM5 today. Do not enable `CONFIG_KSHIM_TOUCH` before the I²C
address, interrupt and reset GPIO have been verified on hardware.
