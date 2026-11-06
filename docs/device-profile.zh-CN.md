# sheng 平台笔记

[English](device-profile.md) | **简体中文**

这里只记录移植菜单所需的平台事实，不包含单机标识、分区偏移或备份路径。

| 项目 | 值 |
| --- | --- |
| SoC | Qualcomm SM8550 / kalama |
| 显示 | 3048 × 2032，双 DSI + DSC，XRGB8888 |
| 触控 | Novatek NT36532E，**暂不支持** |
| 按键 | `pmk8550-pwrkey` + `qcom,sm8550-tlmm` 音量键 |
| PMIC 仲裁器 | SPMI v7 |
| 槽位 | A/B；菜单固定在 A 槽 |

## Boot 镜像

- 原厂 `boot` 使用 v4 头、页大小 4096，无 ramdisk、无附加 DTB；内核是
  ARM64 PE/COFF `Image`（0x38 处为 `ARM\x64`）。
- `dtbo` 带 SDE 拓扑：`qcom,sde-kms`、`cont_splash_region`、10 个 SSPP、
  6 个 CTL、6 个真实 mixer（另有两个 `0xf0f` 占位），没有
  `qcom,sde-hw-version`。
- `init_boot` 与 `vendor_boot` 由 ABL 按当前槽位加载。

## DT 识别

- ABL DT 根是 `qcom,kalama-mtp` / `qcom,kalama`，不是 `qcom,sm8550`。
- 设备通过 `xiaomi,sheng` 或含 `sheng` 的 model 字符串识别。
- `/memory` 有一个 size=0 的占位 bank，内存统计会跳过它。

## 分区

从设备读取分区表（`/dev/block/by-name`、`lsblk`、`sgdisk -p`），不要照抄
另一台机器的偏移。GPT 与分区备份只保存在本地，不要提交到仓库。
