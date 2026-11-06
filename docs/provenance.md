# Provenance, upstream and local changes

**English** | [简体中文](provenance.zh-CN.md)

This repository builds on kShimC and MultiBootKernelPatcher to support sheng
(Xiaomi Pad 6S Pro / SM8550). It is not a from-scratch implementation.

## Lineage

```text
sunflower2333/kShimC                 upstream runtime (GPL-3.0)
        └── mydanyi/armadaos-freedom Y700 snapshot + STAR_CHART menu
              ├── vendor/kshim       runtime snapshot in this repo
              └── vendor/packer      MultiBootKernelPatcher

code002-2/Xiaomi-pad-6s-pro-Linux    dual-boot partition layout and install notes
ianchb/debian-sheng                  Debian rootfs build workflow
ianchb/sm8550-mainline               sheng mainline kernel / boot.img
```

## Original to this repository

The following files were written or substantially rewritten here and are
released under **GPL-3.0**:

- `packer/` — boot v0–v4 parsing/repacking, payload extraction, pure-Python FDT,
  manifest verification.
- `scripts/` — prepare, build, backup, test boot, flash, runtime build.
- `config/` — sheng defconfig, partition and cmdline settings.
- `vendor/kshim/src/ui/menu_atlas.h`, `atlas_icons.h`, `tools/gen_atlas_icons.py` —
  Atlas six-page UI and icons.
- `tests/`, `windows/`, `docs/`, `docs/ui-preview/render.py`.

The sheng patch list is in `patches/README.md` and `vendor/README.md`.

## Upstream code distributed with this repository

| Path | Upstream / snapshot | License |
|---|---|---|
| `vendor/kshim/` | [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC), via armadaos-freedom commit `244b7451` (2026-10-22), `devices/lenovo-tb323fu/boot/kshim-success-source` | GPL-3.0 |
| `vendor/packer/` | [Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher) (MBKP 2.0.0.0) | GPL-3.0-or-later |
| `vendor/packer/lib/inih/` | [benhoyt/inih](https://github.com/benhoyt/inih) | BSD-3-Clause |
| `prebuilt/sheng-runtime.bin` | built by this repository, 4 MiB, sha256 `05d77681ddd87eb196aa991fdb839e2eacc2846fa5034070cee11ee9c5e95ed3` | GPL-3.0 |
| `prebuilt/windows/*.exe` | cross-compiled with LLVM-MinGW 20261020 | GPL-3.0-or-later |

## Fetched at build time (not committed)

| Component | Revision |
|---|---|
| [CrDK](https://github.com/Project-Aloha/CrDK) | `b6c3335136b7f6b28271917de11e8f11982975fc` |
| [picolibc](https://github.com/picolibc/picolibc) | `f7edb0869e64b9d687f46961adeef7154921fe92` |
| [LVGL](https://github.com/lvgl/lvgl) | `7f07a129e8d77f4984fff8e623fd5be18ff42e74` |
| libfdt | `lib/libfdt` from kShimC `82ce6eaa10c36e64cfd3f16799172d854f9a955b` |
| kconfiglib | system package or `/opt/klipper/lib/kconfiglib` |

Fetch script: `scripts/fetch_deps.sh` (defaults to `ghproxy.net`; override with
`GHPROXY=`).

## Device images and rootfs (not committed)

| Content | Source |
|---|---|
| Partition layout / install notes | [code002-2/Xiaomi-pad-6s-pro-Linux](https://github.com/code002-2/Xiaomi-pad-6s-pro-Linux); adjusted here for 128 GiB Linux |
| Debian forky/KDE rootfs | [ianchb/debian-sheng](https://github.com/ianchb/debian-sheng) `rootfs.yml` (`IMAGE_SIZE: 10G`) |
| sheng kernel / boot.img | [ianchb/sm8550-mainline](https://github.com/ianchb/sm8550-mainline) release `7.2.6-mac` |
| sheng firmware | [ianchb/sheng-firmware](https://github.com/ianchb/sheng-firmware) |
| TWRP | [NoobMaster233/twrp-sheng-build](https://github.com/NoobMaster233/twrp-sheng-build) v1.0.1, sha256 `55909845eae82e2d22748271d948ff2a09cf821d29761c183621404cde9397d5` |
| TWRP device tree | [map220v/android_device_xiaomi_sheng](https://github.com/map220v/android_device_xiaomi_sheng) |
| Stock boot/dtbo/init_boot/ROM | user device dump / official fastboot package; **not distributed**, local backup at `/home/tc191/6spBackup/rom/` |

## Main changes from upstream

### Runtime

- `CMakeLists.txt`: support `KSHIM_TOOLCHAIN_PREFIX`.
- `device_info.c`: recognize `qcom,kalama` / `xiaomi,sheng`; skip zero-size
  memory banks.
- `splash.c` + `resources.c`: SM8550 10 SSPP / 8 mixer decoding; on failure use
  `cont_splash_region` as a forced framebuffer (`CONFIG_KSHIM_SPLASH_FALLBACK`).
- `qcom_reboot.c`: accept SPMI v5/v7/v8/8.5 (sheng is v7).
- `qcom_nv.c`: `KSET` v2 records in SDAM `0x8400/0x8500` (last boot, language,
  delay).
- `platform.c`: sheng device row, `CONFIG_KSHIM_MENU_SINGLE_LINUX`, Atlas UI
  data hooks.
- `ports/crdk/patches/0002-freestanding-resources.patch`: relax the legacy pipe
  count limit.
- `src/ui/menu_atlas.h`: Atlas six-page UI; `atlas_icons.h` holds 56×56 A8
  icons.
- Font `lxgw_wenkai_menu.ksf`: LXGW WenKai (SIL OFL 1.1) subset, 741 glyphs;
  source and sha256 in the adjacent `.provenance.json`.

### Packer

- `src/pack_config.c`: recognize Windows absolute paths.
- `BUILD_SHIMS=OFF` skips building the ARM64 shim.

## Tools and acknowledgements

- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) 20261020.
- [bkerler/edl](https://github.com/bkerler/edl): used by the EDL backup script.
- Thanks to kShimC, armadaos-freedom, Project-Aloha/DuoWoA, ianchb, code002-2,
  NoobMaster233, map220v and the other upstream authors.

If any attribution or revision is wrong, please open an issue or edit this file.
