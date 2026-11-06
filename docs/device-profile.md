# Device profile: Xiaomi Pad 6S Pro (sheng)

**English** | [简体中文](device-profile.zh-CN.md)

| Item | Value |
| --- | --- |
| SoC | Qualcomm SM8550 / kalama |
| RAM | 12 GB (DT reports 7888 MiB) |
| Storage | 256 GB UFS (Micron MT256GBEAX4U40) |
| Display | 3048 × 2032, dual DSI + DSC, XRGB8888 |
| Touch | Novatek NT36532E, **not supported** |
| Keys | `pmk8550-pwrkey` + `qcom,sm8550-tlmm` volume keys |
| PMIC arbiter | SPMI v7 |
| boot partition | 201,326,592 B (192 MiB), header v4, page 4096 |
| Slots | A/B; the shim is pinned to slot A |

## Partitions (512 B sectors)

```text
super      start 1,688,064   size 23,068,672
userdata   start 24,756,736  size 487,300,568   (stock 232.4 GiB)
linux      created by the user, PARTLABEL=linux
```

The full GPT and partition backup live in `/home/tc191/6spBackup/rom/`
(local only; not distributed).

## Stock boot

- `boot_a` / `boot_b` are both 201,326,592 B, v4, kernel 40,417,792 B, no
  ramdisk and no DTB; the kernel is PE/COFF with `ARM\x64` at 0x38.
- `dtbo_a` is 25,165,824 B and contains `qcom,sde-kms`,
  `cont_splash_region`, 10 SSPPs, 6 CTLs and 6 real mixers (plus two `0xf0f`
  placeholders), with no `qcom,sde-hw-version`.
- `init_boot_a` is 8,388,608 B; `vendor_boot_a` is loaded by ABL per slot.

## DT detection

- The ABL DT root is `qcom,kalama-mtp` / `qcom,kalama`, not `qcom,sm8550`;
- the device is identified by `xiaomi,sheng` or a model string containing
  `sheng`;
- `/memory` has a zero-size placeholder bank; memory accounting skips it.
