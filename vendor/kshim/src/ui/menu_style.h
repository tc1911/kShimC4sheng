#ifndef KSHIM_UI_MENU_STYLE_H
#define KSHIM_UI_MENU_STYLE_H
#include <stdint.h>
#if defined(CONFIG_KSHIM_MENU_STYLE_LVGL_DEFAULT) && CONFIG_KSHIM_MENU_STYLE_LVGL_DEFAULT
#define KSHIM_MENU_IS_NATIVE 1
#else
#define KSHIM_MENU_IS_NATIVE 0
#endif
/* Compile-time palette. Only the selected preset is emitted. */
typedef struct {
    const char *Name;
    uint32_t Screen;
    uint32_t Panel;
    uint32_t Text;
    uint32_t Muted;
    uint32_t Item;
    uint32_t Focus;
    uint32_t FocusText;
    uint32_t Scrollbar;
    uint32_t Boot;
    uint32_t BootPressed;
    uint32_t BootText;
    uint32_t Border;
    uint32_t FocusBorder;
    uint8_t PanelOpacity;
    uint8_t ItemOpacity;
    uint8_t FocusOpacity;
    uint8_t RowBorderWidth;
    uint8_t PanelOutlineWidth;
    uint8_t BorderAtRest;
    uint8_t LeftBorderOnly;
    uint8_t RadiusMin;
    uint8_t RadiusMax;
    uint8_t RowGapDivisor;
    uint8_t AnimatedBackdrop;
    uint8_t Modern;
    uint8_t Layout;
    uint8_t Divider;
    uint8_t PanelRadius;
    uint8_t BootRadius;
    uint8_t RowGap;
    uint8_t FocusSlide;
    uint16_t PressScale;
    uint8_t PanelShadow;
    uint8_t RowShadow;
    uint8_t FocusGlow;
    uint8_t FocusOutline;
    uint32_t ScreenEnd;
    uint32_t PanelEnd;
    uint32_t FocusEnd;
    uint32_t Press;
    uint32_t PressText;
    uint32_t Shadow;
    uint32_t Navigation;
    uint8_t FocusPill;
    uint8_t BootShadow;
} kshim_menu_style_t;

#if (defined(CONFIG_KSHIM_MENU_STYLE_FLAT) && CONFIG_KSHIM_MENU_STYLE_FLAT) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_CLASSIC) && CONFIG_KSHIM_MENU_STYLE_CLASSIC) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_CARDS) && CONFIG_KSHIM_MENU_STYLE_CARDS) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_TERMINAL) && CONFIG_KSHIM_MENU_STYLE_TERMINAL) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_MINIMAL) && CONFIG_KSHIM_MENU_STYLE_MINIMAL) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_HIGH_CONTRAST) && CONFIG_KSHIM_MENU_STYLE_HIGH_CONTRAST) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT) && CONFIG_KSHIM_MENU_STYLE_FLUENT) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_MATERIAL3) && CONFIG_KSHIM_MENU_STYLE_MATERIAL3) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_SURFACE) && CONFIG_KSHIM_MENU_STYLE_SURFACE) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_CUPERTINO) && CONFIG_KSHIM_MENU_STYLE_CUPERTINO) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_GLASS) && CONFIG_KSHIM_MENU_STYLE_GLASS) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_AURORA) && CONFIG_KSHIM_MENU_STYLE_AURORA) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_SOFT_UI) && CONFIG_KSHIM_MENU_STYLE_SOFT_UI) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_BENTO) && CONFIG_KSHIM_MENU_STYLE_BENTO) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_NEON) && CONFIG_KSHIM_MENU_STYLE_NEON) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_NORD) && CONFIG_KSHIM_MENU_STYLE_NORD) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_MATERIAL2) && CONFIG_KSHIM_MENU_STYLE_MATERIAL2) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_MATERIAL2_DARK) && CONFIG_KSHIM_MENU_STYLE_MATERIAL2_DARK) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT2) && CONFIG_KSHIM_MENU_STYLE_FLUENT2) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT2_DARK) && CONFIG_KSHIM_MENU_STYLE_FLUENT2_DARK) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT_DARK) && CONFIG_KSHIM_MENU_STYLE_FLUENT_DARK) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_SURFACE_DARK) && CONFIG_KSHIM_MENU_STYLE_SURFACE_DARK) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_HARMONYOS) && CONFIG_KSHIM_MENU_STYLE_HARMONYOS) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_CLOVER) && CONFIG_KSHIM_MENU_STYLE_CLOVER) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_IOS_HIG) && CONFIG_KSHIM_MENU_STYLE_IOS_HIG) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_METRO) && CONFIG_KSHIM_MENU_STYLE_METRO) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_ONE_UI) && CONFIG_KSHIM_MENU_STYLE_ONE_UI) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_ADWAITA) && CONFIG_KSHIM_MENU_STYLE_ADWAITA) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_HOLO) && CONFIG_KSHIM_MENU_STYLE_HOLO) + \
    (defined(CONFIG_KSHIM_MENU_STYLE_LVGL_DEFAULT) && CONFIG_KSHIM_MENU_STYLE_LVGL_DEFAULT) > 1
#error "Select exactly one KSHIM_MENU_STYLE preset"
#endif

#if KSHIM_MENU_IS_NATIVE
/* No simulated palette: actual colors and widget styling come from LVGL. */
static const kshim_menu_style_t mMenuStyle = {
    .Name="LVGL_DEFAULT", .PanelOpacity=255U, .ItemOpacity=255U,
    .FocusOpacity=255U, .RowGapDivisor=4U
};
#elif defined(CONFIG_KSHIM_MENU_STYLE_METRO) && CONFIG_KSHIM_MENU_STYLE_METRO
#include "styles/metro.h"
#elif defined(CONFIG_KSHIM_MENU_STYLE_ONE_UI) && CONFIG_KSHIM_MENU_STYLE_ONE_UI
#include "styles/one_ui.h"
#elif defined(CONFIG_KSHIM_MENU_STYLE_ADWAITA) && CONFIG_KSHIM_MENU_STYLE_ADWAITA
#include "styles/adwaita.h"
#elif defined(CONFIG_KSHIM_MENU_STYLE_HOLO) && CONFIG_KSHIM_MENU_STYLE_HOLO
#include "styles/holo.h"
#elif defined(CONFIG_KSHIM_MENU_STYLE_STAR_CHART) && CONFIG_KSHIM_MENU_STYLE_STAR_CHART
#include "styles/star_chart.h"
#elif (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT) && CONFIG_KSHIM_MENU_STYLE_FLUENT) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_MATERIAL3) && CONFIG_KSHIM_MENU_STYLE_MATERIAL3) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_SURFACE) && CONFIG_KSHIM_MENU_STYLE_SURFACE) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_CUPERTINO) && CONFIG_KSHIM_MENU_STYLE_CUPERTINO) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_GLASS) && CONFIG_KSHIM_MENU_STYLE_GLASS) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_AURORA) && CONFIG_KSHIM_MENU_STYLE_AURORA) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_SOFT_UI) && CONFIG_KSHIM_MENU_STYLE_SOFT_UI) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_BENTO) && CONFIG_KSHIM_MENU_STYLE_BENTO) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_NEON) && CONFIG_KSHIM_MENU_STYLE_NEON) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_NORD) && CONFIG_KSHIM_MENU_STYLE_NORD) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_MATERIAL2) && CONFIG_KSHIM_MENU_STYLE_MATERIAL2) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_MATERIAL2_DARK) && CONFIG_KSHIM_MENU_STYLE_MATERIAL2_DARK) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT2) && CONFIG_KSHIM_MENU_STYLE_FLUENT2) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT2_DARK) && CONFIG_KSHIM_MENU_STYLE_FLUENT2_DARK) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_FLUENT_DARK) && CONFIG_KSHIM_MENU_STYLE_FLUENT_DARK) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_SURFACE_DARK) && CONFIG_KSHIM_MENU_STYLE_SURFACE_DARK) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_HARMONYOS) && CONFIG_KSHIM_MENU_STYLE_HARMONYOS) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_CLOVER) && CONFIG_KSHIM_MENU_STYLE_CLOVER) || \
    (defined(CONFIG_KSHIM_MENU_STYLE_IOS_HIG) && CONFIG_KSHIM_MENU_STYLE_IOS_HIG)
#include "styles/modern.h"
#else
static const kshim_menu_style_t mMenuStyle = {
#if defined(CONFIG_KSHIM_MENU_STYLE_CLASSIC) && CONFIG_KSHIM_MENU_STYLE_CLASSIC
    .Name = "CLASSIC",
    .Screen = 0x101828U,
    .Panel = 0x202c44U,
    .Text = 0xffffffU,
    .Muted = 0xc7d1e0U,
    .Item = 0x182338U,
    .Focus = 0x1d4ed8U,
    .FocusText = 0xffffffU,
    .Scrollbar = 0xaabbd2U,
    .Boot = 0xc7d1e0U,
    .BootPressed = 0xaabbd2U,
    .BootText = 0x101828U,
    .Border = 0x8293aaU,
    .FocusBorder = 0xc7d1e0U,
    .PanelOpacity = 255U,
    .ItemOpacity = 255U,
    .FocusOpacity = 255U,
    .RowBorderWidth = 1U,
    .PanelOutlineWidth = 2U,
    .BorderAtRest = 1U,
    .LeftBorderOnly = 0U,
    .RadiusMin = 0U,
    .RadiusMax = 0U,
    .RowGapDivisor = 4U,
    .AnimatedBackdrop = 0U,
#elif defined(CONFIG_KSHIM_MENU_STYLE_CARDS) && CONFIG_KSHIM_MENU_STYLE_CARDS
    .Name = "CARDS",
    .Screen = 0xe8eef6U,
    .Panel = 0xf6f8fcU,
    .Text = 0x17243bU,
    .Muted = 0x46546bU,
    .Item = 0xffffffU,
    .Focus = 0xdae7ffU,
    .FocusText = 0x173d83U,
    .Scrollbar = 0x1d4ed8U,
    .Boot = 0x1d4ed8U,
    .BootPressed = 0x1e40afU,
    .BootText = 0xffffffU,
    .Border = 0xc4d0e3U,
    .FocusBorder = 0x1d4ed8U,
    .PanelOpacity = 255U,
    .ItemOpacity = 255U,
    .FocusOpacity = 255U,
    .RowBorderWidth = 1U,
    .PanelOutlineWidth = 0U,
    .BorderAtRest = 1U,
    .LeftBorderOnly = 0U,
    .RadiusMin = 12U,
    .RadiusMax = 24U,
    .RowGapDivisor = 2U,
    .AnimatedBackdrop = 0U,
#elif defined(CONFIG_KSHIM_MENU_STYLE_TERMINAL) && CONFIG_KSHIM_MENU_STYLE_TERMINAL
    .Name = "TERMINAL",
    .Screen = 0x000805U,
    .Panel = 0x020e08U,
    .Text = 0xa7ffc0U,
    .Muted = 0x87ce98U,
    .Item = 0x02150bU,
    .Focus = 0x8ff0a4U,
    .FocusText = 0x07120aU,
    .Scrollbar = 0x8ff0a4U,
    .Boot = 0x8ff0a4U,
    .BootPressed = 0x70c788U,
    .BootText = 0x07120aU,
    .Border = 0x389e58U,
    .FocusBorder = 0x8ff0a4U,
    .PanelOpacity = 255U,
    .ItemOpacity = 255U,
    .FocusOpacity = 255U,
    .RowBorderWidth = 1U,
    .PanelOutlineWidth = 1U,
    .BorderAtRest = 1U,
    .LeftBorderOnly = 0U,
    .RadiusMin = 0U,
    .RadiusMax = 0U,
    .RowGapDivisor = 8U,
    .AnimatedBackdrop = 0U,
#elif defined(CONFIG_KSHIM_MENU_STYLE_MINIMAL) && CONFIG_KSHIM_MENU_STYLE_MINIMAL
    .Name = "MINIMAL",
    .Screen = 0x080808U,
    .Panel = 0x080808U,
    .Text = 0xffffffU,
    .Muted = 0xbfbfbfU,
    .Item = 0x080808U,
    .Focus = 0x242424U,
    .FocusText = 0xffffffU,
    .Scrollbar = 0xbfbfbfU,
    .Boot = 0xffffffU,
    .BootPressed = 0xd6d6d6U,
    .BootText = 0x080808U,
    .Border = 0xbfbfbfU,
    .FocusBorder = 0xffffffU,
    .PanelOpacity = 255U,
    .ItemOpacity = 255U,
    .FocusOpacity = 255U,
    .RowBorderWidth = 3U,
    .PanelOutlineWidth = 0U,
    .BorderAtRest = 0U,
    .LeftBorderOnly = 1U,
    .RadiusMin = 0U,
    .RadiusMax = 0U,
    .RowGapDivisor = 6U,
    .AnimatedBackdrop = 0U,
#elif defined(CONFIG_KSHIM_MENU_STYLE_HIGH_CONTRAST) && CONFIG_KSHIM_MENU_STYLE_HIGH_CONTRAST
    .Name = "HIGH_CONTRAST",
    .Screen = 0x000000U,
    .Panel = 0x000000U,
    .Text = 0xffffffU,
    .Muted = 0xffffffU,
    .Item = 0x000000U,
    .Focus = 0xffffffU,
    .FocusText = 0x000000U,
    .Scrollbar = 0xffffffU,
    .Boot = 0xffffffU,
    .BootPressed = 0xccccccU,
    .BootText = 0x000000U,
    .Border = 0xffffffU,
    .FocusBorder = 0xffffffU,
    .PanelOpacity = 255U,
    .ItemOpacity = 255U,
    .FocusOpacity = 255U,
    .RowBorderWidth = 2U,
    .PanelOutlineWidth = 2U,
    .BorderAtRest = 1U,
    .LeftBorderOnly = 0U,
    .RadiusMin = 0U,
    .RadiusMax = 0U,
    .RowGapDivisor = 4U,
    .AnimatedBackdrop = 0U,
#else
    .Name = "FLAT",
    .Screen = 0x13235aU,
    .Panel = 0x161c3eU,
    .Text = 0xffffffU,
    .Muted = 0xc9d1eeU,
    .Item = 0xffffffU,
    .Focus = 0x2f6bf0U,
    .FocusText = 0xffffffU,
    .Scrollbar = 0x7aa2ffU,
    .Boot = 0xf4f7ffU,
    .BootPressed = 0xdfe5fbU,
    .BootText = 0x161c3eU,
    .Border = 0xc9d1eeU,
    .FocusBorder = 0x7aa2ffU,
    .PanelOpacity = 128U,
    .ItemOpacity = 20U,
    .FocusOpacity = 214U,
    .RowBorderWidth = 0U,
    .PanelOutlineWidth = 0U,
    .BorderAtRest = 0U,
    .LeftBorderOnly = 0U,
    .RadiusMin = 8U,
    .RadiusMax = 16U,
    .RowGapDivisor = 4U,
    .AnimatedBackdrop = 1U,
#endif
};
#endif
static inline uint32_t kshim_menu_style_radius(uint32_t Height) {
    uint32_t Radius = Height / 5U;
    if (Radius < mMenuStyle.RadiusMin) Radius = mMenuStyle.RadiusMin;
    if (Radius > mMenuStyle.RadiusMax) Radius = mMenuStyle.RadiusMax;
    return Radius < Height / 2U ? Radius : Height / 2U;
}
#endif
