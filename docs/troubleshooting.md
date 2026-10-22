# 排障

## 打包

**`packer not built`**
```sh
make packer
```

**`DTB /chosen bootargs mismatch` / `FDT_ERR_NOSPACE`**
`sheng.dtb` 是 dtc 重新编译过的（`-p 8192`），正常情况下有足够空间。若仍报错，
说明 `dtc` 版本过旧，升级 `device-tree-compiler`。

**`runtime must be exactly 4 MiB`**
`scripts/build_runtime.sh` 的产物 `kShimC.bin` 必须是 4 MiB；不要用
`objcopy` 裁掉尾部。

**`stock kernel header slots 0x20/0x28/0x30 are not zero`**
传入的不是原厂内核（可能已经是被 patch 过的镜像）。从原厂 `boot_a` 重新
`extract.py --stock`。

**`exceeds partition budget`**
192 MiB 减去 64 KiB 是硬上限。优先去掉 initrd（改用内置驱动的 rootfs），
或压缩 Linux 内核（本项目要求 raw Image，不能直接放 gzip）。

## 刷机

**菜单不出现，直接进 Android**
- 当前槽位不是 A：`fastboot getvar current-slot`，`fastboot set_active a`
- `boot_a` 没写进去：重新 `fastboot flash boot_a out/boot_a.img`
- 设备仍从 `boot_b` 启动（B 槽有旧 Linux boot）

**菜单黑屏 / 花屏**
sheng 走 `splash.c` 的 legacy SDE 路径，DPU 拓扑可能不匹配。先用
`CONFIG_KSHIM_DEBUG_CONSOLE=y` 重新构建 runtime，接 UART 看输出；
或在 `patches/` 里给 `splash.c` 加 SM8550 的 `mdp_phys` 解析。

**选 Android 后 boot loop**
- 确认 `init_boot_a`、`vendor_boot_a`、`dtbo_a` 没有被擦
- 确认 `ro.boot.slot_suffix=_a`
- 刷回备份 `boot_a` 验证 Android 本身没坏

**选 Linux 后 panic: unable to mount root**
- `root=PARTLABEL=linux` 与分区 label 不符：`blkid` 检查
- 内核缺 ext4/ufs 驱动：换发行版内核或加 initrd
- DTB 不是这台机器的：`fdtget -t s sheng.dtb / compatible` 应含
  `qcom,sm8550`

**initrd 没生效**
`fdtget -t u sheng.dtb /chosen kshim,initrd-entry` 应为 initrd 条目号（通常 3），
且 manifest 里该条目 `Type=Blob`、`size` 正确。

## 救砖

1. 音量键组合进 fastboot，刷回 `out/backup/boot_a.img`。
2. 仍不行：EDL 9008（sheng 需要小米 service account，社区工具不一定可用）。
3. 不要刷 `abl`：`armada-sheng` 里的 `abl_signed-SM8550.elf` 是给 Odin/AYANEO
   掌机的，sheng 刷了会变砖。
