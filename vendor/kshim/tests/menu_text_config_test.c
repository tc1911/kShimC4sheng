/* Keep menu_text first: firmware consumers must not need a prior LVGL include
 * or -DCONFIG_KSHIM_MENU_CHINESE to select the generated board locale. */
#include <menu_text.h>
#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef KSHIM_TEST_EXPECT_CHINESE
#error "Select the expected locale independently of the generated config"
#endif

int main(void)
{
#if KSHIM_TEST_EXPECT_CHINESE
    assert(strcmp(KSHIM_MENU_TEXT_TITLE("kShimC UEFI"), "KShimC 启动器") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_ACTION("Boot"), "启动") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_DETAIL,
                  "选择系统或操作。\n音量键选择，电源键确认") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_POWEROFF, "关机") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_HELP, "音量＋／－切换 · 电源键确认") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_BOOT, "启动") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_ENTER, "进入") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_RESTART, "重启") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_OFF, "关机") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_BACK, "返回") == 0);
    assert(strcmp(KSHIM_MENU_DESC_BASE, "启动设备原有系统。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_ANDROID, "启动设备原有的 Android 系统。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_GENERIC, "启动所选系统。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_INTERNAL,
                  "启动安装在机身存储中的 ArmadaOS。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_TF,
                  "启动安装在 TF 卡中的 ArmadaOS，请先插入对应 TF 卡。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_DIAGNOSTIC,
                  "启动 Linux 诊断环境，用于检查启动和硬件问题。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_ADVANCED, "查看可用的高级启动模式。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_RESTART, "重新启动设备。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_OFF, "关闭设备电源。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_RECOVERY,
                  "进入 Android 恢复模式，进行系统维护。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_FASTBOOT,
                  "进入 Fastboot，通过 USB 连接电脑进行维护或刷写。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_EDL,
                  "进入高通 9008 下载模式，供电脑端工具恢复或刷写。") == 0);
    assert(strcmp(KSHIM_MENU_DESC_BACK, "返回系统启动选项。") == 0);
#else
    assert(strcmp(KSHIM_MENU_TEXT_TITLE("kShimC UEFI"), "kShimC UEFI") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_ACTION("Boot"), "Boot") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_DETAIL,
                  "Select an image.\nPress Boot to continue.") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_POWEROFF, "Power off") == 0);
    assert(strcmp(KSHIM_MENU_TEXT_HELP, "Volume keys to select, Power to confirm") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_BOOT, "Boot") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_ENTER, "Enter") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_RESTART, "Restart") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_OFF, "Power off") == 0);
    assert(strcmp(KSHIM_MENU_ACTION_BACK, "Back") == 0);
    assert(strcmp(KSHIM_MENU_DESC_BASE, "Start the device's original system.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_ANDROID,
                  "Start the device's original Android system.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_GENERIC, "Start the selected system.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_INTERNAL,
                  "Start ArmadaOS installed on internal storage.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_TF,
                  "Start ArmadaOS installed on the TF card. Insert the matching card first.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_DIAGNOSTIC,
                  "Start the Linux diagnostic environment to investigate boot and hardware problems.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_ADVANCED,
                  "Choose an available advanced boot mode.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_RESTART, "Restart the device.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_OFF, "Turn off the device.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_RECOVERY,
                  "Enter Android recovery mode for system maintenance.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_FASTBOOT,
                  "Enter Fastboot and connect to a computer over USB for maintenance or flashing.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_EDL,
                  "Enter Qualcomm 9008 download mode for recovery or flashing with a computer.") == 0);
    assert(strcmp(KSHIM_MENU_DESC_BACK, "Return to the system boot options.") == 0);
#endif
    puts("menu locale follows generated configuration before LVGL is included");
    return 0;
}
