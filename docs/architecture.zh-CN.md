# 架构

[English](architecture.md) | **简体中文**

```text
ABL
 └─ boot_a: kShimC shim（MBKP 打包，保留原厂 boot 头）
     ├─ Android 条目：恢复原厂内核头，直接跳转
     │   ABL 继续提供 dtbo_a / init_boot_a / vendor_boot_a
     └─ Linux 条目：跳转 manifest 内的 Image + DTB（+ 可选 initrd）
         cmdline 写入 /chosen/bootargs，root=PARTLABEL=linux
```

## 镜像布局

`make-boot.sh` 把原厂内核和 Linux 载荷放进同一个 `boot_a`：

- 原厂 `boot_a` 的 kernel 作为 **BaseImage**，ABL 的 DT/ramdisk 继续有效；
- Linux `Image`、DTB、可选 initrd 作为普通 manifest 条目；
- shim 自身 4 MiB，manifest 在镜像尾部；
- 总大小必须小于 `boot_a` 分区（sheng 为 192 MiB）。

manifest 由 MultiBootKernelPatcher 生成；`packer/manifest.py` 可在不启动设备
的情况下回读校验条目、DTB bootargs 和 initrd 索引。

## 为什么放在 boot_a

sheng 是 A/B 设备，ABL 按当前槽位加载 `boot_x`、`init_boot_x`、
`vendor_boot_x`、`dtbo_x`。shim 固定在 `boot_a`、设备停在 A 槽：

- Android 的 ramdisk/DTBO 与 `system_a` 对齐，不需要伪造 `slot_suffix`；
- 选 Android 时不需要二次 bootloader、不写 `misc`/BCB；
- 选 Linux 时不需要电脑、root 或 WoaManager。

代价是 Android OTA 会覆盖 `boot_a`，升级后需要重新刷菜单镜像。

## 显示

sheng 的 SDE 是 10 SSPP legacy 布局，CrDK 的 `CrSplashRead()` 原本只接受
≤8 路，解码失败。当前使用 `CONFIG_KSHIM_SPLASH_FALLBACK`：直接复用 ABL 留下
的 `cont_splash_region`（0xB8000000，stride 12192，XRGB8888）作为扫描缓冲。
SDE 路由解码本身尚未修，属于已知限制。

## 输入与高级启动

- 音量键映射 PREV/NEXT，电源键映射 ENTER；触屏未适配。
- Recovery/Fastboot/EDL 走 `qcom_reboot`：sheng 的 SPMI 仲裁器是 **v7**，
  已放宽到 CrDK 支持的 5/7/8/8.5；EDL 未实测。

## 设置与上次启动

选中条目成功交接后，把条目索引、语言、开机延迟写入 PMIC SDAM：

- 主记录 `0x8400`，备份 `0x8500`，偏移 `0x50/0x80`；
- `KSET` v2 magic + CRC32；
- 记录失效时回退 manifest 默认，不阻止手动选择。

## 关键限制

- 只写 `boot_a`；不碰 `super`、`userdata`、`linux`。
- splash 保留区或 ABL 行为变化可能导致黑屏。
- `fastboot boot` 与正常开机的 bootconfig/vendor_boot 路径不完全等价。
