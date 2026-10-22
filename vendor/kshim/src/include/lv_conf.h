#ifndef LV_CONF_H
#define LV_CONF_H

#if defined(__has_include)
#if __has_include(<config.h>)
#include <config.h>
#endif
#endif

/* The animated background and antialiased fonts render into ARGB8888. */
#define LV_COLOR_DEPTH 32
#if defined(CONFIG_KSHIM_SMP) && CONFIG_KSHIM_SMP && defined(__aarch64__)
#define LV_USE_OS LV_OS_CUSTOM
#define LV_OS_CUSTOM_INCLUDE <kshim_lvgl_os.h>
#define LV_DRAW_SW_DRAW_UNIT_CNT 8
#else
#define LV_USE_OS LV_OS_NONE
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#endif
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#if defined(KSHIM_STANDALONE) && KSHIM_STANDALONE && \
    (!defined(CONFIG_KSHIM_SMP_SELFTEST) || !CONFIG_KSHIM_SMP_SELFTEST)
#define LV_MEM_SIZE (8U * 1024U * 1024U)
#else
#define LV_MEM_SIZE (512U * 1024U)
#endif

#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SUPPORT_I1 1
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
#define LV_DRAW_THREAD_STACK_SIZE (8U * 1024U)
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 8

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/* The menu draws Noto Sans CJK SC (src/ui/font.c) with TinyTTF at any size;
 * Montserrat 20 is only the Latin fallback when the font cannot be
 * inflated (no scratch memory outside the runtime slot). */
#if defined(CONFIG_KSHIM_MENU_STYLE_LVGL_DEFAULT) && CONFIG_KSHIM_MENU_STYLE_LVGL_DEFAULT
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_THEME_DEFAULT 1
#else
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_DEFAULT &lv_font_montserrat_20
#define LV_USE_THEME_DEFAULT 0
#endif
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_UNSCII_8 0
#define LV_FONT_UNSCII_16 0
#define LV_FONT_SIMSUN_14_CJK 0
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 128
#define LV_USE_LZ4_INTERNAL 1
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/* The menu only needs labels, buttons, and a list. */
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_BAR 0
#define LV_USE_BUTTON 1
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 1
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LABEL 1
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 1
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_USE_FLEX 1
#define LV_USE_GRID 0
#define LV_USE_PRIVATE_API 0
#define LV_USE_OBSERVER 0
#define LV_USE_OBJ_ID 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_FLOAT 0
#define LV_USE_MATRIX 0

#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_SDL 0
#define LV_USE_X11 0
#define LV_USE_WAYLAND 0
#define LV_USE_LINUX_FBDEV 0
#define LV_USE_NUTTX 0
#define LV_USE_LINUX_DRM 0
#define LV_USE_EVDEV 0
#define LV_USE_LIBINPUT 0
#define LV_USE_TFT_ESPI 0

#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON 0
#define LV_USE_PROFILER 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif /* LV_CONF_H */
