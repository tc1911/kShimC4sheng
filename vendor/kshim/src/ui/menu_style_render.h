#ifndef KSHIM_MENU_STYLE_RENDER_H
#define KSHIM_MENU_STYLE_RENDER_H

#include "menu_ios.h"

/* 绘制两个端点的渐变；相同端点使用纯色，按压态可显式关闭渐变。 */
static void KshimStyleGradient(
    lv_obj_t *Object,
    uint32_t First,
    uint32_t Last,
    lv_style_selector_t State
)
{
  lv_obj_set_style_bg_color(Object, lv_color_hex(First), State);
  lv_obj_set_style_bg_grad_color(Object, lv_color_hex(Last), State);
  lv_obj_set_style_bg_grad_dir(Object,
      First == Last ? LV_GRAD_DIR_NONE : LV_GRAD_DIR_VER, State);
}

/* 在原有面板上应用现代材质，阴影范围限制在预设的小尺寸预算内。 */
static void KshimModernPanel(lv_obj_t *Screen, lv_obj_t *Panel)
{
  if (mMenuStyle.Modern == 0U)
    return;
  KshimStyleGradient(Screen, mMenuStyle.Screen, mMenuStyle.ScreenEnd, 0);
  KshimStyleGradient(Panel, mMenuStyle.Panel, mMenuStyle.PanelEnd, 0);
  lv_obj_set_style_shadow_width(Panel, mMenuStyle.PanelShadow, 0);
  lv_obj_set_style_shadow_color(Panel, lv_color_hex(mMenuStyle.Shadow), 0);
  lv_obj_set_style_shadow_opa(Panel, 32, 0);
  lv_obj_set_style_shadow_offset_y(Panel, mMenuStyle.PanelShadow / 4U, 0);
}

/* 只在绘制事件提交短焦点标记，不在绘制阶段修改控件样式。 */
static void KshimDrawFocusPill(lv_event_t *Event)
{
  lv_obj_t *Button = lv_event_get_target_obj(Event);
  if (!lv_obj_has_state(Button, LV_STATE_FOCUSED))
    return;
  lv_area_t Area;
  lv_obj_get_coords(Button, &Area);
  int32_t Height = lv_obj_get_height(Button);
  int32_t Mark = Height / 3;
  if (Mark < 6) Mark = 6;
  if (Mark > 24) Mark = 24;
  Area.x1 += 7;
  Area.x2 = Area.x1 + 2;
  Area.y1 += (Height - Mark) / 2;
  Area.y2 = Area.y1 + Mark - 1;
  lv_draw_rect_dsc_t Draw;
  lv_draw_rect_dsc_init(&Draw);
  Draw.bg_color = lv_color_hex(mMenuStyle.FocusBorder);
  Draw.bg_opa = lv_obj_get_style_opa_recursive(Button, LV_PART_MAIN);
  Draw.radius = LV_RADIUS_CIRCLE;
  lv_draw_rect(lv_event_get_layer(Event), &Draw, &Area);
}

/* Material 2 与 Fluent 2 的主按钮层次；零字段不修改既有主题。 */
static void KshimModernBoot(lv_obj_t *Button)
{
  if (mMenuStyle.BootShadow == 0U)
    return;
  lv_obj_set_style_shadow_width(Button, mMenuStyle.BootShadow, 0);
  lv_obj_set_style_shadow_color(Button, lv_color_hex(mMenuStyle.Shadow), 0);
  lv_obj_set_style_shadow_opa(Button, 48, 0);
  lv_obj_set_style_shadow_offset_y(Button, mMenuStyle.BootShadow / 3U, 0);
  lv_obj_set_style_shadow_width(Button, 2, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_offset_y(Button, 1, LV_STATE_PRESSED);
}

/* 创建只用于标识真实启动项序号的图形，不从名称推断 OS 或虚构元数据。 */
static int KshimEntryBadge(
    lv_obj_t *Button,
    lv_obj_t *Label,
    const lv_font_t *Font,
    size_t Index,
    int32_t Width,
    int32_t Height,
    bool Tall
)
{
  bool Disk = mMenuStyle.Layout == 4U;
  int32_t Size = Disk ? Height / 2 : Tall ? Height / 3 : Height / 2;
  if (Size > 120) Size = 120;
  if (Size < 24) Size = 24;
  int32_t Padding = lv_obj_get_style_pad_left(Button, LV_PART_MAIN);
  int32_t VerticalPadding = lv_obj_get_style_pad_top(Button, LV_PART_MAIN);
  int32_t Line = lv_font_get_line_height(lv_obj_get_style_text_font(Label, LV_PART_MAIN));
  if (Tall) {
    int32_t Limit = Height - VerticalPadding * 2 - Line - 8;
    if (Limit >= 16 && Size > Limit) Size = Limit;
  }
  int32_t LabelWidth = Width - Padding * 2 - 4;
  lv_obj_add_flag(Label, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(Label, Tall ? (Disk ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_BOTTOM_LEFT)
      : LV_ALIGN_LEFT_MID, Tall ? 0 : Size + 12, 0);
  if (!Tall) LabelWidth -= Size + 12;
  lv_obj_set_width(Label, LabelWidth > 1 ? LabelWidth : 1);
  /* 约束文本高度，DOT 模式才能截断超长名称，不能反向覆盖图标。 */
  int32_t Lines = Tall ? (Height - VerticalPadding * 2 - Size - 8) / Line : 1;
  if (Lines < 1) Lines = 1;
  if (Lines > 2) Lines = 2;
  lv_obj_set_height(Label, Line * Lines);
  if (Disk) lv_obj_set_style_text_align(Label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *Badge = lv_obj_create(Button);
  if (Badge == NULL) return -1;
  lv_obj_remove_style_all(Badge);
  lv_obj_remove_flag(Badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(Badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(Badge, Size, Size);
  lv_obj_align(Badge, Tall ? (Disk ? LV_ALIGN_TOP_MID : LV_ALIGN_TOP_LEFT)
      : LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(Badge, Disk ? 10 : LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(Badge, lv_color_hex(mMenuStyle.Navigation), 0);
  lv_obj_set_style_bg_opa(Badge, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(Badge, lv_color_hex(mMenuStyle.FocusText), 0);
  lv_obj_set_style_text_font(Badge, Font, 0);
  lv_obj_t *Number = lv_label_create(Badge);
  if (Number == NULL) return -1;
  char Text[12];
  (void)snprintf(Text, sizeof(Text), "%02u", (unsigned)Index + 1U);
  lv_label_set_text(Number, Text);
  lv_obj_remove_flag(Number, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(Number, LV_ALIGN_CENTER, 0, Disk ? -Size / 10 : 0);
  if (Disk) {
    lv_obj_t *Slot = lv_obj_create(Badge);
    if (Slot == NULL) return -1;
    lv_obj_remove_style_all(Slot);
    lv_obj_remove_flag(Slot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(Slot, Size * 2 / 3, 3);
    lv_obj_align(Slot, LV_ALIGN_BOTTOM_MID, 0, -Size / 6);
    lv_obj_set_style_radius(Slot, 2, 0);
    lv_obj_set_style_bg_color(Slot, lv_color_hex(mMenuStyle.FocusBorder), 0);
    lv_obj_set_style_bg_opa(Slot, LV_OPA_COVER, 0);
  }
  return 0;
}

/* 应用行的分隔线、层次、焦点轮廓和独立按压颜色，不改变确认语义。 */
static void KshimModernEntry(lv_obj_t *Button)
{
  if (mMenuStyle.Modern == 0U)
    return;
  if (mMenuStyle.Layout == 5U) {
    lv_obj_set_style_pad_right(Button, 40, 0);
    lv_obj_set_style_border_color(Button, lv_color_hex(mMenuStyle.Border), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(Button, KshimDrawIOSSelection, LV_EVENT_DRAW_POST, NULL);
  }
  if (mMenuStyle.FocusPill != 0U) {
    lv_obj_set_style_pad_left(Button, 20, 0);
    lv_obj_add_event_cb(Button, KshimDrawFocusPill, LV_EVENT_DRAW_POST, NULL);
  }
  if (mMenuStyle.Divider != 0U)
    lv_obj_set_style_border_side(Button, LV_BORDER_SIDE_BOTTOM, 0);
  KshimStyleGradient(Button, mMenuStyle.Focus, mMenuStyle.FocusEnd, LV_STATE_FOCUSED);
  KshimStyleGradient(Button, mMenuStyle.Press, mMenuStyle.Press, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(Button, lv_color_hex(mMenuStyle.PressText), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(Button, mMenuStyle.RowShadow, 0);
  lv_obj_set_style_shadow_color(Button, lv_color_hex(mMenuStyle.Shadow), 0);
  lv_obj_set_style_shadow_opa(Button, 42, 0);
  lv_obj_set_style_shadow_offset_y(Button, mMenuStyle.RowShadow / 3U, 0);
  if (mMenuStyle.RowShadow != 0U) {
    lv_obj_set_style_shadow_width(Button, 2, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_offset_y(Button, 1, LV_STATE_PRESSED);
  }
  if (mMenuStyle.FocusGlow != 0U) {
    lv_obj_set_style_shadow_width(Button, mMenuStyle.FocusGlow, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_color(Button, lv_color_hex(mMenuStyle.FocusBorder), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_opa(Button, 64, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_offset_y(Button, 0, LV_STATE_FOCUSED);
  }
  lv_obj_set_style_outline_color(Button, lv_color_hex(mMenuStyle.FocusBorder), 0);
  lv_obj_set_style_outline_opa(Button, LV_OPA_COVER, 0);
  lv_obj_set_style_outline_pad(Button, 0, 0);
  lv_obj_set_style_outline_width(Button, mMenuStyle.FocusOutline, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(Button, mMenuStyle.FocusOutline,
      LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
}

/* 计算主按钮圆角，Material 3 / Aurora 使用半高胶囊而不复用行圆角。 */
static uint32_t KshimBootRadius(uint32_t Height)
{
  if (mMenuStyle.Modern == 0U)
    return kshim_menu_style_radius(Height);
  uint32_t Radius = mMenuStyle.BootRadius == 255U ? Height / 2U : mMenuStyle.BootRadius;
  return Radius < Height / 2U ? Radius : Height / 2U;
}

#endif
