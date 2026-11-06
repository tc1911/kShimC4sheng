# Prebuilt runtime

**English** | [简体中文](README.zh-CN.md)

`sheng-runtime.bin` is the 4 MiB kShimC runtime for the Xiaomi Pad 6S Pro
(sheng), built from `config/sheng_defconfig`.

- size: 4,194,304 B
- sha256: `9be19f7386670428f74e3dbb7f717c4a65eff03eaf7761be5b4b1fb61030251d`
- source: `vendor/kshim` plus local patches (see `vendor/README.md`)
- toolchain: aarch64-linux-gnu-gcc 16.2.0

You only need to rebuild it when the menu or its configuration changes.
Merging a different Android boot image or Linux kernel does **not** require a
cross compiler — `scripts/make-boot.sh` reuses this file.
