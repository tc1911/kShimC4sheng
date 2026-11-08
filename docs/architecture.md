# Architecture

**English** | [简体中文](architecture.zh-CN.md)

```text
ABL
 └─ boot_a: kShimC shim (packed by MBKP, stock boot header preserved)
     ├─ Android entry: restores the stock kernel header, jumps directly
     │   ABL keeps providing dtbo_a / init_boot_a / vendor_boot_a
     └─ Linux entry: jumps to Image + DTB (+ optional initrd) from the manifest
         cmdline is written to /chosen/bootargs, root=PARTLABEL=linux
```

## Image layout

`make-boot.sh` places the stock kernel and the Linux payload into a single
`boot_a` image:

- the stock `boot_a` kernel becomes the **BaseImage**, so ABL's DT/ramdisk
  remain valid;
- the Linux `Image`, DTB and optional initrd are ordinary manifest entries;
- the shim itself is 4 MiB; the manifest sits at the end of the image;
- the total must fit in the `boot_a` partition (192 MiB on sheng).

The manifest is produced by MultiBootKernelPatcher; `packer/manifest.py` can
read it back and verify entries, DTB bootargs and the initrd index without
booting the device.

## Why boot_a

sheng is an A/B device: ABL loads `boot_x`, `init_boot_x`, `vendor_boot_x` and
`dtbo_x` for the current slot. The shim is pinned to `boot_a` and the device
stays on slot A:

- Android's ramdisk/DTBO match `system_a`; no fake `slot_suffix` is needed;
- selecting Android needs no second bootloader and writes no `misc`/BCB;
- selecting Linux needs no PC, root or WoaManager.

The trade-off is that an Android OTA overwrites `boot_a`; reflash the menu image
after an update.

## Display

sheng's SDE uses a 10-SSPP legacy layout, while CrDK's `CrSplashRead()`
originally accepted at most 8 pipes, so decoding failed. The current build uses
`CONFIG_KSHIM_SPLASH_FALLBACK`: it reuses the `cont_splash_region` left by ABL
(0xB8000000, stride 12192, XRGB8888) as the scanout buffer. SDE routing itself
is still undecoded; this is a known limitation.

## Input and advanced boot

- Volume keys map to PREV/NEXT, power maps to ENTER; touch is not supported.
- Recovery/Fastboot/EDL use `qcom_reboot`: sheng's SPMI arbiter is **v7**, and
  the code now accepts the CrDK-supported 5/7/8/8.5. EDL has not been tested.

## Settings and last boot

After an entry hands off successfully, its index, the menu language and the
boot delay are persisted in PMIC SDAM:

- the last-entry index uses a 4-byte quick slot at offset `0x70` on the SDAM
  base that was found during load;
- language and boot delay use the `KSET` v2 record at `0x50` (CRC32), with the
  secondary base at `0x8500` as fallback;
- if the `KSET` record is invalid, the quick slot is still read on its own; the
  last-boot index survives and language/delay fall back to defaults;
- if neither is valid, fall back to the manifest default without blocking
  manual selection.

The first implementation rewrote a 16-byte record to every candidate slot,
read it back for verification, and re-ran `SpmiLibInit()` on every 4-byte
access. On sheng this delayed the handoff by 30-40 seconds. The current path
reads the record once, caches it, reuses the SPMI controller, writes only the
changed entry and skips the write entirely when the selection did not change.

Measured from menu confirmation to the OS on sheng: about 13 s for Android and
11 s for Linux. Switching to a different entry adds roughly 6 s for the SDAM
write; booting the same entry again does not.

## Key limits

- Only `boot_a` is written; `super`, `userdata` and `linux` are untouched.
- Changes to the splash reservation or ABL behavior can cause a black screen.
- `fastboot boot` is not fully equivalent to a normal boot regarding
  bootconfig/vendor_boot.
