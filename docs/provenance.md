# 来源、上游与本地修改

本仓库是在多个上游项目的基础上做 **sheng（小米平板 6S Pro / SM8550）** 适配，
不是从零实现。下面按“随仓库分发的代码 / 构建时拉取的依赖 / 设备镜像与文档 /
本地修改”分开记录，方便追溯每个文件的来源。

## 0. 继承关系

```
sunflower2333/kShimC                 上游 kShimC runtime（GPL-3.0）
        │
        └── mydanyi/armadaos-freedom  Y700 四代/五代快照 + STAR_CHART 菜单
                ├── vendor/kshim      本仓库的 runtime 源码快照
                └── vendor/packer     MultiBootKernelPatcher（DuoWoA/AlohaWoA）

code002-2/Xiaomi-pad-6s-pro-Linux     双系统分区布局与安装文档
ianchb/debian-sheng                    Debian rootfs 构建工作流
ianchb/sm8550-mainline                 sheng 主线内核 / boot.img
```

## 1. 本仓库原创的部分

以下文件由本项目编写（或在本项目内做了实质性改写），按 **GPL-3.0** 发布：

| 路径 | 说明 |
|---|---|
| `packer/bootimg.py` | Android boot v0–v4 解析/重打包 |
| `packer/extract.py` | 从 stock / 发行版 boot.img 提取 kernel、DTB、initrd |
| `packer/fdt.py` | 纯 Python FDT 读写（替代 dtc/fdtput，Windows 可用） |
| `packer/make_boot.py` | 生成 boot_a.img、manifest 校验、大小预算 |
| `packer/manifest.py` | kShimC manifest 解析 |
| `scripts/*.sh` | 准备、构建、备份、刷机、runtime 构建 |
| `config/sheng*` | sheng 的 defconfig / 分区与 cmdline |
| `tests/` | 纯 Python 回归测试 |
| `windows/` | Windows 一键构建包装 |
| `docs/` | 设备资料、双系统记录、排障、本文档 |

sheng 设备适配补丁（相对 kShimC 快照）见
`patches/README.md` 与 `vendor/README.md`。

## 2. 随仓库分发的上游代码

| 路径 | 上游 | 版本/快照 | 许可证 |
|---|---|---|---|
| `vendor/kshim/` | [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC) | 经 [mydanyi/armadaos-freedom](https://github.com/mydanyi/armadaos-freedom) 提交 `244b74516d99d06cb0518609ce4b842ab278be8a`（2026-10-22）的 `devices/lenovo-tb323fu/boot/kshim-success-source` 快照 | GPL-3.0 |
| `vendor/packer/` | [Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher)（MultiBootKernelPatcher v2.0.0.0；源自 DuoWoA/AlohaWoA 的 SurfaceDuoDualBootKernelPatcher 系列） | 经 armadaos-freedom 的 `packages/kshim-tools/vendor/packer` 快照 | GPL-3.0-or-later |
| `vendor/packer/lib/inih/` | [benhoyt/inih](https://github.com/benhoyt/inih) | 随 MBKP 快照，未单独固定 revision | BSD-3-Clause |
| `prebuilt/sheng-runtime.bin` | 本仓库从 `vendor/kshim` 构建 | 4 MiB，sha256 `a24ac1ef81c11abbaa2e640b3dc0b1f8928f508fe1dfa035d9761773ceaacbb4` | GPL-3.0 |
| `prebuilt/windows/*.exe` | 本仓库从 `vendor/packer` 交叉编译 | LLVM-MinGW 20261020（UCRT，x86-64），`-DBUILD_SHIMS=OFF` | GPL-3.0-or-later |

`vendor/kshim` 的 runtime 上游文档写明使用 **picolibc 1.8.11** 与 **LVGL 9.2.2**；
这两个库不在快照里，构建时按下面的 revision 拉取。

## 3. 构建时拉取、不入库的依赖

| 组件 | 上游 | 固定 revision |
|---|---|---|
| CrDK | [Project-Aloha/CrDK](https://github.com/Project-Aloha/CrDK) | `b6c3335136b7f6b28271917de11e8f11982975fc` |
| picolibc | [picolibc/picolibc](https://github.com/picolibc/picolibc) | `f7edb0869e64b9d687f46961adeef7154921fe92` |
| LVGL | [lvgl/lvgl](https://github.com/lvgl/lvgl) | `7f07a129e8d77f4984fff8e623fd5be18ff42e74` |
| libfdt | [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC) | `82ce6eaa10c36e64cfd3f16799172d854f9a955b`（仅取 `lib/libfdt`） |
| kconfiglib | [ulfalizer/kconfiglib](https://github.com/ulfalizer/kconfiglib) | 使用系统/venv 安装版本；Arch 上也可用 `/opt/klipper/lib/kconfiglib` |

拉取脚本：`scripts/fetch_deps.sh`（默认走 `ghproxy.net`，可用 `GHPROXY=` 覆盖）。

## 4. 设备镜像、rootfs 与文档来源（不入库）

| 内容 | 来源 | 说明 |
|---|---|---|
| 双系统分区布局、安装/切换文档 | [code002-2/Xiaomi-pad-6s-pro-Linux](https://github.com/code002-2/Xiaomi-pad-6s-pro-Linux) | 原版 `docs/安装指南.md` 使用 `parted rm 29`、`mkpart userdata ext4 12.7GB 140.7GB`、`mkpart linux ext4 140.7GB -0MB`；本项目按 128 GiB Linux 调整，见 `docs/sheng-debian-dual-boot.md` |
| Debian forky / KDE rootfs | [ianchb/debian-sheng](https://github.com/ianchb/debian-sheng) | GitHub Actions `rootfs.yml`（`IMAGE_SIZE: 10G`）；本项目使用的是 forky/KDE/language zh_CN 的 artifact |
| sheng 内核与 boot.img | [ianchb/sm8550-mainline](https://github.com/ianchb/sm8550-mainline) | release `7.2.6-mac`（2026-09-17），取 `boot_sheng_dualboot.img`；分支 `sheng-7.2.6`/`sheng-7.2.10` |
| sheng 固件包 | [ianchb/sheng-firmware](https://github.com/ianchb/sheng-firmware) | rootfs 构建时打入 |
| TWRP | [NoobMaster233/twrp-sheng-build](https://github.com/NoobMaster233/twrp-sheng-build) | v1.0.1 `recovery.img`，sha256 `55909845eae82e2d22748271d948ff2a09cf821d29761c183621404cde9397d5` |
| TWRP 设备树（分区/头版本参考） | [map220v/android_device_xiaomi_sheng](https://github.com/map220v/android_device_xiaomi_sheng) | `BoardConfig.mk`：boot 201326592 B、TWRP 侧 header v2 |
| 原厂 boot/dtbo/init_boot、原厂 ROM | 用户自己的设备 dump / 官方 fastboot 包 | **不随仓库分发**；见 `docs/sheng-stock-images.md` 与 `/home/tc191/6spBackup/rom/`（本机备份） |

上游文档中的分区数字有 256G/512G/1T 几组；本项目只针对 256G sheng 验证，
其它容量需要自行换算，不能直接照抄。

## 5. 相对上游的本地修改

### kShimC runtime（`vendor/kshim`）

- `CMakeLists.txt`：UNIX 下允许 `KSHIM_TOOLCHAIN_PREFIX` 覆盖写死的交叉工具链前缀。
- `tools/configure_kconfig.py`：修正未启用 hex 符号被写成 `0x` 的问题。
- `lib/libfdt/fdt_overlay.c`：补 `#include <stdlib.h>`。
- `src/include/device_info.h`、`src/dt/device_info.c`：新增 `DEVICE_SOC_SM8550` /
  `DEVICE_BOARD_SHENG`，识别 `qcom,kalama*` / `xiaomi,sheng`，跳过 size=0 的
  `/memory` 占位 bank。
- `src/dt/splash.c`：SM8550 的 10 SSPP / 8 mixer 表项解码；
  `src/dt/resources.c`：`CONFIG_KSHIM_SPLASH_FALLBACK` 直接复用 ABL 的
  `cont_splash_region`（SDE 路由解码仍失败，见 README）。
- `src/drivers/qcom_reboot.c`：接受 SPMI 仲裁器 v5/v7/v8/8.5（sheng 是 v7），
  增加 stage/detail 诊断；解析 `qcom,spmi-sdam` 的 `reg`（兼容 4-cell），
  为 NV 访问提供 SDAM 基址。
- `src/drivers/qcom_nv.c`、`src/include/qcom_nv.h`：新增“上次启动”记录，
  写到 PMIC SDAM `0x8400/0x8500` 的 `0x50/0x60`，双份 `KSET` 记录 + CRC32；
  `CONFIG_KSHIM_BOOT_LAST` 开启时预选上次条目，记录失效回退 Android。
- `src/core/platform.c`、`src/core/boot_settings.c`、`src/include/boot_settings.h`：
  设备选择页加入 Xiaomi Pad 6S Pro；新增 `CONFIG_KSHIM_MENU_SINGLE_LINUX`
  （无 TF 卡时精简菜单）。0.0.2 起设备固定为 sheng，设置页只保留语言切换，
  “上次启动”移到设备信息页；确认条目后显示“正在启动，请稍候”。
- `src/core/menu_locale.c`、`src/include/menu_locale.h`、`src/include/menu_text.h`：
  版本 0.0.2 文案、署名“原作者：B站 单翼，适配维护：tc191｜免费使用，谨防
  收费诈骗。”与启动提示的中英文案。
- `ports/crdk/patches/0002-freestanding-resources.patch`：CrDK `CrSplashRead()`
  放宽 legacy 路径的 pipe 数限制（见 `UPSTREAM.json` 中的 sha256）。

### 打包器（`vendor/packer`）

- `src/pack_config.c`：识别 Windows 的 `C:\...` / `\\server\...` 绝对路径
  （原逻辑只认 `/`）。
- 其余为上游代码；`BUILD_SHIMS=OFF` 时不编译 ARM64 shim，Windows 交叉编译
  只用 C 运行时。

### 本仓库新增的替代实现

- `packer/fdt.py` 替代 `dtc`/`fdtput`/`fdtget`，因此 Windows 构建不再依赖
  MSYS2 或 device-tree-compiler；行为由 `tests/test_fdt.py` 覆盖。
- `vendor/kshim/src/ui/fonts/lxgw_wenkai_menu.ksf`：基于 LXGW WenKai
  （SIL OFL 1.1）的菜单子集，0.0.2 重新生成到 708 字，加入“者 / 正 / 稍 / 候”；
  来源、源文件 sha256 与字形数见同目录 `.provenance.json`。
- `packer/make_boot.py --distro-boot` 直接解析发行版 boot.img，Linux 与 Windows
  共用同一条 Python 路径。

## 6. 工具与致谢

- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) 20261020：交叉编译
  `prebuilt/windows/*.exe`。
- [bkerler/edl](https://github.com/bkerler/edl)：`scripts/edl-backup.sh` 使用；
  本机对 Python 3.14 的 `rsp.data` bytes/dict 问题做了本地补丁，`tools/`
  不入库。
- armadaos-freedom 的 STAR_CHART 菜单与“适配维护：B站「白菜」”标识来自上游
  快照；本项目只做 sheng 适配与打包工具。
- 感谢 kShimC、armadaos-freedom、Project-Aloha/DuoWoA、ianchb、code002-2、
  NoobMaster233、map220v 等上游作者。

如果发现归属或 revision 记录有误，请开 issue 或直接改本文件。
