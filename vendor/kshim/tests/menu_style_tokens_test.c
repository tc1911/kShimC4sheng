#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/ui/menu_style.h"

/* 无 LVGL 依赖的预设契约检查，并输出实际编译得到的色值。 */
int main(void)
{
    assert(mMenuStyle.Name != NULL && mMenuStyle.Name[0] != '\0');
    assert(mMenuStyle.RadiusMin <= mMenuStyle.RadiusMax);
    assert(mMenuStyle.RowGapDivisor > 0U);
    assert(mMenuStyle.RowBorderWidth <= 3U);
    assert(mMenuStyle.PanelOutlineWidth <= 2U);
    assert(mMenuStyle.AnimatedBackdrop <= 1U);
    assert(mMenuStyle.ItemOpacity > 0U && mMenuStyle.FocusOpacity > 0U);
    for (uint32_t Height = 0U; Height <= 4096U; Height++) {
        uint32_t Radius = kshim_menu_style_radius(Height);
        assert(Radius <= Height / 2U);
        assert(Radius <= mMenuStyle.RadiusMax);
        if (mMenuStyle.RadiusMax == 0U)
            assert(Radius == 0U);
    }
    assert(kshim_menu_style_radius(UINT32_MAX) <= mMenuStyle.RadiusMax);
    if (strcmp(mMenuStyle.Name, "FLAT") == 0) {
        assert(mMenuStyle.Panel == 0x161c3eU);
        assert(mMenuStyle.PanelOpacity == 128U);
        assert(mMenuStyle.ItemOpacity == 20U);
        assert(mMenuStyle.FocusOpacity == 214U);
        assert(mMenuStyle.AnimatedBackdrop == 1U);
        assert(mMenuStyle.RadiusMin == 8U && mMenuStyle.RadiusMax == 16U);
        assert(mMenuStyle.RowBorderWidth == 0U && mMenuStyle.PanelOutlineWidth == 0U);
    } else if (mMenuStyle.Modern == 0U) {
        assert(mMenuStyle.AnimatedBackdrop == 0U);
        assert(mMenuStyle.PanelOpacity == 255U);
        assert(mMenuStyle.ItemOpacity == 255U);
        assert(mMenuStyle.FocusOpacity == 255U);
    }
    if (mMenuStyle.Modern != 0U) {
        assert(mMenuStyle.Layout <= 5U);
        assert(mMenuStyle.PressScale >= 248U && mMenuStyle.PressScale <= 256U);
        assert(mMenuStyle.RowGap <= 12U);
        assert(mMenuStyle.FocusPill <= 1U);
        assert(mMenuStyle.BootShadow <= 16U);
        assert(mMenuStyle.AnimatedBackdrop == 0U);
    }
    printf("%s %06x %06x %06x %06x %06x %06x %06x %06x %06x %06x %06x\n",
           mMenuStyle.Name, (unsigned)mMenuStyle.Screen, (unsigned)mMenuStyle.Panel,
           (unsigned)mMenuStyle.Text, (unsigned)mMenuStyle.Muted,
           (unsigned)mMenuStyle.Item, (unsigned)mMenuStyle.Focus,
           (unsigned)mMenuStyle.FocusText, (unsigned)mMenuStyle.Boot,
           (unsigned)mMenuStyle.BootPressed, (unsigned)mMenuStyle.BootText,
           (unsigned)mMenuStyle.Border);
    return 0;
}
