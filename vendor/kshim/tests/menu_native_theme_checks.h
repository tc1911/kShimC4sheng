#ifndef KSHIM_NATIVE_THEME_CHECKS_H
#define KSHIM_NATIVE_THEME_CHECKS_H
/* Compare with newly-created untouched widgets, never a copied palette. */
static inline void KshimTestNativeChrome(kshim_lvgl_t *ui)
{
    assert(lv_display_get_theme(ui->Display)!=NULL);
    assert(!mMenuDesign.enabled);
    assert(lv_group_get_obj_count(ui->Group)==ui->EntryCount);
    lv_obj_t *holder=lv_obj_create(lv_screen_active());
    lv_obj_add_flag(holder,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *list=lv_list_create(holder);
    lv_obj_t *row=lv_list_add_button(list,NULL,"Reference");
    lv_obj_t *boot=lv_button_create(holder);
    lv_group_remove_obj(row);lv_group_remove_obj(boot);
    lv_obj_t *selected=lv_group_get_focused(ui->Group);
    assert(selected && lv_obj_has_state(selected,LV_STATE_CHECKED));
    lv_obj_remove_state(row,LV_STATE_ANY);
    lv_obj_add_state(row,lv_obj_get_state(selected));
    for(unsigned i=0;i<48;i++) kshim_lvgl_frame(ui,16);
    lv_obj_t *actual[]={ui->List,selected,ui->BootButton};
    lv_obj_t *reference[]={list,row,boot};
    for(unsigned i=0;i<3;i++) {
#define VALUE(prop) assert(lv_obj_get_style_##prop(actual[i],LV_PART_MAIN)==lv_obj_get_style_##prop(reference[i],LV_PART_MAIN))
#define COLOR(prop) assert(lv_color_eq(lv_obj_get_style_##prop(actual[i],LV_PART_MAIN),lv_obj_get_style_##prop(reference[i],LV_PART_MAIN)))
        COLOR(bg_color); COLOR(text_color); COLOR(border_color); COLOR(outline_color);
        VALUE(radius); VALUE(bg_opa); VALUE(border_width); VALUE(shadow_width); VALUE(outline_width);
        VALUE(transform_scale_x); VALUE(transform_scale_y);
        assert(lv_font_get_line_height(lv_obj_get_style_text_font(actual[i],LV_PART_MAIN))==lv_font_get_line_height(LV_FONT_DEFAULT));
#undef VALUE
#undef COLOR
    }
    lv_area_t a,b;lv_obj_get_coords(ui->List,&a);lv_obj_get_coords(ui->BootButton,&b);
    assert(a.y2<b.y1 && b.x1>=0 && b.y1>=0);
    assert(b.x2<(int32_t)ui->Framebuffer->width && b.y2<(int32_t)ui->Framebuffer->height);
    assert(kshim_lvgl_take_index(ui)==-1);
    lv_obj_delete(holder);
}
#endif
