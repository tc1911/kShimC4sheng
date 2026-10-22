/* Real-pixel and fixed-chrome regression for the four additional themes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include <lvgl_port.h>
#include "../src/ui/menu_style.h"
#include "menu_design_lvgl_checks.h"
static void settle(kshim_lvgl_t *ui) {
    for(unsigned i=0;i<48U;i++) kshim_lvgl_frame(ui,16U);
}
/* Sample only the intersection actually visible inside the scrolling list.
 * An offscreen row can lie underneath Holo's cyan header: those header pixels
 * are not a stale selection mark belonging to that row. */
static unsigned visible_mark_pixels(kshim_lvgl_t *ui,unsigned index) {
    kshim_design_geometry_t g;
    assert(kshim_design_layout(&mMenuDesign,ui->Framebuffer->width,ui->Framebuffer->height,
        (uint32_t)ui->EntryCount,KshimTestDesignScale(),&g)==0);
    lv_obj_t *row=lv_group_get_obj_by_index(ui->Group,index);
    lv_area_t a,view;lv_obj_get_coords(row,&a);lv_obj_get_coords(ui->List,&view);
    kshim_design_item_geometry_t item;
    kshim_design_item_layout(&mMenuDesign,&g,index,
        lv_font_get_line_height(lv_obj_get_style_text_font(row,LV_PART_MAIN)),&item);
    if(!item.mark.w) return 0;
    int32_t left=kshim_design_max(0,kshim_design_max(view.x1,a.x1+item.mark.x));
    int32_t top=kshim_design_max(0,kshim_design_max(view.y1,a.y1+item.mark.y));
    int32_t right=kshim_design_min((int32_t)ui->Framebuffer->width-1,
        kshim_design_min(view.x2,a.x1+item.mark.x+item.mark.w-1));
    int32_t bottom=kshim_design_min((int32_t)ui->Framebuffer->height-1,
        kshim_design_min(view.y2,a.y1+item.mark.y+item.mark.h-1));
    unsigned count=0;
    for(int32_t y=top;y<=bottom;y++) {
        const uint32_t *pixels=(const uint32_t *)(const void *)(ui->Framebuffer->render_address+(size_t)y*ui->Framebuffer->stride);
        for(int32_t x=left;x<=right;x++) count+=(pixels[x]&0xffffffU)==mMenuStyle.FocusBorder;
    }
    return count;
}
static void check_size(unsigned width,unsigned height) {
    size_t bytes=(size_t)width*height*4U;
    uint8_t *memory=malloc(bytes+128U);
    assert(memory); memset(memory,0xa5,bytes+128U);
    kshim_framebuffer_config_t config={
        .render_address=(uintptr_t)(memory+64U),.width=width,.height=height,
        .stride=width*4U,.bpp=32U,.format=KSHIM_FB_FORMAT_XRGB8888,.buffer_size=bytes};
    kshim_framebuffer_t fb; assert(kshim_fb_init(&fb,&config)==0);
    static uint8_t scratch[4U<<20] __attribute__((aligned(64)));
    kshim_lvgl_set_scratch(scratch,sizeof scratch);
    kshim_lvgl_t ui; assert(kshim_lvgl_init(&ui,&fb,NULL)==0);
    assert(kshim_lvgl_has_cjk_font(&ui));
    char labels[49][128];const char *names[49];
    for(unsigned i=0;i<49;i++) {
        snprintf(labels[i],sizeof labels[i],"Image %02u / 启动镜像 - long entry for label clipping",i);
        names[i]=labels[i];
    }
    const unsigned counts[]={2,7,49,2};
    for(unsigned c=0;c<sizeof counts/sizeof counts[0];c++) {
        printf("CASE %s %ux%u count=%u scale=%u\n",mMenuStyle.Name,width,height,counts[c],(unsigned)KshimTestDesignScale());
        fflush(stdout);
        assert(kshim_lvgl_set_entries(&ui,names,counts[c],0)==0);settle(&ui);
        KshimTestDesignLayout(&ui);
        kshim_design_geometry_t g;
        assert(kshim_design_layout(&mMenuDesign,width,height,counts[c],KshimTestDesignScale(),&g)==0);
        lv_obj_t *panel=lv_obj_get_parent(ui.List),*title=lv_obj_get_child(panel,0);
        if(!g.compact) {
            if(mMenuDesign.kind==KSHIM_DESIGN_ADWAITA) {
                assert(lv_obj_get_style_text_align(title,LV_PART_MAIN)==LV_TEXT_ALIGN_CENTER);
                assert(lv_obj_get_style_clip_corner(ui.List,LV_PART_MAIN));
            }
            if(mMenuDesign.kind==KSHIM_DESIGN_ONE_UI && height>width) {
                assert(lv_obj_get_style_text_align(title,LV_PART_MAIN)==LV_TEXT_ALIGN_CENTER);
                assert(!lv_obj_get_style_clip_corner(ui.List,LV_PART_MAIN));
                assert(lv_obj_get_style_radius(ui.List,LV_PART_MAIN)==0);
                assert(mMenuDesign.mark==KSHIM_MARK_PILL && g.icon_size>0);
                if(width==1080 && height==2340 && !KshimTestDesignScale())
                    assert(g.panel.y+g.boot.y+g.boot.h>(int32_t)height*9/10);
            }
            if(mMenuDesign.kind==KSHIM_DESIGN_METRO) {
                assert(lv_obj_get_style_text_align(title,LV_PART_MAIN)==LV_TEXT_ALIGN_LEFT);
                assert(lv_obj_get_style_border_width(ui.BootButton,LV_PART_MAIN)>0);
            }
            if(mMenuDesign.kind==KSHIM_DESIGN_HOLO)
                assert(lv_obj_get_style_border_width(ui.BootButton,LV_PART_MAIN)>0);
            /* Real mark pixels move, not merely the LVGL focus flag. */
            assert(visible_mark_pixels(&ui,0)>3U);
            lv_area_t before,after;lv_obj_get_coords(ui.BootButton,&before);
            lv_group_focus_obj(lv_group_get_obj_by_index(ui.Group,1));settle(&ui);
            assert(visible_mark_pixels(&ui,0)==0U);
            assert(visible_mark_pixels(&ui,1)>3U);
            lv_obj_get_coords(ui.BootButton,&after);
            assert(before.x1==after.x1 && before.x2==after.x2 && before.y1==after.y1 && before.y2==after.y2);
            assert(kshim_lvgl_take_index(&ui)==-1);
        }
        lv_obj_t *last=lv_group_get_obj_by_index(ui.Group,counts[c]-1U);
        lv_group_focus_obj(last);settle(&ui);KshimTestDesignLayout(&ui);
        assert(kshim_lvgl_take_index(&ui)==-1);
        assert(lv_obj_send_event(ui.BootButton,LV_EVENT_CLICKED,NULL)==LV_RESULT_OK);
        settle(&ui);assert(kshim_lvgl_take_index(&ui)==(int)counts[c]-1);
        assert(kshim_lvgl_take_index(&ui)==-1);
    }
    kshim_lvgl_deinit(&ui);kshim_lvgl_set_scratch(NULL,0U);
    for(unsigned i=0;i<64U;i++) {assert(memory[i]==0xa5);assert(memory[bytes+64U+i]==0xa5);}
    free(memory);
}
int main(void) {
    check_size(320,240);check_size(360,800);check_size(1080,2340);check_size(1920,1080);
    printf("PASS: %s actual symbols, CJK, adaptive chrome, 49-item rebuild and confirmation\n",mMenuStyle.Name);
}
