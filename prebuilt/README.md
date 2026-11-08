# Prebuilt runtime

**English** | [简体中文](README.zh-CN.md)

`sheng-runtime.bin` is the 4 MiB kShimC runtime for the Xiaomi Pad 6S Pro
(sheng), built from `config/sheng_defconfig`.

- size: 4,194,304 B
- sha256: `6838cc5209e5e0df8c1ec8e689e55cc936773e06849b780e967ca21099ac850e`
- source: `vendor/kshim` plus local patches (see `vendor/README.md`)
- toolchain: aarch64-linux-gnu-gcc 16.2.0

You only need to rebuild it when the menu or its configuration changes.
Merging a different Android boot image or Linux kernel does **not** require a
cross compiler — `scripts/make-boot.sh` reuses this file.
