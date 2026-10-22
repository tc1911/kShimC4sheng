#pragma once

/*
 * Animated menu background and the centred menu panel's geometry.
 *
 * The background is a diagonal indigo gradient (top-right -> bottom-left)
 * with an azure-to-lilac wash along the top and four soft colour blobs
 * drifting on slow Lissajous paths. It is rendered at a bounded texture
 * size and stretched by LVGL; the flat acrylic panel on top of it is an
 * ordinary translucent LVGL fill, so its edges stay sharp at any scale.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct kshim_backdrop_layout {
    uint32_t screen_width;
    uint32_t screen_height;
    uint16_t texture_width;
    uint16_t texture_height;
    int32_t panel_x;
    int32_t panel_y;
    uint32_t panel_width;
    uint32_t panel_height;
    uint32_t panel_radius;
} kshim_backdrop_layout_t;

/* Pick a bounded texture size and the centred panel geometry for a menu
 * with entry_count rows (long menus are capped to a scrollable panel). */
int kshim_backdrop_layout(uint32_t screen_width, uint32_t screen_height,
                          size_t max_texture_pixels, size_t entry_count,
                          kshim_backdrop_layout_t *layout);

/* Render one opaque 0xAARRGGBB background frame at time_ms. Reduced
 * quality drops the two faintest blobs for slow CPUs. */
int kshim_backdrop_render(uint32_t *pixels, size_t capacity_pixels,
                          const kshim_backdrop_layout_t *layout,
                          uint32_t time_ms, bool reduced_quality);
