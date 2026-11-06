# 预编译 runtime

[English](README.md) | **简体中文**

`sheng-runtime.bin` 是小米平板 6S Pro（sheng）的 4 MiB kShimC runtime，
由 `config/sheng_defconfig` 构建。

- 大小：4,194,304 B
- sha256：`9be19f7386670428f74e3dbb7f717c4a65eff03eaf7761be5b4b1fb61030251d`
- 来源：`vendor/kshim` + 本仓库补丁（见 `vendor/README.md`）
- 工具链：aarch64-linux-gnu-gcc 16.2.0

只有修改菜单或其配置时才需要重新构建。合并不同的 Android boot 镜像或
Linux 内核**不需要**交叉编译器——`scripts/make-boot.sh` 会直接复用该文件。
