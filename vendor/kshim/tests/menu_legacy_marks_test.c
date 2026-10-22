/* Preserve the original mark reservation when a dense icon card has little room. */
#include <assert.h>
#include <stdio.h>
#include "../src/ui/menu_design_layout.h"
int main(void) {
    kshim_menu_design_t d=mMenuDesign;
    d.kind=KSHIM_DESIGN_HARMONYOS; d.mark=KSHIM_MARK_CHECK;
    kshim_design_geometry_t g={0};
    g.count=1; g.columns=2; g.scale_q8=256; g.icon_size=96;
    g.items[0]=(kshim_design_rect_t){0,0,140,180};
    kshim_design_item_geometry_t item;
    kshim_design_item_layout(&d,&g,0,20,&item);
    assert(item.icon.w>0 && item.mark.w>0);
    assert(item.mark.x>=0 && item.mark.x+item.mark.w<=g.items[0].w);
    assert(item.mark.y+item.mark.h<=item.label.y);
    puts("PASS: legacy dense-card selection mark remains reserved");
}
