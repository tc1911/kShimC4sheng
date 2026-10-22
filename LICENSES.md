# 许可证说明

本项目自身代码（`packer/`、`scripts/`、`config/`、`tests/`、`docs/`）按
**GPL-3.0** 发布，见 `LICENSE`。这是继承自 kShimC 的许可证，不是本项目另加的。

| 组件 | 位置 | 许可证 |
|---|---|---|
| kShimC runtime | `vendor/kshim/` | GPL-3.0（上游 sunflower2333/kShimC） |
| MultiBootKernelPatcher | `vendor/packer/` | GPL-3.0-or-later（The DuoWoA & The AlohaWoA authors） |
| CrDK | `vendor/kshim/lib/CrDK/` | MIT |
| picolibc | `vendor/kshim/lib/picolibc/` | BSD-3-Clause（newlib 部分另有条款） |
| LVGL | `vendor/kshim/third_party/lvgl/` | MIT |
| libfdt | `vendor/kshim/lib/libfdt/` | GPL-2.0-or-later 或 BSD-2-Clause（二选一） |
| `prebuilt/sheng-runtime.bin` | `prebuilt/` | GPL-3.0（由 `vendor/kshim` 构建） |

## 与 armadaos-freedom 的关系

armadaos-freedom 整体是 **GPL-2.0-or-later**；其中的 kShimC 快照仍按
**GPL-3.0** 分发，两者通过 “or later” 兼容。本项目只使用该 kShimC 快照和
独立的打包器，不包含 ArmadaOS 的其他代码。

## 生成的 boot 镜像

`scripts/make-boot.sh` 会把用户自己提供的原厂 Android 内核打进镜像。
原厂内核、`boot_a` dump、`dtbo` 等属于设备厂商的专有内容，**不要随本项目
或构建产物一起公开分发**；仓库和 release 里只放源码、脚本和 runtime。

如果分发含 kShimC runtime 的镜像，需要同时提供 `vendor/kshim/` 的对应源码
（或书面 offer），这是 GPL-3.0 的要求。
