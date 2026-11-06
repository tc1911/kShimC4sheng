# 设备资料：Xiaomi Pad 6S Pro (sheng)

[English](device-profile.md) | **简体中文**

| 项目 | 值 |
| --- | --- |
| SoC | Qualcomm SM8550 / kalama |
| 内存 | 12 GB（DT 报告 7888 MiB） |
| 存储 | 256 GB UFS（Micron MT256GBEAX4U40） |
| 屏幕 | 3048 × 2032，双 DSI + DSC，XRGB8888 |
| 触控 | Novatek NT36532E，**未适配** |
| 按键 | `pmk8550-pwrkey` + `qcom,sm8550-tlmm` 音量键 |
| PMIC 仲裁器 | SPMI v7 |
| boot 分区 | 201,326,592 B（192 MiB），header v4，page 4096 |
| 槽位 | A/B；shim 固定在 A 槽 |

## 分区（512 B sectors）

```text
super      start 1,688,064   size 23,068,672
userdata   start 24,756,736  size 487,300,568   (原厂 232.4 GiB)
linux      由用户重分区创建，PARTLABEL=linux
```

完整 GPT 与分区备份见 `/home/tc191/6spBackup/rom/`（仅本机，不分发）。

## 原厂 boot

- `boot_a` / `boot_b` 均为 201,326,592 B，v4，kernel 40,417,792 B，无 ramdisk、
  无 DTB；kernel 为 PE/COFF，`ARM\x64` 在 0x38。
- `dtbo_a` 25,165,824 B，含 `qcom,sde-kms`、`cont_splash_region`、10 个 SSPP、
  6 个 CTL、6 个真实 mixer（另有 2 个 `0xf0f` 占位），没有 `qcom,sde-hw-version`。
- `init_boot_a` 8,388,608 B；`vendor_boot_a` 由 ABL 按槽位加载。

## DT 识别

- ABL DT 根是 `qcom,kalama-mtp` / `qcom,kalama`，不是 `qcom,sm8550`；
- 设备识别靠 `xiaomi,sheng` 或 model 含 `sheng`；
- `/memory` 有一个 size=0 的占位 bank，统计内存时跳过。
