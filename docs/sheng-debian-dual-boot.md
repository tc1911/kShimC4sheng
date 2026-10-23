# sheng 双系统：Android + Debian（2026-10-23 实机记录）

设备：Xiaomi Pad 6S Pro 12.4（sheng，SM8550，256 GB）
引导：kShimC 在 `boot_a`，A 槽；不使用 `set_active`、不写 `misc`、不需要电脑。

## 最终分区

GPT 使用 4096 字节逻辑扇区（`sgdisk` / `parted` 都按这个单位）：

```text
磁盘            64,007,168 sectors
29 userdata     3,094,592 – 30,452,735   104.4 GiB   A03A  userdata
30 linux       30,452,736 – 64,007,162   128.0 GiB   8300  linux
```

创建命令（TWRP，root）：

```bash
sgdisk --delete=29 \
  --new=29:3094592:30452735 --typecode=29:A03A --change-name=29:userdata \
  --new=30:30452736:64007162 --typecode=30:8300 --change-name=30:linux \
  /dev/block/sda
sgdisk --verify /dev/block/sda
```

原来的 parted 写法（十进制 GB，userdata 128 GB、Linux 取剩余）也来自上游
`code002-2/Xiaomi-pad-6s-pro-Linux` 的 `docs/安装指南.md`：

```text
rm 29
mkpart userdata ext4 12.7GB 140.7GB
mkpart linux ext4 140.7GB -0MB
```

注意 f2fs 的 userdata 不能缩小，改分区等于删除重建；先备份 `/sdcard` 和应用数据。

## rootfs

来源：`tc1911/debianlinux4sheng`（公开 fork）的 Actions artifact
`rootfs-forky-KDE Plasma-linux`（id `12468629431`）。

```text
build/debian/rootfs.img   10 GiB ext4
UUID                      ee8d3593-59b1-480e-a3b6-4fefb17ee7d8
kernel modules            7.2.6-sm8550-00103-g42f3b40c702a
fstab                     PARTLABEL=linux / ext4 defaults,x-systemd.growfs 0 1
```

写入：

```bash
fastboot flash linux build/debian/rootfs.img
```

首次启动由 `x-systemd.growfs` 扩到 128 GiB；没有自动扩就手动
`sudo resize2fs /dev/sda30`。

## boot 镜像

Debian 的 `boot.img` 是 v0：kernel_offset 4096、Image.gz、无 ramdisk，
gzip 末尾追加 sheng DTB。`make-boot.sh` 会自动解压、拆 DTB：

```bash
scripts/make-boot.sh \
  --stock-boot out/backup/boot_a.img \
  --distro-boot build/debian/boot.img \
  --out out/boot_a_debian.img \
  --default android \
  --bootargs 'root=PARTLABEL=linux rw rootwait udmabuf.size_limit_mb=256'
```

产物：89,997,312 B，sha256
`7a20e585ecaccd8f03c1adb9f75ade44c3c030c57470ac37961cbba53287b449`。

`udmabuf.size_limit_mb=256` 必须保留（原 boot.img 的 cmdline 里有，DTB 里没有）。

刷入：

```bash
fastboot flash boot_a out/boot_a_debian.img
```

菜单默认 Android、8 秒超时；选 Linux 后走内置 Debian Image + DTB，
根分区靠 `root=PARTLABEL=linux`，没有 initrd。

## 备份 / 回滚

改动前的完整备份：

```text
/home/tc191/6spBackup/rom/
├── partitions/   125 个分区 + 主/备 GPT（旧 232.4 GiB userdata 布局）
├── stock/        原厂 boot_a/boot_b/dtbo/init_boot/vendor_boot
└── app/          DataBackup 应用数据
```

- 原厂 boot_a：`fastboot flash boot_a stock/boot_a-stock.img`
- 旧 GPT 只能用于恢复**旧布局**；当前 128 GiB linux 布局需要重新备份 GPT
  （`sgdisk --backup=...`），不要直接 `sgdisk --load-backup` 旧文件。
- Android OTA 会覆盖 `boot_a`，需要重新刷 `out/boot_a_debian.img`。
- 换内核/发行版：重新提取 Image/DTB，跑一次 `make-boot.sh` 再刷 boot_a。
