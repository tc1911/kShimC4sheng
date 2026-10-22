#pragma once

/* Firmware gets its locale from Kconfig, before any LVGL headers are read.
 * Generic host targets can still build without a generated board config. */
#if defined(__has_include)
#if __has_include(<config.h>)
#include <config.h>
#endif
#endif

#include <menu_locale.h>

/* With the runtime language switch enabled every menu string is looked up in the
 * locale table so a live selection flips the whole UI; the compile-time
 * literals below stay authoritative when the switch is off. */
#if defined(CONFIG_KSHIM_MENU_LANGUAGE_SWITCH) && CONFIG_KSHIM_MENU_LANGUAGE_SWITCH
#define KSHIM_MENU_TEXT_TITLE(Fallback) kshim_menu_text(KSHIM_TEXT_TITLE)
#define KSHIM_MENU_TEXT_ACTION(Fallback) kshim_menu_text(KSHIM_TEXT_ACTION)
#define KSHIM_MENU_TEXT_DETAIL kshim_menu_text(KSHIM_TEXT_DETAIL)
#define KSHIM_MENU_TEXT_POWEROFF kshim_menu_text(KSHIM_TEXT_POWEROFF)
#define KSHIM_MENU_TEXT_HELP kshim_menu_text(KSHIM_TEXT_HELP)
#define KSHIM_MENU_ACTION_BOOT kshim_menu_text(KSHIM_TEXT_ACTION_BOOT)
#define KSHIM_MENU_ACTION_ENTER kshim_menu_text(KSHIM_TEXT_ACTION_ENTER)
#define KSHIM_MENU_ACTION_RESTART kshim_menu_text(KSHIM_TEXT_ACTION_RESTART)
#define KSHIM_MENU_ACTION_OFF kshim_menu_text(KSHIM_TEXT_ACTION_OFF)
#define KSHIM_MENU_ACTION_BACK kshim_menu_text(KSHIM_TEXT_ACTION_BACK)
#define KSHIM_MENU_DESC_BASE kshim_menu_text(KSHIM_TEXT_DESC_BASE)
#define KSHIM_MENU_DESC_ANDROID kshim_menu_text(KSHIM_TEXT_DESC_ANDROID)
#define KSHIM_MENU_DESC_GENERIC kshim_menu_text(KSHIM_TEXT_DESC_GENERIC)
#define KSHIM_MENU_DESC_INTERNAL kshim_menu_text(KSHIM_TEXT_DESC_INTERNAL)
#define KSHIM_MENU_DESC_TF kshim_menu_text(KSHIM_TEXT_DESC_TF)
#define KSHIM_MENU_DESC_DIAGNOSTIC kshim_menu_text(KSHIM_TEXT_DESC_DIAGNOSTIC)
#define KSHIM_MENU_DESC_ADVANCED kshim_menu_text(KSHIM_TEXT_DESC_ADVANCED)
#define KSHIM_MENU_DESC_RESTART kshim_menu_text(KSHIM_TEXT_DESC_RESTART)
#define KSHIM_MENU_DESC_OFF kshim_menu_text(KSHIM_TEXT_DESC_OFF)
#define KSHIM_MENU_DESC_RECOVERY kshim_menu_text(KSHIM_TEXT_DESC_RECOVERY)
#define KSHIM_MENU_DESC_FASTBOOT kshim_menu_text(KSHIM_TEXT_DESC_FASTBOOT)
#define KSHIM_MENU_DESC_EDL kshim_menu_text(KSHIM_TEXT_DESC_EDL)
#define KSHIM_MENU_DESC_BACK kshim_menu_text(KSHIM_TEXT_DESC_BACK)
#define KSHIM_MENU_DESC_BACK_PARENT kshim_menu_text(KSHIM_TEXT_DESC_BACK_PARENT)
#define KSHIM_MENU_TEXT_STATUS kshim_menu_text(KSHIM_TEXT_STATUS)
#define KSHIM_MENU_TEXT_CREDIT kshim_menu_text(KSHIM_TEXT_CREDIT)
#define KSHIM_MENU_TEXT_LANGUAGE_STATE kshim_menu_text(KSHIM_TEXT_LANGUAGE_STATE)
/* Board profiles opt into Chinese; upstream theme strings remain the default. */
#elif defined(CONFIG_KSHIM_MENU_CHINESE) && CONFIG_KSHIM_MENU_CHINESE
#define KSHIM_MENU_TEXT_TITLE(Fallback) "KShimC 启动器"
#define KSHIM_MENU_TEXT_ACTION(Fallback) "启动"
#define KSHIM_MENU_TEXT_DETAIL "选择系统或操作。\n音量键选择，电源键确认"
#define KSHIM_MENU_TEXT_POWEROFF "关机"
#define KSHIM_MENU_TEXT_HELP "音量＋／－切换 · 电源键确认"
#define KSHIM_MENU_ACTION_BOOT "启动"
#define KSHIM_MENU_ACTION_ENTER "进入"
#define KSHIM_MENU_ACTION_RESTART "重启"
#define KSHIM_MENU_ACTION_OFF "关机"
#define KSHIM_MENU_ACTION_BACK "返回"
#define KSHIM_MENU_DESC_BASE "启动设备原有系统。"
#define KSHIM_MENU_DESC_ANDROID "启动设备原有的 Android 系统。"
#define KSHIM_MENU_DESC_GENERIC "启动所选系统。"
#define KSHIM_MENU_DESC_INTERNAL "启动安装在机身存储中的 ArmadaOS。"
#define KSHIM_MENU_DESC_TF "启动安装在 TF 卡中的 ArmadaOS，请先插入对应 TF 卡。"
#define KSHIM_MENU_DESC_DIAGNOSTIC "启动 Linux 诊断环境，用于检查启动和硬件问题。"
#define KSHIM_MENU_DESC_ADVANCED "查看可用的高级启动模式。"
#define KSHIM_MENU_DESC_RESTART "重新启动设备。"
#define KSHIM_MENU_DESC_OFF "关闭设备电源。"
#define KSHIM_MENU_DESC_RECOVERY "进入 Android 恢复模式，进行系统维护。"
#define KSHIM_MENU_DESC_FASTBOOT "进入 Fastboot，通过 USB 连接电脑进行维护或刷写。"
#define KSHIM_MENU_DESC_EDL "进入高通 9008 下载模式，供电脑端工具恢复或刷写。"
#define KSHIM_MENU_DESC_BACK "返回系统启动选项。"
#define KSHIM_MENU_DESC_BACK_PARENT "返回上一级。"
#define KSHIM_MENU_TEXT_STATUS "选择要启动的系统"
#define KSHIM_MENU_TEXT_CREDIT "适配维护：B站「单翼」｜免费使用，谨防收费诈骗。"
#define KSHIM_MENU_TEXT_LANGUAGE_STATE "简体中文"
#else
#define KSHIM_MENU_TEXT_TITLE(Fallback) (Fallback)
#define KSHIM_MENU_TEXT_ACTION(Fallback) (Fallback)
#define KSHIM_MENU_TEXT_DETAIL "Select an image.\nPress Boot to continue."
#define KSHIM_MENU_TEXT_POWEROFF "Power off"
#define KSHIM_MENU_TEXT_HELP "Volume keys to select, Power to confirm"
#define KSHIM_MENU_ACTION_BOOT "Boot"
#define KSHIM_MENU_ACTION_ENTER "Enter"
#define KSHIM_MENU_ACTION_RESTART "Restart"
#define KSHIM_MENU_ACTION_OFF "Power off"
#define KSHIM_MENU_ACTION_BACK "Back"
#define KSHIM_MENU_DESC_BASE "Start the device's original system."
#define KSHIM_MENU_DESC_ANDROID "Start the device's original Android system."
#define KSHIM_MENU_DESC_GENERIC "Start the selected system."
#define KSHIM_MENU_DESC_INTERNAL "Start ArmadaOS installed on internal storage."
#define KSHIM_MENU_DESC_TF "Start ArmadaOS installed on the TF card. Insert the matching card first."
#define KSHIM_MENU_DESC_DIAGNOSTIC "Start the Linux diagnostic environment to investigate boot and hardware problems."
#define KSHIM_MENU_DESC_ADVANCED "Choose an available advanced boot mode."
#define KSHIM_MENU_DESC_RESTART "Restart the device."
#define KSHIM_MENU_DESC_OFF "Turn off the device."
#define KSHIM_MENU_DESC_RECOVERY "Enter Android recovery mode for system maintenance."
#define KSHIM_MENU_DESC_FASTBOOT "Enter Fastboot and connect to a computer over USB for maintenance or flashing."
#define KSHIM_MENU_DESC_EDL "Enter Qualcomm 9008 download mode for recovery or flashing with a computer."
#define KSHIM_MENU_DESC_BACK "Return to the system boot options."
#define KSHIM_MENU_DESC_BACK_PARENT "Return to the previous menu."
#define KSHIM_MENU_TEXT_STATUS "选择要启动的系统"
#define KSHIM_MENU_TEXT_CREDIT "Adapted by Danyi on Bilibili · Free to use. Beware of paid scams."
#define KSHIM_MENU_TEXT_LANGUAGE_STATE "English"
#endif

/* Bilingual, identical in every locale. */
#define KSHIM_MENU_TEXT_LANGUAGE_LABEL "语言 / Language"
