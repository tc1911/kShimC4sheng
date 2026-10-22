#include <backdrop.h>

#include <limits.h>

#define KSHIM_BACKDROP_TEXTURE_SHORT_EDGE 384U

/* Diagonal: top-right -> bottom-left. */
#define TOP_RIGHT 0x4b3f9fU      /* violet indigo */
#define BOTTOM_LEFT 0x13235aU    /* deep blue */
/* Wash along the top edge: top-left -> top-right, in 1/256 opacity. */
#define WASH_LEFT 0x3d86d6U      /* azure */
#define WASH_LEFT_OPA 64U
#define WASH_RIGHT 0xa06ad8U     /* lilac */
#define WASH_RIGHT_OPA 16U

#define BLOB_COUNT 4U

/* Blob colour, strength (of 256) and radius (of 1000 of the short edge);
 * the centre moves on base + amplitude * sin(2 pi t / period + phase), in
 * 1/1000 of the screen, phases in 1/65536 turns. */
static const struct {
    uint32_t rgb;
    uint16_t strength;
    uint16_t radius;
    int16_t base_x, base_y, amp_x, amp_y;
    uint32_t period_x_ms, period_y_ms;
    uint16_t phase_x, phase_y;
} blobs[BLOB_COUNT] = {
    {0x7652b8U, 150U, 620U, 250, 300, 180, 160, 19000U, 23000U, 0U, 16384U},
    {0x3d86d6U, 140U, 560U, 760, 700, 170, 200, 23000U, 17000U, 20000U, 0U},
    {0xb05ec8U, 110U, 480U, 700, 220, 220, 140, 29000U, 31000U, 40000U, 30000U},
    {0x4fb6e0U, 90U, 440U, 300, 800, 160, 150, 26000U, 21000U, 8000U, 50000U},
};

/* sin over a quarter turn, 64 steps, Q15. */
static const int16_t quarter_sine[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393,
    7179, 7962, 8739, 9512, 10278, 11039, 11793, 12539, 13279,
    14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519,
    20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
    25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898,
    29268, 29621, 29956, 30273, 30571, 30852, 31113, 31356, 31580,
    31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728,
    32757, 32767,
};

static uint32_t min_u32(uint32_t first, uint32_t second)
{
    return first < second ? first : second;
}

static uint32_t max_u32(uint32_t first, uint32_t second)
{
    return first > second ? first : second;
}

/* Phase in 1/65536 turns -> Q15 sine. */
static int32_t sine(uint32_t phase)
{
    uint32_t quarter = (phase >> 14) & 3U;
    uint32_t within = phase & 0x3fffU;
    int32_t value;

    if (quarter & 1U)
        within = 0x4000U - within;
    value = quarter_sine[within >> 8];
    if ((within >> 8) < 64U)
        value += ((quarter_sine[(within >> 8) + 1U] - quarter_sine[within >> 8]) *
                  (int32_t)(within & 0xffU)) >> 8;
    return quarter >= 2U ? -value : value;
}

/* Blend weight 0..256 of b over a, per channel. */
static uint32_t mix(uint32_t a, uint32_t b, uint32_t weight)
{
    uint32_t result = 0;

    for (unsigned shift = 0; shift < 24U; shift += 8U) {
        uint32_t ca = (a >> shift) & 0xffU;
        uint32_t cb = (b >> shift) & 0xffU;
        result |= ((ca * (256U - weight) + cb * weight) >> 8) << shift;
    }
    return result;
}

int kshim_backdrop_layout(uint32_t screen_width, uint32_t screen_height,
                          size_t max_texture_pixels, size_t entry_count,
                          kshim_backdrop_layout_t *layout)
{
    uint32_t short_edge;
    uint32_t texture_width;
    uint32_t texture_height;
    uint32_t panel_height;

    if (layout == NULL || screen_width < 64U || screen_height < 48U ||
        screen_width > INT32_MAX || screen_height > INT32_MAX ||
        max_texture_pixels < 64U * 48U || entry_count == 0U)
        return -1;

    short_edge = min_u32(screen_width, screen_height);
    uint32_t texture_short = min_u32(short_edge,
                                     KSHIM_BACKDROP_TEXTURE_SHORT_EDGE);
    if (screen_width <= screen_height) {
        texture_width = texture_short;
        texture_height = (uint32_t)(((uint64_t)screen_height * texture_short +
                                     screen_width / 2U) / screen_width);
    } else {
        texture_height = texture_short;
        texture_width = (uint32_t)(((uint64_t)screen_width * texture_short +
                                    screen_height / 2U) / screen_height);
    }
    while ((uint64_t)texture_width * texture_height > max_texture_pixels &&
           texture_short > 64U) {
        texture_short--;
        if (screen_width <= screen_height) {
            texture_width = texture_short;
            texture_height = (uint32_t)(((uint64_t)screen_height * texture_short +
                                         screen_width / 2U) / screen_width);
        } else {
            texture_height = texture_short;
            texture_width = (uint32_t)(((uint64_t)screen_width * texture_short +
                                        screen_height / 2U) / screen_height);
        }
    }
    if (texture_width == 0U || texture_height == 0U ||
        texture_width > UINT16_MAX || texture_height > UINT16_MAX ||
        (uint64_t)texture_width * texture_height > max_texture_pixels)
        return -2;

    uint32_t panel_width = short_edge * 4U / 5U;
    uint32_t padding = short_edge / 28U;
    if (padding < 2U)
        padding = 2U;
    else if (padding > 36U)
        padding = 36U;

    /* Keep the frame compact for the common two-entry UEFI menu while still
     * capping long manifests to a scrollable panel.  These line metrics match
     * the LVGL chrome in lvgl_port.c and are deliberately integer-only. */
    /* Noto Sans CJK lines are 1.448 em: titles of 48/28/20 px and status
     * text of 22/16/12 px (lvgl_port.c). */
    uint32_t title_height = short_edge >= 720U ? 70U :
                            short_edge >= 320U ? 41U : 29U;
    uint32_t status_height = short_edge < 160U ? 0U :
                             short_edge >= 720U ? 32U :
                             short_edge >= 320U ? 23U : 17U;
    uint32_t row_height = short_edge >= 720U ? 72U :
                          short_edge >= 320U ? 52U : 24U;
    uint32_t row_gap = max_u32(2U, padding / 4U);
    uint32_t boot_height = short_edge / 11U;
    boot_height = max_u32(16U, min_u32(boot_height, 88U));
    size_t visible_count = entry_count > 49U ? 49U : entry_count;
    uint64_t rows = (uint64_t)row_height * visible_count;
    if (visible_count > 1U)
        rows += (uint64_t)row_gap * (visible_count - 1U);
    uint64_t fixed = (uint64_t)padding + title_height +
                    (status_height != 0U ? status_height + padding / 3U : 0U) +
                    padding + boot_height + (uint64_t)padding * 2U;
    uint64_t natural_height = fixed + rows;
    uint32_t panel_cap = screen_height * 7U / 10U;
    if (screen_width > screen_height)
        panel_cap = short_edge * 7U / 10U;
    panel_height = (uint32_t)min_u32(
        (uint32_t)min_u32(natural_height > UINT32_MAX ? UINT32_MAX :
                          (uint32_t)natural_height, panel_cap),
        UINT32_MAX);
    uint32_t minimum_height = fixed + row_height;
    if (panel_height < minimum_height)
        panel_height = minimum_height;
    if (panel_height > screen_height)
        panel_height = screen_height;
    panel_width = max_u32(panel_width, min_u32(screen_width, 56U));
    panel_height = max_u32(panel_height, min_u32(screen_height, 44U));

    *layout = (kshim_backdrop_layout_t){
        .screen_width = screen_width,
        .screen_height = screen_height,
        .texture_width = (uint16_t)texture_width,
        .texture_height = (uint16_t)texture_height,
        .panel_x = (int32_t)((screen_width - panel_width) / 2U),
        .panel_y = (int32_t)((screen_height - panel_height) / 2U),
        .panel_width = panel_width,
        .panel_height = panel_height,
        .panel_radius = max_u32(8U, short_edge * 3U / 100U),
    };
    return 0;
}

int kshim_backdrop_render(uint32_t *pixels, size_t capacity_pixels,
                          const kshim_backdrop_layout_t *layout,
                          uint32_t time_ms, bool reduced_quality)
{
    struct {
        int32_t x, y;
        uint32_t radius_squared;
        uint32_t r, g, b;   /* colour * strength */
    } blob[BLOB_COUNT];
    uint32_t width, height, short_edge;
    unsigned blob_count = reduced_quality ? 2U : BLOB_COUNT;

    if (pixels == NULL || layout == NULL || layout->texture_width == 0U ||
        layout->texture_height == 0U)
        return -1;
    width = layout->texture_width;
    height = layout->texture_height;
    if ((uint64_t)width * height > capacity_pixels)
        return -2;
    short_edge = min_u32(width, height);

    for (unsigned index = 0; index < blob_count; index++) {
        uint32_t phase_x = (uint32_t)(((uint64_t)(time_ms % blobs[index].period_x_ms)
                                       << 16) / blobs[index].period_x_ms) +
                           blobs[index].phase_x;
        uint32_t phase_y = (uint32_t)(((uint64_t)(time_ms % blobs[index].period_y_ms)
                                       << 16) / blobs[index].period_y_ms) +
                           blobs[index].phase_y;
        int32_t x = blobs[index].base_x +
                    ((blobs[index].amp_x * sine(phase_x & 0xffffU)) >> 15);
        int32_t y = blobs[index].base_y +
                    ((blobs[index].amp_y * sine(phase_y & 0xffffU)) >> 15);
        uint32_t radius = max_u32(1U, short_edge * blobs[index].radius / 1000U);

        blob[index].x = (int32_t)((int64_t)x * width / 1000);
        blob[index].y = (int32_t)((int64_t)y * height / 1000);
        blob[index].radius_squared = radius * radius;
        blob[index].r = ((blobs[index].rgb >> 16) & 0xffU) * blobs[index].strength;
        blob[index].g = ((blobs[index].rgb >> 8) & 0xffU) * blobs[index].strength;
        blob[index].b = (blobs[index].rgb & 0xffU) * blobs[index].strength;
    }

    for (uint32_t y = 0; y < height; y++) {
        uint32_t down = height > 1U ? y * 256U / (height - 1U) : 0U;
        for (uint32_t x = 0; x < width; x++) {
            uint32_t across = width > 1U ? x * 256U / (width - 1U) : 0U;
            /* 0 at the top-right corner, 256 at the bottom-left one. */
            uint32_t diagonal = ((256U - across) + down) / 2U;
            uint32_t color = mix(TOP_RIGHT, BOTTOM_LEFT, diagonal);
            uint32_t wash_opacity = ((WASH_LEFT_OPA * (256U - across) +
                                      WASH_RIGHT_OPA * across) >> 8) *
                                    (256U - down) >> 8;
            uint32_t r, g, b;

            color = mix(color, mix(WASH_LEFT, WASH_RIGHT, across), wash_opacity);
            r = (color >> 16) & 0xffU;
            g = (color >> 8) & 0xffU;
            b = color & 0xffU;
            /* Additive soft light: (1 - d^2/r^2)^2 inside each blob. */
            for (unsigned index = 0; index < blob_count; index++) {
                int32_t dx = (int32_t)x - blob[index].x;
                int32_t dy = (int32_t)y - blob[index].y;
                uint64_t distance = (uint64_t)((int64_t)dx * dx) +
                                    (uint64_t)((int64_t)dy * dy);
                if (distance >= blob[index].radius_squared)
                    continue;
                uint32_t t = 256U - (uint32_t)(distance * 256U /
                                               blob[index].radius_squared);
                uint32_t falloff = t * t >> 8;
                r += (blob[index].r * falloff) >> 16;
                g += (blob[index].g * falloff) >> 16;
                b += (blob[index].b * falloff) >> 16;
            }
            pixels[(size_t)y * width + x] = 0xff000000U |
                (min_u32(r, 255U) << 16) | (min_u32(g, 255U) << 8) |
                min_u32(b, 255U);
        }
    }
    return 0;
}
