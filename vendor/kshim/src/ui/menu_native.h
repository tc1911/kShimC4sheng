#ifndef KSHIM_MENU_NATIVE_H
#define KSHIM_MENU_NATIVE_H
/* Native LVGL presentation only. Entry order, inputs and boot ownership remain
 * in lvgl_port.c. No custom colors, radius, borders, symbols or animation. */
#if KSHIM_MENU_IS_NATIVE
#if !LV_USE_THEME_DEFAULT
#error "LVGL_DEFAULT must enable the theme in the LVGL library and application"
#endif
static lv_font_t mNativeFont;
static void KshimNativeCreateFonts(void)
{
    /* Keep LVGL's built-in Latin glyphs/metrics. Add the existing CJK font only
     * as a missing-glyph fallback; no system or proprietary font is imported. */
    mNativeFont = *LV_FONT_DEFAULT;
    mFonts[0] = mFonts[2] = NULL;
    mFonts[1] = mFontData == NULL ? NULL :
        lv_tiny_ttf_create_data_ex(mFontData, kshim_ui_font_size(), 14,
            LV_FONT_KERNING_NONE, LV_TINY_TTF_CACHE_GLYPH_CNT);
    if (mFonts[1] != NULL) mNativeFont.fallback = mFonts[1];
    mTitleFont = mRowFont = mStatusFont = &mNativeFont;
}
static int KshimNativeStyleEntry(lv_obj_t *button)
{
    if (button == NULL) return -1;
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label == NULL) return -1;
    /* Let the native list flex layout and theme size/style the row. */
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return 0;
}
static int KshimNativeRelayout(void)
{
    if (!mContext || !mPanel || !mList || !mBootButton) return -1;
    int32_t width = (int32_t)mContext->LogicalWidth;
    int32_t height = (int32_t)mContext->LogicalHeight;
    int32_t edge = width < height ? width : height;
    int32_t margin = edge < 160 ? 2 : lv_dpx(16);
    int32_t pw = kshim_design_min(width - 2 * margin, lv_dpx(720));
    int32_t cap = height - 2 * margin;
    bool tiny = edge < 160;
    bool headings = height >= 240 && width >= 240;
    if (tiny) {
        /* Only geometrical fitting differs on tiny framebuffers. Keep actual
         * default widget colors, radius, borders and state styles. */
        lv_obj_set_style_pad_all(mPanel, 2, 0);
        lv_obj_set_style_pad_row(mPanel, 2, 0);
    }
    int32_t line = lv_font_get_line_height(LV_FONT_DEFAULT);
    int32_t row = line + 2 * lv_dpx(12);
    int32_t desired = (headings ? 2 * line : 0) + lv_dpx(144) +
        (int32_t)mContext->EntryCount * row;
    int32_t ph = kshim_design_min(cap, kshim_design_min(lv_dpx(680), desired));
    lv_obj_set_size(mPanel, pw, ph);
    lv_obj_center(mPanel);
    if (headings) {
        lv_obj_remove_flag(mTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mStatus, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mStatus, LV_OBJ_FLAG_HIDDEN);
    }
    if (tiny) {
        lv_obj_set_height(mBootButton, kshim_design_min(line + 4, cap / 2));
        lv_obj_set_style_pad_ver(mBootButton, 0, 0);
    }
    lv_obj_set_height(mList, 1);
    lv_obj_update_layout(lv_screen_active());
    return 0;
}
static int KshimNativeBuildChrome(void)
{
    lv_obj_t *screen = lv_screen_active();
    if (screen == NULL || mDisplay == NULL) return -1;
    /* Match the pinned LVGL display constructor's default-theme arguments. */
    lv_theme_t *theme = lv_theme_default_init(mDisplay,
        lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
        LV_THEME_DEFAULT_DARK, &mNativeFont);
    if (theme == NULL) return -1;
    lv_display_set_theme(mDisplay, theme);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    /* Keep the public Background handle valid without drawing a backdrop. */
    mBackground = lv_image_create(screen);
    if (mBackground == NULL) return -1;
    lv_obj_add_flag(mBackground, LV_OBJ_FLAG_HIDDEN);
    mPanel = lv_obj_create(screen);
    if (mPanel == NULL) return -1;
    lv_obj_remove_flag(mPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(mPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    mTitle = lv_label_create(mPanel);
    mStatus = lv_label_create(mPanel);
    mList = lv_list_create(mPanel);
    mBootButton = lv_button_create(mPanel);
    if (!mTitle || !mStatus || !mList || !mBootButton) return -1;
    lv_label_set_text(mTitle, "Select OS to Boot");
    lv_label_set_text(mStatus, "Select an image");
    lv_label_set_long_mode(mTitle, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(mStatus, LV_LABEL_LONG_DOT);
    lv_obj_set_width(mTitle, LV_PCT(100));
    lv_obj_set_width(mStatus, LV_PCT(100));
    lv_obj_set_width(mList, LV_PCT(100));
    lv_obj_set_flex_grow(mList, 1);
    lv_obj_t *label = lv_label_create(mBootButton);
    if (label == NULL) return -1;
    lv_label_set_text_static(label, "Boot");
    lv_obj_center(label);
    lv_obj_add_event_cb(mBootButton, KshimBootEvent, LV_EVENT_CLICKED, NULL);
    return KshimNativeRelayout();
}
#else
static inline void KshimNativeCreateFonts(void) { }
static inline int KshimNativeStyleEntry(lv_obj_t *button) { (void)button; return -1; }
static inline int KshimNativeRelayout(void) { return -1; }
static inline int KshimNativeBuildChrome(void) { return -1; }
#endif
#endif
