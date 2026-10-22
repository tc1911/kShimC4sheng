/* Private layout dispatch; the FLAT fallback remains the baseline implementation. */
/* 应用当前行外观；Bento 保留第一个子对象为标题，兼容焦点动画与测试。 */
static int KshimStyleEntry(lv_obj_t *Button, size_t Index)
{
  if (KSHIM_MENU_IS_NATIVE) return KshimNativeStyleEntry(Button);
  if (mMenuDesign.enabled) return KshimDesignEntry(Button, Index);
  uint32_t Height = mRibbon != 0U ? mTileHeight : mColumns > 1U ? mRowHeight * 2U : mRowHeight;
  uint32_t Radius = kshim_menu_style_radius(Height);
  uint32_t PaddingY = max_u32(4U, mRowHeight / 6U);
  if (mMenuStyle.LeftBorderOnly == 0U)
    PaddingY -= min_u32(PaddingY, mMenuStyle.RowBorderWidth);
  if (mMenuStyle.Modern != 0U) {
    uint32_t Line = (uint32_t)lv_font_get_line_height(mRowFont);
    uint32_t Borders = mMenuStyle.LeftBorderOnly != 0U ? 0U :
        mMenuStyle.RowBorderWidth * (mMenuStyle.Divider != 0U ? 1U : 2U);
    PaddingY = min_u32(PaddingY, Height > Line + Borders ? (Height - Line - Borders) / 2U : 0U);
  }
  int32_t Width = lv_obj_get_content_width(mList);
  if (mRibbon != 0U) {
    Width = mTileWidth;
    lv_obj_set_width(Button, Width);
  } else if (mColumns > 1U) {
    Width = (Width - mMenuStyle.RowGap * (mColumns - 1U)) / mColumns;
    lv_obj_set_width(Button, Width > 1 ? Width : 1);
  } else {
    lv_obj_set_width(Button, LV_PCT(100));
  }
  lv_obj_set_height(Button, Height);
  lv_obj_set_style_radius(Button, (int32_t)Radius, 0);
  lv_obj_set_style_bg_color(Button, lv_color_hex(mMenuStyle.Item), 0);
  lv_obj_set_style_bg_opa(Button, mMenuStyle.ItemOpacity, 0);
  lv_obj_set_style_bg_color(Button, KshimAccentColor(), LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(Button, mMenuStyle.FocusOpacity, LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(Button, KshimAccentColor(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(Button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(Button, mMenuStyle.RowBorderWidth, 0);
  lv_obj_set_style_border_side(Button, mMenuStyle.LeftBorderOnly != 0U ? LV_BORDER_SIDE_LEFT : LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_border_color(Button, lv_color_hex(mMenuStyle.Border), 0);
  lv_obj_set_style_border_opa(Button, mMenuStyle.BorderAtRest != 0U ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(Button, lv_color_hex(mMenuStyle.FocusBorder), LV_STATE_FOCUSED);
  lv_obj_set_style_border_opa(Button, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(Button, 0, 0);
  lv_obj_set_style_outline_width(Button, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(Button, 0, 0);
  lv_obj_set_style_text_color(Button, KshimMutedColor(), 0);
  lv_obj_set_style_text_color(Button, lv_color_hex(mMenuStyle.FocusText), LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(Button, lv_color_hex(mMenuStyle.FocusText), LV_STATE_PRESSED);
  lv_obj_set_style_text_font(Button, mRowFont, 0);
  lv_obj_set_style_pad_hor(Button, mRowHeight / 4U, 0);
  lv_obj_set_style_pad_ver(Button, PaddingY, 0);
  lv_obj_set_flex_align(Button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  KshimModernEntry(Button);
  lv_obj_t *Label = lv_obj_get_child(Button, 0);
  if (mMenuStyle.Modern != 0U && Label != NULL) {
    lv_label_set_long_mode(Label, LV_LABEL_LONG_DOT);
    /* 焦点位移预留空间，长标签不进入滚动条和边框。 */
    lv_obj_set_width(Label, LV_PCT(95));
  }
  if (Label != NULL && ((mMenuStyle.Layout == 3U && Height >= 40U) || mRibbon != 0U)) {
    return KshimEntryBadge(Button, Label, mStatusFont, Index, Width, (int32_t)Height,
        mColumns > 1U || mRibbon != 0U);
  }
  if (mColumns > 1U && Label != NULL) {
    lv_obj_add_flag(Label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(Label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *Number = lv_label_create(Button);
    if (Number == NULL)
      return -1;
    char Text[12];
    (void)snprintf(Text, sizeof(Text), "%02u", (unsigned)Index + 1U);
    lv_label_set_text(Number, Text);
    lv_obj_set_style_text_font(Number, mStatusFont, 0);
    lv_obj_add_flag(Number, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(Number, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(Number, LV_ALIGN_TOP_LEFT, 0, 0);
  }
  return 0;
}

static uint32_t KshimPadding(void)
{
  uint32_t ShortEdge = min_u32(mContext->LogicalWidth, mContext->LogicalHeight);
  return clamp_u32(ShortEdge / 28U, 2U, 36U);
}

/* 计算布局：旧主题走原有布局，Surface 宽屏双栏，Bento 窄屏自动回落单列。 */
static int KshimRelayoutChrome(void)
{
  if (KSHIM_MENU_IS_NATIVE) return KshimNativeRelayout();
  if (mMenuDesign.enabled) {
    /* Focus may recursively reveal ancestors during the entry animation.
     * Keep the screen fixed; only the entry list owns a scroll viewport. */
    lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    return KshimDesignRelayout();
  }
  kshim_backdrop_layout_t NextLayout;
  if (mContext == NULL || mFramebuffer == NULL || mContext->EntryCount == 0U)
    return -1;
  if (kshim_backdrop_layout(mContext->LogicalWidth, mContext->LogicalHeight,
      CONFIG_KSHIM_BACKDROP_TEXTURE_PIXELS, mContext->EntryCount, &NextLayout) != 0)
    return -2;
  mBackdropLayout = NextLayout;
  if (mPanel == NULL)
    return 0;
  uint32_t ShortEdge = min_u32(mContext->LogicalWidth, mContext->LogicalHeight);
  uint32_t Padding = KshimPadding();
  int32_t TitleHeight = (int32_t)lv_obj_get_height(mTitle);
  if (TitleHeight <= 0)
    TitleHeight = ShortEdge >= 720U ? 70 : ShortEdge >= 320U ? 41 : 29;
  int32_t StatusHeight = ShortEdge >= 160U ? lv_font_get_line_height(mStatusFont) : 0;
  uint32_t BootHeight = clamp_u32(ShortEdge / 11U, 16U, 88U);
  mColumns = 1U;
  mSplit = 0U;
  mRibbon = 0U;
  mTileHeight = 0U;
  mTileWidth = 0U;
  if (mMenuStyle.Modern != 0U) {
    uint32_t Width = mContext->LogicalWidth;
    uint32_t Height = mContext->LogicalHeight;
    mBackdropLayout.panel_width = Width >= 320U ? min_u32(Width - Padding * 2U, mMenuStyle.Layout == 4U ? 1600U : mMenuStyle.Layout == 5U ? 780U : 1200U) : NextLayout.panel_width;
    mSplit = mMenuStyle.Layout == 1U && Width >= 720U && Width > Height;
    mColumns = mMenuStyle.Layout == 2U && mBackdropLayout.panel_width >= 560U && Height >= 320U ? 2U : 1U;
    if (mMenuStyle.Layout == 3U && Height >= 320U && mBackdropLayout.panel_width >= 560U)
      mColumns = mBackdropLayout.panel_width >= 960U ? 3U : 2U;
    mRibbon = mMenuStyle.Layout == 4U && Width >= 320U && Height >= 240U;
    if (mRibbon != 0U) {
      mTileWidth = (uint16_t)clamp_u32(ShortEdge / 4U, 144U, 240U);
      mTileHeight = (uint16_t)clamp_u32(ShortEdge / 4U, 96U, 240U);
    }
    if (ShortEdge < 160U) {
      TitleHeight = 0;
      lv_obj_add_flag(mTitle, LV_OBJ_FLAG_HIDDEN);
    }
    uint32_t Rows = mRibbon != 0U ? 1U : ((uint32_t)mContext->EntryCount + mColumns - 1U) / mColumns;
    uint32_t RowHeight = mRibbon != 0U ? mTileHeight : mRowHeight * (mColumns > 1U ? 2U : 1U);
    uint32_t Fixed = Padding * 4U + (uint32_t)TitleHeight + (uint32_t)StatusHeight + BootHeight;
    uint32_t Natural = Fixed + Rows * (RowHeight + mMenuStyle.RowGap) + 16U;
    uint32_t Cap = Height - Padding * 2U;
    mBackdropLayout.panel_height = min_u32(max_u32(Fixed + RowHeight, min_u32(Natural, Height * 4U / 5U)), Cap);
    if (mSplit != 0U)
      mBackdropLayout.panel_height = min_u32(max_u32(mBackdropLayout.panel_height, Height * 3U / 4U), Cap);
    mBackdropLayout.panel_x = (int32_t)((Width - mBackdropLayout.panel_width) / 2U);
    mBackdropLayout.panel_y = (int32_t)((Height - mBackdropLayout.panel_height) / 2U);
  }
  int32_t ListY = (int32_t)Padding + TitleHeight +
      (StatusHeight > 0 ? StatusHeight + (int32_t)Padding / 3 : 0) + (int32_t)Padding;
  if (mMenuStyle.Modern != 0U && TitleHeight == 0)
    ListY = (int32_t)Padding;
  int32_t ListHeight = (int32_t)mBackdropLayout.panel_height - ListY - (int32_t)BootHeight - (int32_t)Padding * 2;
  if (ListHeight < 8) ListHeight = 8;
  int32_t ContentWidth = (int32_t)mBackdropLayout.panel_width - (int32_t)Padding * 2;
  lv_obj_set_pos(mPanel, mBackdropLayout.panel_x, mBackdropLayout.panel_y);
  lv_obj_set_size(mPanel, mBackdropLayout.panel_width, mBackdropLayout.panel_height);
  lv_obj_set_style_radius(mPanel, mMenuStyle.Modern != 0U ? mMenuStyle.PanelRadius :
      mMenuStyle.RadiusMax != 0U ? (int32_t)mBackdropLayout.panel_radius : 0, 0);
  lv_obj_set_pos(mTitle, (int32_t)Padding, (int32_t)Padding);
  if (mMenuStyle.Modern != 0U) lv_obj_set_width(mTitle, ContentWidth);
  lv_obj_set_width(mStatus, ContentWidth);
  lv_obj_set_pos(mStatus, (int32_t)Padding, (int32_t)Padding + TitleHeight);
  lv_obj_set_pos(mList, (int32_t)Padding, ListY);
  lv_obj_set_size(mList, ContentWidth, ListHeight);
  lv_obj_set_pos(mBootButton, (int32_t)Padding, (int32_t)mBackdropLayout.panel_height - (int32_t)BootHeight - (int32_t)Padding);
  lv_obj_set_size(mBootButton, ContentWidth, BootHeight);
  lv_obj_set_style_radius(mBootButton, KshimBootRadius(BootHeight), 0);
  if (mMenuStyle.Modern != 0U) {
    uint32_t Gutter = max_u32(mMenuStyle.RowShadow, mMenuStyle.FocusGlow) / 2U + mMenuStyle.FocusOutline;
    lv_obj_set_style_pad_all(mList, Gutter, 0);
    lv_obj_set_style_pad_row(mList, mMenuStyle.RowGap, 0);
    lv_obj_set_style_pad_column(mList, mMenuStyle.RowGap, 0);
    lv_obj_set_flex_flow(mList, mColumns > 1U ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(mList, LV_DIR_VER);
    if (mDetail != NULL) lv_obj_add_flag(mDetail, LV_OBJ_FLAG_HIDDEN);
  }
  if (mSplit != 0U) {
    int32_t NavWidth = ContentWidth * 2 / 5;
    int32_t RightX = (int32_t)Padding * 2 + NavWidth;
    int32_t RightWidth = ContentWidth - NavWidth - (int32_t)Padding;
    ListY = (int32_t)Padding * 2 + TitleHeight;
    lv_obj_set_pos(mList, (int32_t)Padding, ListY);
    lv_obj_set_size(mList, NavWidth, (int32_t)mBackdropLayout.panel_height - ListY - (int32_t)Padding);
    lv_obj_set_style_bg_color(mList, lv_color_hex(mMenuStyle.Navigation), 0);
    lv_obj_set_style_bg_opa(mList, LV_OPA_COVER, 0);
    lv_obj_set_pos(mStatus, RightX, ListY);
    lv_obj_set_width(mStatus, RightWidth);
    lv_obj_set_pos(mBootButton, RightX, (int32_t)mBackdropLayout.panel_height - (int32_t)BootHeight - (int32_t)Padding);
    lv_obj_set_width(mBootButton, RightWidth);
    if (mDetail != NULL) {
      int32_t DetailY = ListY + StatusHeight + (int32_t)Padding;
      int32_t DetailHeight = (int32_t)mBackdropLayout.panel_height - (int32_t)BootHeight -
          (int32_t)Padding * 2 - DetailY;
      lv_obj_set_pos(mDetail, RightX, DetailY);
      lv_obj_set_size(mDetail, RightWidth, DetailHeight > 0 ? DetailHeight : 1);
      if (DetailHeight >= lv_font_get_line_height(mStatusFont))
        lv_obj_remove_flag(mDetail, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (mMenuStyle.Modern != 0U) {
    lv_obj_set_style_bg_opa(mList, LV_OPA_TRANSP, 0);
  }
  if (mRibbon != 0U) {
    /* 单条横向启动带：条目数增加只增加横向滚动，不撑高面板。 */
    uint32_t Gutter = mMenuStyle.FocusOutline + 4U;
    ListY = (int32_t)Padding * 2 + TitleHeight;
    ListHeight = (int32_t)mBackdropLayout.panel_height - ListY -
        (int32_t)BootHeight - StatusHeight - (int32_t)Padding * 3;
    if (ListHeight < 40) ListHeight = 40;
    if (mTileHeight > (uint32_t)ListHeight - Gutter * 2U - 4U)
      mTileHeight = (uint16_t)((uint32_t)ListHeight - Gutter * 2U - 4U);
    lv_obj_set_pos(mList, (int32_t)Padding, ListY);
    lv_obj_set_size(mList, ContentWidth, ListHeight);
    lv_obj_set_style_pad_all(mList, Gutter, 0);
    lv_obj_set_flex_flow(mList, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(mList, LV_DIR_HOR);
    uint32_t Extent = (uint32_t)mContext->EntryCount * (mTileWidth + mMenuStyle.RowGap) - mMenuStyle.RowGap;
    lv_obj_set_flex_align(mList,
        Extent + Gutter * 2U <= (uint32_t)ContentWidth ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_align(mTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(mStatus, (int32_t)Padding, ListY + ListHeight + (int32_t)Padding);
    lv_obj_set_style_text_align(mStatus, LV_TEXT_ALIGN_CENTER, 0);
    int32_t BootWidth = ContentWidth < 320 ? ContentWidth : 320;
    lv_obj_set_width(mBootButton, BootWidth);
    lv_obj_set_x(mBootButton, ((int32_t)mBackdropLayout.panel_width - BootWidth) / 2);
  }
  if (mMenuStyle.Layout == 5U)
    KshimIOSChrome(mTitle, mList, mBootButton, ContentWidth,
        (int32_t)mBackdropLayout.panel_width);
  lv_obj_update_layout(lv_screen_active());
  return 0;
}

