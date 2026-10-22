#include <framebuffer.h>

/* The framebuffer is often MMIO or uncached memory, so every write is volatile. */
static volatile uint8_t *kshim_fb_byte(const kshim_framebuffer_t *framebuffer,
                                       size_t offset)
{
    return (volatile uint8_t *)(framebuffer->render_address + offset);
}

static bool kshim_fb_is_mono(const kshim_framebuffer_t *framebuffer)
{
    return framebuffer->format == KSHIM_FB_FORMAT_MONO1_LSB ||
           framebuffer->format == KSHIM_FB_FORMAT_MONO1_MSB;
}

uint32_t kshim_fb_format_bpp(kshim_fb_format_t format)
{
    switch (format) {
    case KSHIM_FB_FORMAT_MONO1_LSB:
    case KSHIM_FB_FORMAT_MONO1_MSB:
        return 1;
    case KSHIM_FB_FORMAT_GRAY8:
        return 8;
    case KSHIM_FB_FORMAT_RGB565:
        return 16;
    case KSHIM_FB_FORMAT_XRGB8888:
    case KSHIM_FB_FORMAT_ARGB8888:
    case KSHIM_FB_FORMAT_XBGR8888:
    case KSHIM_FB_FORMAT_ABGR8888:
        return 32;
    default:
        return 0;
    }
}

static bool kshim_fb_mul_overflow_size(size_t left, size_t right, size_t *result)
{
    if (left != 0 && right > SIZE_MAX / left)
        return true;
    *result = left * right;
    return false;
}

static bool kshim_fb_address_overflow(uintptr_t address, size_t length)
{
    if (length == 0)
        return false;
    return address > UINTPTR_MAX - (length - 1);
}

static kshim_fb_status_t kshim_fb_validate(const kshim_framebuffer_t *framebuffer)
{
    uint32_t expected_bpp;
    size_t minimum_stride;
    size_t expected_frame_size;

    if (framebuffer == NULL || framebuffer->render_address == 0 ||
        framebuffer->width == 0 || framebuffer->height == 0 ||
        framebuffer->stride == 0 || framebuffer->frame_size == 0)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;

    expected_bpp = kshim_fb_format_bpp(framebuffer->format);
    if (expected_bpp == 0)
        return KSHIM_FB_ERR_UNSUPPORTED_FORMAT;

    if (framebuffer->bpp != expected_bpp)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (framebuffer->format == KSHIM_FB_FORMAT_MONO1_LSB ||
        framebuffer->format == KSHIM_FB_FORMAT_MONO1_MSB)
        minimum_stride = ((size_t)framebuffer->width + 7U) / 8U;
    else if (kshim_fb_mul_overflow_size(framebuffer->width,
                                        expected_bpp / 8U,
                                        &minimum_stride))
        return KSHIM_FB_ERR_OVERFLOW;
    if ((size_t)framebuffer->stride < minimum_stride)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (kshim_fb_mul_overflow_size(framebuffer->stride, framebuffer->height,
                                   &expected_frame_size))
        return KSHIM_FB_ERR_OVERFLOW;
    if (framebuffer->frame_size != expected_frame_size)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;

    if (kshim_fb_address_overflow(framebuffer->render_address,
                                  framebuffer->frame_size))
        return KSHIM_FB_ERR_OVERFLOW;

    if (framebuffer->buffer_size != 0 &&
        framebuffer->frame_size > framebuffer->buffer_size)
        return KSHIM_FB_ERR_BUFFER_TOO_SMALL;

    return KSHIM_FB_OK;
}

static bool kshim_fb_clip_span(int64_t coordinate, uint32_t length,
                               uint32_t limit, uint32_t *start,
                               uint32_t *end)
{
    int64_t first = coordinate;
    int64_t last;

    if (length == 0 || first >= (int64_t)limit || first + (int64_t)length <= 0)
        return false;

    last = first + (int64_t)length;
    if (first < 0)
        first = 0;
    if (last > (int64_t)limit)
        last = limit;
    if (first >= last)
        return false;

    *start = (uint32_t)first;
    *end = (uint32_t)last;
    return true;
}

static void kshim_fb_write_raw(const kshim_framebuffer_t *framebuffer,
                               size_t offset, uint32_t value, uint32_t bytes)
{
    volatile uint8_t *destination = kshim_fb_byte(framebuffer, offset);

    /* Framebuffer pixel values are defined as little-endian raw values. */
    for (uint32_t index = 0; index < bytes; ++index)
        destination[index] = (uint8_t)(value >> (index * 8U));
}

static void kshim_fb_write_value(const kshim_framebuffer_t *framebuffer,
                                 uint32_t x, uint32_t y, uint32_t value)
{
    size_t row_offset = (size_t)y * framebuffer->stride;

    if (kshim_fb_is_mono(framebuffer)) {
        uint32_t bit = x & 7U;
        uint8_t mask = (uint8_t)(1U <<
            (framebuffer->format == KSHIM_FB_FORMAT_MONO1_MSB ? 7U - bit : bit));
        volatile uint8_t *destination = kshim_fb_byte(framebuffer,
                                                      row_offset + (x >> 3));
        uint8_t current = *destination;
        *destination = (value & 1U) ? (uint8_t)(current | mask) :
                                      (uint8_t)(current & (uint8_t)~mask);
        return;
    }

    switch (framebuffer->format) {
    case KSHIM_FB_FORMAT_GRAY8:
        kshim_fb_write_raw(framebuffer, row_offset + x, value, 1);
        break;
    case KSHIM_FB_FORMAT_RGB565:
        kshim_fb_write_raw(framebuffer, row_offset + (size_t)x * 2U, value, 2);
        break;
    case KSHIM_FB_FORMAT_XRGB8888:
    case KSHIM_FB_FORMAT_ARGB8888:
    case KSHIM_FB_FORMAT_XBGR8888:
    case KSHIM_FB_FORMAT_ABGR8888:
        kshim_fb_write_raw(framebuffer, row_offset + (size_t)x * 4U, value, 4);
        break;
    default:
        /* Configuration validation prevents this path. */
        break;
    }
}

static void kshim_fb_write_pixel(const kshim_framebuffer_t *framebuffer,
                                 uint32_t x, uint32_t y, bool on)
{
    kshim_fb_write_value(framebuffer, x, y,
                          on ? framebuffer->foreground : framebuffer->background);
}

static void kshim_fb_draw_hline_inner(const kshim_framebuffer_t *framebuffer,
                                      int64_t x, int64_t y,
                                      uint32_t length, bool on)
{
    uint32_t start, end;

    if (!kshim_fb_clip_span(x, length, framebuffer->width, &start, &end) ||
        y < 0 || y >= (int64_t)framebuffer->height)
        return;
    for (uint32_t column = start; column < end; ++column)
        kshim_fb_write_pixel(framebuffer, column, (uint32_t)y, on);
}

static void kshim_fb_draw_vline_inner(const kshim_framebuffer_t *framebuffer,
                                      int64_t x, int64_t y,
                                      uint32_t length, bool on)
{
    uint32_t start, end;

    if (!kshim_fb_clip_span(y, length, framebuffer->height, &start, &end) ||
        x < 0 || x >= (int64_t)framebuffer->width)
        return;
    for (uint32_t row = start; row < end; ++row)
        kshim_fb_write_pixel(framebuffer, (uint32_t)x, row, on);
}

static uint32_t kshim_fb_line_outcode(const kshim_framebuffer_t *framebuffer,
                                      int64_t x, int64_t y)
{
    uint32_t code = 0;

    if (x < 0)
        code |= 1U;
    else if (x >= (int64_t)framebuffer->width)
        code |= 2U;
    if (y < 0)
        code |= 4U;
    else if (y >= (int64_t)framebuffer->height)
        code |= 8U;
    return code;
}

/*
 * Compute a*b/c without overflowing signed 64-bit arithmetic.  The clipping
 * inputs are derived from int32 coordinates, so the unsigned product is
 * bounded below UINT64_MAX even though it may exceed INT64_MAX.
 */
static int64_t kshim_fb_mul_div_i64(int64_t a, int64_t b, int64_t c)
{
    bool negative = (((a < 0) != (b < 0)) != (c < 0));
    uint64_t ua = a < 0 ? (uint64_t)(-(a + 1)) + 1U : (uint64_t)a;
    uint64_t ub = b < 0 ? (uint64_t)(-(b + 1)) + 1U : (uint64_t)b;
    uint64_t uc = c < 0 ? (uint64_t)(-(c + 1)) + 1U : (uint64_t)c;
    uint64_t quotient = (ua * ub) / uc;

    if (!negative)
        return (int64_t)quotient;
    return -(int64_t)quotient;
}

/* Clip before rasterization so a line spanning int32 extremes cannot stall. */
static bool kshim_fb_clip_line(const kshim_framebuffer_t *framebuffer,
                               int64_t *x0, int64_t *y0,
                               int64_t *x1, int64_t *y1)
{
    uint32_t code0 = kshim_fb_line_outcode(framebuffer, *x0, *y0);
    uint32_t code1 = kshim_fb_line_outcode(framebuffer, *x1, *y1);
    int64_t xmin = 0;
    int64_t xmax = (int64_t)framebuffer->width - 1;
    int64_t ymin = 0;
    int64_t ymax = (int64_t)framebuffer->height - 1;

    for (;;) {
        uint32_t outside;
        int64_t x;
        int64_t y;

        if ((code0 | code1) == 0)
            return true;
        if ((code0 & code1) != 0)
            return false;

        outside = code0 != 0 ? code0 : code1;
        x = *x0;
        y = *y0;
        if ((outside & 4U) != 0) {
            if (*y1 == *y0)
                return false;
            y = ymin;
            x = *x0 + kshim_fb_mul_div_i64(*x1 - *x0, y - *y0,
                                           *y1 - *y0);
        } else if ((outside & 8U) != 0) {
            if (*y1 == *y0)
                return false;
            y = ymax;
            x = *x0 + kshim_fb_mul_div_i64(*x1 - *x0, y - *y0,
                                           *y1 - *y0);
        } else if ((outside & 2U) != 0) {
            if (*x1 == *x0)
                return false;
            x = xmax;
            y = *y0 + kshim_fb_mul_div_i64(*y1 - *y0, x - *x0,
                                           *x1 - *x0);
        } else {
            if (*x1 == *x0)
                return false;
            x = xmin;
            y = *y0 + kshim_fb_mul_div_i64(*y1 - *y0, x - *x0,
                                           *x1 - *x0);
        }

        if (outside == code0) {
            *x0 = x;
            *y0 = y;
            code0 = kshim_fb_line_outcode(framebuffer, x, y);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = kshim_fb_line_outcode(framebuffer, x, y);
        }
    }
}

kshim_fb_status_t kshim_fb_init(kshim_framebuffer_t *framebuffer,
                                const kshim_framebuffer_config_t *config)
{
    uint32_t expected_bpp;
    uint32_t resolved_stride;
    size_t minimum_stride;
    size_t frame_size;

    if (framebuffer == NULL || config == NULL || config->render_address == 0 ||
        config->width == 0 || config->height == 0)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;

    expected_bpp = kshim_fb_format_bpp(config->format);
    if (expected_bpp == 0)
        return KSHIM_FB_ERR_UNSUPPORTED_FORMAT;
    if (config->bpp != expected_bpp)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;

    if (config->format == KSHIM_FB_FORMAT_MONO1_LSB ||
        config->format == KSHIM_FB_FORMAT_MONO1_MSB)
        minimum_stride = ((size_t)config->width + 7U) / 8U;
    else {
        if (kshim_fb_mul_overflow_size(config->width,
                                       expected_bpp / 8U,
                                       &minimum_stride))
            return KSHIM_FB_ERR_OVERFLOW;
    }

    if (minimum_stride == 0 || minimum_stride > UINT32_MAX)
        return KSHIM_FB_ERR_OVERFLOW;

    if (config->stride == 0)
        resolved_stride = (uint32_t)minimum_stride;
    else {
        if ((size_t)config->stride < minimum_stride)
            return KSHIM_FB_ERR_INVALID_ARGUMENT;
        resolved_stride = config->stride;
    }

    if (kshim_fb_mul_overflow_size(resolved_stride, config->height,
                                   &frame_size))
        return KSHIM_FB_ERR_OVERFLOW;
    if (frame_size == 0 || kshim_fb_address_overflow(config->render_address,
                                                     frame_size))
        return KSHIM_FB_ERR_OVERFLOW;
    if (config->buffer_size != 0 && frame_size > config->buffer_size)
        return KSHIM_FB_ERR_BUFFER_TOO_SMALL;

    framebuffer->render_address = config->render_address;
    framebuffer->width = config->width;
    framebuffer->height = config->height;
    framebuffer->stride = resolved_stride;
    framebuffer->bpp = config->bpp;
    framebuffer->format = config->format;
    framebuffer->foreground = config->foreground;
    framebuffer->background = config->background;
    framebuffer->buffer_size = config->buffer_size;
    framebuffer->frame_size = frame_size;
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_set_colors(kshim_framebuffer_t *framebuffer,
                                      uint32_t foreground,
                                      uint32_t background)
{
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    framebuffer->foreground = foreground;
    framebuffer->background = background;
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_set_pixel(kshim_framebuffer_t *framebuffer,
                                     int32_t x, int32_t y, bool on)
{
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    if (x < 0 || y < 0 || (uint32_t)x >= framebuffer->width ||
        (uint32_t)y >= framebuffer->height)
        return KSHIM_FB_ERR_OUT_OF_BOUNDS;
    kshim_fb_write_pixel(framebuffer, (uint32_t)x, (uint32_t)y, on);
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_clear(kshim_framebuffer_t *framebuffer)
{
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;

    if (kshim_fb_is_mono(framebuffer)) {
        uint8_t value = (framebuffer->background & 1U) ? 0xffU : 0U;
        for (size_t offset = 0; offset < framebuffer->frame_size; ++offset)
            *kshim_fb_byte(framebuffer, offset) = value;
        return KSHIM_FB_OK;
    }

    for (uint32_t y = 0; y < framebuffer->height; ++y) {
        for (uint32_t x = 0; x < framebuffer->width; ++x)
            kshim_fb_write_pixel(framebuffer, x, y, false);
        /* Padding bytes are cleared as well, avoiding stale scanout data. */
        size_t used = (size_t)framebuffer->width * (framebuffer->bpp / 8U);
        size_t row = (size_t)y * framebuffer->stride;
        for (size_t offset = used; offset < framebuffer->stride; ++offset)
            *kshim_fb_byte(framebuffer, row + offset) = 0;
    }
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_fill_rect(kshim_framebuffer_t *framebuffer,
                                     int32_t x, int32_t y,
                                     uint32_t width, uint32_t height, bool on)
{
    uint32_t x_start, x_end, y_start, y_end;
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    if (!kshim_fb_clip_span((int64_t)x, width, framebuffer->width,
                            &x_start, &x_end) ||
        !kshim_fb_clip_span((int64_t)y, height, framebuffer->height,
                            &y_start, &y_end))
        return KSHIM_FB_OK;

    for (uint32_t row = y_start; row < y_end; ++row)
        for (uint32_t column = x_start; column < x_end; ++column)
            kshim_fb_write_pixel(framebuffer, column, row, on);
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_draw_rect(kshim_framebuffer_t *framebuffer,
                                     int32_t x, int32_t y,
                                     uint32_t width, uint32_t height, bool on)
{
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    if (width == 0 || height == 0)
        return KSHIM_FB_OK;
    kshim_fb_draw_hline_inner(framebuffer, (int64_t)x, (int64_t)y, width, on);
    if (height > 1)
        kshim_fb_draw_hline_inner(framebuffer, (int64_t)x,
                                  (int64_t)y + height - 1, width, on);
    kshim_fb_draw_vline_inner(framebuffer, (int64_t)x, (int64_t)y, height, on);
    if (width > 1)
        kshim_fb_draw_vline_inner(framebuffer, (int64_t)x + width - 1,
                                  (int64_t)y, height, on);
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_draw_hline(kshim_framebuffer_t *framebuffer,
                                      int32_t x, int32_t y,
                                      uint32_t length, bool on)
{
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    kshim_fb_draw_hline_inner(framebuffer, (int64_t)x, (int64_t)y, length,
                              on);
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_draw_vline(kshim_framebuffer_t *framebuffer,
                                      int32_t x, int32_t y,
                                      uint32_t length, bool on)
{
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    kshim_fb_draw_vline_inner(framebuffer, (int64_t)x, (int64_t)y, length,
                              on);
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_draw_line(kshim_framebuffer_t *framebuffer,
                                     int32_t x0, int32_t y0,
                                     int32_t x1, int32_t y1, bool on)
{
    int64_t first_x = x0;
    int64_t first_y = y0;
    int64_t last_x = x1;
    int64_t last_y = y1;
    int64_t dx, dy, sx, sy, error;
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;

    if (!kshim_fb_clip_line(framebuffer, &first_x, &first_y,
                            &last_x, &last_y))
        return KSHIM_FB_OK;

    dx = first_x < last_x ? last_x - first_x : first_x - last_x;
    dy = first_y < last_y ? last_y - first_y : first_y - last_y;
    sx = first_x < last_x ? 1 : -1;
    sy = first_y < last_y ? 1 : -1;
    error = dx - dy;

    for (;;) {
        kshim_fb_write_pixel(framebuffer, (uint32_t)first_x,
                             (uint32_t)first_y, on);
        if (first_x == last_x && first_y == last_y)
            break;
        int64_t doubled = error * 2;
        if (doubled > -dy) {
            error -= dy;
            first_x += sx;
        }
        if (doubled < dx) {
            error += dx;
            first_y += sy;
        }
    }
    return KSHIM_FB_OK;
}

kshim_fb_status_t kshim_fb_draw_mono_bitmap(kshim_framebuffer_t *framebuffer,
                                            int32_t dst_x, int32_t dst_y,
                                            uint32_t width, uint32_t height,
                                            const uint8_t *source,
                                            uint32_t source_stride,
                                            bool source_msb_first,
                                            bool on,
                                            bool transparent_zero)
{
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    uint32_t source_x_start;
    uint32_t source_x_end;
    uint32_t source_y_start;
    uint32_t source_y_end;
    int64_t source_x_first;
    int64_t source_y_first;
    int64_t source_x_last;
    int64_t source_y_last;
    size_t minimum_source_stride;
    size_t source_size;
    if (status != KSHIM_FB_OK)
        return status;
    if (source == NULL || width == 0 || height == 0)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (sizeof(size_t) <= sizeof(uint32_t) &&
        width > (uint32_t)(SIZE_MAX - 7U))
        return KSHIM_FB_ERR_OVERFLOW;
    minimum_source_stride = ((size_t)width + 7U) / 8U;
    if ((size_t)source_stride < minimum_source_stride)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (kshim_fb_mul_overflow_size(source_stride, height, &source_size))
        return KSHIM_FB_ERR_OVERFLOW;
    (void)source_size;

    /* Clip the source iteration before touching it.  Besides reducing work
     * for partially visible glyphs, this keeps a fully off-screen bitmap
     * from dereferencing an invalid source pointer. */
    source_x_first = dst_x < 0 ? -(int64_t)dst_x : 0;
    source_y_first = dst_y < 0 ? -(int64_t)dst_y : 0;
    source_x_last = (int64_t)framebuffer->width - (int64_t)dst_x;
    source_y_last = (int64_t)framebuffer->height - (int64_t)dst_y;
    if (source_x_last > (int64_t)width)
        source_x_last = width;
    if (source_y_last > (int64_t)height)
        source_y_last = height;
    if (source_x_first < 0)
        source_x_first = 0;
    if (source_y_first < 0)
        source_y_first = 0;
    if (source_x_first >= source_x_last || source_y_first >= source_y_last)
        return KSHIM_FB_OK;
    source_x_start = (uint32_t)source_x_first;
    source_x_end = (uint32_t)source_x_last;
    source_y_start = (uint32_t)source_y_first;
    source_y_end = (uint32_t)source_y_last;

    for (uint32_t source_y = source_y_start; source_y < source_y_end;
         ++source_y) {
        for (uint32_t source_x = source_x_start; source_x < source_x_end;
             ++source_x) {
            uint8_t source_byte = source[(size_t)source_y * source_stride +
                                         (source_x >> 3)];
            uint8_t source_mask = (uint8_t)(1U <<
                (source_msb_first ? 7U - (source_x & 7U) : (source_x & 7U)));
            bool set = (source_byte & source_mask) != 0;
            int64_t x = (int64_t)dst_x + source_x;
            int64_t y = (int64_t)dst_y + source_y;
            if ((!transparent_zero || set) && x >= 0 && y >= 0 &&
                x < framebuffer->width && y < framebuffer->height)
                kshim_fb_write_pixel(framebuffer, (uint32_t)x, (uint32_t)y,
                                     set ? on : !on);
        }
    }
    return KSHIM_FB_OK;
}

static uint32_t kshim_fb_argb_value(kshim_fb_format_t format, const uint8_t *pixel)
{
    uint32_t blue = pixel[0], green = pixel[1], red = pixel[2];
    uint32_t value;
    switch (format) {
    case KSHIM_FB_FORMAT_ARGB8888:
        return blue | green << 8U | red << 16U | (uint32_t)pixel[3] << 24U;
    case KSHIM_FB_FORMAT_XRGB8888:
        return blue | green << 8U | red << 16U | UINT32_C(0xff000000);
    case KSHIM_FB_FORMAT_ABGR8888:
        return red | green << 8U | blue << 16U | (uint32_t)pixel[3] << 24U;
    case KSHIM_FB_FORMAT_XBGR8888:
        return red | green << 8U | blue << 16U | UINT32_C(0xff000000);
    case KSHIM_FB_FORMAT_RGB565:
        return ((red >> 3U) << 11U) | ((green >> 2U) << 5U) | (blue >> 3U);
    default:
        value = (77U * red + 150U * green + 29U * blue + 128U) >> 8U;
        return format == KSHIM_FB_FORMAT_GRAY8 ? value : value >= 128U;
    }
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
/* LVGL owns a byte array. Load a word without aliasing that declared type;
 * callers have checked alignment, so the compiler can keep one word load. */
static uint32_t kshim_fb_load_argb_aligned(const uint8_t *pixel)
{
    uint32_t value;
    __builtin_memcpy(&value, __builtin_assume_aligned(pixel, 4), sizeof(value));
    return value;
}
#endif

int kshim_fb_blit_argb8888(kshim_framebuffer_t *framebuffer,
                           int32_t x, int32_t y, uint32_t width, uint32_t height,
                           const uint32_t *source, size_t source_stride_bytes)
{
    size_t row_bytes, source_size;
    uint32_t first_x, last_x, first_y, last_y;
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    if (width == 0 || height == 0)
        return KSHIM_FB_OK;
    if (source == NULL)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (kshim_fb_mul_overflow_size(width, sizeof(*source), &row_bytes))
        return KSHIM_FB_ERR_OVERFLOW;
    if (source_stride_bytes < row_bytes)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (kshim_fb_mul_overflow_size(height - 1U, source_stride_bytes, &source_size) ||
        row_bytes > SIZE_MAX - source_size)
        return KSHIM_FB_ERR_OVERFLOW;
    source_size += row_bytes;
    if (kshim_fb_address_overflow((uintptr_t)source, source_size))
        return KSHIM_FB_ERR_OVERFLOW;
    if (!kshim_fb_clip_span(x, width, framebuffer->width, &first_x, &last_x) ||
        !kshim_fb_clip_span(y, height, framebuffer->height, &first_y, &last_y))
        return KSHIM_FB_OK;
    if ((uintptr_t)source <= framebuffer->render_address + framebuffer->frame_size - 1U &&
        framebuffer->render_address <= (uintptr_t)source + source_size - 1U)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;

    for (uint32_t row = first_y; row < last_y; ++row) {
        const uint8_t *source_row = (const uint8_t *)source +
            (size_t)((int64_t)row - y) * source_stride_bytes +
            (size_t)((int64_t)first_x - x) * sizeof(*source);
        uintptr_t target = framebuffer->render_address + (size_t)row * framebuffer->stride +
                           (size_t)first_x * (framebuffer->bpp / 8U);
        /* An aligned 32-bit store avoids four MMIO writes per color pixel.
         * Keep the byte path for packed/unaligned surfaces and host models. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if ((framebuffer->format == KSHIM_FB_FORMAT_XRGB8888 ||
             framebuffer->format == KSHIM_FB_FORMAT_ARGB8888) && ((target | (uintptr_t)source_row) & 3U) == 0) {
            volatile uint32_t *destination = (volatile uint32_t *)target;
            const uint32_t *colors = (const uint32_t *)source_row;
            uint32_t alpha = framebuffer->format == KSHIM_FB_FORMAT_XRGB8888 ?
                              UINT32_C(0xff000000) : 0;
            for (uint32_t column = 0; column < last_x - first_x; ++column)
                destination[column] = colors[column] | alpha;
            continue;
        }
#else
        (void)target;
#endif
        for (uint32_t column = first_x; column < last_x; ++column) {
            const uint8_t *pixel = source_row + (size_t)(column - first_x) * 4U;
            kshim_fb_write_value(framebuffer, column, row,
                                 kshim_fb_argb_value(framebuffer->format, pixel));
        }
    }
    return KSHIM_FB_OK;
}

int kshim_fb_blit_argb8888_rotated(kshim_framebuffer_t *framebuffer,
                                   int32_t x, int32_t y, uint32_t width, uint32_t height,
                                   const uint32_t *source, size_t source_stride_bytes,
                                   kshim_fb_rotation_t rotation)
{
    size_t row_bytes, source_size;
    uint32_t first_x, last_x, first_y, last_y;
    uint32_t native_x1, native_x2, native_y1, native_y2;
    if (rotation == KSHIM_FB_ROTATION_0)
        return kshim_fb_blit_argb8888(framebuffer, x, y, width, height,
                                     source, source_stride_bytes);
    if (rotation != KSHIM_FB_ROTATION_CCW_90 && rotation != KSHIM_FB_ROTATION_180 &&
        rotation != KSHIM_FB_ROTATION_CCW_270)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    kshim_fb_status_t status = kshim_fb_validate(framebuffer);
    if (status != KSHIM_FB_OK)
        return status;
    if (width == 0 || height == 0)
        return KSHIM_FB_OK;
    if (source == NULL)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (kshim_fb_mul_overflow_size(width, sizeof(*source), &row_bytes))
        return KSHIM_FB_ERR_OVERFLOW;
    if (source_stride_bytes < row_bytes)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;
    if (kshim_fb_mul_overflow_size(height - 1U, source_stride_bytes, &source_size) ||
        row_bytes > SIZE_MAX - source_size)
        return KSHIM_FB_ERR_OVERFLOW;
    source_size += row_bytes;
    if (kshim_fb_address_overflow((uintptr_t)source, source_size))
        return KSHIM_FB_ERR_OVERFLOW;
    const bool swap_axes = rotation != KSHIM_FB_ROTATION_180;
    const uint32_t logical_width = swap_axes ? framebuffer->height : framebuffer->width;
    const uint32_t logical_height = swap_axes ? framebuffer->width : framebuffer->height;
    if (!kshim_fb_clip_span(x, width, logical_width, &first_x, &last_x) ||
        !kshim_fb_clip_span(y, height, logical_height, &first_y, &last_y))
        return KSHIM_FB_OK;
    if ((uintptr_t)source <= framebuffer->render_address + framebuffer->frame_size - 1U &&
        framebuffer->render_address <= (uintptr_t)source + source_size - 1U)
        return KSHIM_FB_ERR_INVALID_ARGUMENT;

    if (rotation == KSHIM_FB_ROTATION_CCW_90) {
        native_x1 = first_y; native_x2 = last_y;
        native_y1 = framebuffer->height - last_x;
        native_y2 = framebuffer->height - first_x;
    } else if (rotation == KSHIM_FB_ROTATION_180) {
        native_x1 = framebuffer->width - last_x;
        native_x2 = framebuffer->width - first_x;
        native_y1 = framebuffer->height - last_y;
        native_y2 = framebuffer->height - first_y;
    } else {
        native_x1 = framebuffer->width - last_y;
        native_x2 = framebuffer->width - first_y;
        native_y1 = first_x; native_y2 = last_x;
    }
    const size_t source_step = swap_axes ? source_stride_bytes : sizeof(*source);
    const bool source_reverse = rotation != KSHIM_FB_ROTATION_CCW_90;
    const uint32_t count = native_x2 - native_x1;

    /* Walk native rows contiguously: framebuffer is uncached, while strided
     * reads stay within the small cached LVGL dirty-rectangle buffer. */
    for (uint32_t row = native_y1; row < native_y2; ++row) {
        uint32_t logical_x, logical_y;
        if (rotation == KSHIM_FB_ROTATION_CCW_90) {
            logical_x = framebuffer->height - 1U - row;
            logical_y = native_x1;
        } else if (rotation == KSHIM_FB_ROTATION_180) {
            logical_x = framebuffer->width - 1U - native_x1;
            logical_y = framebuffer->height - 1U - row;
        } else {
            logical_x = row;
            logical_y = framebuffer->width - 1U - native_x1;
        }
        const uint8_t *pixel = (const uint8_t *)source +
            (size_t)((int64_t)logical_y - y) * source_stride_bytes +
            (size_t)((int64_t)logical_x - x) * sizeof(*source);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uintptr_t target = framebuffer->render_address + (size_t)row * framebuffer->stride +
                           (size_t)native_x1 * (framebuffer->bpp / 8U);
        if ((framebuffer->format == KSHIM_FB_FORMAT_XRGB8888 ||
             framebuffer->format == KSHIM_FB_FORMAT_ARGB8888) &&
            ((target | (uintptr_t)pixel | source_step) & 3U) == 0U) {
            volatile uint32_t *destination = (volatile uint32_t *)target;
            const uint32_t alpha = framebuffer->format == KSHIM_FB_FORMAT_XRGB8888 ?
                                   UINT32_C(0xff000000) : 0U;
            for (uint32_t column = 0; column < count; ++column) {
                destination[column] = kshim_fb_load_argb_aligned(pixel) | alpha;
                if (column + 1U < count)
                    pixel = source_reverse ? pixel - source_step : pixel + source_step;
            }
            continue;
        }
#endif
        for (uint32_t column = 0; column < count; ++column) {
            kshim_fb_write_value(framebuffer, native_x1 + column, row,
                                 kshim_fb_argb_value(framebuffer->format, pixel));
            if (column + 1U < count)
                pixel = source_reverse ? pixel - source_step : pixel + source_step;
        }
    }
    return KSHIM_FB_OK;
}

void kshim_fb_sync(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
