/* Host regression tests; link with src/drivers/st_ftm5.c and src/drivers/touch.c. */
#include <st_ftm5.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define SCRIPT_MAX_STEPS 96U
#define SCRIPT_MAX_COMMAND 8U
#define SCRIPT_MAX_RESPONSE 257U
#define EVENT_LOG_SIZE 96U

typedef struct script_step {
    uint8_t command[SCRIPT_MAX_COMMAND];
    uint8_t response[SCRIPT_MAX_RESPONSE];
    size_t command_size;
    size_t response_size;
    int result;
} script_step_t;

typedef struct transfer_script {
    script_step_t steps[SCRIPT_MAX_STEPS];
    size_t step_count;
    size_t next_step;
    uint32_t delays[KSHIM_FTM5_READY_ATTEMPTS];
    size_t delay_count;
    kshim_ftm5_bus_kind_t bus_kind;
} transfer_script_t;

typedef struct event_log {
    kshim_touch_event_t events[EVENT_LOG_SIZE];
    size_t count;
} event_log_t;

static void script_add(transfer_script_t *script,
                       const uint8_t *command, size_t command_size,
                       const uint8_t *response, size_t response_size,
                       int result)
{
    script_step_t *step;

    assert(script->step_count < ARRAY_SIZE(script->steps));
    assert(command != NULL);
    assert(command_size <= SCRIPT_MAX_COMMAND);
    assert(response_size <= SCRIPT_MAX_RESPONSE);
    assert(response != NULL || response_size == 0U);

    step = &script->steps[script->step_count++];
    memset(step, 0, sizeof(*step));
    memcpy(step->command, command, command_size);
    if (response_size != 0U)
        memcpy(step->response, response, response_size);
    step->command_size = command_size;
    step->response_size = response_size;
    step->result = result;
}

static void script_add_read(transfer_script_t *script,
                            const uint8_t *command, size_t command_size,
                            const uint8_t *payload, size_t payload_size,
                            int result)
{
    uint8_t response[SCRIPT_MAX_RESPONSE];
    size_t response_size = payload_size;

    assert(payload_size + 1U <= sizeof(response));
    if (script->bus_kind == KSHIM_FTM5_BUS_SPI) {
        response[0] = UINT8_C(0xd0);
        if (payload_size != 0U)
            memcpy(&response[1], payload, payload_size);
        response_size++;
    } else if (payload_size != 0U) {
        memcpy(response, payload, payload_size);
    }
    script_add(script, command, command_size, response, response_size, result);
}

static int scripted_transfer(void *context, const uint8_t *command,
                             size_t command_size, uint8_t *response,
                             size_t response_size)
{
    transfer_script_t *script = context;
    const script_step_t *step;

    assert(script != NULL);
    assert(script->next_step < script->step_count);
    step = &script->steps[script->next_step++];
    assert(command_size == step->command_size);
    assert(memcmp(command, step->command, command_size) == 0);
    assert(response_size == step->response_size);
    assert(response != NULL || response_size == 0U);
    if (step->result == 0 && response_size != 0U)
        memcpy(response, step->response, response_size);
    return step->result;
}

static void scripted_delay(void *context, uint32_t delay_us)
{
    transfer_script_t *script = context;

    assert(script->delay_count < ARRAY_SIZE(script->delays));
    script->delays[script->delay_count++] = delay_us;
}

/* These host stubs exercise the driver's Qualcomm transport adapters without
 * pulling MMIO bus code into the protocol unit test. */
int kshim_i2c_transfer(struct kshim_qcom_bus *bus,
                       const struct kshim_i2c_msg *messages, size_t count,
                       uint32_t timeout_us)
{
    assert(bus != NULL);
    assert(messages != NULL);
    assert(count == 1U || count == 2U);
    assert(timeout_us == 0U);
    assert(messages[0].address == KSHIM_FTM5_I2C_ADDRESS);
    assert(!messages[0].read);
    if (count == 2U) {
        assert(messages[1].address == KSHIM_FTM5_I2C_ADDRESS);
        assert(messages[1].read);
        return scripted_transfer(bus->io.context,
                                 messages[0].data, messages[0].length,
                                 messages[1].data, messages[1].length);
    }
    return scripted_transfer(bus->io.context,
                             messages[0].data, messages[0].length,
                             NULL, 0);
}

int kshim_spi_transfer(struct kshim_qcom_bus *bus,
                       const struct kshim_spi_segment *segments, size_t count,
                       uint32_t timeout_us)
{
    assert(bus != NULL);
    assert(segments != NULL);
    assert(count == 1U || count == 2U);
    assert(timeout_us == 0U);
    assert(segments[0].tx != NULL);
    assert(segments[0].rx == NULL);
    if (count == 2U) {
        assert(segments[0].delay_us == 0U);
        assert(segments[1].tx == NULL);
        assert(segments[1].rx != NULL);
        assert(segments[1].delay_us == KSHIM_FTM5_SPI_CS_HOLD_US);
        return scripted_transfer(bus->io.context,
                                 segments[0].tx, segments[0].length,
                                 segments[1].rx, segments[1].length);
    }
    assert(segments[0].delay_us == KSHIM_FTM5_SPI_CS_HOLD_US);
    return scripted_transfer(bus->io.context,
                             segments[0].tx, segments[0].length, NULL, 0);
}

static void record_event(void *context, const kshim_touch_event_t *event)
{
    event_log_t *log = context;

    assert(log->count < ARRAY_SIZE(log->events));
    log->events[log->count++] = *event;
}

static void init_touch(kshim_touch_t *touch, event_log_t *log)
{
    kshim_touch_config_t config = {
        .source_width = KSHIM_FTM5_NATIVE_WIDTH,
        .source_height = KSHIM_FTM5_NATIVE_HEIGHT,
        .output_width = KSHIM_FTM5_NATIVE_WIDTH,
        .output_height = KSHIM_FTM5_NATIVE_HEIGHT,
        .rotation = KSHIM_TOUCH_ROTATION_0,
        .emit = record_event,
        .emit_context = log,
    };

    memset(log, 0, sizeof(*log));
    assert(kshim_touch_init(touch, &config) == KSHIM_TOUCH_OK);
}

static void make_system_info(uint8_t data[KSHIM_FTM5_SYSTEM_INFO_SIZE])
{
    memset(data, 0, KSHIM_FTM5_SYSTEM_INFO_SIZE);
    data[0] = UINT8_C(0xa5);
    data[1] = UINT8_C(0x01);
    data[16] = UINT8_C(0x34);
    data[17] = UINT8_C(0x12);
    data[20] = UINT8_C(0x78);
    data[21] = UINT8_C(0x56);
    data[80] = UINT8_C(0x38); /* 1080, little endian. */
    data[81] = UINT8_C(0x04);
    data[82] = UINT8_C(0x24); /* 2340, little endian. */
    data[83] = UINT8_C(0x09);
}

static void make_event(uint8_t event[KSHIM_FTM5_EVENT_SIZE], uint8_t type,
                       uint8_t contact_id, uint16_t x, uint16_t y,
                       uint8_t remaining)
{
    memset(event, 0, KSHIM_FTM5_EVENT_SIZE);
    event[0] = type;
    event[1] = (uint8_t)((contact_id << 4) | UINT8_C(0x01));
    event[2] = (uint8_t)x;
    event[3] = (uint8_t)(((x >> 8) & UINT16_C(0x0f)) |
                         ((y & UINT16_C(0x0f)) << 4));
    event[4] = (uint8_t)(y >> 4);
    event[7] = remaining;
}

static void add_standard_init(transfer_script_t *script)
{
    const uint8_t ready_command =
        script->bus_kind == KSHIM_FTM5_BUS_SPI ? UINT8_C(0x87) : UINT8_C(0x86);
    const uint8_t ready_event[KSHIM_FTM5_EVENT_SIZE] = {UINT8_C(0x03)};
    const uint8_t chip_command[] = {
        script->bus_kind == KSHIM_FTM5_BUS_SPI ? UINT8_C(0xfb) : UINT8_C(0xfa),
        UINT8_C(0x20), 0, 0, 0,
    };
    const uint8_t chip_id[] = {UINT8_C(0x36), UINT8_C(0x39)};
    const uint8_t system_command[] = {
        script->bus_kind == KSHIM_FTM5_BUS_SPI ? UINT8_C(0xa7) : UINT8_C(0xa6),
        0, 0,
    };
    const uint8_t scan_command[] = {UINT8_C(0xa0), 0, 1};
    uint8_t system_info[KSHIM_FTM5_SYSTEM_INFO_SIZE];

    make_system_info(system_info);
    script_add_read(script, &ready_command, 1, ready_event,
                    sizeof(ready_event), 0);
    script_add_read(script, chip_command, sizeof(chip_command), chip_id,
                    sizeof(chip_id), 0);
    script_add_read(script, system_command, sizeof(system_command), system_info,
                    sizeof(system_info), 0);
    script_add(script, scan_command, sizeof(scan_command), NULL, 0, 0);
}

static kshim_ftm5_transport_t make_transport(transfer_script_t *script)
{
    return (kshim_ftm5_transport_t){
        .context = script,
        .transfer = scripted_transfer,
        .delay_us = scripted_delay,
        .bus_kind = script->bus_kind,
    };
}

static void expect_event(const event_log_t *log, size_t index,
                         kshim_touch_event_type_t type, uint8_t contact_id,
                         uint32_t x, uint32_t y)
{
    assert(index < log->count);
    assert(log->events[index].type == type);
    assert(log->events[index].contact_id == contact_id);
    assert(log->events[index].x == x);
    assert(log->events[index].y == y);
}

static void assert_script_done(const transfer_script_t *script)
{
    assert(script->next_step == script->step_count);
}

static void test_exact_i2c_initialization(void)
{
    transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
    struct kshim_qcom_bus bus = {
        .io = {.context = &script, .delay_us = scripted_delay},
        .config = {.protocol = KSHIM_GENI_I2C, .speed_hz = 400000},
        .initialized = true,
    };
    kshim_ftm5_t device;
    kshim_touch_t touch;
    event_log_t log;

    add_standard_init(&script);
    init_touch(&touch, &log);
    assert(kshim_ftm5_init_i2c(&device, &bus, &touch) == KSHIM_FTM5_OK);
    assert_script_done(&script);
    assert(script.delay_count == 0U);
    assert(device.initialized);
    assert(device.info.chip_id_low == UINT8_C(0x36));
    assert(device.info.chip_id_high == UINT8_C(0x39));
    assert(device.info.chip_id == UINT16_C(0x3936));
    assert(device.info.firmware_version == UINT16_C(0x1234));
    assert(device.info.config_version == UINT16_C(0x5678));
    assert(device.info.resolution_x == KSHIM_FTM5_NATIVE_WIDTH);
    assert(device.info.resolution_y == KSHIM_FTM5_NATIVE_HEIGHT);
}

static void test_ready_polling_and_timeout(void)
{
    const uint8_t command = UINT8_C(0x86);
    const uint8_t no_event[KSHIM_FTM5_EVENT_SIZE] = {0};
    const uint8_t status_event[KSHIM_FTM5_EVENT_SIZE] = {UINT8_C(0x43)};
    const uint8_t ready_event[KSHIM_FTM5_EVENT_SIZE] = {UINT8_C(0x03)};

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;

        script_add_read(&script, &command, 1, no_event, sizeof(no_event), 0);
        script_add_read(&script, &command, 1, status_event,
                        sizeof(status_event), 0);
        script_add_read(&script, &command, 1, ready_event,
                        sizeof(ready_event), 0);
        {
            transfer_script_t tail = {.bus_kind = KSHIM_FTM5_BUS_I2C};
            add_standard_init(&tail);
            for (size_t index = 1; index < tail.step_count; ++index) {
                const script_step_t *step = &tail.steps[index];
                script_add(&script, step->command, step->command_size,
                           step->response, step->response_size, step->result);
            }
        }
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
        assert_script_done(&script);
        assert(script.delay_count == 2U);
        assert(script.delays[0] == KSHIM_FTM5_READY_DELAY_US);
        assert(script.delays[1] == KSHIM_FTM5_READY_DELAY_US);
    }

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;

        for (unsigned attempt = 0; attempt < KSHIM_FTM5_READY_ATTEMPTS;
             ++attempt)
            script_add_read(&script, &command, 1, no_event,
                            sizeof(no_event), 0);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) ==
               KSHIM_FTM5_ERR_TIMEOUT);
        assert_script_done(&script);
        assert(script.delay_count == KSHIM_FTM5_READY_ATTEMPTS - 1U);
        assert(log.count == 0U);
    }
}

static void add_init_until_failure(transfer_script_t *script,
                                   unsigned failure_stage, int failure)
{
    const uint8_t ready_command = UINT8_C(0x86);
    const uint8_t ready[KSHIM_FTM5_EVENT_SIZE] = {UINT8_C(0x03)};
    const uint8_t chip_command[] = {UINT8_C(0xfa), UINT8_C(0x20), 0, 0, 0};
    const uint8_t chip[] = {UINT8_C(0x36), UINT8_C(0x39)};
    const uint8_t system_command[] = {UINT8_C(0xa6), 0, 0};
    const uint8_t scan_command[] = {UINT8_C(0xa0), 0, 1};
    uint8_t system[KSHIM_FTM5_SYSTEM_INFO_SIZE];

    make_system_info(system);
    script_add_read(script, &ready_command, 1, ready, sizeof(ready),
                    failure_stage == 0U ? failure : 0);
    if (failure_stage == 0U)
        return;
    script_add_read(script, chip_command, sizeof(chip_command), chip,
                    sizeof(chip), failure_stage == 1U ? failure : 0);
    if (failure_stage == 1U)
        return;
    script_add_read(script, system_command, sizeof(system_command), system,
                    sizeof(system), failure_stage == 2U ? failure : 0);
    if (failure_stage == 2U)
        return;
    script_add(script, scan_command, sizeof(scan_command), NULL, 0,
               failure_stage == 3U ? failure : 0);
}

static void test_initialization_bus_failures(void)
{
    const uint8_t stop_command[] = {UINT8_C(0xa0), 0, 0};
    for (unsigned stage = 0; stage < 4U; ++stage) {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;

        add_init_until_failure(&script, stage, KSHIM_BUS_IO);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_BUS_IO);
        assert_script_done(&script);
        assert(script.delay_count == 0U);
        assert(!device.initialized);
        if (stage == 3U) {
            /* Scan-enable may have reached the controller despite the
             * reported error; cleanup must retry an explicit stop. */
            assert(device.scanning_maybe);
            script_add(&script, stop_command, sizeof(stop_command), NULL, 0,
                       KSHIM_BUS_IO);
            script_add(&script, stop_command, sizeof(stop_command), NULL, 0, 0);
            /* The first cleanup attempt is allowed to fail and must retain
             * the uncertain state for a caller retry. */
            assert(kshim_ftm5_quiesce(&device) == KSHIM_BUS_IO);
            assert(device.scanning_maybe);
            assert(kshim_ftm5_quiesce(&device) == KSHIM_FTM5_OK);
            assert(!device.scanning_maybe);
            assert_script_done(&script);
        }
    }
}

static void test_ready_after_queued_status(void)
{
    transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
    kshim_ftm5_transport_t transport = make_transport(&script);
    kshim_ftm5_t device;
    kshim_touch_t touch;
    event_log_t log;
    const uint8_t command = 0x86;
    const uint8_t status[8] = {0x43};

    /* More than three queued records must not be mistaken for reset retries. */
    for (unsigned i = 0; i < 7; ++i)
        script_add_read(&script, &command, 1, status, sizeof(status), 0);
    add_standard_init(&script);
    init_touch(&touch, &log);
    assert(kshim_ftm5_init(&device, &transport, &touch) == 0);
    assert(script.delay_count == 7);
    assert_script_done(&script);
}

static void test_motion_hover_and_reset(void)
{
    transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
    kshim_ftm5_transport_t transport = make_transport(&script);
    kshim_ftm5_t device;
    kshim_touch_t touch;
    event_log_t log;
    const uint8_t command = 0x86;
    const uint8_t ready[8] = {0x03};
    uint8_t event[8];

    add_standard_init(&script);
    make_event(event, 0x23, 1, 120, 230, 0);
    script_add_read(&script, &command, 1, event, sizeof(event), 0);
    event[1] = 0x15; /* Hover: coordinates exist but BTN_TOUCH is zero. */
    script_add_read(&script, &command, 1, event, sizeof(event), 0);
    event[1] = 0x10; /* Invalid type must not create a press. */
    script_add_read(&script, &command, 1, event, sizeof(event), 0);
    event[1] = 0x12; /* Glove motion becomes a new contact, like upstream. */
    script_add_read(&script, &command, 1, event, sizeof(event), 0);
    script_add_read(&script, &command, 1, ready, sizeof(ready), 0);

    init_touch(&touch, &log);
    assert(kshim_ftm5_init(&device, &transport, &touch) == 0);
    assert(kshim_ftm5_poll(&device) == 0);
    expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 1, 120, 230);
    assert(kshim_ftm5_poll(&device) == 0);
    expect_event(&log, 1, KSHIM_TOUCH_EVENT_RELEASE, 1, 120, 230);
    assert(kshim_ftm5_poll(&device) == 0 && log.count == 2);
    assert(kshim_ftm5_poll(&device) == 0);
    expect_event(&log, 2, KSHIM_TOUCH_EVENT_PRESS, 1, 120, 230);
    assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_ERR_PROTOCOL);
    expect_event(&log, 3, KSHIM_TOUCH_EVENT_CANCEL, 1, 120, 230);
    assert(!device.initialized && !touch.primary_active);
    assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_ERR_INVALID);
    assert_script_done(&script);
    const uint8_t stop[] = {0xa0, 0, 0};
    script_add(&script, stop, sizeof(stop), NULL, 0, 0);
    assert(kshim_ftm5_quiesce(&device) == 0);
    assert_script_done(&script);
}

static void test_identity_and_system_info_validation(void)
{
    const uint8_t ready_command = UINT8_C(0x86);
    const uint8_t ready[KSHIM_FTM5_EVENT_SIZE] = {UINT8_C(0x03)};
    const uint8_t chip_command[] = {UINT8_C(0xfa), UINT8_C(0x20), 0, 0, 0};
    const uint8_t chip_command_good[] = {UINT8_C(0x36), UINT8_C(0x39)};
    const uint8_t chip_bad[] = {UINT8_C(0x36), UINT8_C(0x38)};
    const uint8_t system_command[] = {UINT8_C(0xa6), 0, 0};

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;

        script_add_read(&script, &ready_command, 1, ready, sizeof(ready), 0);
        script_add_read(&script, chip_command, sizeof(chip_command), chip_bad,
                        sizeof(chip_bad), 0);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) ==
               KSHIM_FTM5_ERR_NO_DEVICE);
        assert_script_done(&script);
    }

    for (unsigned bad_field = 0; bad_field < 2U; ++bad_field) {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;
        uint8_t system[KSHIM_FTM5_SYSTEM_INFO_SIZE];

        make_system_info(system);
        system[bad_field] = 0;
        script_add_read(&script, &ready_command, 1, ready, sizeof(ready), 0);
        script_add_read(&script, chip_command, sizeof(chip_command),
                        chip_command_good, sizeof(chip_command_good), 0);
        script_add_read(&script, system_command, sizeof(system_command), system,
                        sizeof(system), 0);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) ==
               KSHIM_FTM5_ERR_PROTOCOL);
        assert_script_done(&script);
    }
}

static void test_empty_and_maximum_fifo(void)
{
    transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
    kshim_ftm5_transport_t transport = make_transport(&script);
    kshim_ftm5_t device;
    kshim_touch_t touch;
    event_log_t log;
    const uint8_t fifo_command = UINT8_C(0x86);
    uint8_t empty[KSHIM_FTM5_EVENT_SIZE] = {0};
    uint8_t first[KSHIM_FTM5_EVENT_SIZE] = {UINT8_C(0x43)};
    uint8_t rest[(KSHIM_FTM5_FIFO_DEPTH - 1U) * KSHIM_FTM5_EVENT_SIZE];

    add_standard_init(&script);
    script_add_read(&script, &fifo_command, 1, empty, sizeof(empty), 0);
    first[7] = UINT8_MAX; /* The remaining field is masked and capped at 31. */
    memset(rest, 0, sizeof(rest));
    script_add_read(&script, &fifo_command, 1, first, sizeof(first), 0);
    script_add_read(&script, &fifo_command, 1, rest, sizeof(rest), 0);

    init_touch(&touch, &log);
    assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
    assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
    assert(log.count == 0U);
    assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
    assert(log.count == 0U);
    assert_script_done(&script);
}

static void test_enter_move_leave_decode(void)
{
    transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
    kshim_ftm5_transport_t transport = make_transport(&script);
    kshim_ftm5_t device;
    kshim_touch_t touch;
    event_log_t log;
    const uint8_t command = UINT8_C(0x86);
    uint8_t first[KSHIM_FTM5_EVENT_SIZE];
    uint8_t rest[2U * KSHIM_FTM5_EVENT_SIZE];

    add_standard_init(&script);
    make_event(first, UINT8_C(0x13), 4, 100, 200, 2);
    make_event(&rest[0], UINT8_C(0x23), 4, 321, 654, 1);
    make_event(&rest[KSHIM_FTM5_EVENT_SIZE], UINT8_C(0x33), 4, 0, 0, 0);
    script_add_read(&script, &command, 1, first, sizeof(first), 0);
    script_add_read(&script, &command, 1, rest, sizeof(rest), 0);

    init_touch(&touch, &log);
    assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
    assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
    assert(log.count == 3U);
    expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 4, 100, 200);
    expect_event(&log, 1, KSHIM_TOUCH_EVENT_MOVE, 4, 321, 654);
    expect_event(&log, 2, KSHIM_TOUCH_EVENT_RELEASE, 4, 321, 654);
    assert_script_done(&script);
}

static void test_multitouch_primary_handoff(void)
{
    transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
    kshim_ftm5_transport_t transport = make_transport(&script);
    kshim_ftm5_t device;
    kshim_touch_t touch;
    event_log_t log;
    const uint8_t command = UINT8_C(0x86);
    uint8_t first[KSHIM_FTM5_EVENT_SIZE];
    uint8_t rest[4U * KSHIM_FTM5_EVENT_SIZE];

    add_standard_init(&script);
    make_event(first, UINT8_C(0x13), 2, 10, 20, 4);
    make_event(&rest[0], UINT8_C(0x13), 5, 30, 40, 3);
    make_event(&rest[KSHIM_FTM5_EVENT_SIZE], UINT8_C(0x23), 5, 35, 45, 2);
    make_event(&rest[2U * KSHIM_FTM5_EVENT_SIZE], UINT8_C(0x33), 2, 0, 0, 1);
    make_event(&rest[3U * KSHIM_FTM5_EVENT_SIZE], UINT8_C(0x33), 5, 0, 0, 0);
    script_add_read(&script, &command, 1, first, sizeof(first), 0);
    script_add_read(&script, &command, 1, rest, sizeof(rest), 0);

    init_touch(&touch, &log);
    assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
    assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
    assert(log.count == 4U);
    expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 2, 10, 20);
    expect_event(&log, 1, KSHIM_TOUCH_EVENT_RELEASE, 2, 10, 20);
    expect_event(&log, 2, KSHIM_TOUCH_EVENT_PRESS, 5, 35, 45);
    expect_event(&log, 3, KSHIM_TOUCH_EVENT_RELEASE, 5, 35, 45);
    assert_script_done(&script);
}

static void test_protocol_and_mid_batch_failures(void)
{
    const uint8_t command = UINT8_C(0x86);

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;
        uint8_t first[KSHIM_FTM5_EVENT_SIZE];
        uint8_t invalid[KSHIM_FTM5_EVENT_SIZE];

        add_standard_init(&script);
        make_event(first, UINT8_C(0x13), 1, 11, 22, 1);
        make_event(invalid, UINT8_C(0x23), 10, 30, 40, 0);
        script_add_read(&script, &command, 1, first, sizeof(first), 0);
        script_add_read(&script, &command, 1, invalid, sizeof(invalid), 0);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
        assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_ERR_PROTOCOL);
        assert(log.count == 2U);
        expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 1, 11, 22);
        expect_event(&log, 1, KSHIM_TOUCH_EVENT_CANCEL, 1, 11, 22);
        assert_script_done(&script);
    }

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;
        uint8_t enter[KSHIM_FTM5_EVENT_SIZE];
        uint8_t move[KSHIM_FTM5_EVENT_SIZE];
        uint8_t unread[KSHIM_FTM5_EVENT_SIZE] = {0};
        uint8_t empty[KSHIM_FTM5_EVENT_SIZE] = {0};

        add_standard_init(&script);
        make_event(enter, UINT8_C(0x13), 3, 100, 200, 0);
        make_event(move, UINT8_C(0x23), 3, 120, 220, 1);
        script_add_read(&script, &command, 1, enter, sizeof(enter), 0);
        script_add_read(&script, &command, 1, move, sizeof(move), 0);
        script_add_read(&script, &command, 1, unread, sizeof(unread),
                        KSHIM_BUS_IO);
        script_add_read(&script, &command, 1, empty, sizeof(empty), 0);

        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
        assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
        assert(kshim_ftm5_poll(&device) == KSHIM_BUS_IO);
        assert(log.count == 2U);
        expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 3, 100, 200);
        expect_event(&log, 1, KSHIM_TOUCH_EVENT_CANCEL, 3, 100, 200);
        assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
        assert(log.count == 2U);
        assert_script_done(&script);
    }

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;
        uint8_t enter[KSHIM_FTM5_EVENT_SIZE];
        uint8_t controller_error[KSHIM_FTM5_EVENT_SIZE] = {UINT8_C(0xf3)};

        add_standard_init(&script);
        make_event(enter, UINT8_C(0x13), 7, 70, 80, 0);
        script_add_read(&script, &command, 1, enter, sizeof(enter), 0);
        script_add_read(&script, &command, 1, controller_error,
                        sizeof(controller_error), 0);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
        assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
        assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_ERR_PROTOCOL);
        assert(log.count == 2U);
        expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 7, 70, 80);
        expect_event(&log, 1, KSHIM_TOUCH_EVENT_CANCEL, 7, 70, 80);
        assert_script_done(&script);
    }
}

static void test_spi_opcodes_and_dummy_bytes(void)
{
    transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_SPI};
    struct kshim_qcom_bus bus = {
        .io = {.context = &script, .delay_us = scripted_delay},
        .config = {
            .protocol = KSHIM_GENI_SPI,
            .speed_hz = KSHIM_FTM5_SPI_MAX_HZ,
            .spi_mode = KSHIM_FTM5_SPI_MODE,
        },
        .initialized = true,
    };
    kshim_ftm5_t device;
    kshim_touch_t touch;
    event_log_t log;
    const uint8_t command = UINT8_C(0x87);
    uint8_t enter[KSHIM_FTM5_EVENT_SIZE];

    add_standard_init(&script);
    make_event(enter, UINT8_C(0x13), 6, 444, 777, 0);
    script_add_read(&script, &command, 1, enter, sizeof(enter), 0);
    init_touch(&touch, &log);
    assert(kshim_ftm5_init_spi(&device, &bus, &touch) == KSHIM_FTM5_OK);
    assert(kshim_ftm5_poll(&device) == KSHIM_FTM5_OK);
    expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 6, 444, 777);
    assert_script_done(&script);

    assert(KSHIM_FTM5_SPI_MODE == 0U);
    assert(KSHIM_FTM5_SPI_MAX_HZ == UINT32_C(7000000));
    assert(KSHIM_FTM5_SPI_CS_HOLD_US == 10U);

    bus.config.spi_mode = 1U;
    assert(kshim_ftm5_init_spi(&device, &bus, &touch) ==
           KSHIM_FTM5_ERR_INVALID);
    bus.config.spi_mode = KSHIM_FTM5_SPI_MODE;
    bus.config.speed_hz = KSHIM_FTM5_SPI_MAX_HZ + 1U;
    assert(kshim_ftm5_init_spi(&device, &bus, &touch) ==
           KSHIM_FTM5_ERR_INVALID);
}

static void test_quiesce(void)
{
    const uint8_t stop_command[] = {UINT8_C(0xa0), 0, 0};

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;

        add_standard_init(&script);
        script_add(&script, stop_command, sizeof(stop_command), NULL, 0, 0);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
        assert(kshim_touch_update(&touch, 2, KSHIM_TOUCH_CONTACT_ENTER,
                                  123, 456) == KSHIM_TOUCH_OK);
        assert(kshim_ftm5_quiesce(&device) == KSHIM_FTM5_OK);
        assert(!device.initialized);
        assert(log.count == 2U);
        expect_event(&log, 0, KSHIM_TOUCH_EVENT_PRESS, 2, 123, 456);
        expect_event(&log, 1, KSHIM_TOUCH_EVENT_CANCEL, 2, 123, 456);
        assert_script_done(&script);

        assert(kshim_ftm5_quiesce(&device) == KSHIM_FTM5_OK);
        assert(log.count == 2U);
        assert_script_done(&script);
    }

    {
        transfer_script_t script = {.bus_kind = KSHIM_FTM5_BUS_I2C};
        kshim_ftm5_transport_t transport = make_transport(&script);
        kshim_ftm5_t device;
        kshim_touch_t touch;
        event_log_t log;

        add_standard_init(&script);
        script_add(&script, stop_command, sizeof(stop_command), NULL, 0,
                   KSHIM_BUS_IO);
        script_add(&script, stop_command, sizeof(stop_command), NULL, 0, 0);
        init_touch(&touch, &log);
        assert(kshim_ftm5_init(&device, &transport, &touch) == KSHIM_FTM5_OK);
        assert(kshim_touch_update(&touch, 4, KSHIM_TOUCH_CONTACT_ENTER,
                                  12, 34) == KSHIM_TOUCH_OK);
        assert(kshim_ftm5_quiesce(&device) == KSHIM_BUS_IO);
        assert(device.initialized); /* A retry must still issue scan disable. */
        assert(log.count == 2U);
        expect_event(&log, 1, KSHIM_TOUCH_EVENT_CANCEL, 4, 12, 34);
        assert(kshim_ftm5_quiesce(&device) == KSHIM_FTM5_OK);
        assert(!device.initialized);
        assert(log.count == 2U);
        assert_script_done(&script);
    }
}

int main(void)
{
    test_exact_i2c_initialization();
    test_ready_polling_and_timeout();
    test_ready_after_queued_status();
    test_motion_hover_and_reset();
    test_initialization_bus_failures();
    test_identity_and_system_info_validation();
    test_empty_and_maximum_fifo();
    test_enter_move_leave_decode();
    test_multitouch_primary_handoff();
    test_protocol_and_mid_batch_failures();
    test_spi_opcodes_and_dummy_bytes();
    test_quiesce();
    puts("st_ftm5_test: all tests passed");
    return 0;
}
