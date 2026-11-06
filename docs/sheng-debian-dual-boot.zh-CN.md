# Android + Debian 双系统

[English](sheng-debian-dual-boot.md) | **简体中文**

启动方式：kShimC 常驻 `boot_a`、A 槽；不写 `misc`、不调用 `set_active`。
Android 保留原分区，Linux 放在单独的、标签为 `linux` 的分区。

## 分区

菜单只要求存在 `PARTLABEL=linux` 的分区。用你习惯的工具（TWRP、`sgdisk`
或 `parted`）创建，大小按自己的 rootfs 决定。不要照抄另一台设备的偏移和
容量。

`userdata` 通常是 f2fs，无法原地缩小；重新分区意味着删除并重建。先备份
`/sdcard` 和应用数据。GPT 与分区备份只保存在本地，不要提交到仓库。

## Rootfs

把发行版构建出的 rootfs 镜像刷到 `linux` 分区：

```bash
fastboot flash linux /path/to/rootfs.img
```

如果镜像带 `x-systemd.growfs`，首次启动会自动扩展；否则手动扩容：

```bash
sudo resize2fs /dev/disk/by-partlabel/linux
```

## Boot 镜像

发行版 `boot.img` 通常是 v0：`kernel_offset=4096`、Image.gz、无 ramdisk，
sheng DTB 追加在 gzip 流之后。`make-boot.sh` 会自动解压并拆出 DTB：

```bash
scripts/make-boot.sh \
  --stock-boot /path/to/stock-boot.img \
  --distro-boot /path/to/boot.img \
  --out out/boot_a_debian.img \
  --default android \
  --bootargs 'root=PARTLABEL=linux rw rootwait udmabuf.size_limit_mb=256'
```

`udmabuf.size_limit_mb=256` 必须保留：它在原 boot.img 的 cmdline 里，不在
DTB 中。刷入：

```bash
fastboot flash boot_a out/boot_a_debian.img
```

## 启动行为

- 默认 Android，8 秒倒计时；选择 Linux 会启动内嵌的 Linux Image + DTB。
- 根分区通过 `root=PARTLABEL=linux` 查找，没有 initrd。
- 上次启动会预选条目，但菜单仍会出现，倒计时仍生效。
- 换内核或发行版：提取新的 Image/DTB，重跑 `make-boot.sh` 并重刷 `boot_a`。

## 备份与回滚

原厂 `boot_a` 与 GPT 备份只保存在本地：

```bash
fastboot flash boot_a /path/to/stock-boot.img
```

Android OTA 会覆盖 `boot_a`，之后需要重刷菜单镜像。
