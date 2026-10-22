#pragma once

/*
 * A small, allocation-free framebuffer writer for early boot UI code.
 *
 * The caller owns the backing memory and supplies its address.  The module
 * never probes, maps, or allocates the framebuffer; render_address must
 * already be accessible by the current execution environment and mapped as
 * device/uncached memory or otherwise coherent with the display controller.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum kshim_fb_format {
    /* One pixel per bit.  The first pixel in a byte is bit 0 or bit 7. */
    KSHIM_FB_FORMAT_MONO1_LSB = 1,
    KSHIM_FB_FORMAT_MONO1_MSB = 2,
    KSHIM_FB_FORMAT_GRAY8 = 3,
    KSHIM_FB_FORMAT_RGB565 = 4,
    KSHIM_FB_FORMAT_XRGB8888 = 5,
    KSHIM_FB_FORMAT_ARGB8888 = 6,
    KSHIM_FB_FORMAT_XBGR8888 = 7,
    KSHIM_FB_FORMAT_ABGR8888 = 8,
} kshim_fb_format_t;

typedef enum kshim_fb_status {
    KSHIM_FB_OK = 0,
    KSHIM_FB_ERR_INVALID_ARGUMENT = -1,
    KSHIM_FB_ERR_UNSUPPORTED_FORMAT = -2,
    KSHIM_FB_ERR_OVERFLOW = -3,
    KSHIM_FB_ERR_OUT_OF_BOUNDS = -4,
    KSHIM_FB_ERR_BUFFER_TOO_SMALL = -5,
} kshim_fb_status_t;

/* Rotation of UI pixels into the native scanout, counterclockwise. */
typedef enum kshim_fb_rotation {
    KSHIM_FB_ROTATION_0 = 0,
    KSHIM_FB_ROTATION_CCW_90 = 90,
    KSHIM_FB_ROTATION_180 = 180,
    KSHIM_FB_ROTATION_CCW_270 = 270,
} kshim_fb_rotation_t;

typedef struct kshim_framebuffer_config {
    /* Physical or virtual address of the first byte of the first row. */
    uintptr_t render_address;
    uint32_t width;
    uint32_t height;
    /* Bytes between two rows.  Zero selects the minimum stride. */
    uint32_t stride;
    /* Bits per pixel.  It must match the selected format. */
    uint32_t bpp;
    kshim_fb_format_t format;
    /* Raw pixel values used by monochrome drawing primitives. */
    uint32_t foreground;
    uint32_t background;
    /* Optional bound for validation; zero means the mapping size is unknown. */
    size_t buffer_size;
} kshim_framebuffer_config_t;

typedef struct kshim_framebuffer {
    uintptr_t render_address;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bpp;
    kshim_fb_format_t format;
    uint32_t foreground;
    uint32_t background;
    size_t buffer_size;
    size_t frame_size;
} kshim_framebuffer_t;

/* Return the required bits per pixel for a format, or zero if unsupported. */
uint32_t kshim_fb_format_bpp(kshim_fb_format_t format);

/* Configure a framebuffer without touching its backing memory. */
kshim_fb_status_t kshim_fb_init(
    kshim_framebuffer_t *framebuffer,
    const kshim_framebuffer_config_t *config);

/* Change the on/off colors used by the drawing helpers. */
kshim_fb_status_t kshim_fb_set_colors(
    kshim_framebuffer_t *framebuffer,
    uint32_t foreground,
    uint32_t background);

/* Draw one logical pixel. Coordinates outside the target return OUT_OF_BOUNDS. */
kshim_fb_status_t kshim_fb_set_pixel(
    kshim_framebuffer_t *framebuffer,
    int32_t x,
    int32_t y,
    bool on);

/* Clear the complete configured frame to the configured background color. */
kshim_fb_status_t kshim_fb_clear(kshim_framebuffer_t *framebuffer);

/* Rectangle helpers clip to the framebuffer. Zero-sized rectangles are OK. */
kshim_fb_status_t kshim_fb_fill_rect(
    kshim_framebuffer_t *framebuffer,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    bool on);

kshim_fb_status_t kshim_fb_draw_rect(
    kshim_framebuffer_t *framebuffer,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    bool on);

kshim_fb_status_t kshim_fb_draw_hline(
    kshim_framebuffer_t *framebuffer,
    int32_t x,
    int32_t y,
    uint32_t length,
    bool on);

kshim_fb_status_t kshim_fb_draw_vline(
    kshim_framebuffer_t *framebuffer,
    int32_t x,
    int32_t y,
    uint32_t length,
    bool on);

kshim_fb_status_t kshim_fb_draw_line(
    kshim_framebuffer_t *framebuffer,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    bool on);

/*
 * Copy a packed 1-bit image into the target.  source_stride is in bytes and
 * source_msb_first selects bit 7-first source packing.  The destination is
 * clipped, making this useful for glyphs and menu icons near an edge.
 */
kshim_fb_status_t kshim_fb_draw_mono_bitmap(
    kshim_framebuffer_t *framebuffer,
    int32_t dst_x,
    int32_t dst_y,
    uint32_t width,
    uint32_t height,
    const uint8_t *source,
    uint32_t source_stride,
    bool source_msb_first,
    bool on,
    bool transparent_zero);

/*
 * Copy precomposited ARGB8888 pixels (little-endian B, G, R, A bytes), clipping
 * the target rectangle and preserving row padding. Strides are in bytes and
 * need not be aligned. The caller supplies at least (height-1)*stride+width*4
 * readable bytes; the source must not overlap the framebuffer.
 * ARGB keeps alpha; XRGB writes 0xff in its unused byte. Other formats ignore
 * alpha and convert RGB, using rounded BT.601 luma for gray and a >=128 luma
 * threshold for mono. Monochrome foreground/background colors do not apply.
 * Zero width or height is a successful no-op. Like the drawing helpers, this
 * leaves the display barrier to the caller's kshim_fb_sync() after flushing.
 */
int kshim_fb_blit_argb8888(
    kshim_framebuffer_t *framebuffer,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    const uint32_t *source,
    size_t source_stride_bytes);

/* Blit a logical dirty rectangle directly into native scanout. For CCW 90,
 * logical dimensions are (physical height, physical width), and (x,y) maps
 * to (y, physical height-1-x). Native dimensions, stride and padding stay
 * unchanged. No full-frame surface or rotation scratch buffer is needed. */
int kshim_fb_blit_argb8888_rotated(
    kshim_framebuffer_t *framebuffer,
    int32_t x, int32_t y, uint32_t width, uint32_t height,
    const uint32_t *source, size_t source_stride_bytes,
    kshim_fb_rotation_t rotation);

/* Make framebuffer writes visible before handing the surface to a display. */
void kshim_fb_sync(void);
