#ifndef KSHIM_MENU_IOS_H
#define KSHIM_MENU_IOS_H

/* 仅绘制真实选中项的勾号，不使用字体字形或额外可点击控件。 */
static void KshimDrawIOSSelection(lv_event_t *Event)
{
  lv_obj_t *Button = lv_event_get_target_obj(Event);
  if (!lv_obj_has_state(Button, LV_STATE_FOCUSED))
    return;
  lv_area_t Area;
  lv_obj_get_coords(Button, &Area);
  int32_t CenterY = (Area.y1 + Area.y2) / 2;
  int32_t Right = Area.x2 - 15;
  int32_t Size = lv_obj_get_height(Button) / 3;
  if (Size < 12) Size = 12;
  if (Size > 24) Size = 24;
  lv_draw_line_dsc_t Draw;
  lv_draw_line_dsc_init(&Draw);
  Draw.color = lv_color_hex(mMenuStyle.FocusBorder);
  Draw.width = Size >= 20 ? 4 : 3;
  Draw.opa = lv_obj_get_style_opa_recursive(Button, LV_PART_MAIN);
  Draw.round_start = 1;
  Draw.round_end = 1;
  Draw.p1.x = Right - Size;
  Draw.p1.y = CenterY;
  Draw.p2.x = Right - Size * 2 / 3;
  Draw.p2.y = CenterY + Size / 3;
  lv_draw_line(lv_event_get_layer(Event), &Draw);
  Draw.p1 = Draw.p2;
  Draw.p2.x = Right;
  Draw.p2.y = CenterY - Size / 2;
  lv_draw_line(lv_event_get_layer(Event), &Draw);
}

/* 内缩分组与独立主操作，标题、列表和按钮均复用既有控件。 */
static void KshimIOSChrome(
    lv_obj_t *Title,
    lv_obj_t *List,
    lv_obj_t *Boot,
    int32_t ContentWidth,
    int32_t PanelWidth
)
{
  lv_obj_set_style_text_align(Title, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_radius(List, 16, 0);
  lv_obj_set_style_clip_corner(List, true, 0);
  lv_obj_set_style_bg_color(List, lv_color_hex(mMenuStyle.Navigation), 0);
  lv_obj_set_style_bg_opa(List, LV_OPA_COVER, 0);
  int32_t BootWidth = ContentWidth < 360 ? ContentWidth : 360;
  lv_obj_set_width(Boot, BootWidth);
  lv_obj_set_x(Boot, (PanelWidth - BootWidth) / 2);
}

#endif
