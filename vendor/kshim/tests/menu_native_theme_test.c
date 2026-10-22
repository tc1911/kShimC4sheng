/* Native theme must be real, not a table of imitation colors. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include <lvgl_port.h>
static void settle(kshim_lvgl_t *ui) {
    for(unsigned i=0;i<48;i++) kshim_lvgl_frame(ui,16);
}
static void equivalent(lv_obj_t *a,lv_obj_t *b) {
#define COLOR(prop) assert(lv_color_eq(lv_obj_get_style_##prop(a,LV_PART_MAIN),lv_obj_get_style_##prop(b,LV_PART_MAIN)))
#define VALUE(prop) assert(lv_obj_get_style_##prop(a,LV_PART_MAIN)==lv_obj_get_style_##prop(b,LV_PART_MAIN))
    COLOR(bg_color); COLOR(text_color); COLOR(border_color); COLOR(outline_color); COLOR(shadow_color);
    VALUE(bg_opa); VALUE(bg_grad_dir); VALUE(radius); VALUE(border_width); VALUE(border_opa);
    VALUE(outline_width); VALUE(outline_opa); VALUE(shadow_width); VALUE(shadow_opa);
    VALUE(pad_top); VALUE(pad_bottom); VALUE(pad_left); VALUE(pad_right);
    VALUE(transform_scale_x); VALUE(transform_scale_y);
    const lv_font_t *font=lv_obj_get_style_text_font(a,LV_PART_MAIN);
    assert(lv_font_get_line_height(font)==lv_font_get_line_height(LV_FONT_DEFAULT));
    assert(lv_font_get_glyph_width(font,'W','i')==lv_font_get_glyph_width(LV_FONT_DEFAULT,'W','i'));
#undef COLOR
#undef VALUE
}
static void check(unsigned w,unsigned h,int scratch_enabled) {
    uint32_t *pixels=calloc((size_t)w*h,sizeof *pixels);assert(pixels);
    kshim_framebuffer_config_t c={.render_address=(uintptr_t)pixels,.width=w,.height=h,
        .stride=w*4U,.bpp=32U,.format=KSHIM_FB_FORMAT_XRGB8888,.buffer_size=(size_t)w*h*4U};
    kshim_framebuffer_t fb;assert(kshim_fb_init(&fb,&c)==0);
    static unsigned char scratch[4U<<20] __attribute__((aligned(64)));
    kshim_lvgl_set_scratch(scratch_enabled?scratch:NULL,scratch_enabled?sizeof scratch:0U);
    kshim_lvgl_t ui; assert(kshim_lvgl_init(&ui,&fb,NULL)==0);settle(&ui);
    assert(lv_display_get_theme(ui.Display)!=NULL && "native LVGL theme is disabled or bypassed");
    assert(lv_obj_check_type(ui.List,&lv_list_class));
    assert(lv_obj_check_type(ui.BootButton,&lv_button_class));
    lv_obj_t *reference=lv_obj_create(lv_screen_active());
    lv_obj_add_flag(reference,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *ref_list=lv_list_create(reference);
    lv_obj_t *ref_row=lv_list_add_button(ref_list,NULL,"Reference");
    lv_obj_t *ref_boot=lv_button_create(reference);
    lv_group_remove_obj(ref_row);lv_group_remove_obj(ref_boot);
    char labels[49][48];const char *names[49];
    for(unsigned i=0;i<49;i++){snprintf(labels[i],sizeof labels[i],"Image %u / 启动系统",i);names[i]=labels[i];}
    const unsigned counts[]={2,49,2};
    for(unsigned n=0;n<3;n++) {
        assert(kshim_lvgl_set_entries(&ui,names,counts[n],0)==0);settle(&ui);
        assert(lv_group_get_obj_count(ui.Group)==counts[n]);
        lv_obj_t *row=lv_group_get_obj_by_index(ui.Group,0);
        assert(lv_obj_has_state(row,LV_STATE_CHECKED));
        assert(lv_obj_has_state(row,LV_STATE_FOCUS_KEY));
        assert(!lv_color_eq(lv_obj_get_style_bg_color(row,LV_PART_MAIN),lv_obj_get_style_bg_color(ref_row,LV_PART_MAIN)));
        equivalent(ui.List,ref_list);equivalent(ui.BootButton,ref_boot);
        lv_area_t boot,after,list,panel;
        lv_obj_get_coords(ui.BootButton,&boot);lv_obj_get_coords(ui.List,&list);
        lv_obj_get_coords(lv_obj_get_parent(ui.List),&panel);
        assert(boot.x1>=0 && boot.y1>list.y2 && boot.x2<(int)w && boot.y2<(int)h);
        assert(panel.x1>=0 && panel.y1>=0 && panel.x2<(int)w && panel.y2<(int)h);
        const lv_state_t states[]={LV_STATE_DEFAULT,LV_STATE_CHECKED|LV_STATE_FOCUSED,
            LV_STATE_CHECKED|LV_STATE_FOCUSED|LV_STATE_FOCUS_KEY,LV_STATE_CHECKED|LV_STATE_PRESSED};
        for(unsigned s=0;s<4;s++) {
            lv_obj_remove_state(row,LV_STATE_ANY);lv_obj_remove_state(ref_row,LV_STATE_ANY);
            lv_obj_add_state(row,states[s]);lv_obj_add_state(ref_row,states[s]);settle(&ui);
            equivalent(row,ref_row);
        }
        lv_obj_remove_state(row,LV_STATE_ANY);lv_obj_add_state(row,LV_STATE_CHECKED|LV_STATE_FOCUSED);
        lv_obj_t *last=lv_group_get_obj_by_index(ui.Group,counts[n]-1U);
        lv_group_focus_obj(last);settle(&ui);
        assert(!lv_obj_has_state(row,LV_STATE_CHECKED) && lv_obj_has_state(last,LV_STATE_CHECKED));
        lv_area_t last_area;lv_obj_get_coords(last,&last_area);lv_obj_get_coords(ui.List,&list);
        assert(last_area.y2>=list.y1 && last_area.y1<=list.y2);
        lv_obj_get_coords(ui.BootButton,&after);assert(memcmp(&boot,&after,sizeof boot)==0);
        assert(kshim_lvgl_take_index(&ui)==-1);
        assert(lv_obj_send_event(ui.BootButton,LV_EVENT_CLICKED,NULL)==LV_RESULT_OK);
        settle(&ui);assert(kshim_lvgl_take_index(&ui)==(int)counts[n]-1);assert(kshim_lvgl_take_index(&ui)==-1);
        equivalent(ui.BootButton,ref_boot);
    }
    assert(kshim_lvgl_has_cjk_font(&ui)==scratch_enabled);
    if(scratch_enabled) {
        const lv_font_t *font=lv_obj_get_style_text_font(lv_obj_get_child(lv_group_get_obj_by_index(ui.Group,0),0),LV_PART_MAIN);
        lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(font,&glyph,0x542f,0));
        assert(!glyph.is_placeholder);
    }
    lv_obj_delete(reference);kshim_lvgl_deinit(&ui);kshim_lvgl_set_scratch(NULL,0);free(pixels);
}
int main(void) {
    check(320,240,0);check(360,800,1);check(1080,2340,1);check(1920,1080,1);
    puts("PASS: genuine native widget styles, default font metrics, CJK fallback, state feedback and safe rebuild/confirmation");
}
