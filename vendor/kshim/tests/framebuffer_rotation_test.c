#include <framebuffer.h>

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Distinct source pixels make the direction, transpose and mirroring visible.
 * Each row below is the expected native scanout for a 5x3 logical image. */
static const unsigned maps[4][15] = {
    {0,1,2,3,4, 5,6,7,8,9, 10,11,12,13,14},
    {4,9,14, 3,8,13, 2,7,12, 1,6,11, 0,5,10},
    {14,13,12,11,10, 9,8,7,6,5, 4,3,2,1,0},
    {10,5,0, 11,6,1, 12,7,2, 13,8,3, 14,9,4},
};

static int blit(kshim_framebuffer_t *fb, int32_t x, int32_t y,
                uint32_t w, uint32_t h, const uint32_t *src, size_t stride,
                kshim_fb_rotation_t rotation)
{
#ifdef KSHIM_ROTATION_BASELINE
    (void)rotation;
    return kshim_fb_blit_argb8888(fb, x, y, w, h, src, stride);
#else
    return kshim_fb_blit_argb8888_rotated(fb, x, y, w, h, src, stride, rotation);
#endif
}

static void check_case(kshim_fb_format_t format, unsigned angle,
                       unsigned padding, unsigned misaligned,
                       int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    uint32_t storage[96], expected_storage[96], source_storage[32];
    uint8_t *pixels = (uint8_t *)storage + 16U + misaligned;
    uint8_t *expected = (uint8_t *)expected_storage + 16U + misaligned;
    uint8_t *source = (uint8_t *)source_storage;
    const size_t source_stride = 20U + padding;
    const uint32_t physical_width = angle % 2U ? 3U : 5U;
    const uint32_t physical_height = angle % 2U ? 5U : 3U;
    const uint32_t bpp = kshim_fb_format_bpp(format);
    const uint32_t used = (physical_width * bpp + 7U) / 8U;
    const uint32_t stride = used + padding;
    kshim_framebuffer_config_t config = {
        .render_address = (uintptr_t)pixels,
        .width = physical_width, .height = physical_height, .stride = stride,
        .bpp = bpp, .format = format, .buffer_size = (size_t)stride * physical_height,
    };
    kshim_framebuffer_t fb, reference;
    uint32_t colors[15];
    memset(storage, 0xa5, sizeof(storage));
    memset(expected_storage, 0xa5, sizeof(expected_storage));
    memset(source_storage, 0x39, sizeof(source_storage));
    for (unsigned i = 0; i < 15U; ++i) {
        colors[i] = UINT32_C(0x40102030) + i * UINT32_C(0x0711030d);
        memcpy(source + (i / 5U) * source_stride + (i % 5U) * 4U, &colors[i], 4U);
    }
    assert(kshim_fb_init(&fb, &config) == KSHIM_FB_OK);
    config.render_address = (uintptr_t)expected;
    assert(kshim_fb_init(&reference, &config) == KSHIM_FB_OK);
    /* Reference writes each expected physical pixel independently. The fixed
     * matrix above specifies direction independently of the production loop. */
    for (unsigned py = 0; py < physical_height; ++py) {
        for (unsigned px = 0; px < physical_width; ++px) {
            unsigned logical = maps[angle][py * physical_width + px];
            int64_t sx = (int64_t)(logical % 5U) - x;
            int64_t sy = (int64_t)(logical / 5U) - y;
            if (sx >= 0 && sy >= 0 && sx < width && sy < height) {
                uint32_t color;
                memcpy(&color, source + (size_t)sy * source_stride + (size_t)sx * 4U, 4U);
                assert(kshim_fb_blit_argb8888(&reference, (int32_t)px, (int32_t)py,
                                             1U, 1U, &color, 4U) == KSHIM_FB_OK);
            }
        }
    }
    uint8_t source_before[sizeof(source_storage)];
    memcpy(source_before, source_storage, sizeof(source_before));
    assert(blit(&fb, x, y, width, height, source_storage, source_stride,
                (kshim_fb_rotation_t)(angle * 90U)) == KSHIM_FB_OK);
    assert(memcmp(storage, expected_storage, sizeof(storage)) == 0);
    assert(memcmp(source_storage, source_before, sizeof(source_before)) == 0);
    assert(fb.width == physical_width && fb.height == physical_height && fb.stride == stride);
}

static void check_errors(void)
{
    uint32_t pixels[32], source[16];
    kshim_framebuffer_t fb;
    kshim_framebuffer_config_t config = {
        .render_address = (uintptr_t)pixels, .width = 3U, .height = 5U,
        .stride = 16U, .bpp = 32U, .format = KSHIM_FB_FORMAT_ARGB8888,
        .buffer_size = 80U,
    };
    memset(pixels, 0xa5, sizeof(pixels));
    assert(kshim_fb_init(&fb, &config) == KSHIM_FB_OK);
#define ROTATED(x,y,w,h,src,stride) blit(&fb,x,y,w,h,src,stride,KSHIM_FB_ROTATION_CCW_90)
    assert(ROTATED(0,0,0,1,NULL,0) == KSHIM_FB_OK);
    assert(ROTATED(0,0,1,0,NULL,0) == KSHIM_FB_OK);
    assert(ROTATED(0,0,1,1,NULL,4) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    assert(ROTATED(0,0,3,2,source,11) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    assert(ROTATED(0,0,3,3,source,SIZE_MAX) == KSHIM_FB_ERR_OVERFLOW);
    assert(ROTATED(0,0,1,1,(const uint32_t *)(UINTPTR_MAX-2U),4) == KSHIM_FB_ERR_OVERFLOW);
    assert(ROTATED(0,0,1,1,pixels,4) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    assert(ROTATED(INT32_MIN,INT32_MIN,1,1,(const uint32_t *)(uintptr_t)4U,4) == KSHIM_FB_OK);
    assert(ROTATED(INT32_MAX,INT32_MAX,1,1,(const uint32_t *)(uintptr_t)4U,4) == KSHIM_FB_OK);
    assert(blit(&fb,0,0,1,1,source,4,(kshim_fb_rotation_t)45) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    for (unsigned i = 0; i < 32U; ++i) assert(pixels[i] == UINT32_C(0xa5a5a5a5));
#undef ROTATED
}

int main(void)
{
    for (unsigned format = KSHIM_FB_FORMAT_MONO1_LSB; format <= KSHIM_FB_FORMAT_ABGR8888; ++format) {
        for (unsigned angle = 0; angle < 4U; ++angle) {
            for (unsigned layout = 0; layout < 2U; ++layout) {
                unsigned padding = layout ? 3U : 0U;
                check_case((kshim_fb_format_t)format, angle, padding, layout, 0, 0, 5, 3);
                check_case((kshim_fb_format_t)format, angle, padding, layout, -1, -1, 5, 3);
                check_case((kshim_fb_format_t)format, angle, padding, layout, 3, 2, 3, 2);
                check_case((kshim_fb_format_t)format, angle, padding, layout, 1, 1, 1, 1);
            }
        }
    }
    check_errors();
    puts("PASS: CCW rotation direction, 8 formats, clipping, guards, native stride and source bounds");
    return 0;
}
