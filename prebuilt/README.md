# Prebuilt runtime

`sheng-runtime.bin` is the 4 MiB kShimC runtime for Xiaomi Pad 6S Pro (sheng),
built from `config/sheng_defconfig`.

- size: 4,194,304 B
- sha256: `d8e1becbb584d96d1fdf231e33bbf70dd779905c887ce2ea226054877bc7bd73`
- source: `vendor/kshim` at commit `65c07c2` + local patches (`vendor/README.md`)
- toolchain: aarch64-linux-gnu-gcc 16.2.0

You only need to rebuild it when the menu or its configuration changes.
Merging a different Android boot image or Linux kernel does **not** require a
cross compiler — `scripts/make-boot.sh` reuses this file.
