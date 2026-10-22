# sheng 设备资料

## 已确认

| 项 | 值 | 来源 |
|---|---|---|
| SoC | SM8550 (kalama) | pmOS wiki / 内核仓库 |
| 屏幕 | 12.4" 3048×2032，双 DSI + DSC | pmOS wiki |
| 触控 | Novatek NT36532E（用户态 `thp`） | pmOS wiki |
| boot 分区 | 201326592 B = 192 MiB | `map220v/android_device_xiaomi_sheng` `BoardConfig.mk:76` |
| 分区布局 | A/B：boot / init_boot / vendor_boot / dtbo | `BoardConfig.mk:103-107` |
| 现有 Linux 启动 | mkbootimg v0，`Image.gz`+DTB，无 initrd | `sheng-kernel_build.sh:60` |
| Linux rootfs | 分区 label `linux`（sda30） | 双系统安装指南 |
| 原厂 Android | HyperOS / Android 14–16，GKI | 设备信息 |

## 待上机确认

这些值会直接影响 runtime 和 DTB 处理，第一次上机前逐项记录：

1. **boot 头版本**
   ```sh
   adb shell su -c 'dd if=/dev/block/by-name/boot_a of=/sdcard/boot_a.img'
   adb pull /sdcard/boot_a.img
   python3 packer/bootimg.py   # 或 unpack_bootimg --boot_img boot_a.img
   ```
   预期 v4 / header_size 1584 / page 4096。若是 v2，需要改 `make_boot.py`
   的版本分支。
2. **原厂内核是否 raw Image**：`python3 packer/extract.py --stock ...` 能通过
   `ARM\x64` 校验即可。
3. **DTBO 里的显示拓扑**
   ```sh
   adb shell su -c 'dd if=/dev/block/by-name/dtbo_a of=/sdcard/dtbo_a.img'
   dtc -I dtb -O dts dtbo_a.img | grep -A5 -B5 'sde\|mdp\|dsi'
   ```
   关注 `qcom,sde-kms` 的 `reg` 是否含 `mdp_phys`、mixer/LM 数量。
4. **面板 compatible**：DT 的 `panel@0/compatible`，用于
   `src/dt/device_info.c` 白名单（当前只认 Y700 两代）。
5. **按键**：`getevent -l` 确认音量/电源；DT 里是否有
   `qcom,pmk8550-pwrkey`、`qcom,sm8550-tlmm`。
6. **ABL 允许的最大内核**：先用默认 Android 载荷刷一次，确认不死机；再逐步
   加 Linux 载荷。192 MiB 分区不等于 ABL 一定接受 96 MiB 内核。
7. **槽位**：`adb shell getprop ro.boot.slot_suffix` 必须是 `_a`。

## 已知载荷实测

`code002-2/Xiaomi-pad-6s-pro-Linux` 的 Armada 单系统 `boot_b.img`：

- v0 头、page 4096、kernel 15,700,879 B（gzip）、ramdisk 45,493,538 B
- 解压后 Image 47,073,792 B，尾部 DTB 140,058 B
- cmdline `root=PARTLABEL=userdata ...`（单系统；双系统改为 `linux`）

这些数字用于容量核算，也用于 `tests/` 之外的本地冒烟测试。
