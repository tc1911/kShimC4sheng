# Android + Debian dual boot

**English** | [简体中文](sheng-debian-dual-boot.zh-CN.md)

Boot: kShimC in `boot_a`, slot A; no `set_active`, no `misc` writes.
Android stays on its original partitions; Linux lives on a separate partition
labelled `linux`.

## Partitioning

The menu only requires a partition with `PARTLABEL=linux`. Create it with your
preferred tool (TWRP, `sgdisk` or `parted`) and size it for your own rootfs.
Do not copy offsets or sizes from another device.

`userdata` is normally f2fs and cannot be shrunk in place; repartitioning means
deleting and recreating it. Back up `/sdcard` and app data first. Keep the GPT
and partition backups outside this repository.

## Rootfs

Flash the rootfs image produced by your distro build to the `linux` partition:

```bash
fastboot flash linux /path/to/rootfs.img
```

If the image uses `x-systemd.growfs`, it expands on first boot. Otherwise grow
it manually:

```bash
sudo resize2fs /dev/disk/by-partlabel/linux
```

## Boot image

Distro `boot.img` is usually v0: `kernel_offset=4096`, Image.gz, no ramdisk,
with the sheng DTB appended after the gzip stream. `make-boot.sh` decompresses
it and splits out the DTB automatically:

```bash
scripts/make-boot.sh \
  --stock-boot /path/to/stock-boot.img \
  --distro-boot /path/to/boot.img \
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
  embedded Linux Image + DTB.
- The root partition is found via `root=PARTLABEL=linux`; there is no initrd.
- The last boot preselects an entry, but the menu still appears and the
  countdown still applies.
- To change kernel or distro: extract the new Image/DTB, run `make-boot.sh`
  once and flash `boot_a` again.

## Backup and rollback

Keep your own stock `boot_a` and GPT backup locally:

```bash
fastboot flash boot_a /path/to/stock-boot.img
```

An Android OTA overwrites `boot_a`; reflash the menu image afterwards.
