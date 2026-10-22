/* Host regression tests; link this file with src/drivers/touch.c. */
#include <touch.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

typedef struct event_log {
    kshim_touch_event_t events[64];
    size_t count;
} event_log_t;

static void record_event(void *context, const kshim_touch_event_t *event)
{
    event_log_t *log = context;

    assert(log->count < ARRAY_SIZE(log->events));
    log->events[log->count++] = *event;
}

static void init_touch(kshim_touch_t *touch, event_log_t *log,
                       uint32_t source_width, uint32_t source_height,
                       uint32_t output_width, uint32_t output_height,
                       kshim_touch_rotation_t rotation)
{
    kshim_touch_config_t config = {
        .source_width = source_width,
        .source_height = source_height,
        .output_width = output_width,
        .output_height = output_height,
        .rotation = rotation,
        .emit = record_event,
        .emit_context = log,
    };

    memset(log, 0, sizeof(*log));
    assert(kshim_touch_init(touch, &config) == KSHIM_TOUCH_OK);
}

static void expect_event(const event_log_t *log, size_t index,
                         kshim_touch_event_type_t type, uint8_t id,
                         uint32_t x, uint32_t y)
{
    assert(index < log->count);
    assert(log->events[index].type == type);
    assert(log->events[index].contact_id == id);
    assert(log->events[index].x == x);
    assert(log->events[index].y == y);
}

static void test_stable_primary(void)
{
    kshim_touch_t touch;
    event_log_t log;
    kshim_touch_event_t primary;

    init_touch(&touch, &log, 1000, 2000, 1000, 2000,
               KSHIM_TOUCH_ROTATION_0);
    assert(kshim_touch_update(&touch, 3, KSHIM_TOUCH_CONTACT_ENTER,
                              100, 200) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 1, KSHIM_TOUCH_CONTACT_ENTER,
                              300, 400) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 1, KSHIM_TOUCH_CONTACT_MOVE,
                              350, 450) == KSHIM_TOUCH_OK);
    assert(log.count == 1);
    expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 3, 100, 200);
    assert(kshim_touch_get_primary(&touch, &primary));
    assert(primary.contact_id == 3);

    assert(kshim_touch_update(&touch, 3, KSHIM_TOUCH_CONTACT_MOVE,
                              120, 240) == KSHIM_TOUCH_OK);
    assert(log.count == 2);
    expect_event(&log, 1, KSHIM_TOUCH_EVENT_MOVE, 3, 120, 240);

    assert(kshim_touch_update(&touch, 1, KSHIM_TOUCH_CONTACT_LEAVE,
                              0, 0) == KSHIM_TOUCH_OK);
    assert(log.count == 2);
    assert(kshim_touch_update(&touch, 3, KSHIM_TOUCH_CONTACT_LEAVE,
                              0, 0) == KSHIM_TOUCH_OK);
    assert(log.count == 3);
    expect_event(&log, 2, KSHIM_TOUCH_EVENT_RELEASE, 3, 120, 240);
    assert(!kshim_touch_get_primary(&touch, NULL));

    assert(kshim_touch_update(&touch, 3, KSHIM_TOUCH_CONTACT_LEAVE,
                              0, 0) == KSHIM_TOUCH_OK);
    assert(log.count == 3);
}

static void test_primary_handoff(void)
{
    kshim_touch_t touch;
    event_log_t log;

    init_touch(&touch, &log, 100, 100, 100, 100,
               KSHIM_TOUCH_ROTATION_0);
    assert(kshim_touch_update(&touch, 2, KSHIM_TOUCH_CONTACT_ENTER,
                              10, 20) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 8, KSHIM_TOUCH_CONTACT_ENTER,
                              30, 40) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 4, KSHIM_TOUCH_CONTACT_ENTER,
                              50, 60) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 8, KSHIM_TOUCH_CONTACT_MOVE,
                              31, 41) == KSHIM_TOUCH_OK);
    assert(log.count == 1);

    assert(kshim_touch_update(&touch, 2, KSHIM_TOUCH_CONTACT_LEAVE,
                              0, 0) == KSHIM_TOUCH_OK);
    assert(log.count == 3);
    expect_event(&log, 1, KSHIM_TOUCH_EVENT_RELEASE, 2, 10, 20);
    expect_event(&log, 2, KSHIM_TOUCH_EVENT_PRESS, 8, 31, 41);

    assert(kshim_touch_update(&touch, 8, KSHIM_TOUCH_CONTACT_LEAVE,
                              0, 0) == KSHIM_TOUCH_OK);
    assert(log.count == 5);
    expect_event(&log, 3, KSHIM_TOUCH_EVENT_RELEASE, 8, 31, 41);
    expect_event(&log, 4, KSHIM_TOUCH_EVENT_PRESS, 4, 50, 60);
}

static void test_rotations(void)
{
    static const struct {
        kshim_touch_rotation_t rotation;
        uint32_t output_width;
        uint32_t output_height;
        uint32_t expected_x;
        uint32_t expected_y;
    } cases[] = {
        {KSHIM_TOUCH_ROTATION_0, 4, 3, 0, 0},
        {KSHIM_TOUCH_ROTATION_90, 3, 4, 2, 0},
        {KSHIM_TOUCH_ROTATION_180, 4, 3, 3, 2},
        {KSHIM_TOUCH_ROTATION_270, 3, 4, 0, 3},
    };

    for (size_t index = 0; index < ARRAY_SIZE(cases); ++index) {
        kshim_touch_t touch;
        event_log_t log;

        init_touch(&touch, &log, 4, 3, cases[index].output_width,
                   cases[index].output_height, cases[index].rotation);
        assert(kshim_touch_update(&touch, 0, KSHIM_TOUCH_CONTACT_ENTER,
                                  0, 0) == KSHIM_TOUCH_OK);
        expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 0,
                     cases[index].expected_x, cases[index].expected_y);
    }
}

static void test_scaling_clamping_and_duplicates(void)
{
    kshim_touch_t touch;
    event_log_t log;

    init_touch(&touch, &log, 1080, 2340, 540, 1170,
               KSHIM_TOUCH_ROTATION_0);
    assert(kshim_touch_update(&touch, 0, KSHIM_TOUCH_CONTACT_ENTER,
                              540, 1170) == KSHIM_TOUCH_OK);
    expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 0, 270, 585);

    assert(kshim_touch_update(&touch, 0, KSHIM_TOUCH_CONTACT_ENTER,
                              540, 1170) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 0, KSHIM_TOUCH_CONTACT_MOVE,
                              541, 1170) == KSHIM_TOUCH_OK);
    assert(log.count == 1); /* Both raw X positions map to output X 270. */

    assert(kshim_touch_update(&touch, 0, KSHIM_TOUCH_CONTACT_MOVE,
                              UINT32_MAX, UINT32_MAX) == KSHIM_TOUCH_OK);
    assert(log.count == 2);
    expect_event(&log, 1, KSHIM_TOUCH_EVENT_MOVE, 0, 539, 1169);
}

static void test_error_release_and_no_ghosts(void)
{
    kshim_touch_t touch;
    event_log_t log;

    init_touch(&touch, &log, 100, 200, 100, 200,
               KSHIM_TOUCH_ROTATION_0);
    assert(kshim_touch_update(&touch, 1, KSHIM_TOUCH_CONTACT_ENTER,
                              12, 34) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 2, KSHIM_TOUCH_CONTACT_ENTER,
                              56, 78) == KSHIM_TOUCH_OK);

    kshim_touch_cancel(&touch);
    assert(log.count == 2);
    expect_event(&log, 1, KSHIM_TOUCH_EVENT_CANCEL, 1, 12, 34);
    assert(!kshim_touch_get_primary(&touch, NULL));

    kshim_touch_cancel(&touch);
    assert(kshim_touch_update(&touch, 1, KSHIM_TOUCH_CONTACT_MOVE,
                              20, 40) == KSHIM_TOUCH_OK);
    assert(kshim_touch_update(&touch, 2, KSHIM_TOUCH_CONTACT_LEAVE,
                              0, 0) == KSHIM_TOUCH_OK);
    assert(log.count == 2);

    assert(kshim_touch_update(&touch, 4, KSHIM_TOUCH_CONTACT_ENTER,
                              90, 190) == KSHIM_TOUCH_OK);
    assert(log.count == 3);
    expect_event(&log, 2, KSHIM_TOUCH_EVENT_PRESS, 4, 90, 190);
}

static void test_invalid_configuration(void)
{
    kshim_touch_t touch;
    kshim_touch_config_t config = {
        .source_width = 10,
        .source_height = 10,
        .output_width = 10,
        .output_height = 10,
        .rotation = (kshim_touch_rotation_t)45,
    };

    assert(kshim_touch_init(NULL, &config) == KSHIM_TOUCH_ERR_INVALID);
    assert(kshim_touch_init(&touch, &config) == KSHIM_TOUCH_ERR_INVALID);
    config.rotation = KSHIM_TOUCH_ROTATION_0;
    config.output_width = 0;
    assert(kshim_touch_init(&touch, &config) == KSHIM_TOUCH_ERR_INVALID);
}

int main(void)
{
    test_stable_primary();
    test_primary_handoff();
    test_rotations();
    test_scaling_clamping_and_duplicates();
    test_error_release_and_no_ghosts();
    test_invalid_configuration();
    puts("touch_test: all tests passed");
    return 0;
}
