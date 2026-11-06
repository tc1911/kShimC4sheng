# 来源、上游与本地修改

[English](provenance.md) | **简体中文**

本仓库在 kShimC 与 MultiBootKernelPatcher 的基础上做 sheng（小米平板 6S Pro /
SM8550）适配，不是从零实现。

## 继承关系

```text
sunflower2333/kShimC                 上游 runtime（GPL-3.0）
        └── mydanyi/armadaos-freedom  Y700 快照 + STAR_CHART 菜单
              ├── vendor/kshim       本仓库 runtime 快照
              └── vendor/packer      MultiBootKernelPatcher

code002-2/Xiaomi-pad-6s-pro-Linux    双系统分区布局与安装文档
ianchb/debian-sheng                  Debian rootfs 构建工作流
ianchb/sm8550-mainline               sheng 主线内核 / boot.img
```

## 本仓库原创

以下文件为本项目编写或实质性改写，按 **GPL-3.0** 发布：

- `packer/`：boot v0–v4 解析/重打包、载荷提取、纯 Python FDT、manifest 校验。
- `scripts/`：准备、构建、备份、测试启动、刷机、runtime 构建。
- `config/`：sheng 的 defconfig、分区与 cmdline。
- `vendor/kshim/src/ui/menu_atlas.h`、`atlas_icons.h`、`tools/gen_atlas_icons.py`：
  Atlas 六页 UI 与图标。
- `tests/`、`windows/`、`docs/`、`docs/ui-preview/render.py`。

sheng 适配补丁清单见 `patches/README.zh-CN.md` 与 `vendor/README.md`。

## 随仓库分发的上游代码

| 路径 | 上游 / 快照 | 许可证 |
|---|---|---|
| `vendor/kshim/` | [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC)，经 armadaos-freedom 提交 `244b7451`（2026-10-22）的 `devices/lenovo-tb323fu/boot/kshim-success-source` | GPL-3.0 |
| `vendor/packer/` | [Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher)（MBKP 2.0.0.0） | GPL-3.0-or-later |
| `vendor/packer/lib/inih/` | [benhoyt/inih](https://github.com/benhoyt/inih) | BSD-3-Clause |
| `prebuilt/sheng-runtime.bin` | 本仓库构建，4 MiB，sha256 `ee0340fceb666f09df7a97f05f8affee70af592732444643244c8bb47a65d18a` | GPL-3.0 |
| `prebuilt/windows/*.exe` | LLVM-MinGW 20261020 交叉编译 | GPL-3.0-or-later |

## 构建时拉取（不入库）

| 组件 | revision |
|---|---|
| [CrDK](https://github.com/Project-Aloha/CrDK) | `b6c3335136b7f6b28271917de11e8f11982975fc` |
| [picolibc](https://github.com/picolibc/picolibc) | `f7edb0869e64b9d687f46961adeef7154921fe92` |
| [LVGL](https://github.com/lvgl/lvgl) | `7f07a129e8d77f4984fff8e623fd5be18ff42e74` |
| libfdt | kShimC `82ce6eaa10c36e64cfd3f16799172d854f9a955b` 的 `lib/libfdt` |
| kconfiglib | 系统包或 `/opt/klipper/lib/kconfiglib` |

拉取脚本：`scripts/fetch_deps.sh`（默认 `ghproxy.net`，可用 `GHPROXY=` 覆盖）。

## 设备镜像与 rootfs（不入库）

| 内容 | 来源 |
|---|---|
| 分区布局/安装文档 | [code002-2/Xiaomi-pad-6s-pro-Linux](https://github.com/code002-2/Xiaomi-pad-6s-pro-Linux)；本项目按 128 GiB Linux 调整 |
| Debian forky/KDE rootfs | [ianchb/debian-sheng](https://github.com/ianchb/debian-sheng) `rootfs.yml`（`IMAGE_SIZE: 10G`） |
| sheng 内核/boot.img | [ianchb/sm8550-mainline](https://github.com/ianchb/sm8550-mainline) release `7.2.6-mac` |
| sheng 固件 | [ianchb/sheng-firmware](https://github.com/ianchb/sheng-firmware) |
| TWRP | [NoobMaster233/twrp-sheng-build](https://github.com/NoobMaster233/twrp-sheng-build) v1.0.1，sha256 `55909845eae82e2d22748271d948ff2a09cf821d29761c183621404cde9397d5` |
| TWRP 设备树 | [map220v/android_device_xiaomi_sheng](https://github.com/map220v/android_device_xiaomi_sheng) |
| 原厂 boot/dtbo/init_boot/ROM | 用户设备 dump / 官方 fastboot 包；**不随仓库分发**，本机备份在 `/home/tc191/6spBackup/rom/` |

## 相对上游的主要修改

### runtime

- `CMakeLists.txt`：支持 `KSHIM_TOOLCHAIN_PREFIX`。
- `device_info.c`：识别 `qcom,kalama` / `xiaomi,sheng`，跳过 size=0 的 memory bank。
- `splash.c` + `resources.c`：SM8550 10 SSPP / 8 mixer 解码；失败时用
  `cont_splash_region` 强制 framebuffer（`CONFIG_KSHIM_SPLASH_FALLBACK`）。
- `qcom_reboot.c`：接受 SPMI v5/v7/v8/8.5（sheng 为 v7）。
- `qcom_nv.c`：SDAM `0x8400/0x8500` 的 `KSET` v2 记录（上次启动/语言/延迟）。
- `platform.c`：sheng 设备行、`CONFIG_KSHIM_MENU_SINGLE_LINUX`、Atlas UI 数据入口。
- `ports/crdk/patches/0002-freestanding-resources.patch`：放宽 legacy pipe 数限制。
- `src/ui/menu_atlas.h`：Atlas 六页 UI；`atlas_icons.h` 为 56×56 A8 图标。
- 字库 `lxgw_wenkai_menu.ksf`：LXGW WenKai（SIL OFL 1.1）子集，741 字，
  来源与 sha256 见同目录 `.provenance.json`。

### 打包器

- `src/pack_config.c`：识别 Windows 绝对路径。
- `BUILD_SHIMS=OFF` 时不编译 ARM64 shim。

## 工具与致谢

- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) 20261020。
- [bkerler/edl](https://github.com/bkerler/edl)：EDL 备份脚本使用。
- 感谢 kShimC、armadaos-freedom、Project-Aloha/DuoWoA、ianchb、code002-2、
  NoobMaster233、map220v 等上游作者。

发现归属或 revision 有误，请开 issue 或直接改本文件。
