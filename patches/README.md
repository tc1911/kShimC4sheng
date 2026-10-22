# 待做的 kShimC 源码补丁

`vendor/kshim` 是五代快照，未做修改。sheng 上机后预计需要下面两个补丁，
补丁前请先把实测值写进 `docs/device-profile.md`。

## 1. `src/dt/splash.c`：SM8550 显示拓扑

现状：

- `src/dt/splash.c:241` 只匹配 `qcom,sde-kms`；
- `:253` 只读 `mdp_phys`；
- `:264-280` 只区分 `0xc0000000`（Sun/SM8750）和 `0xd0000000`（Canoe/SM8850），
  其余走 legacy；
- `:292` pipe 上限 8，`:305` mixer 上限 6。

需要：

1. 从 `dtbo_a` 读出 SM8550 的 `sde` 节点和 `mdp_phys` 寄存器；
2. 确认 DPU version（`dmesg | grep -i dpu`）与 mixer/LM 数量；
3. 若超过 legacy 上限，扩展 `offsets()`（`:184-190`）的容量，或为 SM8550 增加
   与 Sun/Canoe 并列的分支。

## 2. `src/dt/device_info.c`：sheng 白名单

现状：`src/dt/device_info.c:203-235` 只认 `lenovo,elden` / `lenovo,baldur`
等 Y700 组合，`enum device_board` 只有 `UNKNOWN/TB322FC/TB323FU`。

需要：

1. 加入 `xiaomi,sheng` + `qcom,sm8550`（以及原厂 panel compatible）；
2. 增加 `SHENG` 枚举与对应分支（设备信息页、默认 DTB 选择）；
3. 若 `qcom,sm8550-tlmm` 的按键 pin 与 Y700 不同，同步 `src/dt/keys.c`。

## 3. 可选：触屏

sheng 的触控是 Novatek NT36532E + 用户态 `thp`，kShimC 目前只支持 ST FTM5。
在真机上摸清 I²C 地址/中断/复位脚之前不要打开 `CONFIG_KSHIM_TOUCH`。
