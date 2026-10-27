# Changelog

## 0.0.3 — 2026-10-27

### 内置充电模式
- 关机插电开机时，识别 ABL 的 `androidboot.mode=charger`，跳过菜单直接显示内置充电界面。
- 识别同时覆盖 `/chosen/bootargs`、Android bootconfig / initrd 尾部参数，以及 PMIC 启动原因兜底。
- 充电界面为两行英文：`CHARGER CONNECTED` / `HOLD POWER TO BOOT`；长按电源键 2 秒进入正常菜单。
- 不依赖 Debian、systemd 或任何发行版 userspace；只使用裸机 framebuffer 和按键。
- 说明：裸机没有电池驱动，不显示电量百分比；面板背光由 KTZ8866 控制，shim 暂不关背光。

### 设置持久化
- SDAM 记录升级到 v2，同一条记录保存：上次启动项、菜单语言、开机延迟。
- 设置页新增“开机延迟”子菜单，与语言选择同一交互：
  `不自动启动 / 3 秒 / 5 秒 / 8 秒 / 10 秒 / 15 秒 / 30 秒`。
- 语言和延迟重启、关机后保留；`0` 显示为“不自动启动”。
- 设备信息页只保留只读字段，删除启动诊断行。

### 修复与打磨
- 修复 bootconfig trailer 大小按本机字节序读取（arm64 小端），此前会误报 `bc=-15`。
- 修复充电界面初始化顺序：在 `menu_build` 前建立 PON 按键描述符，避免 `c=-11`。
- 菜单字库扩到 721 字，覆盖设置页新增文案，消除方块字。
- 删除未采用的 ADC 电量读取实验代码，保留精简充电界面。

## 0.0.2 — 2026-10-24

- 菜单记住上次启动的系统（PMIC SDAM `KSET` 记录 + CRC32、双份回退）。
- 选中条目后显示“正在启动，请稍候”，避免误判卡死。
- 设置页精简：设备固定为 sheng，移除设备选择与默认启动项。
- 版本号、署名和启动提示更新；菜单字库扩到 708 字。
- Windows 一键构建：纯 Python FDT 编辑，自带 `MultiBootKernelPatcher.exe`，无需 dtc/fdtput。

## 0.0.1 — 2026-10-23

- 首个 sheng 适配版本：boot_a 开机菜单，替代 `fastboot set_active` / WoaManager 刷写式切换。
- SM8550 显示走 `cont_splash_region` 回退；按键、设备信息、高级启动（Fastboot/Recovery/EDL）可用。
- 支持打包 Debian/发行版 boot.img，双系统 Android + Debian 真机验证通过。
