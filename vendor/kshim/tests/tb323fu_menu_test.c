/* tests/tb323fu_menu_test.c
 * Real LVGL tests for CONFIG_KSHIM_MENU_CHINESE with either
 * CONFIG_KSHIM_MENU_STYLE_SURFACE_DARK or CONFIG_KSHIM_MENU_STYLE_STAR_CHART.
 * Style-specific assertions are selected at compile time via
 * CONFIG_KSHIM_MENU_STYLE_STAR_CHART; the default branch keeps the original
 * Surface geometry unmodified. Compiled and run by the remote Docker build;
 * do not execute locally.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include <lvgl_port.h>
#include <menu_text.h>
#include <menu_locale.h>
#include "framebuffer.h"

/* ------------------------------------------------------------------ fixture */

#define FB_W 1904U
#define FB_H 3040U

#if !defined(KSHIM_STANDALONE) || !KSHIM_STANDALONE
_Static_assert(LV_MEM_SIZE == 512U * 1024U,
               "The firmware-sized TB323FU test must keep the 512 KiB LVGL heap");
#endif

static uint32_t Inputs[3];

static uint32_t ReadInput(void *Ctx, uint64_t Addr)
{
    (void)Ctx;
    assert(Addr >= 1U && Addr <= 3U);
    return Inputs[Addr - 1U];
}

static void Advance(kshim_lvgl_t *Ui, uint32_t Ms)
{
    while (Ms) {
        uint32_t S = Ms > 16U ? 16U : Ms;
        kshim_lvgl_frame(Ui, S);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
        /* Physical confirm must stay identity: no transformed intermediate buffer. */
        if (Ui->BootButton != NULL) {
            assert(lv_obj_get_style_transform_scale_x(Ui->BootButton, LV_PART_MAIN) == 256);
            assert(lv_obj_get_style_transform_scale_y(Ui->BootButton, LV_PART_MAIN) == 256);
        }
#endif
        Ms -= S;
    }
}

static uint8_t Scratch[4U << 20] __attribute__((aligned(64)));

static void FixtureInitSize(kshim_lvgl_t *Ui, kshim_framebuffer_t *Fb,
                           uint32_t **Pix, QcomKeyDescriptor Desc[3],
                           QcomKeys *Keys, uint32_t Width, uint32_t Height)
{
    size_t Bytes = (size_t)Width * Height * sizeof(uint32_t);
    *Pix = malloc(Bytes);
    assert(*Pix);
    memset(*Pix, 0, Bytes);
    kshim_framebuffer_config_t Cfg = {
        .render_address = (uintptr_t)*Pix,
        .width          = Width,
        .height         = Height,
        .stride         = Width * sizeof(uint32_t),
        .bpp            = 32U,
        .format         = KSHIM_FB_FORMAT_XRGB8888,
        .buffer_size    = Bytes,
    };
    assert(kshim_fb_init(Fb, &Cfg) == KSHIM_FB_OK);
    static const uint32_t Codes[3] = {
        QCOM_KEYCODE_VOLUMEUP, QCOM_KEYCODE_VOLUMEDOWN, QCOM_KEYCODE_POWER,
    };
    for (size_t I = 0; I < 3U; I++)
        assert(QcomKeyMakeGpioAt(&Desc[I], (uint32_t)(I + 1U), 1U, Codes[I], 0U) == 0);
    memset(Inputs, 0, sizeof(Inputs));
    assert(QcomKeysInit(Keys, Desc, 3U, ReadInput, NULL) == 0);
    kshim_lvgl_set_scratch(Scratch, sizeof(Scratch));
    assert(kshim_lvgl_init(Ui, Fb, Keys) == 0);
}

static void FixtureInit(kshim_lvgl_t *Ui, kshim_framebuffer_t *Fb,
                        uint32_t **Pix, QcomKeyDescriptor Desc[3],
                        QcomKeys *Keys)
{
    FixtureInitSize(Ui, Fb, Pix, Desc, Keys, FB_W, FB_H);
}

static void FixtureDeinit(kshim_lvgl_t *Ui, uint32_t *Pix)
{
    kshim_lvgl_deinit(Ui);
    free(Pix);
}

static void PressKey(kshim_lvgl_t *Ui, int Idx)
{
    assert(Ui->Keys && Idx >= 0 && Idx < Ui->Keys->Count);
    assert(Ui->Keys->Stable[Idx] == 0U);
    Inputs[Idx] = 1U;
    for (unsigned Waited = 0; Ui->Keys->Stable[Idx] != 1U && Waited < 1000U; Waited += 16U)
        Advance(Ui, 16U);
    assert(Ui->Keys->Stable[Idx] == 1U);
    Inputs[Idx] = 0U;
    /* LVGL first consumes its synthetic release; subsequent polls must also
     * finish physical release debouncing before another tap can start. */
    for (unsigned Waited = 0; Ui->Keys->Stable[Idx] != 0U && Waited < 1000U; Waited += 16U)
        Advance(Ui, 16U);
    assert(Ui->Keys->Stable[Idx] == 0U);
}

/* Recursively collect lv_label objects under Root into Out[0..Max-1].
 * Returns number collected. */
static int GatherLabels(lv_obj_t *Root, lv_obj_t **Out, int Max)
{
    int N = 0;
    if (lv_obj_check_type(Root, &lv_label_class) && N < Max)
        Out[N++] = Root;
    uint32_t C = lv_obj_get_child_count(Root);
    for (uint32_t I = 0; I < C && N < Max; I++) {
        int Sub = GatherLabels(lv_obj_get_child(Root, (int32_t)I),
                               Out + N, Max - N);
        N += Sub;
    }
    return N;
}

/* Find first label under Root whose text equals Needle. */
static lv_obj_t *FindLabel(lv_obj_t *Root, const char *Needle)
{
    lv_obj_t *Buf[512];
    int N = GatherLabels(Root, Buf, 512);
    for (int I = 0; I < N; I++) {
        const char *T = lv_label_get_text(Buf[I]);
        if (T && strcmp(T, Needle) == 0)
            return Buf[I];
    }
    return NULL;
}

/* Selected headings can repeat row and button names; exclude both subtrees. */
static lv_obj_t *FindChromeLabel(const kshim_lvgl_t *Ui, const char *Needle)
{
    lv_obj_t *Buf[512];
    int N = GatherLabels(lv_screen_active(), Buf, 512);
    for (int I = 0; I < N; I++) {
        lv_obj_t *Ancestor = Buf[I];
        while (Ancestor && Ancestor != Ui->List && Ancestor != Ui->BootButton)
            Ancestor = lv_obj_get_parent(Ancestor);
        if (!Ancestor && strcmp(lv_label_get_text(Buf[I]), Needle) == 0)
            return Buf[I];
    }
    return NULL;
}

/* Assert every non-ASCII UTF-8 codepoint in Text has a real glyph in
 * the style font of Label (no placeholder). */
static void CheckGlyphs(lv_obj_t *Label, const char *Text)
{
    const lv_font_t *F = lv_obj_get_style_text_font(Label, LV_PART_MAIN);
    assert(F);
    const uint8_t *P = (const uint8_t *)Text;
    while (*P) {
        uint32_t Cp;
        uint8_t B = *P;
        if (B < 0x80U) { P++; continue; }
        if      ((B & 0xE0U) == 0xC0U) {
            Cp = (uint32_t)(B & 0x1FU) << 6 | (P[1] & 0x3FU); P += 2;
        } else if ((B & 0xF0U) == 0xE0U) {
            Cp = (uint32_t)(B & 0x0FU) << 12
               | (uint32_t)(P[1] & 0x3FU) << 6
               | (P[2] & 0x3FU);
            P += 3;
        } else {
            Cp = (uint32_t)(B & 0x07U) << 18
               | (uint32_t)(P[1] & 0x3FU) << 12
               | (uint32_t)(P[2] & 0x3FU) << 6
               | (P[3] & 0x3FU);
            P += 4;
        }
        lv_font_glyph_dsc_t Dsc;
        assert(lv_font_get_glyph_dsc(F, &Dsc, Cp, 0U) && !Dsc.is_placeholder);
    }
}

/* ----------------------------------------------------------------- entries */

static const char *const ProductEntries[6] = {
    "原厂 Android",
    "机身 ArmadaOS",
    "TF 卡 ArmadaOS",
    "高级启动模式",
    "重启",
    "关机",
};

#define PRODUCT_ENTRY_COUNT (sizeof(ProductEntries) / sizeof(ProductEntries[0]))

static const char *ProductDescriptions[PRODUCT_ENTRY_COUNT];

static const char *ProductActions[PRODUCT_ENTRY_COUNT];

static const char *const AdvancedEntries[5] = {
    "Linux 诊断模式", "Recovery 恢复模式", "Fastboot 模式", "9008 下载模式", "返回主菜单",
};

#define ADVANCED_ENTRY_COUNT (sizeof(AdvancedEntries) / sizeof(AdvancedEntries[0]))

static const char *AdvancedDescriptions[ADVANCED_ENTRY_COUNT];

static const char *AdvancedActions[ADVANCED_ENTRY_COUNT];

/* KSHIM_MENU_DESC_* / KSHIM_MENU_ACTION_* are runtime functions, so the
 * entry metadata cannot be initialised statically. Assign each macro to its
 * matching index once, before any test runs. */
static void InitEntryText(void)
{
    ProductDescriptions[0] = KSHIM_MENU_DESC_ANDROID;
    ProductDescriptions[1] = KSHIM_MENU_DESC_INTERNAL;
    ProductDescriptions[2] = KSHIM_MENU_DESC_TF;
    ProductDescriptions[3] = KSHIM_MENU_DESC_ADVANCED;
    ProductDescriptions[4] = KSHIM_MENU_DESC_RESTART;
    ProductDescriptions[5] = KSHIM_MENU_DESC_OFF;

    ProductActions[0] = KSHIM_MENU_ACTION_BOOT;
    ProductActions[1] = KSHIM_MENU_ACTION_BOOT;
    ProductActions[2] = KSHIM_MENU_ACTION_BOOT;
    ProductActions[3] = KSHIM_MENU_ACTION_ENTER;
    ProductActions[4] = KSHIM_MENU_ACTION_RESTART;
    ProductActions[5] = KSHIM_MENU_ACTION_OFF;

    AdvancedDescriptions[0] = KSHIM_MENU_DESC_DIAGNOSTIC;
    AdvancedDescriptions[1] = KSHIM_MENU_DESC_RECOVERY;
    AdvancedDescriptions[2] = KSHIM_MENU_DESC_FASTBOOT;
    AdvancedDescriptions[3] = KSHIM_MENU_DESC_EDL;
    AdvancedDescriptions[4] = KSHIM_MENU_DESC_BACK;

    AdvancedActions[0] = KSHIM_MENU_ACTION_BOOT;
    AdvancedActions[1] = KSHIM_MENU_ACTION_ENTER;
    AdvancedActions[2] = KSHIM_MENU_ACTION_ENTER;
    AdvancedActions[3] = KSHIM_MENU_ACTION_ENTER;
    AdvancedActions[4] = KSHIM_MENU_ACTION_BACK;
}

static void CheckSelectedAction(const kshim_lvgl_t *Ui, const char *Action)
{
    lv_obj_t *Labels[4];
    int Count = GatherLabels(Ui->BootButton, Labels, 4);
    assert(Count == 1);
    assert(strcmp(lv_label_get_text(Labels[0]), Action) == 0);
    assert(!lv_obj_has_flag(Labels[0], LV_OBJ_FLAG_HIDDEN));
    CheckGlyphs(Labels[0], Action);
}

static int32_t CheckNativePanel(const kshim_lvgl_t *Ui)
{
    lv_obj_t *Panel = lv_obj_get_parent(Ui->List);
    assert(Panel);
    lv_area_t Area;
    lv_obj_get_coords(Panel, &Area);
    int32_t Height = lv_obj_get_height(Panel);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    /* STAR panel is near fullscreen and symmetric; it must stay inside the
     * logical viewport. */
    assert(Height * 4 > (int32_t)Ui->LogicalHeight * 3);
    assert(Area.x1 >= 0 && Area.y1 >= 0);
    assert(Area.x2 < (int32_t)Ui->LogicalWidth);
    assert(Area.y2 < (int32_t)Ui->LogicalHeight);
#else
    assert(Height * 4 < (int32_t)Ui->LogicalHeight * 3);
#endif
    int32_t TopSpace = Area.y1;
    int32_t BottomSpace = (int32_t)Ui->LogicalHeight - 1 - Area.y2;
    assert(abs(TopSpace - BottomSpace) <= 1);
    return Height;
}

static void CheckSelectedDescription(const kshim_lvgl_t *Ui,
                                     const char *Name, const char *Description)
{
    lv_obj_t *Heading = FindChromeLabel(Ui, Name);
    lv_obj_t *Detail = FindChromeLabel(Ui, Description);
    assert(Heading && Detail && Heading != Detail);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    if (Ui->LogicalHeight < 450U) {
        /* STAR compact fallback hides heading and detail; they must still be
         * distinct objects carrying the requested text with valid glyphs. */
        assert(lv_obj_has_flag(Heading, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(Detail, LV_OBJ_FLAG_HIDDEN));
        assert(strcmp(lv_label_get_text(Heading), Name) == 0);
        assert(strcmp(lv_label_get_text(Detail), Description) == 0);
        CheckGlyphs(Heading, Name);
        CheckGlyphs(Detail, Description);
        return;
    }
#endif
    assert(!lv_obj_has_flag(Heading, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(Detail, LV_OBJ_FLAG_HIDDEN));
    CheckGlyphs(Heading, Name);
    CheckGlyphs(Detail, Description);
    lv_area_t NavArea, HeadingArea, DetailArea, BootArea;
    lv_obj_get_coords(Ui->List, &NavArea);
    lv_obj_get_coords(Heading, &HeadingArea);
    lv_obj_get_coords(Detail, &DetailArea);
    lv_obj_get_coords(Ui->BootButton, &BootArea);
    assert(HeadingArea.x1 > NavArea.x2);
    assert(DetailArea.x1 == HeadingArea.x1);
    assert(DetailArea.y1 > HeadingArea.y2);
    assert(DetailArea.y2 < BootArea.y1);
}

static void CheckFooter(const kshim_lvgl_t *Ui)
{
    lv_obj_t *Help = FindChromeLabel(Ui, KSHIM_MENU_TEXT_HELP);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    if (Ui->LogicalHeight < 450U) {
        /* STAR compact fallback: footer labels exist but are hidden; boot stays
         * fixed below the scrolling list and both remain inside the viewport. */
        assert(Help && lv_obj_has_flag(Help, LV_OBJ_FLAG_HIDDEN));
        CheckGlyphs(Help, KSHIM_MENU_TEXT_HELP);
        lv_obj_t *Credit = FindChromeLabel(Ui, KSHIM_MENU_TEXT_CREDIT);
        lv_obj_t *Language = FindChromeLabel(Ui, KSHIM_MENU_TEXT_LANGUAGE_STATE);
        assert(Credit && lv_obj_has_flag(Credit, LV_OBJ_FLAG_HIDDEN));
        assert(Language && lv_obj_has_flag(Language, LV_OBJ_FLAG_HIDDEN));
        lv_area_t NavArea, BootArea;
        lv_obj_get_coords(Ui->List, &NavArea);
        lv_obj_get_coords(Ui->BootButton, &BootArea);
        assert(NavArea.x1 >= 0 && NavArea.y1 >= 0);
        assert(NavArea.x2 < (int32_t)Ui->LogicalWidth);
        assert(NavArea.y2 < (int32_t)Ui->LogicalHeight);
        assert(BootArea.x1 >= 0 && BootArea.y1 >= 0);
        assert(BootArea.x2 < (int32_t)Ui->LogicalWidth);
        assert(BootArea.y2 < (int32_t)Ui->LogicalHeight);
        assert(BootArea.y1 > NavArea.y2);
        return;
    }
#endif
    assert(Help && !lv_obj_has_flag(Help, LV_OBJ_FLAG_HIDDEN));
    CheckGlyphs(Help, KSHIM_MENU_TEXT_HELP);
    lv_area_t NavArea, HelpArea, BootArea;
    lv_obj_get_coords(Ui->List, &NavArea);
    lv_obj_get_coords(Help, &HelpArea);
    lv_obj_get_coords(Ui->BootButton, &BootArea);
    assert(HelpArea.y1 > NavArea.y2);
    assert(HelpArea.x1 == NavArea.x1 && HelpArea.x2 < BootArea.x1);
    assert(HelpArea.x1 >= 0 && HelpArea.y1 >= 0);
    assert(HelpArea.x2 < (int32_t)Ui->LogicalWidth);
    assert(HelpArea.y2 < (int32_t)Ui->LogicalHeight);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    /* STAR native footer also carries credit and language-state labels. */
    lv_obj_t *Credit = FindChromeLabel(Ui, KSHIM_MENU_TEXT_CREDIT);
    lv_obj_t *Language = FindChromeLabel(Ui, KSHIM_MENU_TEXT_LANGUAGE_STATE);
    assert(Credit && !lv_obj_has_flag(Credit, LV_OBJ_FLAG_HIDDEN));
    assert(Language && !lv_obj_has_flag(Language, LV_OBJ_FLAG_HIDDEN));
    CheckGlyphs(Credit, KSHIM_MENU_TEXT_CREDIT);
    CheckGlyphs(Language, KSHIM_MENU_TEXT_LANGUAGE_STATE);
    lv_area_t CreditArea, LanguageArea;
    lv_obj_get_coords(Credit, &CreditArea);
    lv_obj_get_coords(Language, &LanguageArea);
    assert(CreditArea.x1 >= 0 && CreditArea.y1 >= 0);
    assert(CreditArea.x2 < (int32_t)Ui->LogicalWidth);
    assert(CreditArea.y2 < (int32_t)Ui->LogicalHeight);
    assert(CreditArea.y1 > HelpArea.y2);
    assert(LanguageArea.x1 >= 0 && LanguageArea.y1 >= 0);
    assert(LanguageArea.x2 < (int32_t)Ui->LogicalWidth);
    assert(LanguageArea.y2 < (int32_t)Ui->LogicalHeight);
    lv_obj_t *Title = FindLabel(lv_screen_active(), "KShimC 启动器");
    if (Title) {
        lv_area_t TitleArea;
        lv_obj_get_coords(Title, &TitleArea);
        assert(LanguageArea.x1 > TitleArea.x2 || LanguageArea.x2 < TitleArea.x1 || LanguageArea.y1 > TitleArea.y2 || LanguageArea.y2 < TitleArea.y1);
    }
#endif
    lv_obj_t *RowLabel = FindLabel(Ui->List, Ui->Entries[0]);
    assert(RowLabel);
    const lv_font_t *HelpFont = lv_obj_get_style_text_font(Help, LV_PART_MAIN);
    const lv_font_t *RowFont = lv_obj_get_style_text_font(RowLabel, LV_PART_MAIN);
    assert(HelpFont && RowFont && HelpFont->line_height <= RowFont->line_height);
}

/* ------------------------------------------------------------ test_labels */

static void test_labels(void)
{
    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInit(&Ui, &Fb, &Pix, Desc, &Keys);
    assert(kshim_lvgl_has_cjk_font(&Ui));
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, ProductActions, PRODUCT_ENTRY_COUNT, 0U) == 0);
    Advance(&Ui, 400U);

    lv_obj_t *Screen = lv_screen_active();

    /* The firmware profile rotates this physical scanout into landscape. */
    assert(Fb.width == 1904U && Fb.height == 3040U);
    assert(Fb.stride == 7616U);
    assert(Ui.Rotation == KSHIM_FB_ROTATION_CCW_270);
    assert(lv_display_get_horizontal_resolution(Ui.Display) == 3040);
    assert(lv_display_get_vertical_resolution(Ui.Display) == 1904);

    /* The complete product title must survive layout and font rendering. */
    lv_obj_t *Title = FindLabel(Screen, "KShimC 启动器");
    assert(Title);
    CheckGlyphs(Title, lv_label_get_text(Title));

    /* BootButton child label: 启动 */
    lv_obj_t *Confirm = FindLabel(Ui.BootButton, "启动");
    assert(Confirm);
    CheckGlyphs(Confirm, lv_label_get_text(Confirm));

    lv_obj_t *Detail = FindChromeLabel(&Ui, KSHIM_MENU_DESC_ANDROID);
    assert(Detail && !lv_obj_has_flag(Detail, LV_OBJ_FLAG_HIDDEN));
    CheckGlyphs(Detail, lv_label_get_text(Detail));
    lv_area_t NavArea, DetailArea, BootArea, TitleArea;
    lv_obj_get_coords(Ui.List, &NavArea);
    lv_obj_get_coords(Detail, &DetailArea);
    lv_obj_get_coords(Ui.BootButton, &BootArea);
    lv_obj_get_coords(Title, &TitleArea);
    assert(NavArea.x2 < DetailArea.x1);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    /* STAR right-aligns the boot button with the detail text. */
    assert(BootArea.x1 > DetailArea.x1);
    assert(BootArea.x2 == DetailArea.x2);
#else
    assert(BootArea.x1 == DetailArea.x1);
#endif
    assert(BootArea.y1 > DetailArea.y2);
    assert(TitleArea.y2 < NavArea.y1);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    /* STAR reserves left feather space, so the title starts right of the nav
     * list while staying inside the logical viewport. */
    assert(TitleArea.x1 > NavArea.x1);
    assert(TitleArea.x1 >= 0 && TitleArea.y1 >= 0);
    assert(TitleArea.x2 < (int32_t)Ui.LogicalWidth);
    assert(TitleArea.y2 < (int32_t)Ui.LogicalHeight);
#else
    assert(TitleArea.x1 == NavArea.x1);
#endif
    assert(NavArea.x2 - NavArea.x1 < TitleArea.x2 - NavArea.x1);
    CheckSelectedDescription(&Ui, ProductEntries[0], ProductDescriptions[0]);
    CheckSelectedAction(&Ui, ProductActions[0]);
    CheckFooter(&Ui);
    (void)CheckNativePanel(&Ui);

    /* Physical key focus must keep Surface's muted row and left accent;
     * the keyboard-specific state must not add a rectangular outline. */
    PressKey(&Ui, 1);
    Advance(&Ui, 400U);
    lv_obj_t *Focused = lv_group_get_focused(Ui.Group);
    assert(Focused == lv_group_get_obj_by_index(Ui.Group, 1U));
    assert(lv_obj_has_state(Focused, LV_STATE_FOCUS_KEY));
    lv_color_t FocusColor = lv_obj_get_style_bg_color(Focused, LV_PART_MAIN);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    assert(FocusColor.red == 0x22U && FocusColor.green == 0x3eU && FocusColor.blue == 0x63U);
#else
    assert(FocusColor.red == 0x17U && FocusColor.green == 0x3bU && FocusColor.blue == 0x55U);
#endif
    assert(lv_obj_get_style_outline_width(Focused, LV_PART_MAIN) == 0);
    assert(!lv_obj_has_state(Focused, LV_STATE_PRESSED));
    CheckSelectedDescription(&Ui, ProductEntries[1], ProductDescriptions[1]);
    CheckSelectedAction(&Ui, ProductActions[1]);

    /* All six main-menu entry labels must appear with full glyph coverage. */
    for (size_t I = 0; I < PRODUCT_ENTRY_COUNT; I++) {
        lv_obj_t *L = FindLabel(Screen, ProductEntries[I]);
        assert(L);
        CheckGlyphs(L, lv_label_get_text(L));
        lv_area_t Row;
        lv_obj_get_coords(lv_group_get_obj_by_index(Ui.Group, (uint32_t)I), &Row);
        assert(Row.y1 >= NavArea.y1 && Row.y2 <= NavArea.y2);
    }

    /* Errors and notices must not replace the selected heading or its detail. */
    lv_obj_t *Heading = FindChromeLabel(&Ui, ProductEntries[1]);
    Detail = FindChromeLabel(&Ui, ProductDescriptions[1]);
    static const char *const StatusTexts[] = {
        "音量键选择，电源键启动",
        "音量键暂不可用",
        "已取消自动启动，请手动选择",
        "电源操作失败",
        "切换启动模式失败",
        "切换启动模式失败，恢复原设置失败",
        "重启", "关机",
    };
    for (size_t I = 0; I < sizeof(StatusTexts) / sizeof(StatusTexts[0]); I++) {
        kshim_lvgl_set_status(&Ui, StatusTexts[I]);
        Advance(&Ui, 16U);
        lv_obj_t *Status = FindChromeLabel(&Ui, StatusTexts[I]);
        assert(Status && Status != Heading && Status != Detail);
        CheckGlyphs(Status, StatusTexts[I]);
        assert(strcmp(lv_label_get_text(Heading), ProductEntries[1]) == 0);
        assert(strcmp(lv_label_get_text(Detail), ProductDescriptions[1]) == 0);
        CheckSelectedAction(&Ui, ProductActions[1]);
        lv_area_t NoticeArea;
        lv_obj_get_coords(Status, &NoticeArea);
        lv_obj_get_coords(Detail, &DetailArea);
        assert(NoticeArea.y1 > DetailArea.y2 && NoticeArea.y2 < BootArea.y1);
        CheckFooter(&Ui);
    }

    /* Detail font: check glyphs on every label visible on screen */
    {
        lv_obj_t *Buf[512];
        int N = GatherLabels(Screen, Buf, 512);
        for (int I = 0; I < N; I++) {
            const char *T = lv_label_get_text(Buf[I]);
            if (T && *T)
                CheckGlyphs(Buf[I], T);
        }
    }

    FixtureDeinit(&Ui, Pix);
    puts("test_labels passed");
}

/* Countdown starts on a fresh, untouched menu, as it does at initial boot. */
static void test_countdown_glyphs(void)
{
    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInit(&Ui, &Fb, &Pix, Desc, &Keys);
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, ProductActions, PRODUCT_ENTRY_COUNT, 1U) == 0);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);
    lv_obj_t *Heading = FindChromeLabel(&Ui, ProductEntries[1]);
    lv_obj_t *Detail = FindChromeLabel(&Ui, ProductDescriptions[1]);
    assert(Heading && Detail);
    kshim_lvgl_set_countdown(&Ui, 8000U);
    Advance(&Ui, 16U);
    lv_obj_t *Countdown = FindChromeLabel(&Ui, "8 秒后启动机身 ArmadaOS");
    assert(Countdown);
    assert(Countdown != Heading && Countdown != Detail);
    CheckGlyphs(Countdown, lv_label_get_text(Countdown));
    assert(strcmp(lv_label_get_text(Heading), ProductEntries[1]) == 0);
    assert(strcmp(lv_label_get_text(Detail), ProductDescriptions[1]) == 0);
    CheckSelectedDescription(&Ui, ProductEntries[1], ProductDescriptions[1]);
    CheckSelectedAction(&Ui, ProductActions[1]);
    CheckFooter(&Ui);
    /* Programmatic focus alone does not change the default countdown target. */
    lv_group_focus_obj(lv_group_get_obj_by_index(Ui.Group, 2U));
    Advance(&Ui, 1000U);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);
    assert(strcmp(lv_label_get_text(Countdown), "7 秒后启动机身 ArmadaOS") == 0);
    CheckSelectedDescription(&Ui, ProductEntries[2], ProductDescriptions[2]);
    CheckSelectedAction(&Ui, ProductActions[2]);
    kshim_lvgl_set_countdown(&Ui, 0U);

    /* Rebuilding with a different default must also change the target name. */
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, ProductActions, PRODUCT_ENTRY_COUNT, 2U) == 0);
    kshim_lvgl_set_countdown(&Ui, Ui.ElapsedMs + 8000U);
    Advance(&Ui, 16U);
    Countdown = FindChromeLabel(&Ui, "8 秒后启动TF 卡 ArmadaOS");
    assert(Countdown);
    CheckGlyphs(Countdown, lv_label_get_text(Countdown));
    CheckSelectedDescription(&Ui, ProductEntries[2], ProductDescriptions[2]);
    FixtureDeinit(&Ui, Pix);
    puts("test_countdown_glyphs passed");
}

/* Metadata follows focus and page replacement, without matching label text. */
static void test_descriptions(void)
{
    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInit(&Ui, &Fb, &Pix, Desc, &Keys);
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, ProductActions, PRODUCT_ENTRY_COUNT, 0U) == 0);
    Advance(&Ui, 400U);
    for (size_t I = 0; I < PRODUCT_ENTRY_COUNT; I++) {
        if (I)
            PressKey(&Ui, 1);
        Advance(&Ui, 16U);
        assert(lv_group_get_focused(Ui.Group) ==
               lv_group_get_obj_by_index(Ui.Group, (uint32_t)I));
        assert(Ui.Descriptions[I] == ProductDescriptions[I]);
        CheckSelectedDescription(&Ui, ProductEntries[I], ProductDescriptions[I]);
        CheckSelectedAction(&Ui, ProductActions[I]);
        CheckFooter(&Ui);
    }
    assert(kshim_lvgl_set_entries_described(&Ui, AdvancedEntries,
        AdvancedDescriptions, AdvancedActions, ADVANCED_ENTRY_COUNT, 0U) == 0);
    Advance(&Ui, 400U);
    for (size_t I = 0; I < ADVANCED_ENTRY_COUNT; I++) {
        if (I)
            PressKey(&Ui, 1);
        Advance(&Ui, 16U);
        assert(Ui.Descriptions[I] == AdvancedDescriptions[I]);
        CheckSelectedDescription(&Ui, AdvancedEntries[I], AdvancedDescriptions[I]);
        CheckSelectedAction(&Ui, AdvancedActions[I]);
        CheckFooter(&Ui);
    }

    /* NULL and empty entries fall back independently; a supplied description
     * remains authoritative even if the display label suggests another type. */
    const char *const PartialDescriptions[] = {
        NULL, "", KSHIM_MENU_DESC_EDL,
    };
    const char *const PartialActions[] = {
        NULL, "", KSHIM_MENU_ACTION_BACK,
    };
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        PartialDescriptions, PartialActions, 3U, 0U) == 0);
    Advance(&Ui, 400U);
    for (size_t I = 0; I < 3U; I++) {
        lv_group_focus_obj(lv_group_get_obj_by_index(Ui.Group, (uint32_t)I));
        Advance(&Ui, 16U);
        CheckSelectedDescription(&Ui, ProductEntries[I], I == 2U ?
            KSHIM_MENU_DESC_EDL : KSHIM_MENU_DESC_GENERIC);
        CheckSelectedAction(&Ui, I == 2U ? KSHIM_MENU_ACTION_BACK : KSHIM_MENU_ACTION_BOOT);
    }

    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, NULL, PRODUCT_ENTRY_COUNT, 5U) == 0);
    Advance(&Ui, 400U);
    CheckSelectedDescription(&Ui, ProductEntries[5], ProductDescriptions[5]);
    CheckSelectedAction(&Ui, KSHIM_MENU_ACTION_BOOT);

    /* Legacy callers supply names only and must not inherit stale metadata. */
    assert(kshim_lvgl_set_entries(&Ui, ProductEntries, PRODUCT_ENTRY_COUNT, 1U) == 0);
    Advance(&Ui, 400U);
    for (size_t I = 0; I < PRODUCT_ENTRY_COUNT; I++) {
        assert(Ui.Descriptions[I] == NULL);
        assert(Ui.ActionLabels[I] == NULL);
        lv_group_focus_obj(lv_group_get_obj_by_index(Ui.Group, (uint32_t)I));
        Advance(&Ui, 16U);
        CheckSelectedDescription(&Ui, ProductEntries[I], KSHIM_MENU_DESC_GENERIC);
        CheckSelectedAction(&Ui, KSHIM_MENU_ACTION_BOOT);
    }
    CheckFooter(&Ui);
    FixtureDeinit(&Ui, Pix);
    puts("test_descriptions passed");
}

/* Exercise the actual LVGL display flush, including the native row stride. */
static void test_rotated_flush(void)
{
    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInit(&Ui, &Fb, &Pix, Desc, &Keys);
    assert(Ui.Rotation == KSHIM_FB_ROTATION_CCW_270);
    assert(Fb.width == 1904U && Fb.height == 3040U && Fb.stride == 7616U);
    assert(Ui.LogicalWidth == 3040U && Ui.LogicalHeight == 1904U);
    const uint32_t Colors[] = {0xff0000U, 0x00ff00U, 0x0000ffU, 0xffff00U};
    for (size_t I = 0; I < 4U; I++) {
        lv_obj_t *Corner = lv_obj_create(lv_layer_top());
        assert(Corner);
        lv_obj_remove_style_all(Corner);
        lv_obj_remove_flag(Corner, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(Corner, 16, 16);
        lv_obj_set_pos(Corner, I & 1U ? (int32_t)Ui.LogicalWidth - 16 : 0,
                       I & 2U ? (int32_t)Ui.LogicalHeight - 16 : 0);
        lv_obj_set_style_bg_color(Corner, lv_color_hex(Colors[I]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(Corner, LV_OPA_COVER, LV_PART_MAIN);
    }
    lv_refr_now(Ui.Display);
    const uint32_t PhysicalExpected[] = {0x0000ffU, 0xff0000U, 0xffff00U, 0x00ff00U};
    for (size_t I = 0; I < 4U; I++) {
        uint32_t X = I & 1U ? Fb.width - 8U : 8U;
        uint32_t Y = I & 2U ? Fb.height - 8U : 8U;
        const uint32_t *Row = (const uint32_t *)(const void *)(
            Fb.render_address + (size_t)Y * Fb.stride);
        assert((Row[X] & 0xffffffU) == PhysicalExpected[I]);
    }
    FixtureDeinit(&Ui, Pix);
    puts("test_rotated_flush passed");
}

/* -------------------------------------------------- test_has_interacted */

static void test_has_interacted(void)
{
    /* NULL is safe */
    assert(kshim_lvgl_has_interacted(NULL) == 0);

    /* Zero-initialised, not ready: safe and false */
    kshim_lvgl_t ZeroUi;
    memset(&ZeroUi, 0, sizeof(ZeroUi));
    assert(kshim_lvgl_has_interacted(&ZeroUi) == 0);

    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInit(&Ui, &Fb, &Pix, Desc, &Keys);

    /* Initial focus and setup are not physical interaction. */
    assert(Ui.Ready);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);

    /* After set_entries + programmatic focus + frame: still false */
    assert(kshim_lvgl_set_entries(&Ui, ProductEntries, PRODUCT_ENTRY_COUNT, 0U) == 0);
    lv_obj_t *Second = lv_group_get_obj_by_index(Ui.Group, 1U);
    assert(Second);
    lv_group_focus_obj(Second);
    Advance(&Ui, 16U);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);
    Advance(&Ui, 400U);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);

    /* Physical keys: VolumeUp=0, VolumeDown=1, Power=2 */
    for (int K = 0; K < 3; K++) {
        assert(kshim_lvgl_set_entries(&Ui, ProductEntries, PRODUCT_ENTRY_COUNT, 0U) == 0);
        assert(kshim_lvgl_has_interacted(&Ui) == 0);
        PressKey(&Ui, K);
        assert(kshim_lvgl_has_interacted(&Ui) != 0);
        /* successful set_entries resets the interaction flag */
        assert(kshim_lvgl_set_entries(&Ui, ProductEntries, PRODUCT_ENTRY_COUNT, 0U) == 0);
        assert(kshim_lvgl_has_interacted(&Ui) == 0);
    }

    /* Touch MOVE / RELEASE / CANCEL on fresh state must not mark */
    assert(kshim_lvgl_set_entries(&Ui, ProductEntries, PRODUCT_ENTRY_COUNT, 0U) == 0);
    kshim_touch_event_t Ev = {0};
    Ev.type = KSHIM_TOUCH_EVENT_MOVE;    Ev.x = FB_W / 2U; Ev.y = FB_H / 2U;
    kshim_lvgl_touch(&Ui, &Ev);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);
    Ev.type = KSHIM_TOUCH_EVENT_RELEASE;
    kshim_lvgl_touch(&Ui, &Ev);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);
    Ev.type = KSHIM_TOUCH_EVENT_CANCEL;
    kshim_lvgl_touch(&Ui, &Ev);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);

    /* Out-of-bounds PRESS (x == width, y == 0) must not mark */
    assert(kshim_lvgl_set_entries(&Ui, ProductEntries, PRODUCT_ENTRY_COUNT, 0U) == 0);
    Ev.type = KSHIM_TOUCH_EVENT_PRESS; Ev.x = FB_W; Ev.y = 0U;
    kshim_lvgl_touch(&Ui, &Ev);
    Advance(&Ui, 16U);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);

    /* CANCEL resets pointer state; subsequent in-bounds PRESS must mark */
    assert(kshim_lvgl_set_entries(&Ui, ProductEntries, PRODUCT_ENTRY_COUNT, 0U) == 0);
    Ev.type = KSHIM_TOUCH_EVENT_CANCEL; Ev.x = 0U; Ev.y = 0U;
    kshim_lvgl_touch(&Ui, &Ev);
    assert(kshim_lvgl_has_interacted(&Ui) == 0);
    Ev.type = KSHIM_TOUCH_EVENT_PRESS;  Ev.x = FB_W / 2U; Ev.y = FB_H / 2U;
    kshim_lvgl_touch(&Ui, &Ev);
    Advance(&Ui, 16U);
    assert(kshim_lvgl_has_interacted(&Ui) != 0);

    FixtureDeinit(&Ui, Pix);
    puts("test_has_interacted passed");
}

/* Both menu levels use the same UI without stale labels or hidden actions. */
static void test_menu_levels(void)
{
    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInit(&Ui, &Fb, &Pix, Desc, &Keys);
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, ProductActions, PRODUCT_ENTRY_COUNT, 3U) == 0);
    Advance(&Ui, 400U);
    CheckSelectedDescription(&Ui, ProductEntries[3], ProductDescriptions[3]);
    CheckSelectedAction(&Ui, ProductActions[3]);
    int32_t MainHeight = CheckNativePanel(&Ui);
    PressKey(&Ui, 2);
    assert(kshim_lvgl_take_index(&Ui) == 3);
    assert(kshim_lvgl_set_entries_described(&Ui, AdvancedEntries,
        AdvancedDescriptions, AdvancedActions, ADVANCED_ENTRY_COUNT, 0U) == 0);
    Advance(&Ui, 400U);
    CheckSelectedDescription(&Ui, AdvancedEntries[0], AdvancedDescriptions[0]);
    CheckSelectedAction(&Ui, AdvancedActions[0]);
    int32_t AdvancedHeight = CheckNativePanel(&Ui);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    /* STAR uses a fixed canvas, so both levels share the same panel height. */
    assert(AdvancedHeight == MainHeight);
#else
    assert(AdvancedHeight < MainHeight);
#endif
    assert(lv_group_get_obj_count(Ui.Group) == ADVANCED_ENTRY_COUNT);
    assert(FindLabel(lv_screen_active(), "KShimC 启动器"));
    assert(FindLabel(Ui.BootButton, "启动"));
    assert(!FindLabel(Ui.List, "原厂 Android"));
    for (size_t I = 0; I < ADVANCED_ENTRY_COUNT; I++) {
        lv_obj_t *Label = FindLabel(Ui.List, AdvancedEntries[I]);
        assert(Label);
        CheckGlyphs(Label, AdvancedEntries[I]);
    }
    for (size_t I = 1; I < ADVANCED_ENTRY_COUNT; I++) {
        PressKey(&Ui, 1);
        assert(lv_group_get_focused(Ui.Group) == lv_group_get_obj_by_index(Ui.Group, (uint32_t)I));
        assert(kshim_lvgl_take_index(&Ui) == -1);
        CheckSelectedDescription(&Ui, AdvancedEntries[I], AdvancedDescriptions[I]);
        CheckSelectedAction(&Ui, AdvancedActions[I]);
    }
    PressKey(&Ui, 2);
    assert(kshim_lvgl_take_index(&Ui) == (int)ADVANCED_ENTRY_COUNT - 1);
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, ProductActions, PRODUCT_ENTRY_COUNT, 3U) == 0);
    Advance(&Ui, 400U);
    assert(lv_group_get_obj_count(Ui.Group) == PRODUCT_ENTRY_COUNT);
    assert(lv_group_get_focused(Ui.Group) == lv_group_get_obj_by_index(Ui.Group, 3U));
    assert(!FindLabel(Ui.List, "返回主菜单"));
    assert(FindLabel(Ui.List, "高级启动模式"));
    CheckSelectedDescription(&Ui, ProductEntries[3], ProductDescriptions[3]);
    CheckSelectedAction(&Ui, ProductActions[3]);
    assert(CheckNativePanel(&Ui) == MainHeight);
    CheckFooter(&Ui);
    FixtureDeinit(&Ui, Pix);
    puts("test_menu_levels passed");
}

/* Main actions must remain reachable when the same profile has to scroll. */
static void test_small_landscape_scroll(void)
{
    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInitSize(&Ui, &Fb, &Pix, Desc, &Keys, 360U, 800U);
    assert(kshim_lvgl_set_entries_described(&Ui, ProductEntries,
        ProductDescriptions, ProductActions, PRODUCT_ENTRY_COUNT, 0U) == 0);
    Advance(&Ui, 400U);
    assert(lv_display_get_horizontal_resolution(Ui.Display) == 800);
    assert(lv_display_get_vertical_resolution(Ui.Display) == 360);
    CheckSelectedDescription(&Ui, ProductEntries[0], ProductDescriptions[0]);
    CheckSelectedAction(&Ui, ProductActions[0]);
    CheckFooter(&Ui);
    lv_area_t HelpBefore;
    lv_obj_get_coords(FindChromeLabel(&Ui, KSHIM_MENU_TEXT_HELP), &HelpBefore);
    lv_area_t BootBefore;
    lv_obj_get_coords(Ui.BootButton, &BootBefore);
    for (size_t I = 1; I < PRODUCT_ENTRY_COUNT; I++)
        PressKey(&Ui, 1);
    Advance(&Ui, 400U);
    lv_obj_t *Last = lv_group_get_obj_by_index(Ui.Group, PRODUCT_ENTRY_COUNT - 1U);
    assert(Last && lv_group_get_focused(Ui.Group) == Last);
    assert(lv_obj_get_scroll_y(Ui.List) > 0);
    lv_area_t NavArea, LastArea, BootAfter;
    lv_obj_get_coords(Ui.List, &NavArea);
    lv_obj_get_coords(Last, &LastArea);
    lv_obj_get_coords(Ui.BootButton, &BootAfter);
    assert(LastArea.y1 >= NavArea.y1 && LastArea.y2 <= NavArea.y2);
    assert(BootAfter.x1 == BootBefore.x1 && BootAfter.y1 == BootBefore.y1);
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    /* STAR keeps boot fixed below the scrolling list, inside the viewport. */
    assert(BootAfter.y1 > NavArea.y2 && BootAfter.x1 >= 0
           && BootAfter.x2 < 800 && BootAfter.y2 < 360);
#else
    assert(BootAfter.x1 > NavArea.x2 && BootAfter.x2 < 800 && BootAfter.y2 < 360);
#endif
    CheckSelectedDescription(&Ui, ProductEntries[PRODUCT_ENTRY_COUNT - 1U],
        ProductDescriptions[PRODUCT_ENTRY_COUNT - 1U]);
    CheckSelectedAction(&Ui, ProductActions[PRODUCT_ENTRY_COUNT - 1U]);
    CheckFooter(&Ui);
    lv_area_t HelpAfter;
    lv_obj_get_coords(FindChromeLabel(&Ui, KSHIM_MENU_TEXT_HELP), &HelpAfter);
    assert(HelpBefore.x1 == HelpAfter.x1 && HelpBefore.y1 == HelpAfter.y1);
    assert(kshim_lvgl_take_index(&Ui) == -1);
    PressKey(&Ui, 2);
    assert(kshim_lvgl_take_index(&Ui) == (int)PRODUCT_ENTRY_COUNT - 1);
    assert(kshim_lvgl_set_entries_described(&Ui, AdvancedEntries,
        AdvancedDescriptions, AdvancedActions, ADVANCED_ENTRY_COUNT, 0U) == 0);
    Advance(&Ui, 400U);
    for (size_t I = 0; I < ADVANCED_ENTRY_COUNT; I++) {
        if (I)
            PressKey(&Ui, 1);
        Advance(&Ui, 16U);
        CheckSelectedDescription(&Ui, AdvancedEntries[I], AdvancedDescriptions[I]);
        CheckSelectedAction(&Ui, AdvancedActions[I]);
        CheckFooter(&Ui);
    }
    FixtureDeinit(&Ui, Pix);
    puts("test_small_landscape_scroll passed");
}

/* ------------------------------------------------------- test_limits */

static void test_limits(void)
{
    kshim_lvgl_t Ui;
    kshim_framebuffer_t Fb;
    uint32_t *Pix;
    QcomKeyDescriptor Desc[3];
    QcomKeys Keys;
    FixtureInit(&Ui, &Fb, &Pix, Desc, &Keys);

    /* Build KSHIM_MENU_MAX_ENTRIES+1 distinct, all-non-null strings */
    static char Bufs[KSHIM_MENU_MAX_ENTRIES + 1][16];
    static const char *Entries[KSHIM_MENU_MAX_ENTRIES + 1];
    for (int I = 0; I < (int)KSHIM_MENU_MAX_ENTRIES + 1; I++) {
        snprintf(Bufs[I], sizeof(Bufs[I]), "Entry%d", I);
        Entries[I] = Bufs[I];
        assert(Entries[I] != NULL);
    }

    /* KSHIM_MENU_MAX_ENTRIES entries accepted; initial focus = last */
    assert(kshim_lvgl_set_entries(&Ui, Entries, KSHIM_MENU_MAX_ENTRIES,
        KSHIM_MENU_MAX_ENTRIES - 1) == 0);
    assert((uint32_t)Ui.EntryCount == KSHIM_MENU_MAX_ENTRIES);
    assert(lv_group_get_obj_count(Ui.Group) == KSHIM_MENU_MAX_ENTRIES);
    lv_obj_t *Last   = lv_group_get_obj_by_index(Ui.Group, KSHIM_MENU_MAX_ENTRIES - 1);
    lv_obj_t *Beyond = lv_group_get_obj_by_index(Ui.Group, KSHIM_MENU_MAX_ENTRIES);
    assert(Last   != NULL);
    assert(Beyond == NULL);
    assert(lv_group_get_focused(Ui.Group) == Last);

    /* KSHIM_MENU_MAX_ENTRIES+1 entries rejected; state (EntryCount,
     * entries, focus) preserved */
    assert(kshim_lvgl_set_entries(&Ui, Entries, KSHIM_MENU_MAX_ENTRIES + 1, 0) != 0);
    assert((uint32_t)Ui.EntryCount == KSHIM_MENU_MAX_ENTRIES);
    assert(strcmp(Ui.Entries[0], "Entry0") == 0);
    assert(strcmp(Ui.Entries[KSHIM_MENU_MAX_ENTRIES - 1],
                  Bufs[KSHIM_MENU_MAX_ENTRIES - 1]) == 0);
    assert(lv_group_get_focused(Ui.Group) == Last);

    /* initial_focus == count is out-of-range; rejected, state still full */
    assert(kshim_lvgl_set_entries(&Ui, Entries, KSHIM_MENU_MAX_ENTRIES,
        KSHIM_MENU_MAX_ENTRIES) != 0);
    assert((uint32_t)Ui.EntryCount == KSHIM_MENU_MAX_ENTRIES);

    FixtureDeinit(&Ui, Pix);
    puts("test_limits passed");
}

/* --------------------------------------------- test_device_info_summary */

#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
/* The STAR Device info page keeps the whole home list and turns the selected
 * Device info row into one localized multi-line summary.  Against the ordinary
 * single-line entry the title and detail grow upward while the confirm button
 * keeps its geometry.  Real LVGL is exercised at both supported scanouts and
 * both locales; the values come from a synthetic fixture that claims no
 * captured stock FDT. */
static void test_device_info_summary(void)
{
    static const kshim_menu_role_t Roles[8] = {
        KSHIM_MENU_ROLE_ANDROID, KSHIM_MENU_ROLE_INTERNAL, KSHIM_MENU_ROLE_TF,
        KSHIM_MENU_ROLE_ADVANCED, KSHIM_MENU_ROLE_LANGUAGE,
        KSHIM_MENU_ROLE_DEVICE_INFO, KSHIM_MENU_ROLE_RESTART, KSHIM_MENU_ROLE_OFF,
    };
    static const kshim_menu_text_id FieldLabels[7] = {
        KSHIM_TEXT_INFO_BOARD, KSHIM_TEXT_INFO_SOC, KSHIM_TEXT_INFO_MODEL,
        KSHIM_TEXT_INFO_COMPATIBLE, KSHIM_TEXT_INFO_RAM,
        KSHIM_TEXT_INFO_FRAMEBUFFER, KSHIM_TEXT_INFO_VERSION,
    };
    static const uint32_t Sizes[2][2] = {{1904U, 3040U}, {1080U, 1920U}};
    static const kshim_menu_language_t Locales[2] = {
        KSHIM_MENU_LANG_ZH_CN, KSHIM_MENU_LANG_EN,
    };
    enum { InfoRow = 5 };

    /* A 120-character model and a three-line compatible list, each truncated
     * with the shared ellipsis.  They are never shortened to fit a legacy
     * single-line implementation. */
    char Model[160];
    memset(Model, 'M', 120);
    memcpy(Model + 120, "\xe2\x80\xa6", 4);
    char Compat[200];
    size_t CompatLength = 0;
    for (int Line = 0; Line < 3; Line++) {
        memset(Compat + CompatLength, 'W', 44);
        CompatLength += 44;
        memcpy(Compat + CompatLength, "\xe2\x80\xa6", 3);
        CompatLength += 3;
        if (Line < 2) Compat[CompatLength++] = '\n';
    }
    Compat[CompatLength] = '\0';

    for (size_t L = 0; L < 2U; L++) {
        for (size_t S = 0; S < 2U; S++) {
            kshim_lvgl_t Ui;
            kshim_framebuffer_t Fb;
            uint32_t *Pix;
            QcomKeyDescriptor Desc[3];
            QcomKeys Keys;
            FixtureInitSize(&Ui, &Fb, &Pix, Desc, &Keys, Sizes[S][0], Sizes[S][1]);
            assert(Ui.LogicalWidth == Sizes[S][1] && Ui.LogicalHeight == Sizes[S][0]);
            assert(kshim_menu_set_language(Locales[L]) == 0);

            const char *Names[8];
            const char *Descriptions[8];
            const char *Actions[8];
            for (size_t I = 0; I < 8U; I++) {
                Names[I] = kshim_menu_role_name(Roles[I], "");
                Descriptions[I] = kshim_menu_role_description(Roles[I], "");
            }
            Actions[0] = KSHIM_MENU_ACTION_BOOT;
            Actions[1] = KSHIM_MENU_ACTION_BOOT;
            Actions[2] = KSHIM_MENU_ACTION_BOOT;
            Actions[3] = KSHIM_MENU_ACTION_ENTER;
            Actions[4] = KSHIM_MENU_ACTION_ENTER;
            Actions[5] = KSHIM_MENU_ACTION_ENTER;
            Actions[6] = KSHIM_MENU_ACTION_RESTART;
            Actions[7] = KSHIM_MENU_ACTION_OFF;

            /* One localized "label: value" block per field, version last. */
            char Framebuffer[48];
            int FbLength = snprintf(Framebuffer, sizeof(Framebuffer),
                                    "%ux%u stride %u", (unsigned)Sizes[S][0],
                                    (unsigned)Sizes[S][1], (unsigned)(Sizes[S][0] * 4U));
            assert(FbLength > 0 && (size_t)FbLength < sizeof(Framebuffer));
            const char *Values[7] = {
                "TB323FU", "SM8750", Model, Compat,
                "8192 MiB", Framebuffer, "0.0.1",
            };
            char Summary[1024];
            size_t Off = 0;
            for (size_t I = 0; I < 7U; I++) {
                int N = snprintf(Summary + Off, sizeof(Summary) - Off, "%s: %s%s",
                                 kshim_menu_text(FieldLabels[I]), Values[I],
                                 (I + 1U < 7U) ? "\n" : "");
                assert(N > 0 && (size_t)N < sizeof(Summary) - Off);
                Off += (size_t)N;
            }

            /* Ordinary entry: the Device info row keeps ENTER and its own
             * localized description. */
            assert(kshim_lvgl_set_entries_described(&Ui, Names, Descriptions,
                    Actions, 8U, (size_t)InfoRow) == 0);
            kshim_lvgl_set_entry_roles(&Ui, Roles, 8U);
            kshim_lvgl_set_countdown(&Ui, 0U);
            kshim_lvgl_set_status(&Ui, "");
            Advance(&Ui, 400U);

            lv_obj_t *HeadN = FindChromeLabel(&Ui, Names[InfoRow]);
            lv_obj_t *DetailN = FindChromeLabel(&Ui, Descriptions[InfoRow]);
            assert(HeadN && DetailN && HeadN != DetailN);
            CheckGlyphs(HeadN, Names[InfoRow]);
            lv_area_t HeadAreaN, DetailAreaN, BootAreaN;
            lv_obj_get_coords(HeadN, &HeadAreaN);
            lv_obj_get_coords(DetailN, &DetailAreaN);
            lv_obj_get_coords(Ui.BootButton, &BootAreaN);

            /* Summary page: the same eight rows, the Device info row now holds
             * the summary and Back, and the initial focus already selects it so
             * no refocus is needed. */
            Descriptions[InfoRow] = Summary;
            Actions[InfoRow] = KSHIM_MENU_ACTION_BACK;
            assert(kshim_lvgl_set_entries_described(&Ui, Names, Descriptions,
                    Actions, 8U, (size_t)InfoRow) == 0);
            kshim_lvgl_set_entry_roles(&Ui, Roles, 8U);
            kshim_lvgl_set_countdown(&Ui, 0U);
            kshim_lvgl_set_status(&Ui, "");
            Advance(&Ui, 400U);

            lv_obj_t *HeadS = FindChromeLabel(&Ui, Names[InfoRow]);
            lv_obj_t *DetailS = FindChromeLabel(&Ui, Summary);
            assert(HeadS && DetailS && HeadS != DetailS);
            CheckGlyphs(HeadS, Names[InfoRow]);
            CheckGlyphs(DetailS, Summary);
            lv_area_t HeadAreaS, DetailAreaS, BootAreaS;
            lv_obj_get_coords(HeadS, &HeadAreaS);
            lv_obj_get_coords(DetailS, &DetailAreaS);
            lv_obj_get_coords(Ui.BootButton, &BootAreaS);

            /* The confirm button shows the localized Back action. */
            lv_obj_t *Button[4];
            int ButtonCount = GatherLabels(Ui.BootButton, Button, 4);
            assert(ButtonCount == 1);
            assert(strcmp(lv_label_get_text(Button[0]), KSHIM_MENU_ACTION_BACK) == 0);

            /* The whole home list stays: eight rows, pending index untouched. */
            assert(lv_group_get_obj_count(Ui.Group) == 8U);
            for (size_t I = 0; I < 8U; I++)
                assert(FindLabel(Ui.List, Names[I]));
            assert(kshim_lvgl_take_index(&Ui) == -1);

            /* Measure the summary with the detail label's own typography. */
            const lv_font_t *DetailFont =
                lv_obj_get_style_text_font(DetailS, LV_PART_MAIN);
            assert(DetailFont);
            lv_point_t TextSize;
            lv_text_get_size(&TextSize, lv_label_get_text(DetailS), DetailFont,
                             lv_obj_get_style_text_letter_space(DetailS, LV_PART_MAIN),
                             lv_obj_get_style_text_line_space(DetailS, LV_PART_MAIN),
                             lv_obj_get_content_width(DetailS), LV_TEXT_FLAG_NONE);
            printf("device-info logical=%ux%u locale=%d font_line=%d text_height=%d "
                   "detail_height=%d\n", (unsigned)Ui.LogicalWidth,
                   (unsigned)Ui.LogicalHeight, (int)Locales[L],
                   (int)DetailFont->line_height, (int)TextSize.y,
                   (int)lv_obj_get_height(DetailS));
            assert(TextSize.y <= lv_obj_get_content_height(DetailS));
            assert(DetailAreaS.y2 < BootAreaS.y1);

            /* The summary grows upward and leaves the button geometry alone. */
            assert(HeadAreaS.y1 < HeadAreaN.y1);
            assert(DetailAreaS.y1 < DetailAreaN.y1);
            assert(BootAreaS.x1 == BootAreaN.x1 && BootAreaS.y1 == BootAreaN.y1);
            assert(BootAreaS.x2 == BootAreaN.x2 && BootAreaS.y2 == BootAreaN.y2);

            /* Focusing Android restores the ordinary description, action and
             * geometry; focusing Device info brings the summary geometry back. */
            lv_group_focus_obj(lv_group_get_obj_by_index(Ui.Group, 0U));
            Advance(&Ui, 400U);
            lv_obj_t *HeadA = FindChromeLabel(&Ui, Names[0]);
            lv_obj_t *DetailA = FindChromeLabel(&Ui, Descriptions[0]);
            assert(HeadA && DetailA);
            CheckGlyphs(HeadA, Names[0]);
            CheckGlyphs(DetailA, Descriptions[0]);
            ButtonCount = GatherLabels(Ui.BootButton, Button, 4);
            assert(ButtonCount == 1);
            assert(strcmp(lv_label_get_text(Button[0]), KSHIM_MENU_ACTION_BOOT) == 0);
            assert(!FindChromeLabel(&Ui, Summary));
            lv_area_t HeadAreaA, DetailAreaA;
            lv_obj_get_coords(HeadA, &HeadAreaA);
            lv_obj_get_coords(DetailA, &DetailAreaA);
            assert(HeadAreaA.y1 == HeadAreaN.y1 && HeadAreaA.y2 == HeadAreaN.y2);
            assert(DetailAreaA.y1 == DetailAreaN.y1 && DetailAreaA.y2 == DetailAreaN.y2);

            lv_group_focus_obj(lv_group_get_obj_by_index(Ui.Group, (uint32_t)InfoRow));
            Advance(&Ui, 400U);
            lv_obj_t *HeadS2 = FindChromeLabel(&Ui, Names[InfoRow]);
            lv_obj_t *DetailS2 = FindChromeLabel(&Ui, Summary);
            assert(HeadS2 && DetailS2);
            lv_area_t HeadAreaS2, DetailAreaS2;
            lv_obj_get_coords(HeadS2, &HeadAreaS2);
            lv_obj_get_coords(DetailS2, &DetailAreaS2);
            assert(HeadAreaS2.y1 == HeadAreaS.y1 && HeadAreaS2.y2 == HeadAreaS.y2);
            assert(DetailAreaS2.y1 == DetailAreaS.y1 && DetailAreaS2.y2 == DetailAreaS.y2);

            /* Rebuilding the ordinary home restores the info ENTER entry and the
             * ordinary geometry. */
            Descriptions[InfoRow] = kshim_menu_role_description(
                KSHIM_MENU_ROLE_DEVICE_INFO, "");
            Actions[InfoRow] = KSHIM_MENU_ACTION_ENTER;
            assert(kshim_lvgl_set_entries_described(&Ui, Names, Descriptions,
                    Actions, 8U, (size_t)InfoRow) == 0);
            kshim_lvgl_set_entry_roles(&Ui, Roles, 8U);
            kshim_lvgl_set_countdown(&Ui, 0U);
            kshim_lvgl_set_status(&Ui, "");
            Advance(&Ui, 400U);
            lv_obj_t *HeadS3 = FindChromeLabel(&Ui, Names[InfoRow]);
            lv_obj_t *DetailS3 = FindChromeLabel(&Ui, Descriptions[InfoRow]);
            assert(HeadS3 && DetailS3);
            ButtonCount = GatherLabels(Ui.BootButton, Button, 4);
            assert(ButtonCount == 1);
            assert(strcmp(lv_label_get_text(Button[0]), KSHIM_MENU_ACTION_ENTER) == 0);
            lv_area_t HeadAreaS3, DetailAreaS3;
            lv_obj_get_coords(HeadS3, &HeadAreaS3);
            lv_obj_get_coords(DetailS3, &DetailAreaS3);
            assert(HeadAreaS3.y1 == HeadAreaN.y1 && HeadAreaS3.y2 == HeadAreaN.y2);
            assert(DetailAreaS3.y1 == DetailAreaN.y1 && DetailAreaS3.y2 == DetailAreaN.y2);

            /* Optional coordinator probe (KSHIM_INFO_FONT_SCAN on 1920-wide
             * hosts): sweep candidate summary font sizes on DetailS3's own
             * (ordinary body) font and report measured text height against the
             * expanded detail limit. Measurement only; production typography is
             * restored to 30px before the fixture teardown. */
            if ((unsigned)Ui.LogicalWidth == 1920U &&
                getenv("KSHIM_INFO_FONT_SCAN")) {
                const lv_font_t *InfoFont =
                    lv_obj_get_style_text_font(DetailS3, LV_PART_MAIN);
                assert(InfoFont);
                const lv_coord_t ScanLetterSpace =
                    lv_obj_get_style_text_letter_space(DetailS3, LV_PART_MAIN);
                const lv_coord_t ScanLineSpace =
                    lv_obj_get_style_text_line_space(DetailS3, LV_PART_MAIN);
                const lv_coord_t ScanWidth = lv_obj_get_content_width(DetailS3);
                const int ScanLimit = (int)(DetailAreaS.y2 - DetailAreaS.y1 + 1);
                for (int Pixel = 21; Pixel <= 30; Pixel++) {
                    lv_tiny_ttf_set_size((lv_font_t *)InfoFont, (uint32_t)Pixel);
                    lv_point_t ScanSize;
                    lv_text_get_size(&ScanSize, Summary, InfoFont,
                                     ScanLetterSpace, ScanLineSpace, ScanWidth,
                                     LV_TEXT_FLAG_NONE);
                    printf("font-scan locale=%d pixel=%d font_line=%d "
                           "text_height=%d limit=%d\n", (int)Locales[L], Pixel,
                           (int)InfoFont->line_height, (int)ScanSize.y,
                           ScanLimit);
                }
                lv_tiny_ttf_set_size((lv_font_t *)InfoFont, 30U);
            }

            FixtureDeinit(&Ui, Pix);
        }
    }
    puts("test_device_info_summary passed");
}
#endif

/* -------------------------------------------------------------- main */

int main(void)
{
    InitEntryText();
    test_labels();
    test_countdown_glyphs();
    test_descriptions();
    test_rotated_flush();
    test_has_interacted();
    test_menu_levels();
    test_small_landscape_scroll();
    test_limits();
#if defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
    test_device_info_summary();
#endif
    puts("all tb323fu menu tests passed");
    return 0;
}
