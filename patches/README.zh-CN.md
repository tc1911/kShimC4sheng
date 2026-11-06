# kShimC 源码补丁

[English](README.md) | **简体中文**

`vendor/kshim` 是五代 kShimC 快照。下面的 sheng 补丁已经在本仓库中应用，
此文件记录改动内容与仍未做的可选项。

## 1. `src/dt/splash.c`：SM8550 显示拓扑

已应用：

- 从运行时 DT 读取 SM8550 的 `sde` 节点与 `mdp_phys` 寄存器；
- 接受 10 个 SSPP、最多 8 个 mixer 槽位（6 个真实 mixer + 2 个 `0xf0f`
  占位）；
- 非 Sun/Canoe 平台继续走 legacy CTL/SSPP 解码。

如果 SDE 解码仍失败，`CONFIG_KSHIM_SPLASH_FALLBACK` 会把 ABL 留下的
`cont_splash_region` 当作 XRGB8888 framebuffer 使用（见
`src/dt/resources.c`）。SDE 路由本身仍未解码。

## 2. `src/dt/device_info.c`：sheng 识别

已应用：

- 把 `qcom,kalama` 识别为 SM8550，把 `xiaomi,sheng`（或 model 含 `sheng`）
  识别为本机；
- 统计内存时跳过 `/memory` 中 size=0 的占位 bank；
- UI 和设置页显示 `Xiaomi Pad 6S Pro` / `sheng (SM8550)`。

## 3. `src/drivers/qcom_reboot.c`：SPMI v7

已应用：接受 SPMI v5/v7/v8/8.5，而不是只接受 v8/8.5。sheng 报告 v7，
否则 Recovery/Fastboot/EDL 会返回 `KSHIM_QCOM_REBOOT_UNSUPPORTED`。

## 4. 设置与 Atlas UI

已应用：

- SDAM `0x8400/0x8500` 的 `KSET` v2 记录，保存上次启动、语言和开机延迟；
- Atlas 六页 UI（`src/ui/menu_atlas.h`）与 56×56 A8 图标
  （`src/ui/atlas_icons.h`）。

## 5. 可选：触屏

sheng 使用 Novatek NT36532E + 用户态 `thp`，kShimC 目前只支持 ST FTM5。
在真机上确认 I²C 地址、中断和复位 GPIO 之前，不要打开
`CONFIG_KSHIM_TOUCH`。
