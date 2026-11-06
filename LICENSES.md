# Licenses and provenance

**English** | [简体中文](LICENSES.zh-CN.md)

The original code in this repository (`packer/`, `scripts/`, `config/`,
`tests/`, `docs/`, `windows/`) is released under **GPL-3.0**, see
[`LICENSE`](LICENSE). This is inherited from kShimC, not an additional
restriction.

The full upstream list, pinned revisions and local changes are in
[`docs/provenance.md`](docs/provenance.md).

| Component | Location | Upstream / revision | License |
|---|---|---|---|
| kShimC runtime | `vendor/kshim/` | [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC), via armadaos-freedom `244b74516d99d06cb0518609ce4b842ab278be8a` `kshim-success-source` | GPL-3.0 |
| MultiBootKernelPatcher | `vendor/packer/` | [Project-Aloha/DualBootKernelPatcher](https://github.com/Project-Aloha/DualBootKernelPatcher) (v2.0.0.0, DuoWoA/AlohaWoA) | GPL-3.0-or-later |
| inih | `vendor/packer/lib/inih/` | [benhoyt/inih](https://github.com/benhoyt/inih) (bundled with MBKP) | BSD-3-Clause |
| CrDK | `vendor/kshim/lib/CrDK/` (fetched) | [Project-Aloha/CrDK](https://github.com/Project-Aloha/CrDK) `b6c3335136b7f6b28271917de11e8f11982975fc` | MIT |
| picolibc | `vendor/kshim/lib/picolibc/` (fetched) | [picolibc/picolibc](https://github.com/picolibc/picolibc) `f7edb0869e64b9d687f46961adeef7154921fe92` | BSD-3-Clause (newlib parts have additional terms) |
| LVGL | `vendor/kshim/third_party/lvgl/` (fetched) | [lvgl/lvgl](https://github.com/lvgl/lvgl) `7f07a129e8d77f4984fff8e623fd5be18ff42e74` | MIT |
| libfdt | `vendor/kshim/lib/libfdt/` (fetched) | `lib/libfdt` from [sunflower2333/kShimC](https://github.com/sunflower2333/kShimC) `82ce6eaa10c36e64cfd3f16799172d854f9a955b` | GPL-2.0-or-later or BSD-2-Clause |
| `prebuilt/sheng-runtime.bin` | `prebuilt/` | built here from `vendor/kshim` | GPL-3.0 |
| `prebuilt/windows/*.exe` | `prebuilt/windows/` | built here from `vendor/packer` with LLVM-MinGW 20261020 | GPL-3.0-or-later |

## Relation to armadaos-freedom

armadaos-freedom as a whole is **GPL-2.0-or-later**; its kShimC snapshot is
still distributed under **GPL-3.0**, and the two are compatible through "or
later". This project uses only that kShimC snapshot, the standalone packer and
its own sheng adaptation code; it contains no other ArmadaOS code.

The "适配维护：B站「白菜」" line in the menu UI is the upstream
armadaos-freedom attribution and was not changed here.

## Generated boot images

`scripts/make-boot.sh` embeds the stock Android kernel supplied by the user.
Stock kernels, `boot_a` dumps and `dtbo` images are vendor proprietary
content: **do not redistribute them** with this project or its build outputs.
The repository and releases contain only source, scripts and the runtime.

If you distribute an image containing the kShimC runtime, you must also provide
the corresponding `vendor/kshim/` source (or a written offer), as required by
the GPL.
