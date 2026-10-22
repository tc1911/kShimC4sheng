/* Exercise the real LVGL renderer and both physical-key and touch paths. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "../src/ui/menu_style.h"
#include <lvgl_port.h>
#include <ui_font.h>

#ifdef CONFIG_KSHIM_UEFI_TEST_MENU
#define DEFAULT_ENTRY_COUNT 2U
#define DEFAULT_SECOND_ACTION KSHIM_MENU_RECOVERY
#else
#define DEFAULT_ENTRY_COUNT 3U
#define DEFAULT_SECOND_ACTION KSHIM_MENU_RECOVERY
#endif

typedef struct test_ui {
    uint8_t *storage;
    size_t storage_bytes;
    size_t frame_bytes;
    unsigned guard;
    unsigned width;
    unsigned height;
    unsigned content_bytes;
    kshim_framebuffer_t framebuffer;
    kshim_lvgl_t ui;
    QcomKeys keys;
    QcomKeyDescriptor descriptors[3];
} test_ui_t;

static uint32_t Inputs[3];

static uint32_t ReadInput(void *Context, uint64_t Address)
{
    (void)Context;
    assert(Address >= 1U && Address <= 3U);
    return Inputs[Address - 1U];
}

static void Advance(kshim_lvgl_t *Ui, unsigned Milliseconds)
{
    while (Milliseconds != 0U) {
        unsigned Step = Milliseconds > 16U ? 16U : Milliseconds;
        kshim_lvgl_frame(Ui, Step);
        Milliseconds -= Step;
    }
}

static void PressKey(kshim_lvgl_t *Ui, unsigned Index)
{
    Inputs[Index] = 1U;
    Advance(Ui, 112U);
    Inputs[Index] = 0U;
    Advance(Ui, 112U);
}

static void InitStableUi(test_ui_t *Test, unsigned Width, unsigned Height,
                         kshim_fb_format_t Format)
{
    memset(Test, 0, sizeof(*Test));
    static const uint32_t Codes[] = {
        QCOM_KEYCODE_VOLUMEUP, QCOM_KEYCODE_VOLUMEDOWN,
        QCOM_KEYCODE_POWER,
    };
    unsigned Bpp = kshim_fb_format_bpp(Format);
    Test->content_bytes = (Width * Bpp + 7U) / 8U;
    unsigned Stride = Test->content_bytes + 13U;
    Test->frame_bytes = (size_t)Stride * Height;
    Test->guard = 64U;
    Test->storage_bytes = Test->frame_bytes + Test->guard * 2U;
    Test->storage = malloc(Test->storage_bytes);
    assert(Test->storage != NULL);
    memset(Test->storage, 0xa5, Test->storage_bytes);
    Test->width = Width;
    Test->height = Height;
    memset(Inputs, 0, sizeof(Inputs));

    for (unsigned Index = 0; Index < 3U; Index++)
        assert(QcomKeyMakeGpioAt(&Test->descriptors[Index], Index + 1U,
                                 1U, Codes[Index], 0U) == 0);
    assert(QcomKeysInit(&Test->keys, Test->descriptors, 3U,
                        ReadInput, NULL) == 0);
    kshim_framebuffer_config_t Config = {
        .render_address = (uintptr_t)(Test->storage + Test->guard),
        .width = Width,
        .height = Height,
        .stride = Stride,
        .bpp = Bpp,
        .format = Format,
        .foreground = Bpp == 1U ? 1U : UINT32_MAX,
        .background = 0U,
        .buffer_size = Test->frame_bytes,
    };
    assert(kshim_fb_init(&Test->framebuffer, &Config) == KSHIM_FB_OK);
    assert(kshim_fb_clear(&Test->framebuffer) == KSHIM_FB_OK);
    assert(kshim_lvgl_init(&Test->ui, &Test->framebuffer, &Test->keys) == 0);
    assert(kshim_lvgl_ready(&Test->ui));
}

static uint64_t FrameHash(const test_ui_t *Test)
{
    const uint8_t *Frame = Test->storage + Test->guard;
    uint64_t Hash = UINT64_C(14695981039346656037);
    unsigned Changed = 0U;
    for (unsigned Y = 0; Y < Test->height; Y++) {
        for (unsigned X = 0; X < Test->content_bytes; X++) {
            uint8_t Value = Frame[(size_t)Y * Test->framebuffer.stride + X];
            Hash = (Hash ^ Value) * UINT64_C(1099511628211);
            Changed += Value != 0U;
        }
    }
    assert(Changed > Test->height);
    return Hash;
}

static void CheckGuards(const test_ui_t *Test)
{
    for (unsigned Index = 0; Index < Test->guard; Index++) {
        assert(Test->storage[Index] == 0xa5);
        assert(Test->storage[Test->guard + Test->frame_bytes + Index] == 0xa5);
    }
    const uint8_t *Frame = Test->storage + Test->guard;
    for (unsigned Y = 0; Y < Test->height; Y++)
        for (unsigned X = Test->content_bytes;
             X < Test->framebuffer.stride; X++)
            assert(Frame[(size_t)Y * Test->framebuffer.stride + X] == 0U);
}

static void DestroyUi(test_ui_t *Test)
{
    kshim_lvgl_deinit(&Test->ui);
    assert(!kshim_lvgl_ready(&Test->ui));
    CheckGuards(Test);
    free(Test->storage);
}

static void TestFormats(void)
{
    uint64_t Hashes[6] = {0};
    for (int Format = KSHIM_FB_FORMAT_MONO1_LSB;
         Format <= KSHIM_FB_FORMAT_ARGB8888; Format++) {
        test_ui_t Test;
        InitStableUi(&Test, 320U, 240U, (kshim_fb_format_t)Format);
        Advance(&Test.ui, KSHIM_UI_ENTRY_DURATION_MS + 64U);
        Hashes[Format - 1] = FrameHash(&Test);
        assert(Test.ui.TextureWidth > 0U && Test.ui.TextureHeight > 0U);
        assert(lv_group_get_obj_count(Test.ui.Group) == DEFAULT_ENTRY_COUNT);
        DestroyUi(&Test);
    }
    assert(Hashes[0] != Hashes[2]);
    assert(Hashes[3] != Hashes[4]);
}

static void TestDefaultKeys(void)
{
    test_ui_t Test;
    InitStableUi(&Test, 640U, 360U, KSHIM_FB_FORMAT_ARGB8888);
    lv_group_t *Group = Test.ui.Group;
    lv_obj_t *First = lv_group_get_obj_by_index(Group, 0U);
    lv_obj_t *Second = lv_group_get_obj_by_index(Group, 1U);
    assert(lv_group_get_focused(Group) == First);
    assert(kshim_lvgl_take_selection(&Test.ui) == KSHIM_MENU_NONE);
    assert(kshim_lvgl_take_index(&Test.ui) == -1);

    PressKey(&Test.ui, 1U);
    assert(lv_group_get_focused(Group) == Second);
    lv_obj_t *SecondLabel = lv_obj_get_child(Second, 0U);
    const lv_anim_t *Focus = lv_anim_get(SecondLabel, NULL);
    if (KSHIM_MENU_IS_NATIVE) {
        assert(Focus == NULL);
        assert(lv_obj_has_state(Second, LV_STATE_CHECKED));
        assert(!lv_obj_has_state(First, LV_STATE_CHECKED));
    } else assert(Focus != NULL && Focus->duration == KSHIM_UI_FOCUS_DURATION_MS);

    PressKey(&Test.ui, 2U);
    assert(kshim_lvgl_take_selection(&Test.ui) == DEFAULT_SECOND_ACTION);
    assert(kshim_lvgl_take_selection(&Test.ui) == KSHIM_MENU_NONE);
    assert(kshim_lvgl_take_index(&Test.ui) == 1);
    assert(kshim_lvgl_take_index(&Test.ui) == -1);
    const lv_anim_t *Confirm = lv_anim_get(Test.ui.BootButton, NULL);
    if (KSHIM_MENU_IS_NATIVE) assert(Confirm == NULL);
    else assert(Confirm != NULL &&
           Confirm->duration == KSHIM_UI_CONFIRM_DURATION_MS);

    PressKey(&Test.ui, 0U);
    assert(lv_group_get_focused(Group) == First);
    PressKey(&Test.ui, 2U);
    assert(kshim_lvgl_take_selection(&Test.ui) == KSHIM_MENU_BOOT);
    DestroyUi(&Test);
}

static void Touch(kshim_lvgl_t *Ui, lv_obj_t *Object)
{
    lv_area_t Area;
    lv_obj_get_coords(Object, &Area);
    kshim_touch_event_t Event = {
        .type = KSHIM_TOUCH_EVENT_PRESS,
        .contact_id = 0U,
        .x = (uint32_t)((Area.x1 + Area.x2) / 2),
        .y = (uint32_t)((Area.y1 + Area.y2) / 2),
    };
    kshim_lvgl_touch(Ui, &Event);
    Advance(Ui, 48U);
    Event.type = KSHIM_TOUCH_EVENT_RELEASE;
    kshim_lvgl_touch(Ui, &Event);
    Advance(Ui, 48U);
}

static void TestDynamicAndTouch(void)
{
    test_ui_t Test;
    char Storage[KSHIM_MENU_MAX_ENTRIES][24];
    const char *Names[KSHIM_MENU_MAX_ENTRIES];
    for (size_t Index = 0; Index < KSHIM_MENU_MAX_ENTRIES; Index++) {
        int Length = snprintf(Storage[Index], sizeof(Storage[Index]),
                              "Image %02zu", Index);
        assert(Length > 0 && (size_t)Length < sizeof(Storage[Index]));
        Names[Index] = Storage[Index];
    }

    InitStableUi(&Test, 640U, 360U, KSHIM_FB_FORMAT_XRGB8888);
    assert(kshim_lvgl_set_entries(&Test.ui, Names,
                                  KSHIM_MENU_MAX_ENTRIES, 37U) == 0);
    assert(lv_group_get_obj_count(Test.ui.Group) == KSHIM_MENU_MAX_ENTRIES);
    assert(lv_group_get_focused(Test.ui.Group) ==
           lv_group_get_obj_by_index(Test.ui.Group, 37U));
    assert(kshim_lvgl_set_entries(&Test.ui, Names, 0U, 0U) != 0);
    assert(kshim_lvgl_set_entries(&Test.ui, Names,
                                  KSHIM_MENU_MAX_ENTRIES, KSHIM_MENU_MAX_ENTRIES) != 0);

    PressKey(&Test.ui, 1U);
    assert(lv_group_get_focused(Test.ui.Group) ==
           lv_group_get_obj_by_index(Test.ui.Group, 38U));
    PressKey(&Test.ui, 2U);
    assert(kshim_lvgl_take_index(&Test.ui) == 38);
    assert(kshim_lvgl_take_selection(&Test.ui) == KSHIM_MENU_NONE);

    lv_obj_t *Focused = lv_group_get_focused(Test.ui.Group);
    Touch(&Test.ui, Focused);
    assert(kshim_lvgl_take_index(&Test.ui) == -1);
    Touch(&Test.ui, Test.ui.BootButton);
    assert(kshim_lvgl_take_index(&Test.ui) == 38);

    lv_area_t ListArea;
    lv_area_t FocusedArea;
    lv_obj_get_coords(Test.ui.List, &ListArea);
    lv_obj_get_coords(Focused, &FocusedArea);
    assert(FocusedArea.y2 >= ListArea.y1 && FocusedArea.y1 <= ListArea.y2);

    lv_obj_send_event(Focused, LV_EVENT_PRESSED, NULL);
    const lv_anim_t *Press = lv_anim_get(Focused, NULL);
    if (KSHIM_MENU_IS_NATIVE) {
        /* The native theme owns pressed feedback; no kShim transform is installed. */
        assert(Press == NULL);
        assert(lv_obj_get_style_transform_scale_x(Focused, LV_PART_MAIN) == 256);
    } else {
        assert(Press != NULL && Press->duration == KSHIM_UI_PRESS_DURATION_MS);
    }
    DestroyUi(&Test);
}

static void TestBatchedTouchAndCancel(void)
{
    test_ui_t Test;
    kshim_touch_t TouchState;
    const char *Names[] = {"Linux", "UEFI"};
    InitStableUi(&Test, 640U, 360U, KSHIM_FB_FORMAT_XRGB8888);
    assert(kshim_lvgl_set_entries(&Test.ui, Names, 2, 0) == 0);
    Advance(&Test.ui, 500U);
    kshim_touch_config_t Config = {
        .source_width = 640, .source_height = 360,
        .output_width = 640, .output_height = 360,
        .emit = kshim_lvgl_touch, .emit_context = &Test.ui,
    };
    assert(kshim_touch_init(&TouchState, &Config) == 0);
    lv_area_t Area;
    lv_obj_t *Second = lv_group_get_obj_by_index(Test.ui.Group, 1);
    lv_obj_get_coords(Second, &Area);
    uint32_t X = (uint32_t)((Area.x1 + Area.x2) / 2);
    uint32_t Y = (uint32_t)((Area.y1 + Area.y2) / 2);
    /* Exactly the callback sequence from one ST FIFO read, with no frame
     * between ENTER and LEAVE. The old sampled pointer loses this tap. */
    assert(kshim_touch_update(&TouchState, 0, KSHIM_TOUCH_CONTACT_ENTER, X, Y) == 0);
    assert(kshim_touch_update(&TouchState, 0, KSHIM_TOUCH_CONTACT_LEAVE, X, Y) == 0);
    if(lv_group_get_focused(Test.ui.Group) != Second) {
        lv_area_t l,p,b;lv_obj_get_coords(Test.ui.List,&l);
        lv_obj_get_coords(lv_obj_get_parent(Test.ui.List),&p);lv_obj_get_coords(Test.ui.BootButton,&b);
        fprintf(stderr,"TOUCH %s XY=%u,%u row=%ld,%ld..%ld,%ld list=%ld,%ld..%ld,%ld panel=%ld,%ld boot=%ld,%ld scroll=%ld\n",mMenuStyle.Name,X,Y,(long)Area.x1,(long)Area.y1,(long)Area.x2,(long)Area.y2,(long)l.x1,(long)l.y1,(long)l.x2,(long)l.y2,(long)p.x1,(long)p.y1,(long)b.x1,(long)b.y1,(long)lv_obj_get_scroll_y(Test.ui.List));
    }
    assert(lv_group_get_focused(Test.ui.Group) == Second);
    assert(kshim_lvgl_take_index(&Test.ui) == -1);

    lv_obj_get_coords(Test.ui.BootButton, &Area);
    X = (uint32_t)((Area.x1 + Area.x2) / 2);
    Y = (uint32_t)((Area.y1 + Area.y2) / 2);
    assert(kshim_touch_update(&TouchState, 0, KSHIM_TOUCH_CONTACT_ENTER, X, Y) == 0);
    kshim_touch_cancel(&TouchState); /* NACK/reset/shutdown must not boot. */
    Advance(&Test.ui, 48U);
    assert(kshim_lvgl_take_index(&Test.ui) == -1);
    assert(Test.ui.PointerPressed == 0);
    assert(!lv_obj_has_state(Test.ui.BootButton, LV_STATE_PRESSED));

    assert(kshim_touch_update(&TouchState, 0, KSHIM_TOUCH_CONTACT_ENTER, X, Y) == 0);
    assert(kshim_touch_update(&TouchState, 0, KSHIM_TOUCH_CONTACT_LEAVE, X, Y) == 0);
    assert(kshim_lvgl_take_index(&Test.ui) == 1);
    DestroyUi(&Test);
}

/* With scratch memory the menu inflates Noto Sans CJK and draws CJK labels
 * with real glyphs; without it (every other test) it falls back to Latin. */
static void TestCjkFont(void)
{
    const size_t Bytes = 4U << 20;
    void *Scratch = aligned_alloc(64, Bytes);
    const void *Font;
    test_ui_t Test;
    static const char *const Names[] = {"Android 启动", "恢复模式", "ひらがな"};
    static const uint32_t Glyphs[] = {0x542f, 0x6062, 0x3072, 0x0041};

    assert(Scratch != NULL);
    assert(kshim_ui_font_size() > 800000U);
    assert(kshim_ui_font_inflate(Scratch, 1024U, &Font) == -2 && Font == NULL);
    assert(kshim_ui_font_inflate(Scratch, Bytes, &Font) == 0 && Font == Scratch);
    assert(memcmp(Scratch, "OTTO", 4) == 0);   /* CFF OpenType */

    kshim_lvgl_set_scratch(Scratch, Bytes);
    InitStableUi(&Test, 1920U, 1080U, KSHIM_FB_FORMAT_ARGB8888);
    assert(kshim_lvgl_has_cjk_font(&Test.ui));
    assert(kshim_lvgl_set_entries(&Test.ui, Names, 3U, 0U) == 0);
    Advance(&Test.ui, KSHIM_UI_ENTRY_DURATION_MS + 64U);
    lv_obj_t *Label = lv_obj_get_child(lv_obj_get_child(Test.ui.List, 0), 0);
    assert(Label != NULL);
    const lv_font_t *RowFont = lv_obj_get_style_text_font(Label, LV_PART_MAIN);
    assert(RowFont != &lv_font_montserrat_20);
    for (size_t Index = 0; Index < sizeof(Glyphs) / sizeof(Glyphs[0]); Index++) {
        lv_font_glyph_dsc_t Glyph;
        assert(lv_font_get_glyph_dsc(RowFont, &Glyph, Glyphs[Index], 0));
        assert(Glyph.box_w > 4U && Glyph.box_h > 4U);
    }
    DestroyUi(&Test);

    /* Too little scratch for the font: Latin fallback, still a menu. */
    kshim_lvgl_set_scratch(Scratch, 64U * 1024U);
    InitStableUi(&Test, 320U, 240U, KSHIM_FB_FORMAT_ARGB8888);
    assert(!kshim_lvgl_has_cjk_font(&Test.ui));
    DestroyUi(&Test);
    kshim_lvgl_set_scratch(NULL, 0U);
    free(Scratch);
}

int main(void)
{
    TestFormats();
    TestDefaultKeys();
    TestDynamicAndTouch();
    TestBatchedTouchAndCancel();
    TestCjkFont();
    puts("LVGL color formats, 51-entry menu, key mapping, touch confirmation and motion tests passed");
    return 0;
}
