# Android + Debian dual boot

**English** | [简体中文](sheng-debian-dual-boot.zh-CN.md)

Device: Xiaomi Pad 6S Pro (sheng, SM8550, 256 GB).
Boot: kShimC in `boot_a`, slot A; no `set_active`, no `misc` writes.

## Partitions

The GPT uses 4096-byte logical sectors:

```text
29 userdata   3,094,592 – 30,452,735   104.4 GiB   A03A
30 linux     30,452,736 – 64,007,162   128.0 GiB   8300
```

Create them in TWRP:

```bash
sgdisk --delete=29 \
  --new=29:3094592:30452735 --typecode=29:A03A --change-name=29:userdata \
  --new=30:30452736:64007162 --typecode=30:8300 --change-name=30:linux \
  /dev/block/sda
sgdisk --verify /dev/block/sda
```

`userdata` is f2fs and cannot be shrunk in place; repartitioning means deleting
and recreating it. Back up `/sdcard` and app data first.

## Rootfs

Source: the `tc1911/debianlinux4sheng` Actions artifact
`rootfs-forky-KDE Plasma-linux` (10 GiB ext4).

```bash
fastboot flash linux build/debian/rootfs.img
```

On first boot `x-systemd.growfs` expands it to the partition size. If it does
not run automatically:

```bash
sudo resize2fs /dev/sda30
```

## Boot image

Debian's `boot.img` is v0: `kernel_offset=4096`, Image.gz, no ramdisk, with the
sheng DTB appended after the gzip stream. `make-boot.sh` decompresses it and
splits out the DTB automatically:

```bash
scripts/make-boot.sh \
  --stock-boot /home/tc191/6spBackup/rom/stock/boot_a-stock.img \
  --distro-boot build/debian/boot.img \
  --out out/boot_a_debian.img \
  --default android \
  --bootargs 'root=PARTLABEL=linux rw rootwait udmabuf.size_limit_mb=256'
```

`udmabuf.size_limit_mb=256` must be preserved: it lives in the original
boot.img cmdline, not in the DTB. Flash it:

```bash
fastboot flash boot_a out/boot_a_debian.img
```

## Boot behavior

- Android is the default with an 8-second countdown; choosing Linux boots the
  embedded Debian Image + DTB.
- The root partition is found via `root=PARTLABEL=linux`; there is no initrd.
- The last boot preselects an entry, but the menu still appears and the
  countdown still applies.
- To change kernel or distro: extract the new Image/DTB, run `make-boot.sh`
  once and flash `boot_a` again.

## Backup and rollback

```text
/home/tc191/6spBackup/rom/
├── partitions/   125 partitions + primary/backup GPT (old 232.4 GiB userdata layout)
├── stock/        stock boot_a/boot_b/dtbo/init_boot/vendor_boot
└── app/          DataBackup app data
```

- Stock boot: `fastboot flash boot_a stock/boot_a-stock.img`
- The old GPT only matches the old layout; the current 128 GiB layout needs a
  fresh GPT backup.
- An Android OTA overwrites `boot_a`; reflash the menu image afterwards.
