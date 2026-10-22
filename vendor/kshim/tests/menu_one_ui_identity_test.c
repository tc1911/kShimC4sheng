#include <assert.h>
#include <stdio.h>
#include "../src/ui/menu_design_layout.h"
int main(void) {
    assert(mMenuDesign.kind==KSHIM_DESIGN_ONE_UI);
    assert(mMenuDesign.layout==KSHIM_LAYOUT_SPLIT);
    assert(mMenuDesign.mark==KSHIM_MARK_PILL);
    assert(mMenuDesign.icon_size>=28U && mMenuDesign.row_height>=60U);
    kshim_design_geometry_t g;
    const unsigned counts[]={2,7,49};
    for(unsigned i=0;i<3;i++) {
        assert(kshim_design_layout(&mMenuDesign,1080,2340,counts[i],0,&g)==0);
        assert(!g.compact && !g.split && g.columns==1);
        assert(g.panel.y+g.boot.y+g.boot.h>2340*9/10);
        if(counts[i]<=7) assert(g.panel.y+g.list.y>=2340*45/100);
        else assert(g.list.h>2340/2);
        assert(g.title.y+g.title.h<=g.status.y);
        assert(g.status.y+g.status.h<g.list.y);
        assert(g.boot.w>=g.list.w*3/4);
        kshim_design_item_geometry_t item;
        kshim_design_item_layout(&mMenuDesign,&g,0,32,&item);
        assert(item.icon.w>0 && item.mark.x<item.icon.x && item.icon.x+item.icon.w<item.label.x);
        assert(kshim_design_layout(&mMenuDesign,1920,1080,counts[i],0,&g)==0);
        assert(!g.compact && g.split && g.columns==1);
        assert(g.list.x+g.list.w<g.status.x);
        assert(g.boot.x>=g.status.x && g.boot.y>=g.status.y+g.status.h);
        assert(g.detail.w>0 && g.detail.h>0);
    }
    assert(kshim_design_layout(&mMenuDesign,640,360,2,0,&g)==0);
    assert(g.split && !g.compact);
    assert(g.items[1].y+g.items[1].h<=g.list.h);
    puts("PASS: One UI viewing/interaction layout, wide action, leading icon/pill and tablet split");
}
