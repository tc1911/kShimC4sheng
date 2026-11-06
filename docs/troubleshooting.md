# Troubleshooting

**English** | [简体中文](troubleshooting.zh-CN.md)

## Black screen with backlight

1. Confirm the image has `CONFIG_KSHIM_SPLASH_FALLBACK=y`.
2. Check that the ABL DT still has `/reserved-memory/cont_splash_region`
   (currently 0xB8000000, size 0x2B00000 on sheng).
3. Verify with `fastboot boot out/boot_a.img` first; on failure a reboot
   returns to the original boot chain.

## Menu does not appear, Android boots directly

- Confirm the current slot is A: `fastboot getvar current-slot`.
- Confirm you flashed `boot_a`, not `boot_b`.
- If an OTA just ran, `boot_a` was overwritten by stock; flash the menu again.
- `fastboot boot` uses a different bootconfig path from a normal boot; trust
  the flashed behavior.

## Android will not boot

- Switch back to slot A and flash the stock boot:

  ```bash
  fastboot set_active a
  fastboot flash boot_a /home/tc191/6spBackup/rom/stock/boot_a-stock.img
  ```

- Check whether `dtbo_a` / `init_boot_a` / `vendor_boot_a` were erased.

## Linux hangs or drops to an emergency shell

- Confirm the `linux` partition exists and has `PARTLABEL=linux`.
- Confirm the DTB matches the kernel and `/chosen/bootargs` contains
  `root=PARTLABEL=linux rw rootwait`.
- With an initrd, confirm `kshim,initrd-entry` points to the right entry.
- Debian needs to grow the filesystem on first boot; if it hangs, check the
  serial console or `journalctl`.

## Recovery / Fastboot switching fails

- sheng's SPMI arbiter is v7; older builds only accepted v8/8.5.
- Confirm the image includes `qcom_reboot` support for v7; errors look like
  `-102,s5`.

## fastboot cannot find the device

- Try another cable/USB port and confirm the device is in bootloader
  (volume down + power).
- Do not flash while `fastboot devices` is empty.

## EDL / 9008

- sheng is Kalama/SM8550 and needs the matching firehose. A Kailua (SM8635)
  loader passes Sahara but drops USB during UFS init.
- Prefer partition backups plus the official fastboot package; do not rely on
  EDL write-back.

## Proxy / downloads

- If GitHub is unreachable, `gh-proxy.com` can proxy the API and artifacts.
  Artifacts of private repositories cannot go through public proxies.
