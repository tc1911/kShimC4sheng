# Android + Debian 双系统

[English](sheng-debian-dual-boot.md) | **简体中文**

设备：Xiaomi Pad 6S Pro（sheng，SM8550，256 GB）。
引导：kShimC 在 `boot_a`、A 槽，不使用 `set_active`，不写 `misc`。

## 分区

GPT 使用 4096 字节逻辑扇区：

```text
29 userdata   3,094,592 – 30,452,735   104.4 GiB   A03A
30 linux     30,452,736 – 64,007,162   128.0 GiB   8300
```

TWRP 下创建：

```bash
sgdisk --delete=29 \
  --new=29:3094592:30452735 --typecode=29:A03A --change-name=29:userdata \
  --new=30:30452736:64007162 --typecode=30:8300 --change-name=30:linux \
  /dev/block/sda
sgdisk --verify /dev/block/sda
```

userdata 是 f2fs，不能缩小；改分区等于删除重建。先备份 `/sdcard` 和应用数据。

## rootfs

来源：`tc1911/debianlinux4sheng` 的 Actions artifact
`rootfs-forky-KDE Plasma-linux`（10 GiB ext4）。

```bash
fastboot flash linux build/debian/rootfs.img
```

首次启动由 `x-systemd.growfs` 扩到分区大小；没有自动扩时：

```bash
sudo resize2fs /dev/sda30
```

## boot 镜像

Debian 的 `boot.img` 是 v0：`kernel_offset=4096`、Image.gz、无 ramdisk，
gzip 末尾追加 sheng DTB。`make-boot.sh` 会自动解压并拆出 DTB：

```bash
scripts/make-boot.sh \
  --stock-boot /home/tc191/6spBackup/rom/stock/boot_a-stock.img \
  --distro-boot build/debian/boot.img \
  --out out/boot_a_debian.img \
  --default android \
  --bootargs 'root=PARTLABEL=linux rw rootwait udmabuf.size_limit_mb=256'
```

`udmabuf.size_limit_mb=256` 必须保留：它在原 boot.img 的 cmdline 里，DTB 里
没有。刷入：

```bash
fastboot flash boot_a out/boot_a_debian.img
```

## 启动行为

- 默认 Android，8 秒倒计时；选 Linux 后走内嵌 Debian Image + DTB。
- 根分区靠 `root=PARTLABEL=linux`，没有 initrd。
- 上次启动会预选，但菜单仍显示、倒计时仍生效。
- 换内核/发行版：重新提取 Image/DTB，跑一次 `make-boot.sh` 再刷 `boot_a`。

## 备份与回滚

```text
/home/tc191/6spBackup/rom/
├── partitions/   125 个分区 + 主/备 GPT（旧 232.4 GiB userdata 布局）
├── stock/        原厂 boot_a/boot_b/dtbo/init_boot/vendor_boot
└── app/          DataBackup 应用数据
```

- 原厂 boot：`fastboot flash boot_a stock/boot_a-stock.img`
- 旧 GPT 只适用于旧布局；当前 128 GiB 布局需要重新备份 GPT。
- Android OTA 会覆盖 `boot_a`，升级后重刷菜单镜像。
