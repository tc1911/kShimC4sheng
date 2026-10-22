# kShimC4sheng

为 **小米平板 6S Pro（sheng / SM8550）** 做一个开机菜单，替代现在
`fastboot set_active a|b` / WoaManager 刷 boot 分区的双系统切换方式。

刷入后每次开机先进入 kShimC 菜单：

- **Android**：直接跳转原厂内核，ABL 仍会加载 `init_boot_a` / `vendor_boot_a` /
  `dtbo_a`，不重启、不切槽。
- **Linux**：用内嵌的 Linux Image + DTB + 可选 initrd 启动 `linux` 分区上的
  rootfs（`root=PARTLABEL=linux`）。

菜单默认 8 秒后进 Android，音量键选择、电源键确认（sheng 的触屏驱动未适配，
与两代 Y700 一样只用按键）。

> 状态：**打包链路已用真实载荷验证，runtime 已成功构建，尚未上机**。
> 合成 stock 镜像 + Armada 真实 Linux 载荷产出 99,643,392 B（192 MiB 分区，
> 剩余 101,683,200 B）；`UnpackTool` 能完整还原 Android/Linux/DTB/initrd/shim。
> 上机前请先读 `docs/troubleshooting.md` 的救砖部分。

## 为什么放在 boot_a

sheng 是 A/B 设备，ABL 按当前槽位加载 `boot_x`、`init_boot_x`、
`vendor_boot_x`、`dtbo_x`。把 shim 放在 **boot_a** 并让设备停在 A 槽：

- Android 的 ramdisk/DTBO 与 `system_a` 天然对齐，不需要伪造 `slot_suffix`；
- 选 Android 时 shim 恢复原厂内核头并直接跳转，不经过 secondary bootloader；
- 选 Linux 时不碰 `misc`/BCB，不需要 root、WoaManager 或电脑。

代价：Android OTA 会覆盖 `boot_a`，菜单随之消失，需要重新刷一次。

## 目录

```
packer/            boot 镜像解析、载荷提取、打包、manifest 校验
  bootimg.py       Android boot v0-v4 读写
  extract.py       从 stock / 社区 boot.img 提取 kernel、DTB、initrd
  make_boot.py     调 MultiBootKernelPatcher，产出 boot_a.img
  manifest.py      kShimC manifest 解析（校验用）
config/
  sheng.env        分区大小、默认项、cmdline
  sheng_defconfig  kShimC runtime 配置（3048 宽、按键、无触屏）
scripts/
  prepare.sh       提取 stock 内核 + Linux 载荷
  build.sh         生成 out/boot_a.img
  build_runtime.sh 构建 4 MiB kShimC runtime
  fetch_deps.sh    拉取 CrDK/picolibc/LVGL/libfdt（GitHub 镜像，需网络）
  fetch_toolchain.sh 在本机解包 aarch64 交叉工具链（Arch，无需 root）
  backup.sh        通过 adb 备份 boot/dtbo/init_boot/vendor_boot
  flash.sh         刷 boot_a
vendor/
  kshim/           五代 kShimC 快照（GPL-3.0）
  packer/          MultiBootKernelPatcher（GPL-3.0）
tests/             纯 Python 回归（合成镜像，不需要设备）
docs/              架构、设备资料、上机计划、排障
```

## 快速开始

```sh
make packer                       # 编译 MultiBootKernelPatcher

# 1. 准备 payload
scripts/prepare.sh \
  --stock-boot  stock_boot_a.img \
  --distro-boot debian-sheng-boot.img \
  --out out/payload

# 2. 构建 runtime（Arch/CachyOS 可自动装工具链，其他发行版装 aarch64-linux-gnu-gcc）
scripts/fetch_toolchain.sh
scripts/fetch_deps.sh
scripts/build_runtime.sh

# 3. 打包
scripts/build.sh --payload out/payload --runtime out/runtime.bin --out out/boot_a.img

# 4. 备份 + 刷入
scripts/backup.sh
scripts/flash.sh -y
```

没有 `aarch64-linux-gnu-gcc` 时，runtime 也可以在任意装了该交叉编译器的
容器/发行版里构建，只要把产物放到 `out/runtime.bin`。打包阶段只需要
`out/runtime.bin`，不需要重新编译 runtime。本次构建产物：
4,194,304 B，sha256 `b97cbe7c90842dd33e71f6bc3f9b27d37ab9c0c714d9ced2374ae86ef2f8c3b8`。

## 迁移步骤（从现有双系统）

1. 备份 `boot_a`、`boot_b`、`dtbo_a`、`dtbo_b`、`init_boot_a`、`vendor_boot_a`。
2. 确认 Android 在 A 槽并能正常启动：`fastboot set_active a && fastboot reboot`。
3. 保留 `linux` 分区上的 rootfs（重刷官方包会重建 `userdata`，需要重新分区、
   重新写 rootfs）。
4. 刷 `boot_a`，重启，菜单出现。默认 Android 与刷入前行为一致。
5. 需要回滚：`fastboot flash boot_a out/backup/boot_a.img`。

## 已知风险

- **未上机验证**：`splash.c` 只识别 `qcom,sde-kms` 和 `mdp_phys`，SM8550 走
  legacy 路径，DPU 拓扑（mixer 数、双 LM）与 Y700 不同，菜单可能黑屏。
- 触屏不可用（与两代 Y700 一致），只能音量/电源键。
- Android OTA 覆盖 `boot_a` 后菜单消失。
- 解锁 bootloader 是前提；sheng 的 EDL 恢复需要小米 service account。
- 本项目不修改 `super`、`userdata`、`linux` 分区，只写 `boot_a`。

## 测试

```sh
make test
```

测试用合成 boot 镜像覆盖：v4 stock 解析、v0 `Image.gz`+DTB 提取、manifest
布局、DTB `bootargs`、initrd 条目、超限拒绝、gzip 内核拒绝。

## 许可

GPL-3.0-or-later。`vendor/` 下为上游原样副本，见各自 LICENSE。
