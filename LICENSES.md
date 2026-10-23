# 许可证与来源说明

本项目自身代码（`packer/`、`scripts/`、`config/`、`tests/`、`docs/`、`windows/`）
按 **GPL-3.0** 发布，见 [`LICENSE`](LICENSE)。这继承自 kShimC 的许可证，
不是本项目另加的。

完整的上游清单、固定 revision 和本地修改记录见
[`docs/provenance.md`](docs/provenance.md)。

| 组件 | 位置 | 上游 / revision | 许可证 |
|---|---|---|---|
| kShimC runtime | `vendor/kshim/` | [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC)，经 [armadaos-freedom](https://github.com/mydanyi/armadaos-freedom) `244b74516d99d06cb0518609ce4b842ab278be8a` 的 `kshim-success-source` 快照 | GPL-3.0 |
| MultiBootKernelPatcher | `vendor/packer/` | [Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher)（v2.0.0.0，DuoWoA/AlohaWoA） | GPL-3.0-or-later |
| inih | `vendor/packer/lib/inih/` | [benhoyt/inih](https://github.com/benhoyt/inih)（随 MBKP 快照） | BSD-3-Clause |
| CrDK | `vendor/kshim/lib/CrDK/`（构建时拉取） | [Project-Aloha/CrDK](https://github.com/Project-Aloha/CrDK) `b6c3335136b7f6b28271917de11e8f11982975fc` | MIT |
| picolibc | `vendor/kshim/lib/picolibc/`（构建时拉取） | [picolibc/picolibc](https://github.com/picolibc/picolibc) `f7edb0869e64b9d687f46961adeef7154921fe92` | BSD-3-Clause（newlib 部分另有条款） |
| LVGL | `vendor/kshim/third_party/lvgl/`（构建时拉取） | [lvgl/lvgl](https://github.com/lvgl/lvgl) `7f07a129e8d77f4984fff8e623fd5be18ff42e74` | MIT |
| libfdt | `vendor/kshim/lib/libfdt/`（构建时拉取） | [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC) `82ce6eaa10c36e64cfd3f16799172d854f9a955b` 的 `lib/libfdt` | GPL-2.0-or-later 或 BSD-2-Clause（二选一） |
| `prebuilt/sheng-runtime.bin` | `prebuilt/` | 由本仓库从 `vendor/kshim` 构建 | GPL-3.0 |
| `prebuilt/windows/*.exe` | `prebuilt/windows/` | 由本仓库从 `vendor/packer` 用 LLVM-MinGW 20261020 构建 | GPL-3.0-or-later |

## 与 armadaos-freedom 的关系

armadaos-freedom 整体是 **GPL-2.0-or-later**；其中的 kShimC 快照仍按
**GPL-3.0** 分发，两者通过 “or later” 兼容。本项目只使用该 kShimC 快照、
独立的打包器和本项目自己的 sheng 适配代码，不包含 ArmadaOS 的其他代码。

菜单 UI 里的 “适配维护：B站「白菜」” 是上游 armadaos-freedom 的标识；
本仓库没有改动这段归属文字。

## 生成的 boot 镜像

`scripts/make-boot.sh` 会把用户自己提供的原厂 Android 内核打进镜像。
原厂内核、`boot_a` dump、`dtbo` 等属于设备厂商的专有内容，**不要随本项目
或构建产物一起公开分发**；仓库和 release 里只放源码、脚本和 runtime。

如果分发含 kShimC runtime 的镜像，需要同时提供 `vendor/kshim/` 的对应源码
（或书面 offer），这是 GPL 的要求。
