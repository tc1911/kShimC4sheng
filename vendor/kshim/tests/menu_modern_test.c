/* 现代主题的布局和交互专项；使用真实 LVGL，不模拟绘制 API。 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include <lvgl_port.h>
#include "../src/ui/menu_style.h"
#include "menu_design_lvgl_checks.h"

/* 等待入场、焦点及滚动动画稳定。 */
static void Settle(kshim_lvgl_t *Ui)
{
    for (unsigned Frame = 0; Frame < 48U; Frame++)
        kshim_lvgl_frame(Ui, 16U);
}

/* 从真实控件坐标发送触摸，验证卡片或分栏不会绕过确认规则。 */
static void Tap(kshim_lvgl_t *Ui, lv_obj_t *Object, int Cancel)
{
    lv_area_t Area;
    lv_obj_get_coords(Object, &Area);
    assert(Area.x1 >= 0 && Area.y1 >= 0);
    kshim_touch_event_t Event = {
        .type = KSHIM_TOUCH_EVENT_PRESS,
        .x = (uint32_t)((Area.x1 + Area.x2) / 2),
        .y = (uint32_t)((Area.y1 + Area.y2) / 2),
    };
    kshim_lvgl_touch(Ui, &Event);
    Event.type = Cancel ? KSHIM_TOUCH_EVENT_CANCEL : KSHIM_TOUCH_EVENT_RELEASE;
    kshim_lvgl_touch(Ui, &Event);
    Settle(Ui);
}

/* 检查几何边界以及卡片行优先顺序，不能让 Boot 按钮覆盖菜单。 */
static void CheckLayout(kshim_lvgl_t *Ui, unsigned Width, unsigned Height)
{
    if (mMenuDesign.enabled) { KshimTestDesignLayout(Ui); return; }
    lv_area_t Panel, List, Boot, First, Second;
    lv_obj_get_coords(lv_obj_get_parent(Ui->List), &Panel);
    lv_obj_get_coords(Ui->List, &List);
    lv_obj_get_coords(Ui->BootButton, &Boot);
    assert(Panel.x1 >= 0 && Panel.y1 >= 0);
    assert(Panel.x2 < (int32_t)Width && Panel.y2 < (int32_t)Height);
    assert(Boot.x1 >= Panel.x1 && Boot.x2 <= Panel.x2);
    assert(Boot.y1 >= Panel.y1 && Boot.y2 <= Panel.y2);
    if (mMenuStyle.Layout == 1U && Width >= 720U && Width > Height) {
        assert(Boot.x1 > List.x2);
    } else {
        assert(Boot.y1 > List.y2);
    }
    lv_obj_t *A = lv_group_get_obj_by_index(Ui->Group, 0);
    lv_obj_t *B = lv_group_get_obj_by_index(Ui->Group, 1);
    assert(A != NULL);
    if (B == NULL) return;
    lv_obj_get_coords(A, &First);
    lv_obj_get_coords(B, &Second);
    int32_t PanelWidth = lv_obj_get_width(lv_obj_get_parent(Ui->List));
    if (mMenuStyle.Layout == 4U && Width >= 320U && Height >= 240U) {
        assert(First.y1 == Second.y1 && First.x2 < Second.x1);
        assert(lv_obj_get_scroll_dir(Ui->List) == LV_DIR_HOR);
        assert(First.y1 >= List.y1 && First.y2 <= List.y2);
        assert(lv_obj_get_child_count(A) == 2U);
    } else if ((mMenuStyle.Layout == 2U || mMenuStyle.Layout == 3U) && PanelWidth >= 560 && Height >= 320U) {
        assert(First.y1 == Second.y1 && First.x2 < Second.x1);
        assert(lv_obj_get_child_count(A) == 2U);
        if (mMenuStyle.Layout == 3U && PanelWidth >= 960 && Ui->EntryCount >= 3U) {
            lv_area_t Third;
            lv_obj_get_coords(lv_group_get_obj_by_index(Ui->Group, 2U), &Third);
            assert(Third.y1 == First.y1 && Third.x1 > Second.x2);
        }
    } else {
        assert(First.y2 < Second.y1);
    }
}

/* 验证菜单数目、长标签、触摸取消和重建后的风格，逐像素检查边界哨兵。 */
static void TestSize(unsigned Width, unsigned Height)
{
    size_t Bytes = (size_t)Width * Height * 4U;
    uint8_t *Memory = malloc(Bytes + 128U);
    assert(Memory != NULL);
    memset(Memory, 0xa5, Bytes + 128U);
    kshim_framebuffer_config_t Config = {
        .render_address = (uintptr_t)(Memory + 64U),
        .width = Width, .height = Height, .stride = Width * 4U,
        .bpp = 32U, .format = KSHIM_FB_FORMAT_ARGB8888,
        .buffer_size = Bytes, .foreground = UINT32_MAX,
    };
    kshim_framebuffer_t Fb;
    kshim_lvgl_t Ui;
    assert(kshim_fb_init(&Fb, &Config) == 0);
    assert(kshim_lvgl_init(&Ui, &Fb, NULL) == 0);
    char Text[KSHIM_MENU_MAX_ENTRIES][96];
    const char *Names[KSHIM_MENU_MAX_ENTRIES];
    for (unsigned I = 0; I < KSHIM_MENU_MAX_ENTRIES; I++) {
        (void)snprintf(Text[I], sizeof(Text[I]), "Image %02u - a deliberately long boot entry for clipping checks", I);
        Names[I] = Text[I];
    }
    const unsigned Counts[] = {1U, 2U, 3U, 49U, 2U};
    for (unsigned I = 0; I < sizeof(Counts) / sizeof(Counts[0]); I++) {
        unsigned Count = Counts[I];
        assert(kshim_lvgl_set_entries(&Ui, Names, Count, Count - 1U) == 0);
        Settle(&Ui);
        assert(lv_group_get_focused(Ui.Group) == lv_group_get_obj_by_index(Ui.Group, Count - 1U));
        CheckLayout(&Ui, Width, Height);
        lv_obj_t *Focused = lv_group_get_focused(Ui.Group);
        lv_area_t Selected, View;
        lv_obj_get_coords(Focused, &Selected);
        lv_obj_get_coords(Ui.List, &View);
        assert((Selected.x1 + Selected.x2) / 2 >= View.x1);
        assert((Selected.x1 + Selected.x2) / 2 <= View.x2);
        Tap(&Ui, Focused, 0);
        if ((mMenuStyle.Layout == 3U || mMenuStyle.Layout == 4U) &&
            lv_obj_get_child_count(Focused) > 1U) {
            lv_area_t Label, Badge;
            lv_obj_get_coords(lv_obj_get_child(Focused, 0U), &Label);
            lv_obj_get_coords(lv_obj_get_child(Focused, 1U), &Badge);
            assert(Label.x1 >= Selected.x1 && Label.x2 <= Selected.x2);
            assert(Label.y1 >= Selected.y1 && Label.y2 <= Selected.y2);
            assert(Badge.y2 < Label.y1 || Badge.x2 < Label.x1);
            Tap(&Ui, lv_obj_get_child(Focused, 1U), 0);
            assert(lv_group_get_focused(Ui.Group) == Focused);
        }
        assert(kshim_lvgl_take_index(&Ui) == -1);
        Tap(&Ui, Ui.BootButton, 1);
        assert(kshim_lvgl_take_index(&Ui) == -1);
        Tap(&Ui, Ui.BootButton, 0);
        assert(kshim_lvgl_take_index(&Ui) == (int)Count - 1);
        assert(kshim_lvgl_take_index(&Ui) == -1);
        assert(kshim_lvgl_take_selection(&Ui) == KSHIM_MENU_NONE);
    }
    kshim_lvgl_deinit(&Ui);
    for (unsigned I = 0; I < 64U; I++) {
        assert(Memory[I] == 0xa5);
        assert(Memory[64U + Bytes + I] == 0xa5);
    }
    free(Memory);
}

typedef struct {
    kshim_lvgl_t *Ui;
    unsigned Calls;
} closing_probe_t;

/* 销毁回调中公共交互入口必须失效，不能访问尚在删除的对象或重新确认。 */
static void CheckClosing(lv_event_t *Event)
{
    closing_probe_t *Probe = lv_event_get_user_data(Event);
    assert(Probe != NULL && !kshim_lvgl_ready(Probe->Ui));
    assert(Probe->Ui->Ready == 0U);
    kshim_lvgl_set_status(Probe->Ui, "must not touch a closing label");
    kshim_lvgl_set_countdown(Probe->Ui, 1000U);
    kshim_lvgl_frame(Probe->Ui, 16U);
    kshim_touch_event_t Touch = {.type = KSHIM_TOUCH_EVENT_PRESS, .x = 1U, .y = 1U};
    kshim_lvgl_touch(Probe->Ui, &Touch);
    assert(kshim_lvgl_take_index(Probe->Ui) == -1);
    assert(kshim_lvgl_take_selection(Probe->Ui) == KSHIM_MENU_NONE);
    kshim_lvgl_deinit(Probe->Ui);
    Probe->Calls++;
}

/* 每款新旧主题都检查活动焦点/动画下的销毁、重入和重新初始化。 */
static void TestClosing(void)
{
    const size_t Bytes = 320U * 240U * 4U;
    void *Pixels = calloc(1U, Bytes);
    assert(Pixels != NULL);
    kshim_framebuffer_config_t Config = {
        .render_address = (uintptr_t)Pixels, .width = 320U, .height = 240U,
        .stride = 1280U, .bpp = 32U, .format = KSHIM_FB_FORMAT_ARGB8888,
        .buffer_size = Bytes,
    };
    kshim_framebuffer_t Fb;
    assert(kshim_fb_init(&Fb, &Config) == 0);
    for (unsigned Pass = 0U; Pass < 8U; Pass++) {
        kshim_lvgl_t Ui;
        closing_probe_t Probe = {.Ui = &Ui};
        assert(kshim_lvgl_init(&Ui, &Fb, NULL) == 0);
        lv_obj_t *Panel = lv_obj_get_parent(Ui.List);
        lv_obj_t *Status = lv_obj_get_child(Panel, 1U);
        assert(Status != NULL);
        lv_obj_add_event_cb(Panel, CheckClosing, LV_EVENT_DELETE, &Probe);
        lv_obj_add_event_cb(Status, CheckClosing, LV_EVENT_DELETE, &Probe);
        lv_obj_t *Row = lv_group_get_obj_by_index(Ui.Group, Pass % Ui.EntryCount);
        lv_group_focus_obj(Row);
        lv_obj_send_event(Row, LV_EVENT_PRESSED, NULL);
        kshim_lvgl_deinit(&Ui);
        assert(Probe.Calls == 2U && !kshim_lvgl_ready(&Ui));
        kshim_lvgl_deinit(&Ui);
    }
    free(Pixels);
}

/* 只比较 Boot 区域，焦点文字或背景变化不能代替按钮的真实反馈。 */
static uint64_t BootHash(const kshim_framebuffer_t *Fb, const lv_area_t *Area)
{
    assert(Area->x1 >= 0 && Area->y1 >= 0);
    assert(Area->x2 < (int32_t)Fb->width && Area->y2 < (int32_t)Fb->height);
    uint64_t Hash = UINT64_C(14695981039346656037);
    for (int32_t Y = Area->y1; Y <= Area->y2; Y++) {
        const uint32_t *Row = (const uint32_t *)(const void *)(Fb->render_address + (size_t)Y * Fb->stride);
        for (int32_t X = Area->x1; X <= Area->x2; X++)
            Hash = (Hash ^ Row[X]) * UINT64_C(1099511628211);
    }
    return Hash;
}

/* 仅通过普通 frame 循环观察确认动效及恢复，禁止强制刷新掩盖显示问题。 */
static void TestAutomaticConfirmation(unsigned Width, unsigned Height)
{
    size_t Bytes = (size_t)Width * Height * 4U;
    void *Pixels = calloc(1U, Bytes);
    assert(Pixels != NULL);
    kshim_framebuffer_config_t Config = {
        .render_address = (uintptr_t)Pixels, .width = Width, .height = Height,
        .stride = Width * 4U, .bpp = 32U, .format = KSHIM_FB_FORMAT_ARGB8888,
        .buffer_size = Bytes,
    };
    kshim_framebuffer_t Fb;
    assert(kshim_fb_init(&Fb, &Config) == 0);
    for (unsigned Phase = 0U; Phase <= 32U; Phase += 16U) {
        kshim_lvgl_t Ui;
        assert(kshim_lvgl_init(&Ui, &Fb, NULL) == 0);
        Settle(&Ui);
        kshim_lvgl_frame(&Ui, Phase);
        lv_area_t Area;
        lv_obj_get_coords(Ui.BootButton, &Area);
        uint64_t Before = BootHash(&Fb, &Area);
        unsigned Changed = 0U;
        assert(lv_obj_send_event(Ui.BootButton, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK);
        assert(Ui.PendingIndex == 0);
        for (unsigned Frame = 0U; Frame < 32U; Frame++) {
            kshim_lvgl_frame(&Ui, 16U);
            Changed |= BootHash(&Fb, &Area) != Before;
        }
        assert(KSHIM_MENU_IS_NATIVE ? Changed == 0U : Changed != 0U);
        assert(BootHash(&Fb, &Area) == Before);
        assert(lv_obj_get_style_transform_scale_x(Ui.BootButton, LV_PART_MAIN) == 256);
        assert(lv_obj_get_style_transform_scale_y(Ui.BootButton, LV_PART_MAIN) == 256);
        assert(kshim_lvgl_take_index(&Ui) == 0);
        assert(kshim_lvgl_take_index(&Ui) == -1);
        kshim_lvgl_deinit(&Ui);
    }
    free(Pixels);
}

/* 覆盖会触发两种自适应布局的阈值两侧和常见横竖屏。 */
int main(void)
{
    TestClosing();
    if (mMenuDesign.enabled || KSHIM_MENU_IS_NATIVE) {
        TestAutomaticConfirmation(320U, 240U);
        TestAutomaticConfirmation(1080U, 2340U);
        TestSize(320U, 240U);
        TestSize(559U, 360U);
        TestSize(640U, 360U);
        TestSize(720U, 480U);
        TestSize(575U, 360U);
        TestSize(1000U, 720U);
        TestSize(1080U, 1920U);
        TestSize(1920U, 1080U);
    }
    printf("PASS: %s lifecycle, geometry, automatic confirmation and touch cancellation\n", mMenuStyle.Name);
    return 0;
}
