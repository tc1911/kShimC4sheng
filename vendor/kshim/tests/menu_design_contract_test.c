#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/ui/menu_design_layout.h"
#ifndef EXPECTED_STYLE
#define EXPECTED_STYLE "FLAT"
#endif
static int inside(kshim_design_rect_t a, kshim_design_rect_t b) {
    return a.w > 0 && a.h > 0 && a.x >= b.x && a.y >= b.y &&
           a.x + a.w <= b.x + b.w && a.y + a.h <= b.y + b.h;
}
static int overlap(kshim_design_rect_t a, kshim_design_rect_t b) {
    return a.x < b.x+b.w && a.x+a.w > b.x && a.y < b.y+b.h && a.y+a.h > b.y;
}
static void check(unsigned w, unsigned h, unsigned n, unsigned override) {
    kshim_design_geometry_t g, again;
    assert(kshim_design_layout(&mMenuDesign,w,h,n,override,&g) == 0);
    assert(kshim_design_layout(&mMenuDesign,w,h,n,override,&again) == 0);
    assert(memcmp(&g,&again,sizeof g)==0);
    assert(inside(g.panel,(kshim_design_rect_t){0,0,(int32_t)w,(int32_t)h}));
    kshim_design_rect_t panel={0,0,g.panel.w,g.panel.h};
    assert(inside(g.list,panel)); assert(inside(g.boot,panel));
    assert(!overlap(g.list,g.boot));
    assert(g.count==n && g.columns>=1 && g.columns<=3);
    assert(g.scale_q8>=96 && g.scale_q8<=512);
    for(unsigned i=0;i<n;i++) {
        assert(g.items[i].w>0 && g.items[i].h>0);
        assert(g.items[i].x>=0 && g.items[i].y>=0);
        kshim_design_item_geometry_t item;
        kshim_design_item_layout(&mMenuDesign,&g,i,32,&item);
        kshim_design_rect_t box={0,0,g.items[i].w,g.items[i].h};
        assert(inside(item.label,box));
        if(item.icon.w) { assert(inside(item.icon,box)); assert(!overlap(item.icon,item.label)); }
        if(item.mark.w) { assert(inside(item.mark,box)); assert(!overlap(item.mark,item.label)); }
        if(item.prefix.w) { assert(inside(item.prefix,box)); assert(!overlap(item.prefix,item.label)); }
        if(g.horizontal) assert(g.items[i].y+g.items[i].h<=g.list.h);
        else assert(g.items[i].x+g.items[i].w<=g.list.w);
        for(unsigned j=0;j<i;j++) assert(!overlap(g.items[i],g.items[j]));
        if(i) assert(g.items[i].y>g.items[i-1].y ||
           (g.items[i].y==g.items[i-1].y && g.items[i].x>g.items[i-1].x));
    }
    assert(g.content_width>=g.list.w && g.content_height>=g.list.h);
    if(g.compact) assert(g.icon_size==0 && g.columns==1 && !g.horizontal && !g.split);
}
int main(void) {
    /* Subpixel design scales must not erase enabled borders or marks. */
    assert(kshim_design_stroke(0, 96)==0);
    assert(kshim_design_stroke(1, 96)==1);
    assert(kshim_design_stroke(2, 96)==1);
    assert(kshim_design_stroke(1, 512)==2);
    assert(strcmp(mMenuDesign.name,EXPECTED_STYLE)==0);
    if(!mMenuDesign.enabled) {
        assert(!strcmp(EXPECTED_STYLE,"FLAT") || !strcmp(EXPECTED_STYLE,"LVGL_DEFAULT"));
        printf("%s: custom design bypass\n",EXPECTED_STYLE); return 0;
    }
    if(mMenuDesign.kind==KSHIM_DESIGN_STAR_CHART) {
        assert(mMenuDesign.max_width>=360 && mMenuDesign.max_width<=1600);
        assert(mMenuDesign.row_height>=36 && mMenuDesign.row_height<=80);
    } else {
        assert(mMenuDesign.max_width>=360 && mMenuDesign.max_width<=1200);
        assert(mMenuDesign.row_height>=36 && mMenuDesign.row_height<=64);
    }
    const unsigned sizes[][2]={{64,48},{128,64},{159,96},{160,160},{240,320},{320,240},{559,360},{560,360},{640,360},{720,480},{960,720},{1000,720},{1080,2340},{1920,1080},{3040,1904},{4096,2160}};
    const unsigned counts[]={1,2,3,7,49,50,51};
    const unsigned overrides[]={0,96,256,384,512};
    for(unsigned s=0;s<sizeof sizes/sizeof sizes[0];s++)
        for(unsigned n=0;n<sizeof counts/sizeof counts[0];n++)
            for(unsigned q=0;q<sizeof overrides/sizeof overrides[0];q++)
                check(sizes[s][0],sizes[s][1],counts[n],overrides[q]);
    for(unsigned w=64;w<=1024;w+=17)
        for(unsigned h=48;h<=768;h+=31) check(w,h,49,0);
    kshim_design_geometry_t g;
    assert(kshim_design_layout(NULL,640,480,1,0,&g)<0);
    assert(kshim_design_layout(&mMenuDesign,640,480,1,0,NULL)<0);
    assert(kshim_design_layout(&mMenuDesign,0,480,1,0,&g)<0);
    assert(kshim_design_layout(&mMenuDesign,640,480,0,0,&g)<0);
    assert(kshim_design_layout(&mMenuDesign,640,480,KSHIM_DESIGN_MAX_ENTRIES+1U,0,&g)<0);
    assert(kshim_design_layout(&mMenuDesign,UINT32_MAX,480,1,0,&g)<0);
    if(mMenuDesign.kind==KSHIM_DESIGN_BENTO) {
        assert(kshim_design_layout(&mMenuDesign,1920,1080,7,0,&g)==0);
        assert(g.columns==2 && g.items[0].w>g.items[1].w);
    }
    if(mMenuDesign.kind==KSHIM_DESIGN_SURFACE) {
        assert(kshim_design_layout(&mMenuDesign,1920,1080,7,0,&g)==0);
        assert(g.split && g.boot.x>g.list.x+g.list.w);
    }
    if(mMenuDesign.kind==KSHIM_DESIGN_CLOVER) {
        const unsigned displays[][2]={{1920,1080},{1080,2340}};
        for(unsigned size=0;size<2;size++) {
            for(unsigned entries=1;entries<=49;entries+=6) {
                assert(kshim_design_layout(&mMenuDesign,displays[size][0],displays[size][1],entries,0,&g)==0);
                assert(g.horizontal && !g.compact);
                assert(g.status.y>=g.list.y+g.list.h);
                assert(g.boot.y>=g.status.y+g.status.h);
                kshim_design_item_geometry_t item;
                kshim_design_item_layout(&mMenuDesign,&g,0,36,&item);
                assert(item.icon.w>0 && item.mark.w>0);
            }
        }
    }
    if(mMenuDesign.kind==KSHIM_DESIGN_STAR_CHART) {
        const unsigned starsizes[][2]={{3040,1904},{1920,1080}};
        for(unsigned size=0;size<2;size++) {
            assert(kshim_design_layout(&mMenuDesign,starsizes[size][0],starsizes[size][1],7,0,&g)==0);
            assert(!g.compact && g.split);
            assert(g.items[6].y+g.items[6].h+g.gutter<=g.list.h);
            kshim_design_rect_t panel={0,0,g.panel.w,g.panel.h};
            assert(inside(g.title,panel)); assert(inside(g.status,panel));
            assert(inside(g.detail,panel)); assert(inside(g.notice,panel));
            assert(inside(g.footer,panel)); assert(inside(g.credit,panel));
            assert(inside(g.language,panel)); assert(inside(g.illustration,panel));
            assert(!overlap(g.illustration,g.status));
            assert(!overlap(g.illustration,g.detail));
            assert(!overlap(g.illustration,g.notice));
            assert(!overlap(g.illustration,g.boot));
            assert(!overlap(g.status,g.detail));
            assert(!overlap(g.status,g.notice));
            assert(!overlap(g.status,g.boot));
            assert(!overlap(g.detail,g.notice));
            assert(!overlap(g.detail,g.boot));
            assert(!overlap(g.notice,g.boot));
            assert(!overlap(g.list,g.illustration));
            assert(!overlap(g.list,g.status));
            assert(!overlap(g.list,g.detail));
            assert(!overlap(g.list,g.notice));
            assert(!overlap(g.list,g.boot));
            assert(!overlap(g.footer,g.credit));
            assert(!overlap(g.title,g.language));
            assert(g.detail.h>=kshim_design_px(60U,g.scale_q8));
        }
#if defined(CONFIG_KSHIM_MENU_LANGUAGE_SWITCH) && CONFIG_KSHIM_MENU_LANGUAGE_SWITCH
        assert(KSHIM_DESIGN_MAX_ENTRIES==53U);
        assert(kshim_design_layout(&mMenuDesign,1920,1080,KSHIM_DESIGN_MAX_ENTRIES,0,&g)==0);
        assert(g.count==KSHIM_DESIGN_MAX_ENTRIES);
        assert(g.content_height>g.list.h);
        for(unsigned i=0;i<KSHIM_DESIGN_MAX_ENTRIES;i++) {
            assert(g.items[i].w>0 && g.items[i].h>0);
            if(i) assert(g.items[i].y>g.items[i-1].y && g.items[i].x==g.items[i-1].x);
        }
#endif
    }
    if(mMenuDesign.kind==KSHIM_DESIGN_MATERIAL2) assert(mMenuDesign.row_shadow==0);
    printf("PASS %s: geometry boundaries, 51-item order, deterministic layout, compact fallback\n",EXPECTED_STYLE);
}
