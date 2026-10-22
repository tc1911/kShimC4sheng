# vendor

- `kshim/`：`mydanyi/armadaos-freedom` 五代快照
  （`devices/lenovo-tb323fu/boot/kshim-success-source`），GPL-3.0。
- `packer/`：MultiBootKernelPatcher / UnpackTool 源码，GPL-3.0。

CrDK、picolibc、LVGL 体积大且需要固定 revision，未入库；运行
`scripts/fetch_deps.sh` 拉取到 `vendor/kshim/lib/` 与 `vendor/kshim/third_party/`。

## 对 kshim 快照的本地修改（构建可移植性，不涉及设备逻辑）

1. `CMakeLists.txt`：UNIX 下允许 `KSHIM_TOOLCHAIN_PREFIX` 覆盖写死的
   `/usr/bin/aarch64-linux-gnu-`。
2. `tools/configure_kconfig.py`：kconfiglib 把未启用的 hex 符号写成 `0x`，
   生成 `config.h` 前替换为 `0`（否则 C 编译报 invalid suffix）。
3. `lib/libfdt/fdt_overlay.c`：补 `#include <stdlib.h>`（`strtoul` 隐式声明）。

设备相关补丁（splash / device_info / 按键）见 `patches/README.md`。
