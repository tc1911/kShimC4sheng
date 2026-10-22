#include <smp_selftest.h>

#include <config.h>
#ifndef CONFIG_KSHIM_SMP_SELFTEST
#define CONFIG_KSHIM_SMP_SELFTEST 0
#endif
#ifndef KSHIM_LVGL_TEST_FORCE_DELETE_FAILURE
#define KSHIM_LVGL_TEST_FORCE_DELETE_FAILURE 0
#endif

#if CONFIG_KSHIM_SMP_SELFTEST
#include <kshim_lvgl_os.h>
#include <lvgl.h>
#endif
#include <smp.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_KSHIM_SMP_WORKERS
#define CONFIG_KSHIM_SMP_WORKERS 3
#endif

#define SELFTEST_PROBE_ADDRESS UINT64_C(0x46000000)
#define SELFTEST_ITERATIONS UINT64_C(1024)
#define SELFTEST_PERMANENT_VISITS UINT64_C(4)
#define SELFTEST_MPIDR_MASK UINT64_C(0x000000ff00ffffff)
#if CONFIG_KSHIM_SMP_SELFTEST
#define SELFTEST_LVGL_WIDTH 64U
#define SELFTEST_LVGL_HEIGHT 48U
#define SELFTEST_LVGL_DRAW_UNITS (LV_DRAW_SW_DRAW_UNIT_CNT - 1U)
#endif

typedef struct {
    uint64_t expected_mpidr;
    uint64_t *probe;
    uint64_t iterations;
    uint64_t observed_mpidr;
} smp_task_context_t;

typedef struct {
    uint64_t visits;
} smp_permanent_context_t;

#if CONFIG_KSHIM_SMP_SELFTEST
typedef struct {
    size_t pixels;
    uint32_t flushes;
    uint16_t first_pixel;
    uint8_t have_first;
    uint8_t varied;
} lvgl_flush_context_t;

static uint16_t mLvglBuffer[SELFTEST_LVGL_WIDTH * SELFTEST_LVGL_HEIGHT]
    __attribute__((aligned(8)));
#endif

static uint64_t selftest_mpidr(void)
{
#if defined(__aarch64__)
    uint64_t value;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(value));
    return value & SELFTEST_MPIDR_MASK;
#else
    return 0U;
#endif
}

static int shared_probe_task(void *opaque)
{
    smp_task_context_t *context = opaque;
    uint64_t mpidr = selftest_mpidr();

    context->observed_mpidr = mpidr;
    for (uint64_t index = 0U; index < context->iterations; index++)
        (void)__atomic_fetch_add(context->probe, 1U, __ATOMIC_RELAXED);
    return mpidr == context->expected_mpidr ? 0 : -1;
}

static int permanent_probe(void *opaque)
{
    smp_permanent_context_t *context = opaque;

    (void)__atomic_fetch_add(&context->visits, 1U, __ATOMIC_RELAXED);
    return 0;
}

static int wait_for_permanent_callbacks(
    smp_permanent_context_t *contexts, size_t count, uint64_t timeout_us)
{
    uint64_t start = kshim_smp_time_us();

    for (;;) {
        size_t ready = 0U;

        for (size_t index = 0U; index < count; index++) {
            if (__atomic_load_n(&contexts[index].visits, __ATOMIC_ACQUIRE) >=
                SELFTEST_PERMANENT_VISITS)
                ready++;
        }
        if (ready == count)
            return 0;
        if (kshim_smp_time_us() - start >= timeout_us)
            return -1;
#if defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
}

#if CONFIG_KSHIM_SMP_SELFTEST
static size_t bit_count(uint32_t value)
{
    size_t count = 0U;

    while (value != 0U) {
        count += value & 1U;
        value >>= 1;
    }
    return count;
}

static void lvgl_flush(
    lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    lvgl_flush_context_t *context = lv_display_get_user_data(display);
    int32_t width;
    int32_t height;
    uint32_t stride;

    if (context == NULL || area == NULL || pixels == NULL) {
        lv_display_flush_ready(display);
        return;
    }
    width = area->x2 - area->x1 + 1;
    height = area->y2 - area->y1 + 1;
    if (width <= 0 || height <= 0) {
        lv_display_flush_ready(display);
        return;
    }
    stride = lv_draw_buf_width_to_stride(
        (uint32_t)width, LV_COLOR_FORMAT_RGB565);
    for (int32_t y = 0; y < height; y++) {
        const uint16_t *row = (const uint16_t *)(pixels + (size_t)y * stride);

        for (int32_t x = 0; x < width; x++) {
            uint16_t pixel = row[x];

            if (context->have_first == 0U) {
                context->first_pixel = pixel;
                context->have_first = 1U;
            }
            else if (pixel != context->first_pixel) {
                context->varied = 1U;
            }
            context->pixels++;
        }
    }
    context->flushes++;
    lv_display_flush_ready(display);
}

static int lvgl_smp_selftest(size_t worker_count)
{
    lvgl_flush_context_t flush = {0};
    lv_display_t *display = NULL;
    lv_obj_t *screen;
    lv_obj_t *box;
    size_t expected = worker_count;
    size_t active = 0U;
    size_t peak = 0U;
    uint32_t mask = 0U;
    int failure = 0;

    if (expected > SELFTEST_LVGL_DRAW_UNITS)
        expected = SELFTEST_LVGL_DRAW_UNITS;
    if (kshim_lvgl_os_reset_stats() != 0)
        return -120;

    memset(mLvglBuffer, 0x5a, sizeof(mLvglBuffer));
    lv_init();
    active = kshim_lvgl_os_active_threads();
    peak = kshim_lvgl_os_peak_threads();
    mask = kshim_lvgl_os_worker_mask();
    if (active != expected || peak != expected || bit_count(mask) != expected)
        failure = -121;

    if (failure == 0) {
        display = lv_display_create(
            (int32_t)SELFTEST_LVGL_WIDTH, (int32_t)SELFTEST_LVGL_HEIGHT);
        if (display == NULL)
            failure = -122;
    }
    if (failure == 0) {
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(
            display, mLvglBuffer, NULL, sizeof(mLvglBuffer),
            LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_user_data(display, &flush);
        lv_display_set_flush_cb(display, lvgl_flush);

        screen = lv_screen_active();
        box = screen != NULL ? lv_obj_create(screen) : NULL;
        if (screen == NULL || box == NULL) {
            failure = -123;
        }
        else {
            lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
            lv_obj_remove_style_all(box);
            lv_obj_set_size(box, 24, 16);
            lv_obj_center(box);
            lv_obj_set_style_bg_color(box, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
            lv_obj_invalidate(screen);
            lv_refr_now(display);
            if (flush.flushes == 0U ||
                flush.pixels < SELFTEST_LVGL_WIDTH * SELFTEST_LVGL_HEIGHT ||
                flush.varied == 0U)
                failure = -124;
        }
    }

#if KSHIM_LVGL_TEST_FORCE_DELETE_FAILURE
    printf("LVGL_FAILSTOP: delete failure armed\n");
#endif
    lv_deinit();
#if KSHIM_LVGL_TEST_FORCE_DELETE_FAILURE
    printf("LVGL_FAILSTOP: ERROR deinit returned\n");
#endif
    if ((kshim_lvgl_os_active_threads() != 0U ||
         kshim_lvgl_os_worker_mask() != 0U ||
         kshim_lvgl_os_prepare_shutdown() != 0) && failure == 0)
        failure = -125;
    printf("LVGL_SMP: expected=%u active=%u peak=%u mask=0x%x "
           "flushes=%u pixels=%u varied=%u shutdown=%s\n",
           (unsigned)expected, (unsigned)active, (unsigned)peak, mask,
           flush.flushes, (unsigned)flush.pixels, flush.varied,
           kshim_lvgl_os_prepare_shutdown() == 0 ? "clean" : "dirty");
    return failure;
}
#endif

int kshim_smp_selftest(const void *fdt)
{
    smp_task_context_t tasks[KSHIM_SMP_MAX_CPUS - 1U] = {0};
    smp_permanent_context_t permanent[KSHIM_SMP_MAX_CPUS - 1U] = {0};
    kshim_smp_ticket_t tickets[KSHIM_SMP_MAX_CPUS - 1U] = {0};
    uint64_t permanent_snapshot[KSHIM_SMP_MAX_CPUS - 1U] = {0};
    uint64_t *probe = (uint64_t *)(uintptr_t)SELFTEST_PROBE_ADDRESS;
    size_t cpu_count;
    size_t worker_count;
    size_t permanent_set = 0U;
    int failure = 0;
    int status;

    status = kshim_smp_init(fdt);
    if (status != KSHIM_SMP_OK) {
        printf("SMP: init failed (%d)\n", status);
        return -100;
    }
    cpu_count = kshim_smp_cpu_count();
    if (cpu_count == 0U || cpu_count > KSHIM_SMP_MAX_CPUS)
        return -101;
    worker_count = cpu_count - 1U;
    if (worker_count > (size_t)CONFIG_KSHIM_SMP_WORKERS)
        worker_count = (size_t)CONFIG_KSHIM_SMP_WORKERS;

    status = kshim_smp_start_workers(KSHIM_SMP_DEFAULT_TIMEOUT_US);
    if (status != KSHIM_SMP_OK) {
        printf("SMP: CPU_ON failed (%d)\n", status);
        (void)kshim_smp_shutdown(KSHIM_SMP_DEFAULT_TIMEOUT_US);
        return -102;
    }
    if (kshim_smp_online_count() != worker_count + 1U ||
        kshim_smp_available_count() != worker_count) {
        failure = -103;
        goto Shutdown;
    }

    printf("SMP: discovered %u CPU(s), started %u worker(s)\n",
           (unsigned)cpu_count, (unsigned)worker_count);
    if (worker_count == 0U) {
#if CONFIG_KSHIM_SMP_SELFTEST
        goto Lvgl;
#else
        goto Shutdown;
#endif
    }

    __atomic_store_n(probe, 0U, __ATOMIC_RELEASE);
    for (size_t index = 0U; index < worker_count; index++) {
        tasks[index].expected_mpidr = kshim_smp_cpu_mpidr(index + 1U);
        tasks[index].probe = probe;
        tasks[index].iterations = SELFTEST_ITERATIONS;
        status = kshim_smp_submit(
            index, shared_probe_task, &tasks[index], &tickets[index]);
        if (status != KSHIM_SMP_OK || tickets[index] == 0U) {
            failure = -104;
            break;
        }
    }
    for (uint64_t index = 0U; index < SELFTEST_ITERATIONS; index++)
        (void)__atomic_fetch_add(probe, 1U, __ATOMIC_RELAXED);

    for (size_t index = 0U; index < worker_count; index++) {
        int result = -1;

        if (tickets[index] == 0U)
            continue;
        status = kshim_smp_join(
            index, tickets[index], KSHIM_SMP_DEFAULT_TIMEOUT_US, &result);
        if ((status != KSHIM_SMP_OK || result != 0 ||
             tasks[index].observed_mpidr != tasks[index].expected_mpidr) &&
            failure == 0)
            failure = -105;
    }
    if (failure == 0 &&
        __atomic_load_n(probe, __ATOMIC_ACQUIRE) !=
            (worker_count + 1U) * SELFTEST_ITERATIONS)
        failure = -106;
    if (failure != 0)
        goto Shutdown;

    for (size_t index = 0U; index < worker_count; index++) {
        status = kshim_smp_set_permanent_callback(
            index, permanent_probe, &permanent[index]);
        if (status != KSHIM_SMP_OK) {
            failure = -107;
            break;
        }
        permanent_set++;
    }
    if (failure == 0 && wait_for_permanent_callbacks(
            permanent, worker_count, KSHIM_SMP_DEFAULT_TIMEOUT_US) != 0)
        failure = -108;

    for (size_t index = 0U; index < permanent_set; index++) {
        status = kshim_smp_set_permanent_callback(index, NULL, NULL);
        if (status != KSHIM_SMP_OK && failure == 0)
            failure = -109;
        permanent_snapshot[index] =
            __atomic_load_n(&permanent[index].visits, __ATOMIC_ACQUIRE);
    }
    for (unsigned spin = 0U; spin < 256U; spin++) {
#if defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
    for (size_t index = 0U; index < permanent_set; index++) {
        if (__atomic_load_n(&permanent[index].visits, __ATOMIC_ACQUIRE) !=
                permanent_snapshot[index] && failure == 0)
            failure = -110;
    }

#if CONFIG_KSHIM_SMP_SELFTEST
Lvgl:
    status = lvgl_smp_selftest(worker_count);
    if (status != 0 && failure == 0)
        failure = status;
#endif

Shutdown:
    status = kshim_smp_shutdown(KSHIM_SMP_DEFAULT_TIMEOUT_US);
    if (status != KSHIM_SMP_OK && failure == 0)
        failure = -111;
    if (status == KSHIM_SMP_OK &&
        (kshim_smp_online_count() != 1U ||
         kshim_smp_available_count() != 0U) && failure == 0)
        failure = -112;
    if (failure != 0)
        printf("SMP: self-test stage failed (%d)\n", failure);
    return failure;
}
