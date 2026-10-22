#ifndef KSHIM_MENU_DESIGN_LVGL_CHECKS_H
#define KSHIM_MENU_DESIGN_LVGL_CHECKS_H
/* Real LVGL assertions used by the existing host regression executables. */
#include "../src/ui/menu_design_layout.h"
static inline uint32_t KshimTestDesignScale(void) {
#ifdef CONFIG_KSHIM_MENU_SCALE_Q8
    return CONFIG_KSHIM_MENU_SCALE_Q8;
#else
    return 0U;
#endif
}
static inline void KshimTestColor(lv_color_t color,uint32_t rgb) {
    assert(color.red==((rgb>>16)&255U)); assert(color.green==((rgb>>8)&255U)); assert(color.blue==(rgb&255U));
}
static inline void KshimTestDesignLayout(kshim_lvgl_t *ui) {
    kshim_design_geometry_t g;
    assert(kshim_design_layout(&mMenuDesign,ui->Framebuffer->width,ui->Framebuffer->height,
        (uint32_t)ui->EntryCount,KshimTestDesignScale(),&g)==0);
    lv_obj_t *panel=lv_obj_get_parent(ui->List);
    lv_area_t pa,li,bo; lv_obj_get_coords(panel,&pa);lv_obj_get_coords(ui->List,&li);lv_obj_get_coords(ui->BootButton,&bo);
    if(pa.x1!=g.panel.x || pa.y1!=g.panel.y)
        fprintf(stderr,"GEOMETRY %s %ux%u count=%u compact=%u: panel=(%ld,%ld) expected=(%ld,%ld) root-scroll=(%ld,%ld) translation=(%ld,%ld)\n",
            mMenuDesign.name,(unsigned)ui->Framebuffer->width,(unsigned)ui->Framebuffer->height,
            (unsigned)ui->EntryCount,g.compact,(long)pa.x1,(long)pa.y1,(long)g.panel.x,(long)g.panel.y,
            (long)lv_obj_get_scroll_x(lv_screen_active()),(long)lv_obj_get_scroll_y(lv_screen_active()),
            (long)lv_obj_get_style_translate_x(panel,LV_PART_MAIN),(long)lv_obj_get_style_translate_y(panel,LV_PART_MAIN));
    assert(pa.x1==g.panel.x && pa.y1==g.panel.y);
    assert(!lv_obj_has_flag(lv_screen_active(),LV_OBJ_FLAG_SCROLLABLE));
    assert(lv_obj_get_scroll_x(lv_screen_active())==0 && lv_obj_get_scroll_y(lv_screen_active())==0);
    assert(lv_obj_get_width(panel)==g.panel.w && lv_obj_get_height(panel)==g.panel.h);
    assert(pa.x1>=0 && pa.y1>=0 && pa.x2<(int32_t)ui->Framebuffer->width && pa.y2<(int32_t)ui->Framebuffer->height);
    assert(li.x1==pa.x1+g.list.x && li.y1==pa.y1+g.list.y);
    assert(bo.x1==pa.x1+g.boot.x && bo.y1==pa.y1+g.boot.y);
    assert(bo.x2<=pa.x2 && bo.y2<=pa.y2);
    if(g.split) assert(bo.x1>li.x2); else assert(bo.y1>li.y2);
    assert(lv_obj_get_scroll_dir(ui->List)==(g.horizontal?LV_DIR_HOR:LV_DIR_VER));
    for(unsigned i=0;i<ui->EntryCount;i++) {
        lv_obj_t *row=lv_group_get_obj_by_index(ui->Group,i);
        assert(row && lv_obj_get_child_count(row)>=1U);
        assert(lv_obj_get_width(row)==g.items[i].w && lv_obj_get_height(row)==g.items[i].h);
        lv_obj_t *label=lv_obj_get_child(row,0);
        lv_area_t ra,la; lv_obj_get_coords(row,&ra);lv_obj_get_coords(label,&la);
        assert(la.x1>=ra.x1 && la.x2<=ra.x2 && la.y1>=ra.y1 && la.y2<=ra.y2);
        kshim_design_item_geometry_t metrics;
        kshim_design_item_layout(&mMenuDesign,&g,i,lv_font_get_line_height(lv_obj_get_style_text_font(row,LV_PART_MAIN)),&metrics);
        assert(la.x1==ra.x1+metrics.label.x && la.y1==ra.y1+metrics.label.y);
        if(metrics.icon.w) assert(metrics.icon.y+metrics.icon.h<=metrics.label.y || metrics.icon.x+metrics.icon.w<=metrics.label.x);
    }
    lv_obj_t *focused=lv_group_get_focused(ui->Group);
    if(focused) {
        lv_area_t r; lv_obj_get_coords(focused,&r);
        assert((r.x1+r.x2)/2>=li.x1 && (r.x1+r.x2)/2<=li.x2);
        assert((r.y1+r.y2)/2>=li.y1 && (r.y1+r.y2)/2<=li.y2);
    }
}
static inline unsigned KshimTestSelectionPixels(kshim_lvgl_t *ui,unsigned index) {
    kshim_design_geometry_t g;
    assert(kshim_design_layout(&mMenuDesign,ui->Framebuffer->width,ui->Framebuffer->height,
        (uint32_t)ui->EntryCount,KshimTestDesignScale(),&g)==0);
    lv_obj_t *row=lv_group_get_obj_by_index(ui->Group,index); lv_area_t a;lv_obj_get_coords(row,&a);
    kshim_design_item_geometry_t r;
    kshim_design_item_layout(&mMenuDesign,&g,index,lv_font_get_line_height(lv_obj_get_style_text_font(row,LV_PART_MAIN)),&r);
    if(!r.mark.w) return 0;
    unsigned count=0;
    for(int32_t y=a.y1+r.mark.y;y<a.y1+r.mark.y+r.mark.h;y++) {
        if(y<0 || y>=(int32_t)ui->Framebuffer->height) continue;
        const uint32_t *pixels=(const uint32_t *)(const void *)(ui->Framebuffer->render_address+(size_t)y*ui->Framebuffer->stride);
        for(int32_t x=a.x1+r.mark.x;x<a.x1+r.mark.x+r.mark.w;x++)
            if(x>=0 && x<(int32_t)ui->Framebuffer->width && (pixels[x]&0xffffffU)==mMenuStyle.FocusBorder) count++;
    }
    return count;
}
static inline void KshimTestDesignChrome(kshim_lvgl_t *ui) {
    lv_obj_t *a=lv_group_get_obj_by_index(ui->Group,0),*b=lv_group_get_obj_by_index(ui->Group,1);
    assert(a && b);
    lv_group_focus_obj(a);
    for(unsigned i=0;i<48;i++) kshim_lvgl_frame(ui,16);
    KshimTestDesignLayout(ui);
    KshimTestColor(lv_obj_get_style_text_color(a,LV_PART_MAIN),mMenuStyle.FocusText);
    KshimTestColor(lv_obj_get_style_text_color(b,LV_PART_MAIN),mMenuStyle.Text);
    KshimTestColor(lv_obj_get_style_bg_color(ui->BootButton,LV_PART_MAIN),mMenuStyle.Boot);
    lv_obj_add_state(a,LV_STATE_PRESSED);
    KshimTestColor(lv_obj_get_style_text_color(a,LV_PART_MAIN),mMenuStyle.Modern?mMenuStyle.PressText:mMenuStyle.FocusText);
    lv_obj_remove_state(a,LV_STATE_PRESSED);
    if(mMenuDesign.kind==KSHIM_DESIGN_TERMINAL) {
        const lv_font_t *font=lv_obj_get_style_text_font(a,LV_PART_MAIN);
        assert(lv_font_get_glyph_width(font,'i',0)==lv_font_get_glyph_width(font,'W',0));
        assert(lv_font_get_glyph_width(font,'M',0)==lv_font_get_glyph_width(font,'1',0));
    }
    kshim_design_geometry_t g;
    assert(kshim_design_layout(&mMenuDesign,ui->Framebuffer->width,ui->Framebuffer->height,(uint32_t)ui->EntryCount,KshimTestDesignScale(),&g)==0);
    if(mMenuDesign.kind==KSHIM_DESIGN_IOS_HIG && !g.compact) {
        assert(KshimTestSelectionPixels(ui,0)>3);
        lv_group_focus_obj(b);
        for(unsigned i=0;i<48;i++) kshim_lvgl_frame(ui,16);
        assert(KshimTestSelectionPixels(ui,0)==0);
        assert(KshimTestSelectionPixels(ui,1)>3);
        lv_group_focus_obj(a);
        for(unsigned i=0;i<48;i++) kshim_lvgl_frame(ui,16);
        assert(KshimTestSelectionPixels(ui,0)>3);
    }
    assert(kshim_lvgl_take_index(ui)==-1);
}
static inline bool KshimTestDesignBackgroundExpected(void) {
#if defined(CONFIG_KSHIM_MENU_REDUCED_EFFECTS) && CONFIG_KSHIM_MENU_REDUCED_EFFECTS
    return false;
#else
    return mMenuDesign.background!=0U;
#endif
}
#endif
