#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/ui/menu_design_layout.h"
#ifndef EXPECTED_STYLE
#error EXPECTED_STYLE required
#endif
static void check(unsigned w,unsigned h,unsigned n,unsigned scale) {
    kshim_design_geometry_t g;
    assert(kshim_design_layout(&mMenuDesign,w,h,n,scale,&g)==0);
    assert(g.panel.x>=0 && g.panel.y>=0);
    assert(g.panel.x+g.panel.w<=(int)w && g.panel.y+g.panel.h<=(int)h);
    if(g.split) assert(g.boot.x>g.list.x+g.list.w);
    else assert(g.boot.y>=g.list.y+g.list.h);
    assert(g.boot.y+g.boot.h<=g.panel.h);
    assert(g.columns==1 && !g.horizontal);
    if(g.split) assert(!strcmp(EXPECTED_STYLE,"ONE_UI") && w>h);
    for(unsigned i=0;i<n;i++) {
        kshim_design_item_geometry_t m;
        kshim_design_item_layout(&mMenuDesign,&g,i,32,&m);
        assert(m.label.w>0 && m.label.h>0);
        assert(m.label.x>=0 && m.label.x+m.label.w<=g.items[i].w);
        assert(m.label.y>=0 && m.label.y+m.label.h<=g.items[i].h);
        if(i) assert(g.items[i].y>=g.items[i-1].y+g.items[i-1].h);
    }
    if(!strcmp(EXPECTED_STYLE,"ONE_UI") && !scale && w==1080 && h==2340) {
        assert(!g.compact);
        assert(g.panel.y+g.boot.y+g.boot.h>(int)h*9/10);
        if(n<=3) assert(g.panel.y+g.list.y>(int)h/2);
        if(n==49) assert(g.list.h>(int)h/2);
    }
}
int main(void) {
    assert(mMenuDesign.enabled && "new theme must not silently fall back to FLAT");
    assert(!strcmp(mMenuDesign.name,EXPECTED_STYLE));
    assert(!mMenuDesign.background && !mMenuDesign.row_shadow && !mMenuDesign.focus_glow);
    if(!strcmp(EXPECTED_STYLE,"METRO")) {
        assert(mMenuDesign.title_size>=40 && mMenuDesign.row_size>=24);
        assert(!mMenuDesign.panel_radius && !mMenuDesign.row_radius);
    }
    if(!strcmp(EXPECTED_STYLE,"HOLO")) assert(!mMenuDesign.row_radius && !mMenuDesign.gap);
    const unsigned sizes[][2]={{64,48},{128,64},{240,320},{320,240},{360,800},{559,360},{560,360},{720,480},{1080,2340},{1920,1080}};
    const unsigned counts[]={1,2,3,7,49};
    const unsigned scales[]={0,96,256,384,512};
    for(unsigned s=0;s<sizeof sizes/sizeof sizes[0];s++)
      for(unsigned n=0;n<sizeof counts/sizeof counts[0];n++)
       for(unsigned q=0;q<sizeof scales/sizeof scales[0];q++) check(sizes[s][0],sizes[s][1],counts[n],scales[q]);
    puts("PASS: " EXPECTED_STYLE " registration, independent geometry, readable reservations and viewport bounds");
}
