#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include <lvgl_port.h>
#include <ui_font.h>

#include "../src/ui/menu_style.h"
#include "menu_design_lvgl_checks.h"
#include "menu_native_theme_checks.h"

/* 比较 RGB 分量，不依赖 lv_color_t 的内存排列或填充。 */
static void CheckColor(lv_color_t Actual, uint32_t Expected)
{
    assert(Actual.red == ((Expected >> 16) & 255U));
    assert(Actual.green == ((Expected >> 8) & 255U));
    assert(Actual.blue == (Expected & 255U));
}

/* 推进渲染与原有焦点动画，使布局检查不受入场动画影响。 */
static void Advance(kshim_lvgl_t *Ui)
{
    for (unsigned Index = 0U; Index < 32U; Index++)
        kshim_lvgl_frame(Ui, 16U);
}

/* 统计右侧预留区的实际强调色像素，不以控件状态代替勾号绘制验证。 */
static unsigned IOSMarkPixels(kshim_lvgl_t *Ui, lv_obj_t *Row)
{
    lv_area_t Area;
    lv_obj_get_coords(Row, &Area);
    int32_t CenterY = (Area.y1 + Area.y2) / 2;
    unsigned Count = 0U;
    for (int32_t Y = CenterY - 15; Y <= CenterY + 15; Y++) {
        if (Y < 0 || Y >= (int32_t)Ui->Framebuffer->height) continue;
        const uint32_t *Pixels = (const uint32_t *)(const void *)(
            Ui->Framebuffer->render_address + (size_t)Y * Ui->Framebuffer->stride);
        for (int32_t X = Area.x2 - 42; X <= Area.x2 - 5; X++) {
            if (X >= 0 && X < (int32_t)Ui->Framebuffer->width)
                Count += (Pixels[X] & 0xffffffU) == mMenuStyle.FocusBorder;
        }
    }
    return Count;
}

/* 检查实际 LVGL 对象上的行、面板、Boot 按钮和焦点状态。 */
static void CheckChrome(kshim_lvgl_t *Ui)
{
    if (KSHIM_MENU_IS_NATIVE) { KshimTestNativeChrome(Ui); return; }
    if (mMenuDesign.enabled) { KshimTestDesignChrome(Ui); return; }
    lv_obj_t *Panel = lv_obj_get_parent(Ui->List);
    lv_obj_t *First = lv_group_get_obj_by_index(Ui->Group, 0U);
    lv_obj_t *Second = lv_group_get_obj_by_index(Ui->Group, 1U);
    lv_obj_t *BootLabel = lv_obj_get_child(Ui->BootButton, 0U);
    assert(Panel != NULL && First != NULL && Second != NULL && BootLabel != NULL);
    CheckColor(lv_obj_get_style_bg_color(Panel, LV_PART_MAIN), mMenuStyle.Panel);
    assert(lv_obj_get_style_bg_opa(Panel, LV_PART_MAIN) == mMenuStyle.PanelOpacity);
    assert(lv_obj_get_style_outline_width(Panel, LV_PART_MAIN) == mMenuStyle.PanelOutlineWidth);
    assert(lv_obj_get_style_border_width(Panel, LV_PART_MAIN) == 0);
    if (mMenuStyle.Modern != 0U) {
        assert(lv_obj_get_style_radius(Panel, LV_PART_MAIN) == mMenuStyle.PanelRadius);
        assert(lv_obj_get_style_shadow_width(Panel, LV_PART_MAIN) == mMenuStyle.PanelShadow);
    }
    if (mMenuStyle.Modern == 0U && mMenuStyle.RadiusMax == 0U)
        assert(lv_obj_get_style_radius(Panel, LV_PART_MAIN) == 0);

    lv_group_focus_obj(First);
    CheckColor(lv_obj_get_style_bg_color(First, LV_PART_MAIN), mMenuStyle.Focus);
    CheckColor(lv_obj_get_style_text_color(First, LV_PART_MAIN), mMenuStyle.FocusText);
    assert(lv_obj_get_style_bg_opa(First, LV_PART_MAIN) == mMenuStyle.FocusOpacity);
    CheckColor(lv_obj_get_style_bg_color(Second, LV_PART_MAIN), mMenuStyle.Item);
    CheckColor(lv_obj_get_style_text_color(Second, LV_PART_MAIN), mMenuStyle.Muted);
    assert(lv_obj_get_style_border_width(First, LV_PART_MAIN) == mMenuStyle.RowBorderWidth);
    assert(lv_obj_get_style_border_width(Second, LV_PART_MAIN) == mMenuStyle.RowBorderWidth);
    assert(lv_obj_get_style_border_opa(Second, LV_PART_MAIN) ==
           (mMenuStyle.BorderAtRest != 0U ? LV_OPA_COVER : LV_OPA_TRANSP));
    assert(lv_obj_get_style_border_side(First, LV_PART_MAIN) ==
           (mMenuStyle.Divider != 0U ? LV_BORDER_SIDE_BOTTOM :
            mMenuStyle.LeftBorderOnly != 0U ? LV_BORDER_SIDE_LEFT : LV_BORDER_SIDE_FULL));
    assert(lv_obj_get_style_radius(First, LV_PART_MAIN) ==
           (int32_t)kshim_menu_style_radius((uint32_t)lv_obj_get_height(First)));

    CheckColor(lv_obj_get_style_bg_color(Ui->BootButton, LV_PART_MAIN), mMenuStyle.Boot);
    CheckColor(lv_obj_get_style_text_color(BootLabel, LV_PART_MAIN), mMenuStyle.BootText);
    assert(lv_obj_get_style_shadow_width(Ui->BootButton, LV_PART_MAIN) == mMenuStyle.BootShadow);
    if (mMenuStyle.FocusPill != 0U)
        assert(lv_obj_get_style_pad_left(First, LV_PART_MAIN) == 20);
    lv_obj_add_state(Ui->BootButton, LV_STATE_PRESSED);
    if (mMenuStyle.BootShadow != 0U)
        assert(lv_obj_get_style_shadow_width(Ui->BootButton, LV_PART_MAIN) == 2);
    CheckColor(lv_obj_get_style_bg_color(Ui->BootButton, LV_PART_MAIN), mMenuStyle.BootPressed);
    lv_obj_remove_state(Ui->BootButton, LV_STATE_PRESSED);
    lv_obj_add_state(First, LV_STATE_PRESSED);
    CheckColor(lv_obj_get_style_text_color(First, LV_PART_MAIN),
               mMenuStyle.Modern != 0U ? mMenuStyle.PressText : mMenuStyle.FocusText);
    lv_obj_remove_state(First, LV_STATE_PRESSED);
    if (mMenuStyle.Layout == 5U) {
        assert(lv_obj_get_style_radius(Ui->List, LV_PART_MAIN) == 16);
        assert(lv_obj_get_style_clip_corner(Ui->List, LV_PART_MAIN));
        assert(lv_obj_get_style_pad_right(First, LV_PART_MAIN) == 40);
        if (Ui->Framebuffer->width >= 320U && Ui->Framebuffer->height >= 160U) {
            Advance(Ui);
            assert(IOSMarkPixels(Ui, First) > 5U);
            lv_group_focus_obj(Second);
            Advance(Ui);
            assert(IOSMarkPixels(Ui, First) == 0U);
            assert(IOSMarkPixels(Ui, Second) > 5U);
            lv_group_focus_obj(First);
            Advance(Ui);
            assert(IOSMarkPixels(Ui, First) > 5U);
        }
    }
    assert(kshim_lvgl_take_index(Ui) == -1);
}

/* 覆盖横竖屏、字体/纹理 scratch 和动态菜单重建，检查帧缓冲哨兵。 */
static void TestSurface(
    unsigned Width,
    unsigned Height,
    size_t ScratchBytes,
    int ExpectedCjk
)
{
    const size_t Guard = 64U;
    size_t FrameBytes = (size_t)Width * Height * 4U;
    uint8_t *Storage = malloc(FrameBytes + 2U * Guard);
    void *Scratch = ScratchBytes != 0U ? aligned_alloc(64U, ScratchBytes) : NULL;
    kshim_framebuffer_t Framebuffer;
    kshim_lvgl_t Ui;
    char Labels[KSHIM_MENU_MAX_ENTRIES][24];
    const char *Names[KSHIM_MENU_MAX_ENTRIES];

    assert(Storage != NULL && (ScratchBytes == 0U || Scratch != NULL));
    memset(Storage, 0xa5, FrameBytes + 2U * Guard);
    kshim_framebuffer_config_t Config = {
        .render_address = (uintptr_t)(Storage + Guard),
        .width = Width, .height = Height, .stride = Width * 4U,
        .bpp = 32U, .format = KSHIM_FB_FORMAT_ARGB8888,
        .foreground = UINT32_MAX, .background = 0U, .buffer_size = FrameBytes,
    };
    assert(kshim_fb_init(&Framebuffer, &Config) == KSHIM_FB_OK);
    assert(kshim_fb_clear(&Framebuffer) == KSHIM_FB_OK);
    kshim_lvgl_set_scratch(Scratch, ScratchBytes);
    assert(kshim_lvgl_init(&Ui, &Framebuffer, NULL) == 0);
    Advance(&Ui);
    assert(kshim_lvgl_has_cjk_font(&Ui) == ExpectedCjk);
    CheckChrome(&Ui);

    const void *Background = lv_image_get_src(Ui.Background);
    if (mMenuDesign.enabled) {
        assert((Background != NULL) == KshimTestDesignBackgroundExpected());
        kshim_lvgl_frame(&Ui, 128U);
        assert(lv_image_get_src(Ui.Background) == Background);
    } else {
        assert((Background != NULL) == (ScratchBytes >= (512U << 10) &&
                                        mMenuStyle.AnimatedBackdrop != 0U));
        kshim_lvgl_frame(&Ui, 128U);
        if (Background != NULL)
            assert(lv_image_get_src(Ui.Background) != Background);
        else
            assert(lv_image_get_src(Ui.Background) == NULL);
    }

    for (unsigned Index = 0U; Index < KSHIM_MENU_MAX_ENTRIES; Index++) {
        (void)snprintf(Labels[Index], sizeof(Labels[Index]), "System %02u", Index);
        Names[Index] = Labels[Index];
    }
    assert(kshim_lvgl_set_entries(&Ui, Names, KSHIM_MENU_MAX_ENTRIES, 37U) == 0);
    assert(lv_group_get_focused(Ui.Group) == lv_group_get_obj_by_index(Ui.Group, 37U));
    Advance(&Ui);
    CheckChrome(&Ui);
    assert(kshim_lvgl_set_entries(&Ui, Names, 2U, 0U) == 0);
    Advance(&Ui);
    CheckChrome(&Ui);
    kshim_lvgl_deinit(&Ui);
    kshim_lvgl_set_scratch(NULL, 0U);
    for (size_t Index = 0U; Index < Guard; Index++) {
        assert(Storage[Index] == 0xa5);
        assert(Storage[Guard + FrameBytes + Index] == 0xa5);
    }
    free(Scratch);
    free(Storage);
}

/* 真实渲染测试与现有键盘/触摸回归一起用于每个预设的构建矩阵。 */
int main(void)
{
    size_t FontOnlyBytes = (kshim_ui_font_size() + 63U) & ~(size_t)63U;
    TestSurface(320U, 240U, 0U, 0);
    TestSurface(128U, 64U, 0U, 0);
    TestSurface(640U, 360U, 4U << 20, 1);
    TestSurface(1080U, 1920U, 4U << 20, 1);
    /* 静态预设无需为两张背景纹理预留 512 KiB，字体可直接使用该区域。 */
    TestSurface(640U, 360U, FontOnlyBytes, mMenuStyle.AnimatedBackdrop == 0U);
    printf("PASS: %s real LVGL styles, focus, scratch, relayout and framebuffer guards\n",
           mMenuStyle.Name);
    return 0;
}
