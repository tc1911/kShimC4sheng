# 排障

## 黑屏但有背光

1. 确认镜像里有 `CONFIG_KSHIM_SPLASH_FALLBACK=y`。
2. 检查 ABL DT 是否仍有 `/reserved-memory/cont_splash_region`
   （当前 sheng 为 0xB8000000，大小 0x2B00000）。
3. 用 `fastboot boot out/boot_a.img` 先验证；失败重启会自动回原启动链。

## 菜单不出现，直接进 Android

- 确认当前槽位是 A：`fastboot getvar current-slot`。
- 确认刷的是 `boot_a`，不是 `boot_b`。
- 若刚做完 OTA，`boot_a` 已被原厂覆盖，需要重刷。
- `fastboot boot` 与正常开机的 bootconfig 路径不同，以刷入后为准。

## Android 起不来

- 切回 A 槽并刷原厂 boot：

  ```bash
  fastboot set_active a
  fastboot flash boot_a /home/tc191/6spBackup/rom/stock/boot_a-stock.img
  ```

- 检查 `dtbo_a` / `init_boot_a` / `vendor_boot_a` 是否被擦除。

## Linux 卡住或进 emergency shell

- 确认 `linux` 分区存在且 `PARTLABEL=linux`。
- 确认 DTB 与内核版本匹配，`/chosen/bootargs` 含
  `root=PARTLABEL=linux rw rootwait`。
- 有 initrd 时确认 `kshim,initrd-entry` 指向正确条目。
- Debian 首次启动需要扩容，卡住时看串口或 `journalctl`。

## Recovery / Fastboot 切换失败

- sheng 的 SPMI 仲裁器是 v7；旧版只接受 v8/8.5。
- 确认镜像包含 `qcom_reboot` 对 v7 的支持；错误码形如 `-102,s5`。

## fastboot 找不到设备

- 换数据线/USB 口，确认设备在 bootloader（音量下 + 电源）。
- `fastboot devices` 为空时不要继续刷机。

## EDL / 9008

- sheng 是 Kalama/SM8550，必须使用对应 firehose；Kailua（SM8635）loader
  能过 Sahara，但 UFS 初始化时会掉 USB。
- 优先使用分区备份 + 官方 fastboot 包恢复，不依赖 EDL 写回。

## 代理 / 下载

- 直连 GitHub 失败时可用 `gh-proxy.com` 代理 API 与 artifact；
  私有仓库的 artifact 不能走公共代理。
