/* Host regression tests; link this file with src/ui/framebuffer.c. */
#include <framebuffer.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GUARD_SIZE = 17, GUARD_VALUE = 0xa5, WIDTH = 9, HEIGHT = 5 };

static const char *test_name;
static size_t checks;

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s: %s\n", \
                __FILE__, __LINE__, test_name, #expression); \
        abort(); \
    } \
} while (0)

typedef struct {
    const char *name;
    kshim_fb_format_t format;
    uint32_t bpp;
    uint32_t foreground;
    uint32_t background;
    uint8_t foreground_bytes[4];
    uint8_t background_bytes[4];
} format_case_t;

/* Literal byte values also check little-endian storage and color truncation. */
static const format_case_t formats[] = {
    {"mono LSB", KSHIM_FB_FORMAT_MONO1_LSB, 1, 0xffffffffU, 0xfffffffeU,
     {1}, {0}},
    {"mono MSB", KSHIM_FB_FORMAT_MONO1_MSB, 1, 0x12345679U, 0x89abcdecU,
     {1}, {0}},
    {"gray8", KSHIM_FB_FORMAT_GRAY8, 8, 0x123456a7U, 0x89abcd3cU,
     {0xa7}, {0x3c}},
    {"RGB565", KSHIM_FB_FORMAT_RGB565, 16, 0xa1b21234U, 0xc3d4abcdU,
     {0x34, 0x12}, {0xcd, 0xab}},
    {"XRGB8888", KSHIM_FB_FORMAT_XRGB8888, 32, 0x001234abU, 0x005678cdU,
     {0xab, 0x34, 0x12, 0x00}, {0xcd, 0x78, 0x56, 0x00}},
    {"ARGB8888", KSHIM_FB_FORMAT_ARGB8888, 32, 0x9a1234abU, 0x805678cdU,
     {0xab, 0x34, 0x12, 0x9a}, {0xcd, 0x78, 0x56, 0x80}},
    {"XBGR8888", KSHIM_FB_FORMAT_XBGR8888, 32, 0x001234abU, 0x005678cdU,
     {0xab, 0x34, 0x12, 0x00}, {0xcd, 0x78, 0x56, 0x00}},
    {"ABGR8888", KSHIM_FB_FORMAT_ABGR8888, 32, 0x9a1234abU, 0x805678cdU,
     {0xab, 0x34, 0x12, 0x9a}, {0xcd, 0x78, 0x56, 0x80}},
};

typedef struct {
    const format_case_t *format;
    kshim_framebuffer_t fb;
    kshim_framebuffer_config_t config;
    uint8_t *allocation;
    uint8_t *pixels;
    size_t size;
    size_t used_per_row;
    bool inverted_colors;
} fixture_t;

static void check_guards(const fixture_t *fixture)
{
    for (size_t i = 0; i < GUARD_SIZE; ++i) {
        CHECK(fixture->allocation[i] == GUARD_VALUE);
        CHECK(fixture->pixels[fixture->size + i] == GUARD_VALUE);
    }
}

static void make_fixture(fixture_t *fixture, const format_case_t *format,
                         uint32_t padding, bool automatic_stride)
{
    *fixture = (fixture_t){.format = format};
    fixture->used_per_row = format->bpp == 1 ? 2 : WIDTH * (format->bpp / 8);
    fixture->size = (fixture->used_per_row + padding) * HEIGHT;
    fixture->allocation = malloc(fixture->size + 2 * GUARD_SIZE);
    CHECK(fixture->allocation != NULL);
    memset(fixture->allocation, GUARD_VALUE, fixture->size + 2 * GUARD_SIZE);
    /* Deliberately unaligned: packed formats must not require aligned stores. */
    fixture->pixels = fixture->allocation + GUARD_SIZE;
    fixture->config = (kshim_framebuffer_config_t){
        .render_address = (uintptr_t)fixture->pixels,
        .width = WIDTH,
        .height = HEIGHT,
        .stride = automatic_stride ? 0 : (uint32_t)(fixture->used_per_row + padding),
        .bpp = format->bpp,
        .format = format->format,
        .foreground = format->foreground,
        .background = format->background,
        .buffer_size = fixture->size,
    };
    CHECK(kshim_fb_format_bpp(format->format) == format->bpp);
    CHECK(kshim_fb_init(&fixture->fb, &fixture->config) == KSHIM_FB_OK);
    CHECK(fixture->fb.stride == fixture->used_per_row + padding);
    CHECK(fixture->fb.frame_size == fixture->size);
    for (size_t i = 0; i < fixture->size; ++i)
        CHECK(fixture->pixels[i] == GUARD_VALUE); /* init must not draw */
    check_guards(fixture);
}

static void destroy_fixture(fixture_t *fixture)
{
    check_guards(fixture);
    free(fixture->allocation);
}

static void expect_pixel(const fixture_t *fixture, uint32_t x, uint32_t y,
                         bool on)
{
    bool foreground = on != fixture->inverted_colors;
    const uint8_t *expected = foreground ? fixture->format->foreground_bytes :
                                           fixture->format->background_bytes;
    const uint8_t *row = fixture->pixels + (size_t)y * fixture->fb.stride;
    if (fixture->format->bpp == 1) {
        unsigned bit = fixture->format->format == KSHIM_FB_FORMAT_MONO1_LSB ?
                           x % 8 : 7 - x % 8;
        CHECK(((row[x / 8] >> bit) & 1U) == expected[0]);
    } else {
        size_t bytes = fixture->format->bpp / 8;
        CHECK(memcmp(row + x * bytes, expected, bytes) == 0);
    }
}

static void expect_padding(const fixture_t *fixture, uint8_t value)
{
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        const uint8_t *row = fixture->pixels + (size_t)y * fixture->fb.stride;
        for (size_t i = fixture->used_per_row; i < fixture->fb.stride; ++i)
            CHECK(row[i] == value);
        if (fixture->format->bpp == 1) {
            uint8_t unused = fixture->format->format == KSHIM_FB_FORMAT_MONO1_LSB ?
                                 0xfe : 0x7f;
            CHECK((row[1] & unused) == (value & unused));
        }
    }
    check_guards(fixture);
}

static void expect_rows(const fixture_t *fixture, const char *const rows[HEIGHT])
{
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        CHECK(strlen(rows[y]) == WIDTH);
        for (uint32_t x = 0; x < WIDTH; ++x)
            expect_pixel(fixture, x, y, rows[y][x] == '#');
    }
    expect_padding(fixture, fixture->inverted_colors && fixture->format->bpp == 1 ?
                                0xff : 0);
}

static void expect_uniform(const fixture_t *fixture, bool on)
{
    for (uint32_t y = 0; y < HEIGHT; ++y)
        for (uint32_t x = 0; x < WIDTH; ++x)
            expect_pixel(fixture, x, y, on);
    expect_padding(fixture, fixture->inverted_colors && fixture->format->bpp == 1 ?
                                0xff : 0);
}

static void clear_fixture(fixture_t *fixture)
{
    CHECK(kshim_fb_clear(&fixture->fb) == KSHIM_FB_OK);
    expect_uniform(fixture, false);
}

static void test_raw_colors(const format_case_t *format)
{
    fixture_t fixture;
    static const char *const pixels[HEIGHT] = {
        "#.......#", "....#....", ".........", ".........", "........."
    };

    test_name = format->name;
    make_fixture(&fixture, format, 0, true);
    clear_fixture(&fixture);
    destroy_fixture(&fixture);

    make_fixture(&fixture, format, 3, false);
    clear_fixture(&fixture);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 0, 0, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 8, 0, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 4, 1, true) == KSHIM_FB_OK);
    expect_rows(&fixture, pixels);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 4, 1, false) == KSHIM_FB_OK);
    expect_pixel(&fixture, 4, 1, false);

    CHECK(kshim_fb_set_colors(&fixture.fb, format->background,
                              format->foreground) == KSHIM_FB_OK);
    fixture.inverted_colors = true;
    clear_fixture(&fixture);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 0, 0, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 8, 0, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 4, 1, true) == KSHIM_FB_OK);
    expect_rows(&fixture, pixels);
    kshim_fb_sync();
    destroy_fixture(&fixture);
}

static void test_clipping(const format_case_t *format)
{
    fixture_t fixture;
    static const char *const filled[HEIGHT] = {
        "###......", "###......", ".........", ".........", "........."
    };
    static const char *const rectangle[HEIGHT] = {
        ".........", "####.....", "...#.....", "...#.....", "####....."
    };
    static const char *const axes[HEIGHT] = {
        "........#", "........#", "##.......", ".........", "........."
    };
    static const char *const diagonal[HEIGHT] = {
        "#........", ".#.......", "..#......", "...#.....", "....#...."
    };
    static const char *const extreme_axes[HEIGHT] = {
        "#########", "........#", "........#", "........#", "........#"
    };

    test_name = format->name;
    make_fixture(&fixture, format, 3, false);
    clear_fixture(&fixture);
    CHECK(kshim_fb_fill_rect(&fixture.fb, -2, -1, 5, 3, true) == KSHIM_FB_OK);
    expect_rows(&fixture, filled);
    clear_fixture(&fixture);
    CHECK(kshim_fb_draw_rect(&fixture.fb, -1, 1, 5, 4, true) == KSHIM_FB_OK);
    expect_rows(&fixture, rectangle);
    clear_fixture(&fixture);
    CHECK(kshim_fb_draw_hline(&fixture.fb, -4, 2, 6, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_vline(&fixture.fb, 8, -2, 4, true) == KSHIM_FB_OK);
    expect_rows(&fixture, axes);
    clear_fixture(&fixture);
    CHECK(kshim_fb_draw_line(&fixture.fb, -5, -5, 20, 20, true) == KSHIM_FB_OK);
    expect_rows(&fixture, diagonal);
    clear_fixture(&fixture);
    CHECK(kshim_fb_draw_line(&fixture.fb, INT32_MAX, INT32_MAX,
                             INT32_MIN, INT32_MIN, true) == KSHIM_FB_OK);
    expect_rows(&fixture, diagonal);
    clear_fixture(&fixture);
    CHECK(kshim_fb_draw_hline(&fixture.fb, INT32_MIN, 0, UINT32_MAX,
                              true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_vline(&fixture.fb, 8, INT32_MIN, UINT32_MAX,
                              true) == KSHIM_FB_OK);
    expect_rows(&fixture, extreme_axes);
    clear_fixture(&fixture);
    CHECK(kshim_fb_fill_rect(&fixture.fb, INT32_MIN, INT32_MIN,
                             UINT32_MAX, UINT32_MAX, true) == KSHIM_FB_OK);
    expect_uniform(&fixture, true);

    clear_fixture(&fixture);
    CHECK(kshim_fb_set_pixel(&fixture.fb, -1, 0, true) == KSHIM_FB_ERR_OUT_OF_BOUNDS);
    CHECK(kshim_fb_set_pixel(&fixture.fb, WIDTH, 0, true) == KSHIM_FB_ERR_OUT_OF_BOUNDS);
    CHECK(kshim_fb_set_pixel(&fixture.fb, 0, HEIGHT, true) == KSHIM_FB_ERR_OUT_OF_BOUNDS);
    CHECK(kshim_fb_set_pixel(&fixture.fb, INT32_MIN, INT32_MAX,
                             true) == KSHIM_FB_ERR_OUT_OF_BOUNDS);
    CHECK(kshim_fb_fill_rect(&fixture.fb, INT32_MAX, INT32_MAX,
                             UINT32_MAX, UINT32_MAX, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_rect(&fixture.fb, INT32_MIN, INT32_MIN,
                             UINT32_MAX, UINT32_MAX, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_line(&fixture.fb, INT32_MIN, -1,
                             INT32_MAX, -1, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_line(&fixture.fb, INT32_MAX, INT32_MIN,
                             INT32_MAX, INT32_MAX, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_hline(&fixture.fb, 0, -1, UINT32_MAX, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_vline(&fixture.fb, -1, 0, UINT32_MAX, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_fill_rect(&fixture.fb, 0, 0, 0, HEIGHT, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_rect(&fixture.fb, 0, 0, WIDTH, 0, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_hline(&fixture.fb, 0, 0, 0, true) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_vline(&fixture.fb, 0, 0, 0, true) == KSHIM_FB_OK);
    expect_uniform(&fixture, false);
    destroy_fixture(&fixture);
}

static void test_bitmaps(const format_case_t *format)
{
    static const uint8_t packed[2][9] = {
        {0x09, 0x03, 0xe7, 0xb6, 0x00, 0x5a, 0x63, 0x01, 0xc3}, /* LSB */
        {0x90, 0xc0, 0xe7, 0x6d, 0x00, 0x5a, 0xc6, 0x80, 0xc3}, /* MSB */
    };
    static const char *const glyph[HEIGHT] = {
        "#..#....#", ".##.##.#.", "##...##.#", ".........", "........."
    };
    static const char *const inverted[HEIGHT] = {
        ".##.####.", "#..#..#.#", "..###..#.", ".........", "........."
    };
    static const char *const top_left[HEIGHT] = {
        ".##.#....", "..##.#...", ".........", ".........", "........."
    };
    static const char *const bottom_right[HEIGHT] = {
        ".........", ".........", ".........", ".......#.", "........#"
    };
    fixture_t fixture;
    uint8_t *source = malloc(sizeof(packed[0]));

    test_name = format->name;
    CHECK(source != NULL);
    make_fixture(&fixture, format, 3, false);
    for (unsigned msb = 0; msb <= 1; ++msb) {
        memcpy(source, packed[msb], sizeof(packed[msb]));
        clear_fixture(&fixture);
        CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0, 10, 3,
              source, 3, msb != 0, true, false) == KSHIM_FB_OK);
        expect_rows(&fixture, glyph);
        clear_fixture(&fixture);
        CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0, 10, 3,
              source, 3, msb != 0, false, false) == KSHIM_FB_OK);
        expect_rows(&fixture, inverted);
        clear_fixture(&fixture);
        CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, -3, -1, 10, 3,
              source, 3, msb != 0, true, false) == KSHIM_FB_OK);
        expect_rows(&fixture, top_left);
        clear_fixture(&fixture);
        CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 7, 3, 10, 3,
              source, 3, msb != 0, true, false) == KSHIM_FB_OK);
        expect_rows(&fixture, bottom_right);

        clear_fixture(&fixture);
        CHECK(kshim_fb_fill_rect(&fixture.fb, 0, 0, WIDTH, HEIGHT,
                                 true) == KSHIM_FB_OK);
        CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0, 10, 3,
              source, 3, msb != 0, true, true) == KSHIM_FB_OK);
        expect_uniform(&fixture, true); /* zero bits must preserve foreground */
        clear_fixture(&fixture);
        CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0, 10, 3,
              source, 3, msb != 0, false, true) == KSHIM_FB_OK);
        expect_uniform(&fixture, false); /* zero bits must preserve background */
        CHECK(memcmp(source, packed[msb], sizeof(packed[msb])) == 0);
    }

    /* Fully clipped images must not touch their source, even at int32 extremes. */
    const uint8_t *inaccessible = (const uint8_t *)(uintptr_t)1;
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, INT32_MIN, INT32_MIN,
          10, 3, inaccessible, 2, true, true, false) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, INT32_MAX, INT32_MAX,
          10, 3, inaccessible, 2, true, true, false) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, WIDTH, 0,
          10, 3, inaccessible, 2, true, true, false) == KSHIM_FB_OK);
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, HEIGHT,
          10, 3, inaccessible, 2, true, true, false) == KSHIM_FB_OK);
#if SIZE_MAX > UINT32_MAX
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, INT32_MAX, INT32_MAX,
          UINT32_MAX, UINT32_MAX, inaccessible, UINT32_MAX,
          true, true, false) == KSHIM_FB_OK);
#endif
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0,
          10, 3, NULL, 2, true, true, false) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0,
          0, 3, source, 2, true, true, false) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0,
          10, 0, source, 2, true, true, false) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_mono_bitmap(&fixture.fb, 0, 0,
          10, 3, source, 1, true, true, false) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    expect_uniform(&fixture, false);
    destroy_fixture(&fixture);
    free(source);
}

static void test_invalid_configurations(const format_case_t *format)
{
    fixture_t fixture;
    kshim_framebuffer_t ignored;
    kshim_framebuffer_config_t config;

    test_name = format->name;
    make_fixture(&fixture, format, 3, false);
    clear_fixture(&fixture);
    CHECK(kshim_fb_init(NULL, &fixture.config) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_init(&ignored, NULL) == KSHIM_FB_ERR_INVALID_ARGUMENT);
#define INVALID_CONFIG(member, value, error) do { \
    config = fixture.config; \
    config.member = (value); \
    CHECK(kshim_fb_init(&ignored, &config) == (error)); \
} while (0)
    INVALID_CONFIG(render_address, 0, KSHIM_FB_ERR_INVALID_ARGUMENT);
    INVALID_CONFIG(width, 0, KSHIM_FB_ERR_INVALID_ARGUMENT);
    INVALID_CONFIG(height, 0, KSHIM_FB_ERR_INVALID_ARGUMENT);
    INVALID_CONFIG(format, (kshim_fb_format_t)0, KSHIM_FB_ERR_UNSUPPORTED_FORMAT);
    INVALID_CONFIG(format, (kshim_fb_format_t)9, KSHIM_FB_ERR_UNSUPPORTED_FORMAT);
    INVALID_CONFIG(bpp, 24, KSHIM_FB_ERR_INVALID_ARGUMENT);
    INVALID_CONFIG(stride, (uint32_t)fixture.used_per_row - 1,
                   KSHIM_FB_ERR_INVALID_ARGUMENT);
    INVALID_CONFIG(buffer_size, fixture.size - 1, KSHIM_FB_ERR_BUFFER_TOO_SMALL);
    INVALID_CONFIG(render_address, UINTPTR_MAX - fixture.size + 2,
                   KSHIM_FB_ERR_OVERFLOW);
#undef INVALID_CONFIG
    config = fixture.config;
    config.render_address = UINTPTR_MAX - fixture.size + 1;
    CHECK(kshim_fb_init(&ignored, &config) == KSHIM_FB_OK); /* exact upper bound */
    config = fixture.config;
    config.buffer_size = 0;
    CHECK(kshim_fb_init(&ignored, &config) == KSHIM_FB_OK); /* unknown bound */
    config.buffer_size = fixture.size + 1;
    CHECK(kshim_fb_init(&ignored, &config) == KSHIM_FB_OK);

    ignored = fixture.fb;
    ignored.frame_size--;
    CHECK(kshim_fb_clear(&ignored) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    ignored = fixture.fb;
    ignored.buffer_size--;
    CHECK(kshim_fb_set_pixel(&ignored, 0, 0, true) == KSHIM_FB_ERR_BUFFER_TOO_SMALL);
    ignored = fixture.fb;
    ignored.render_address = UINTPTR_MAX;
    CHECK(kshim_fb_fill_rect(&ignored, 0, 0, 1, 1, true) == KSHIM_FB_ERR_OVERFLOW);
    expect_uniform(&fixture, false);
    destroy_fixture(&fixture);
}

static void test_size_overflow_and_null(void)
{
    kshim_framebuffer_t fb;
    uint8_t pixel = 0x5a;
    kshim_framebuffer_config_t config = {
        .render_address = (uintptr_t)&pixel,
        .width = UINT32_MAX,
        .height = 1,
        .bpp = 32,
        .format = KSHIM_FB_FORMAT_ARGB8888,
    };

    test_name = "overflow and null";
    CHECK(kshim_fb_init(&fb, &config) == KSHIM_FB_ERR_OVERFLOW);
    config.bpp = 16;
    config.format = KSHIM_FB_FORMAT_RGB565;
    CHECK(kshim_fb_init(&fb, &config) == KSHIM_FB_ERR_OVERFLOW);
    config = (kshim_framebuffer_config_t){
        .render_address = UINTPTR_MAX,
        .width = 1,
        .height = UINT32_MAX,
        .stride = UINT32_MAX,
        .bpp = 8,
        .format = KSHIM_FB_FORMAT_GRAY8,
    };
    CHECK(kshim_fb_init(&fb, &config) == KSHIM_FB_ERR_OVERFLOW);
    config.render_address = 1;
    config.buffer_size = 1;
#if SIZE_MAX > UINT32_MAX
    CHECK(kshim_fb_init(&fb, &config) == KSHIM_FB_ERR_BUFFER_TOO_SMALL);
#else
    CHECK(kshim_fb_init(&fb, &config) == KSHIM_FB_ERR_OVERFLOW);
#endif
    config = (kshim_framebuffer_config_t){
        .render_address = (uintptr_t)&pixel,
        .width = 1, .height = 1, .bpp = 8,
        .format = KSHIM_FB_FORMAT_GRAY8,
        .foreground = 0x13, .background = 0x27, .buffer_size = 1,
    };
    CHECK(kshim_fb_init(&fb, &config) == KSHIM_FB_OK);
    CHECK(pixel == 0x5a);
    CHECK(kshim_fb_clear(&fb) == KSHIM_FB_OK);
    CHECK(pixel == 0x27);
    CHECK(kshim_fb_set_pixel(&fb, 0, 0, true) == KSHIM_FB_OK);
    CHECK(pixel == 0x13);
    CHECK(kshim_fb_format_bpp((kshim_fb_format_t)0) == 0);
    CHECK(kshim_fb_format_bpp((kshim_fb_format_t)9) == 0);
    CHECK(kshim_fb_clear(NULL) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_set_colors(NULL, 1, 0) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_set_pixel(NULL, 0, 0, true) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_fill_rect(NULL, 0, 0, 1, 1, true) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_rect(NULL, 0, 0, 1, 1, true) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_line(NULL, 0, 0, 1, 1, true) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_hline(NULL, 0, 0, 1, true) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_vline(NULL, 0, 0, 1, true) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_draw_mono_bitmap(NULL, 0, 0, 1, 1, &pixel, 1,
          true, true, false) == KSHIM_FB_ERR_INVALID_ARGUMENT);
}

/* Golden values cover primaries, gray conversion, and a translucent color.
 * Alpha is already composited by LVGL and must never darken RGB a second time. */
static const uint32_t argb_colors[] = {
    0xff000000, 0xffffffff, 0xffff0000, 0xff00ff00, 0xff0000ff, 0x80204060,
};
static const uint32_t converted_colors[][6] = {
    {0, 1, 0, 1, 0, 0},
    {0, 1, 0, 1, 0, 0},
    {0, 255, 77, 149, 29, 58},
    {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0x220c},
    {0xff000000, 0xffffffff, 0xffff0000, 0xff00ff00, 0xff0000ff, 0xff204060},
    {0xff000000, 0xffffffff, 0xffff0000, 0xff00ff00, 0xff0000ff, 0x80204060},
    {0xff000000, 0xffffffff, 0xff0000ff, 0xff00ff00, 0xffff0000, 0xff604020},
    {0xff000000, 0xffffffff, 0xff0000ff, 0xff00ff00, 0xffff0000, 0x80604020},
};

static void expect_color(const fixture_t *fixture, uint32_t x, uint32_t y,
                          uint32_t value)
{
    const uint8_t *row = fixture->pixels + y * fixture->fb.stride;
    if (fixture->fb.bpp == 1) {
        unsigned bit = fixture->fb.format == KSHIM_FB_FORMAT_MONO1_LSB ?
                       x % 8U : 7U - x % 8U;
        CHECK(((row[x / 8U] >> bit) & 1U) == value);
    } else {
        for (unsigned byte = 0; byte < fixture->fb.bpp / 8U; ++byte)
            CHECK(row[x * (fixture->fb.bpp / 8U) + byte] ==
                  (uint8_t)(value >> (byte * 8U)));
    }
}

static void test_color_blit(size_t format_index)
{
    fixture_t fixture;
    const format_case_t *format = &formats[format_index];
    const uint32_t *expected = converted_colors[format_index];
    uint32_t padded_source[10];
    uint8_t snapshot[sizeof(padded_source)];
    test_name = format->name;
    make_fixture(&fixture, format, 3, false);
    clear_fixture(&fixture);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 1, 1, 6, 1, argb_colors,
                                sizeof(argb_colors)) == KSHIM_FB_OK);
    for (uint32_t y = 0; y < HEIGHT; ++y)
        for (uint32_t x = 0; x < WIDTH; ++x) {
            if (y == 1 && x >= 1 && x <= 6)
                expect_color(&fixture, x, y, expected[x - 1]);
            else
                expect_pixel(&fixture, x, y, false);
        }
    expect_padding(&fixture, 0);

    /* Odd source stride makes the second row unaligned, with sentinel padding. */
    memset(padded_source, 0xa3, sizeof(padded_source));
    memcpy(padded_source, argb_colors, 3 * sizeof(uint32_t));
    memcpy((uint8_t *)padded_source + 19, argb_colors + 3, 3 * sizeof(uint32_t));
    memcpy(snapshot, padded_source, sizeof(snapshot));
    clear_fixture(&fixture);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, -1, 1, 3, 2, padded_source, 19) == KSHIM_FB_OK);
    for (uint32_t y = 0; y < HEIGHT; ++y)
        for (uint32_t x = 0; x < WIDTH; ++x) {
            if (x < 2 && (y == 1 || y == 2))
                expect_color(&fixture, x, y, expected[(y - 1) * 3 + x + 1]);
            else
                expect_pixel(&fixture, x, y, false);
        }
    expect_padding(&fixture, 0);
    clear_fixture(&fixture);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, WIDTH - 2, HEIGHT - 1, 3, 2,
                                padded_source, 19) == KSHIM_FB_OK);
    for (uint32_t y = 0; y < HEIGHT; ++y)
        for (uint32_t x = 0; x < WIDTH; ++x) {
            if (y == HEIGHT - 1 && x >= WIDTH - 2)
                expect_color(&fixture, x, y, expected[x - (WIDTH - 2)]);
            else
                expect_pixel(&fixture, x, y, false);
        }
    CHECK(memcmp(snapshot, padded_source, sizeof(snapshot)) == 0);
    expect_padding(&fixture, 0);
    clear_fixture(&fixture);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, -1, 3, 2, padded_source, 19) == KSHIM_FB_OK);
    for (uint32_t x = 0; x < 3; ++x)
        expect_color(&fixture, x, 0, expected[x + 3]);
    expect_padding(&fixture, 0);

    clear_fixture(&fixture);
    const uint32_t *inaccessible = (const uint32_t *)(uintptr_t)4;
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, INT32_MIN, INT32_MIN, 3, 2,
                                inaccessible, 12) == KSHIM_FB_OK);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, INT32_MAX, INT32_MAX, 3, 2,
                                inaccessible, 12) == KSHIM_FB_OK);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 0, 2, NULL, 0) == KSHIM_FB_OK);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 3, 0, NULL, 0) == KSHIM_FB_OK);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 3, 2, NULL, 12) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 3, 2, padded_source, 11) ==
          KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 3, 3, padded_source, SIZE_MAX) ==
          KSHIM_FB_ERR_OVERFLOW);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 3, 2, padded_source, SIZE_MAX - 11U) ==
          KSHIM_FB_ERR_OVERFLOW);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 1, 1,
                                (const uint32_t *)(UINTPTR_MAX - 2U), 4) == KSHIM_FB_ERR_OVERFLOW);
    CHECK(kshim_fb_blit_argb8888(NULL, 0, 0, 1, 1, argb_colors, 4) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    CHECK(kshim_fb_blit_argb8888(&fixture.fb, 0, 0, 1, 1,
                                (const uint32_t *)fixture.pixels, 4) == KSHIM_FB_ERR_INVALID_ARGUMENT);
    expect_uniform(&fixture, false);
    destroy_fixture(&fixture);
}

static void test_aligned_color_blit(void)
{
    const uint32_t source[8] = {
        0x80204060, 0x00000000, 0xffffffff, 0xdeadc0de,
        0xffff0000, 0xff00ff00, 0xff0000ff, 0xdeadc0de,
    };
    uint32_t target[8];
    kshim_framebuffer_t fb;
    kshim_framebuffer_config_t config = {
        .render_address = (uintptr_t)target, .width = 3, .height = 2, .stride = 16,
        .bpp = 32, .format = KSHIM_FB_FORMAT_ARGB8888, .buffer_size = sizeof(target),
    };
    test_name = "aligned ARGB/XRGB color blit";
    for (unsigned mode = 0; mode < 2; ++mode) {
        config.format = mode == 0 ? KSHIM_FB_FORMAT_ARGB8888 : KSHIM_FB_FORMAT_XRGB8888;
        memset(target, 0xa5, sizeof(target));
        CHECK(kshim_fb_init(&fb, &config) == KSHIM_FB_OK);
        CHECK(kshim_fb_blit_argb8888(&fb, 0, 0, 3, 2, source, 16) == KSHIM_FB_OK);
        for (unsigned i = 0; i < 8; ++i)
            CHECK(target[i] == (i % 4 == 3 ? 0xa5a5a5a5U :
                               source[i] | (mode ? 0xff000000U : 0)));
    }
}

int main(void)
{
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        test_raw_colors(&formats[i]);
        test_clipping(&formats[i]);
        test_bitmaps(&formats[i]);
        test_color_blit(i);
        test_invalid_configurations(&formats[i]);
    }
    test_size_overflow_and_null();
    test_aligned_color_blit();
    printf("framebuffer: 6 formats, ARGB blits, raw colors, padding, clipping, bitmaps and "
           "invalid configurations passed (%zu checks)\n", checks);
    return 0;
}
