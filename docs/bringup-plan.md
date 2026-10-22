# 上机计划

## 阶段 0：准备

- [ ] bootloader 已解锁（sheng 需要小米社区申请；EDL 需 service account）
- [ ] 备份 `boot_a/boot_b/dtbo_a/dtbo_b/init_boot_a/vendor_boot_a`
- [ ] 确认 Android 在 A 槽可启动
- [ ] 保留 `linux` 分区与 rootfs，记录 `/etc/fstab`、cmdline

## 阶段 1：工具链

打包链路（已完成）：

```sh
make packer
make test          # 8 个合成镜像回归
```

runtime 依赖（需要网络）：

```sh
scripts/fetch_deps.sh
python3 -m venv build/venv
build/venv/bin/pip install kconfiglib
export PATH="$PWD/build/venv/bin:$PATH"
```

还需要 `aarch64-linux-gnu-gcc`、`cmake`、`ninja`，然后：

```sh
scripts/build_runtime.sh
```

产物必须是 **恰好 4 MiB**，否则 `make_boot.py` 直接拒绝。

## 阶段 2：payload

```sh
scripts/prepare.sh --stock-boot stock_boot_a.img \
                   --distro-boot debian-boot.img --out out/payload
```

检查：

- `stock-boot.img` 的原厂内核由 `ARM\x64` 校验通过
- `linux-Image` 是 raw Image（不是 `Image.gz`）
- `linux.dtb` 能被 `fdtget` 读出 `/compatible`
- 如使用 Armada，`linux-initrd.img` 存在

## 阶段 3：首次刷入（只验证 Android）

先用 `--default android`、带上完整 Linux 载荷打包（体积与最终一致），
刷 `boot_a`，观察：

1. 菜单是否显示（若黑屏，见 troubleshooting 的 splash 一节）
2. 8 秒后是否自动进 Android
3. Android 的 Wi-Fi、触屏、指纹、声音是否正常（shim 不应改变这些）

**通过标准**：连续 5 次冷启动都能进 Android，`ro.boot.slot_suffix=_a`。

## 阶段 4：菜单交互

- 音量上/下移动，电源键确认
- 超时倒计时、默认项高亮
- 长按电源菜单（重启/关机）
- 语言切换（中/英）

## 阶段 5：Linux（无 initrd，Debian）

选 Linux，确认：

- 内核日志出现 `Run /init as init process` 或发行版启动日志
- 根分区挂载为 `PARTLABEL=linux`
- 能进桌面/串口；重启后回到菜单

失败时记录：最后 30 行内核日志、`/chosen/bootargs`、DTB 的
`kshim,initrd-entry`（应为空）。

## 阶段 6：Linux（带 initrd，Armada）

重新打包（加上 `linux-initrd.img`），确认：

- manifest 里有 `initrd` 条目且 DTB 的 `kshim,initrd-entry=3`
- 内核能解压 initramfs 并切根

## 阶段 7：回归

| 场景 | 期望 |
|---|---|
| 冷启动 × 10 | 菜单稳定，无花屏/卡死 |
| 选 Android × 10 | 每次都能进系统 |
| 选 Linux × 10 | 每次都能进系统 |
| 超时不操作 | 进默认项 |
| 刷回备份 boot_a | Android 恢复原状 |

## 阶段 8：文档

把实测的 boot 头版本、DPU 拓扑、面板名、按键节点补进
`docs/device-profile.md`，并把 `splash.c`/`device_info.c` 需要的补丁写进
`patches/`。

## 回滚

```sh
fastboot flash boot_a out/backup/boot_a.img
fastboot set_active a
```

如果 bootloader 进不去：音量键组合进 fastboot；仍不行则 EDL（需账号）。
