# kShimC 依赖准备

本文件是 kShimC 快照的依赖准备指引，四代（TB322FC）与五代（TB323FU）构建共用。

以下命令从 Freedom 仓库根目录开始，在 Ubuntu 24.04 Linux 容器内执行。

## 为什么需要单独准备

Freedom 仓库中的这份快照省略了顶层 `.git/`、`lib/CrDK`、`lib/picolibc`、`third_party/lvgl` 与 `lib/libfdt`。因此不能在这里执行 `git submodule update --init --recursive`，仅解析 `.gitmodules` 也不足以还原依赖。

需要恢复四个目录，版本固定：

| 依赖 | 上游仓库 | 固定提交 | 目标路径 |
|---|---|---|---|
| CrDK | `https://github.com/Project-Aloha/CrDK` | `b6c3335136b7f6b28271917de11e8f11982975fc` | `lib/CrDK` |
| picolibc | `https://github.com/picolibc/picolibc` | `f7edb0869e64b9d687f46961adeef7154921fe92` | `lib/picolibc` |
| LVGL | `https://github.com/lvgl/lvgl` | `7f07a129e8d77f4984fff8e623fd5be18ff42e74` | `third_party/lvgl` |
| libfdt | `https://github.com/sunflower2333/kShimC` | `82ce6eaa10c36e64cfd3f16799172d854f9a955b` | `lib/libfdt` |

`lib/CrDK` 必须保留为有效的独立 Git 仓库（含 `.git`）：`ports/crdk/prepare.py` 会读取其 `git rev-parse HEAD` 并执行 `git archive`，只放一份不带 `.git` 的归档副本会导致配置阶段失败。picolibc 与 LVGL 由 CMake 直接 `add_subdirectory`，不需要 `.git`，但按相同方式恢复便于核对固定版本。

## 环境

`.gitignore` 已忽略 `build/`，缓存与 Python 虚拟环境可以放在该目录。

```sh
apt-get update
apt-get install -y --no-install-recommends \
  git ca-certificates cmake ninja-build \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  python3 python3-venv
```

需要 Python 3.12 或更高版本。顶层 `CMakeLists.txt` 直接引用 `/usr/bin/aarch64-linux-gnu-gcc`、`/usr/bin/aarch64-linux-gnu-objcopy`，`tools/build_runtime.py` 还会调用 `aarch64-linux-gnu-nm`，因此交叉工具链需位于 `/usr/bin`（上述软件包满足）。LVGL 的 CMake 配置会检测 C++ 编译器，因此需要 `g++-aarch64-linux-gnu`。

源码路径用 `K` 表示，在此定义一次；后续命令中的依赖目标均加引号：

```sh
K="$PWD/devices/lenovo-tb323fu/boot/kshim-success-source"
```

`genconfig` 由 kconfiglib 提供，必须在 `PATH` 中，否则 CMake 配置会报错：

```sh
python3 -m venv "$K/build/venv"
"$K/build/venv/bin/pip" install kconfiglib
export PATH="$K/build/venv/bin:$PATH"
command -v genconfig
```

下面的恢复命令假定这些依赖目录尚不存在。已准备好依赖的环境只需核对版本，不要重复初始化，也不要用 `rm -rf` 或 `git reset --hard` 覆盖已有目录。

## 恢复三个 Git 依赖

```sh
git init "$K/lib/CrDK"
git -C "$K/lib/CrDK" remote add origin https://github.com/Project-Aloha/CrDK
git -C "$K/lib/CrDK" fetch --depth=1 origin b6c3335136b7f6b28271917de11e8f11982975fc
git -C "$K/lib/CrDK" checkout --detach FETCH_HEAD
```

```sh
git init "$K/lib/picolibc"
git -C "$K/lib/picolibc" remote add origin https://github.com/picolibc/picolibc
git -C "$K/lib/picolibc" fetch --depth=1 origin f7edb0869e64b9d687f46961adeef7154921fe92
git -C "$K/lib/picolibc" checkout --detach FETCH_HEAD
```

```sh
git init "$K/third_party/lvgl"
git -C "$K/third_party/lvgl" remote add origin https://github.com/lvgl/lvgl
git -C "$K/third_party/lvgl" fetch --depth=1 origin 7f07a129e8d77f4984fff8e623fd5be18ff42e74
git -C "$K/third_party/lvgl" checkout --detach FETCH_HEAD
```

`--depth=1` 配合 `FETCH_HEAD` 检出固定提交即可，不需要完整历史。若目录已存在，核对版本：

```sh
git -C "$K/lib/CrDK" rev-parse HEAD
git -C "$K/lib/picolibc" rev-parse HEAD
git -C "$K/third_party/lvgl" rev-parse HEAD
```

三者应依次输出上表中的固定提交。版本不符时先自行移走对应目录再重新获取，不要删除来源不明的已有内容。

## 恢复 libfdt

`lib/libfdt` 不在这份快照中，由公开 `sunflower2333/kShimC` 的固定提交 `82ce6eaa10c36e64cfd3f16799172d854f9a955b` 单独取回，只恢复该提交下的 `lib/libfdt` 目录。该提交仅用作 libfdt 的恢复来源，不是整个 Freedom STAR_CHART 快照的原始提交；不要把上游当前源码整体覆盖到本快照。

```sh
git init "$K/build/dependency-sources/kShimC"
git -C "$K/build/dependency-sources/kShimC" remote add origin https://github.com/sunflower2333/kShimC
git -C "$K/build/dependency-sources/kShimC" fetch --depth=1 origin 82ce6eaa10c36e64cfd3f16799172d854f9a955b
git -C "$K/build/dependency-sources/kShimC" checkout --detach FETCH_HEAD
git -C "$K/build/dependency-sources/kShimC" archive \
  82ce6eaa10c36e64cfd3f16799172d854f9a955b lib/libfdt | tar -x -C "$K"
```

`git archive` 只导出 `lib/libfdt` 这一个目录，结果是 `"$K/lib/libfdt"`，其中包含 `include/libfdt_env.h` 和 `CMakeLists.txt` 引用的 `fdt*.c`。

### 环境头适配

该公开版本的 `include/libfdt_env.h` 缺少历史构建适配所需的一行 `#include <stdlib.h>`。这是 libfdt 在 kShim freestanding 环境下的适配，不能用普通 dtc 的系统头替代。

先重新导出原文件，再执行且只执行一次 `sed`，确保重复运行不会插入两次：

```sh
git -C "$K/build/dependency-sources/kShimC" archive \
  82ce6eaa10c36e64cfd3f16799172d854f9a955b lib/libfdt | tar -x -C "$K"
sed -i '/^#include <stdint.h>$/a #include <stdlib.h>' "$K/lib/libfdt/include/libfdt_env.h"
```

适配后的文件为 LF 换行，校验其 SHA-256：

```sh
cd "$K"
printf '%s  %s\n' \
  132cb14b8f0b323e5701d4bfcb0ebba4b1f35726bfdfa6739f18ad638d20e582 \
  lib/libfdt/include/libfdt_env.h | sha256sum -c -
```

预期输出 `lib/libfdt/include/libfdt_env.h: OK`。若校验失败，按上面的顺序重新导出并重新执行一次 `sed`，不要追加其他修改。

恢复后的 `lib/libfdt` 内 `UPSTREAM-check.json` 只记录 dtc v1.7.0 的 `fdt_check.c` 校验，不能据此认为整个目录原样来自 dtc v1.7.0。

## CrDK 补丁由构建自动应用

`ports/crdk/CMakeLists.txt` 在 CMake 配置阶段调用 `ports/crdk/prepare.py`，校验 `lib/CrDK` 的 HEAD 与 `UPSTREAM.json` 中的补丁哈希，把固定提交归档到构建目录副本，再按顺序应用补丁；原始依赖目录不会被修改。因此不要手工给 `lib/CrDK` 打补丁，也不要修改构建目录中的生成副本——需要调整时修改 `ports/crdk/patches/` 中的补丁本身。

从 Git 归档还原的 `ports/crdk/patches/*.patch` 可能是 CRLF 换行，而 `ports/crdk/UPSTREAM.json` 记录的是 LF 换行下的 SHA-256。构建前先把随快照附带的补丁文件换行统一为 LF，才能匹配记录的哈希；这一步不修改原始 CrDK 依赖，也不跳过哈希校验：

```sh
sed -i 's/\r$//' "$K/ports/crdk/patches/"*.patch
```

## 构建 runtime

依赖就绪且 `genconfig` 在 `PATH` 中后，在源码树内分别构建两代配置：

```sh
cd "$K"
python3 tools/build_runtime.py --config configs/tb322fc_android_defconfig --out build/tb322fc-android --jobs 4
python3 tools/build_runtime.py --config configs/tb323fu_unified_defconfig --out build/tb323fu-unified --jobs 4
```

产物分别是 `build/tb322fc-android/kShimC.bin` 与 `build/tb323fu-unified/kShimC.bin`，必须恰好为 4194304 字节。脚本会拒绝未定义符号，并写出记录大小、SHA-256 与 `hardware_tested: false` 的 `validation.json`。构建通过只表示离线编译与链接检查通过，不代表真机验收。

字体无需生成：快照自带 `src/ui/fonts/` 下的菜单字库，本流程无需重新生成字体。
