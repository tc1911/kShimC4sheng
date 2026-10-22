# Y700 启动菜单（四代与五代）

四代与五代共用菜单实现，均选择 STAR_CHART、简体中文和逆时针 270 度输出。机型配置与启动载荷分别维护：

| 设备 | 配置 | 构建说明 |
|---|---|---|
| Y700 四代 / TB322FC | `configs/tb322fc_android_defconfig` | [四代引导](../../../../lenovo-tb322fc/boot/README.md) |
| Y700 五代 / TB323FU | `configs/tb323fu_unified_defconfig`；早期流程使用 `tb323fu_defconfig` | [五代引导](../../star-chart-build/README.md) |

- 主菜单按 manifest 显示系统启动项，并提供高级启动、设置、设备信息、语言、重启和关机。某个机型出现 Linux 或 TF 卡入口，不等于它已经附带可启动的对应载荷；可用范围以各机型文档为准。
- 高级启动模式：Linux 诊断模式、Recovery 恢复模式、Fastboot 模式、9008 下载模式、返回主菜单。
- 默认启动项和超时由 manifest 决定。常用打包配置为 8 秒；四代 Android 基座默认 Android，四代已安装布局与五代离线布局默认机身 Linux。有效按键交互取消倒计时，设置中的默认启动项修改仅在本次启动生效。
- Linux 诊断项通过自定义 DT 的 `/chosen/armada,boot-stage = maintenance` 识别；平台层保存原 payload 索引，不能用显示行号直接索引 manifest。
- 说明文字读取同一 payload 的自定义 DT：`armada,boot-stage = maintenance` 优先识别诊断环境，其次按 `/chosen/armada,root-medium = internal / tf` 识别机身和 TF 卡。实际输入 DT 使用 `root-medium`，不存在 `boot-medium`。只有 TB 配置启用的 `CONFIG_KSHIM_MENU_BASE_ANDROID` 将无自定义 DT 的 base image 明确描述为 Android；其他配置保留“启动设备原有系统。”，未知元数据使用“启动所选系统。”。不按显示名称或固定顺序猜测。
- 右侧标题、说明和按钮文字跟随焦点，设备信息在详情区展示。倒计时、失败提示与实体按键提示单独显示。两代配置均启用 `CONFIG_KSHIM_MENU_ENTRY_DETAILS` 和 `CONFIG_KSHIM_MENU_SESSION_SETTINGS`；机型选择支持自动识别和手动指定，尚不保存到存储。

| 选项 | 说明 | 按钮 |
| --- | --- | --- |
| 原厂 Android | 启动设备原有的 Android 系统。 | 启动 |
| 机身 ArmadaOS | 启动安装在机身存储中的 ArmadaOS。 | 启动 |
| TF 卡 ArmadaOS | 启动安装在 TF 卡中的 ArmadaOS，请先插入对应 TF 卡。 | 启动 |
| 高级启动模式 | 进入 Linux 诊断、恢复或刷机模式。 | 进入 |
| 设置 | 选择自动／手动机型及本次默认启动项。 | 进入 |
| 设备信息 | 在详情区查看当前识别信息。 | 返回 |
| 重启 | 重新启动设备。 | 重启 |
| 关机 | 关闭设备电源。 | 关机 |
| Linux 诊断模式 | 启动 Linux 诊断环境，用于检查启动和硬件问题。 | 启动 |
| Recovery 恢复模式 | 进入 Android 恢复模式，进行系统维护。 | 进入 |
| Fastboot 模式 | 进入 Fastboot，通过 USB 连接电脑进行维护或刷写。 | 进入 |
| 9008 下载模式 | 进入高通 9008 下载模式，供电脑端工具恢复或刷写。 | 进入 |
| 返回主菜单 | 返回系统启动选项。 | 返回 |

显示资源由上级 BL 的 DT 和已有扫描输出确定，LVGL 按配置旋转脏矩形，不改原 framebuffer 描述。五代参考输出为 1904×3040、stride 7616，逻辑横屏画布为 3040×1904。主机预览沿用真实帧缓冲路径；新增机型仍需核对原生尺寸和物理方向，不能只靠旋转预览图验证。

`menu_text.h` 自行先加载生成的 `config.h`，避免调用方头文件顺序把本应为中文的标题和按钮提前固化为英文。主机中文测试读取真实机型 Kconfig，另有英文配置回归。

## 高级模式的实现与验证边界

Recovery/Fastboot 使用当前设备 DT 指向的 SDAM restart reason；9008 使用 DT 指向的 SCM download cookie 和已查询支持的暖复位入口。五代参考配置的恢复／Fastboot 原因为 1/2、SDAM cell 为 `<1 7>`，保留 bit 0；EDL 值为 1。其他机型仍须按自己的 DT 和固件核对，不能直接沿用这些地址或假定高级操作已验证。

只在确认相应菜单动作时访问重启控制字段。调用返回视为失败，回滚原字段并保留菜单。高级模式的真实可用状态见[四代](../../../../lenovo-tb322fc/README.md#已知问题与待完成项)与[五代](../../../README.md#已知限制)设备文档。

## 菜单前延迟的诊断

以下计时接口用于排查菜单出现前的等待，不将早期设备的等待时间作为两代当前启动耗时。四代配置默认关闭计时，五代配置默认开启。

开启 `CONFIG_KSHIM_BOOT_TIMING` 时，向成功准备的自定义 Linux DT `/chosen` 写入：

- `kshim,boot-timing-version`：大端 u32，当前为 1。
- `kshim,boot-timing-ms`：8 个大端 u64，依次为 C 入口、资源解析结束、MMU 初始化阶段结束、字体准备结束、UI 初始化结束、菜单内容设置结束、首帧 flush 完成、菜单选择结束。
- `kshim,boot-entry-counter-ms`：C 入口时的原始计数器毫秒值；原点未验证，不能当作从上电经过的时间。

阶段值相对 C 入口，缺失为 UINT64_MAX。最后一项包含在菜单等待的时间。C 入口以前的固件加载/解压、GOT/BSS/栈清零和重定位不在相对计时内，需结合整机观测区分。属性导出失败不阻止启动，也不改 Android 原厂 DT。

## 离线验证

相关目标：`menu_text_chinese_test`、`menu_text_english_test`、`framebuffer_rotation_test`、`tb323fu_menu_test`、`platform_advanced_menu_test`、`qcom_reboot_test`、`boot_metrics_test`。

真实原厂 DT 可通过 `qcom_reboot_test --hardware-dtb merged-stock.dtb` 只读解析，不执行任何重启或写操作。

上述测试用于验证菜单、旋转、动作分派和启动交接。机型的实际显示、实体按键与高级启动结果分别记录在设备文档中。
