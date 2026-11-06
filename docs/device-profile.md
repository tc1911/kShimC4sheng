# sheng platform notes

**English** | [简体中文](device-profile.zh-CN.md)

This page only records the platform facts needed to port the menu. It contains
no per-unit identifiers, partition offsets or backup paths.

| Item | Value |
| --- | --- |
| SoC | Qualcomm SM8550 / kalama |
| Display | 3048 × 2032, dual DSI + DSC, XRGB8888 |
| Touch | Novatek NT36532E, **not supported** |
| Keys | `pmk8550-pwrkey` + `qcom,sm8550-tlmm` volume keys |
| PMIC arbiter | SPMI v7 |
| Slots | A/B; the shim is pinned to slot A |

## Boot image

- Retail `boot` uses header v4, page size 4096, no ramdisk and no appended DTB;
  the kernel is an ARM64 PE/COFF `Image` (`ARM\x64` at 0x38).
- `dtbo` carries the SDE topology: `qcom,sde-kms`, `cont_splash_region`,
  10 SSPPs, 6 CTLs and 6 real mixers (plus two `0xf0f` placeholders), with no
  `qcom,sde-hw-version`.
- `init_boot` and `vendor_boot` are loaded by ABL for the active slot.

## DT detection

- The ABL DT root is `qcom,kalama-mtp` / `qcom,kalama`, not `qcom,sm8550`.
- The device is identified by `xiaomi,sheng` or a model string containing
  `sheng`.
- `/memory` has a zero-size placeholder bank; memory accounting skips it.

## Partitioning

Read the partition table from the device (`/dev/block/by-name`, `lsblk`,
`sgdisk -p`) instead of copying offsets from another unit. Keep GPT and
partition backups local; never commit them.
