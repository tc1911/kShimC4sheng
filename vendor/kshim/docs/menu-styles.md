# LVGL 启动菜单风格

## 当前范围

本文记录早期 **25 个预设**阶段的布局和渲染实现，包括在原有 16 个预设上新增的下表 9 项。它不是当前完整主题清单；后续新增主题见 [new-themes.md](new-themes.md)，完整选项以 `src/Kconfig` 和 `src/ui/styles/` 为准。

Y700 四代与五代当前都使用 STAR_CHART，配置与交互见 [Y700 启动菜单](tb323fu-boot-menu.md)。主题在构建时选择，不提供运行时换肤。下文的测试数量与 CI 矩阵对应早期实现阶段。

| 新增配置后缀 | 外观与布局 |
| --- | --- |
| MATERIAL2 | 浅色 Material 2 卡片、小圆角、有限卡片及主按钮阴影 |
| MATERIAL2_DARK | 深灰背景、较亮抬升表面、低饱和紫色主操作，不做机械反色 |
| FLUENT2 | 中性浅色表面、4 像素行/按钮圆角、内缩短圆角焦点标记 |
| FLUENT2_DARK | 同一几何的深色版，浅蓝色主操作与短焦点标记 |
| FLUENT_DARK | 既有 FLUENT 的深色兄弟预设，保留整高侧边线和原有几何 |
| SURFACE_DARK | Surface-inspired 深色版，保留宽横屏双栏与窄屏单栏 |
| HARMONYOS | 蓝白表面、圆形序号徽标、大圆角卡片，自适应 1/2/3 列 |
| CLOVER | Clover EFI-inspired 横向大图标启动带、所选名称、底部居中 Boot |
| IOS_HIG | 大标题、内缩圆角列表、尾部勾选标记、独立胶囊主按钮 |

`FLUENT` 早期曾描述为 Fluent 2-inspired，本轮不重命名或覆盖该配置。
新的 `FLUENT2` 通过短焦点标记、行/按钮圆角和按钮阴影与旧版区分。
Material 2 是本轮按要求新增；Material 3 继续使用原有色调容器与大圆角。

`IOS_HIG` 与既有 `CUPERTINO` 分开：灰色画布、最大 780 像素内容宽度、
白色内缩圆角组和右侧勾号。勾号表示当前选中项，不表示已经启动。
所有适配均为裸机 LVGL 自定义实现，不声称移植或完整遵循官方 UI 工具包。
不提供 UIKit、Dynamic Type、Liquid Glass、SF Symbols、Mica 或动态取色。
字体仍使用仓库原有 CJK 字体，不引入系统专用字体或品牌图片。

HarmonyOS 与 Clover 只显示实际传入的启动项名称和序号。Clover 图形是程序
构建的通用存储图标，不从名称猜测 OS、不伪造设备信息或未实现的 UEFI 设置，
也不导入 Clover 的 theme.plist/PNG/SVG 主题包。

原有配置为 FLAT、CLASSIC、CARDS、TERMINAL、MINIMAL、HIGH_CONTRAST、FLUENT、
MATERIAL3、SURFACE、CUPERTINO、GLASS、AURORA、SOFT_UI、BENTO、NEON、NORD。

## 选择与构建

```sh
cmake --build build --target menuconfig
```

进入 `Framebuffer and UI -> Boot menu style`，选择并保存，然后重新构建：

```sh
cmake --build build --parallel
```

例如 `CONFIG_KSHIM_MENU_STYLE_IOS_HIG=y`、`CONFIG_KSHIM_MENU_STYLE_HARMONYOS=y`、
`CONFIG_KSHIM_MENU_STYLE_CLOVER=y`、`CONFIG_KSHIM_MENU_STYLE_FLUENT2_DARK=y`。
这些配置属于同一个 choice，不要同时追加多个 `=y`。修改 defconfig 时关闭
其他风格符号。主机测试的 `KSHIM_TEST_MENU_STYLE` 不替代固件的 Kconfig。

## 布局与交互

Surface 在屏幕宽度至少 720 且横屏时双栏。Bento 在面板宽度至少 560、
屏幕高度至少 320 时两列。HarmonyOS 使用同样的两列门槛，面板宽度达到
960 时三列，其余尺寸回落单列。布局在动态菜单重建时重新计算。

Clover 在屏幕宽度至少 320、高度至少 240 时采用单条横向滚动启动带；
条目数增加不会撑高面板。少量图标能完整放入时居中，溢出时可以滚动，
更小屏幕回落普通列表。焦点变化会将所选项滚动到可见区。

iOS 使用单列分组、右侧独立预留的勾选区与居中主按钮；勾号直接绘制，
不依赖特殊字体，也不添加独立触摸目标。HarmonyOS/Clover 的徽标同样不
接管触摸；长名称限制在可用一至两行内，省略截断，不遮盖图标。

触摸行或图标只选中，点击 Boot 才确认；取消触摸不会启动。物理按键继续
按真实条目顺序前进/后退/确认，保留 49 项上限、倒计时、中文和启动接口。

## 实现与资源

`src/ui/menu_style.h` 定义预设字段，`src/ui/styles/*.h` 保存独立数据，
只编入所选预设。`menu_style_render.h` 应用材质、层次、徽标与焦点效果，
`menu_ios.h` 实现 iOS 分组和勾号，`lvgl_port.c` 共用控件与事件回调。

新增预设不分配动态背景纹理 scratch。FLAT 继续按原逻辑使用两张默认预算
256*256*4 字节纹理；少占用的 512 KiB 是 scratch 预算，不是镜像缩小量。
阴影和透明度仍有软件绘制成本，未经真机测量不承诺帧率。极小屏幕的
裁剪/滚动和内存安全退化不代表能完整显示所有标题及长文本。

## 验证与预览

```sh
python3 tools/test_menu_styles.py --cc gcc --kconfig
python3 tools/test_menu_styles.py --cc clang
cmake -S tests -B build-menu -DKSHIM_TEST_MENU_STYLE=IOS_HIG
cmake --build build-menu --parallel 2 --target \
    lvgl_test lvgl_hdk_test menu_style_render_test menu_modern_test ui_preview_test
ctest --test-dir build-menu --output-on-failure \
    -R '^(lvgl_test|lvgl_hdk_test|menu_style_render_test|menu_modern_test)$'
mkdir -p previews
./build-menu/ui_preview_test previews
python3 tools/preview_manifest.py previews
```

轻量测试覆盖 25 个预设、300 组非法双选、隐式/显式 FLAT、0/1 宏、圆角
边界和真实 Kconfig 生成头。文字检查包含透明度合成、渐变、按压态及新
图标徽标的对比度，阈值 4.5:1，不等同于整体无障碍认证。

CI 配置 25 款主机回归与 19 款现代预设 ARM64 runtime 编译。主机回归开启
ASan/UBSan，覆盖 1/2/3/49 项、横竖屏、多列与横向布局、长文本、图标触摸、
确认/取消、普通帧循环的确认反馈及恢复、生命周期与帧缓冲哨兵。
iOS 额外检查勾号在实际像素中的出现与消失，而不只检查逻辑焦点状态。
FLAT 与最初渲染器在两种分辨率、四个阶段进行逐像素对照。

`menu-STYLE` Actions artifact 保存真实 LVGL 绘制的 PNG、索引 HTML、
SHA256 清单和测试日志，保留 14 天。两种预览尺寸为 1080x2340 与 1920x1080，
包含入场、就绪、焦点与确认四阶段。PNG 仅转换实际 PPM 帧，不重新绘制 UI。
通过情况以对应提交 CI 为准；交叉编译不等同于 QEMU 或真机验收。

## 设计参考

- Material 2 dark surfaces: https://m2.material.io/develop/android/theming/dark
- Fluent 2 shapes: https://fluent2.microsoft.design/shapes
- Surface UEFI: https://learn.microsoft.com/en-us/surface/manage-surface-uefi-settings
- HarmonyOS concepts: https://developer.huawei.com/consumer/cn/design/concept/
- Clover layout: https://github.com/CloverHackyColor/CloverBootloader/wiki/Design
- Apple HIG lists: https://developer.apple.com/cn/design/human-interface-guidelines/lists-and-tables
- Apple HIG layout: https://developer.apple.com/cn/design/human-interface-guidelines/layout
