/* SPDX-License-Identifier: MIT
 * ST FTM5 protocol subset for Xiaomi Mi Mix 3 5G (andromeda).
 * Provenance: MiCode Xiaomi_Kernel_OpenSource, andromeda-p-oss, ST 5.2.4.
 * Firmware update, flash, calibration, and manufacturing commands are absent.
 */
#include <st_ftm5.h>

#include <string.h>

#define FTM5_FIFO_I2C UINT8_C(0x86)
#define FTM5_FIFO_SPI UINT8_C(0x87)
#define FTM5_HW_READ_I2C UINT8_C(0xfa)
#define FTM5_HW_READ_SPI UINT8_C(0xfb)
#define FTM5_FRAMEBUFFER_I2C UINT8_C(0xa6)
#define FTM5_FRAMEBUFFER_SPI UINT8_C(0xa7)
#define FTM5_SCAN_MODE UINT8_C(0xa0)

#define FTM5_EVENT_NONE UINT8_C(0x00)
#define FTM5_EVENT_READY UINT8_C(0x03)
#define FTM5_EVENT_ERROR UINT8_C(0xf3)
#define FTM5_EVENT_ENTER UINT8_C(0x13)
#define FTM5_EVENT_MOVE UINT8_C(0x23)
#define FTM5_EVENT_LEAVE UINT8_C(0x33)

#define FTM5_CHIP_ID_LOW UINT8_C(0x36)
#define FTM5_CHIP_ID_HIGH UINT8_C(0x39)
#define FTM5_SYSTEM_INFO_SIGNATURE UINT8_C(0xa5)
#define FTM5_SYSTEM_INFO_TYPE UINT8_C(0x01)
#define FTM5_MAX_COMMAND_SIZE 5U
#define FTM5_MAX_RAW_RESPONSE (KSHIM_FTM5_FIFO_DEPTH * KSHIM_FTM5_EVENT_SIZE + 1U)

static uint16_t ftm5_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static int ftm5_fail(kshim_ftm5_t *device, int status)
{
    if (device == NULL)
        return status;
    if (device->touch != NULL)
        kshim_touch_cancel(device->touch);
    return status;
}

static int ftm5_transfer(kshim_ftm5_t *device, const uint8_t *command,
                         size_t command_size, uint8_t *response,
                         size_t response_size)
{
    uint8_t raw_response[FTM5_MAX_RAW_RESPONSE];
    int status;

    if (device == NULL || device->transport.transfer == NULL ||
        command == NULL || command_size == 0U ||
        (response_size != 0U && response == NULL))
        return KSHIM_FTM5_ERR_INVALID;

    if (device->transport.bus_kind == KSHIM_FTM5_BUS_I2C) {
        return device->transport.transfer(device->transport.context,
                                          command, command_size,
                                          response, response_size);
    }

    if (device->transport.bus_kind != KSHIM_FTM5_BUS_SPI)
        return KSHIM_FTM5_ERR_INVALID;

    if (response_size == 0U) {
        return device->transport.transfer(device->transport.context,
                                          command, command_size, NULL, 0);
    }
    if (response_size + 1U > sizeof(raw_response))
        return KSHIM_FTM5_ERR_INVALID;

    status = device->transport.transfer(device->transport.context,
                                        command, command_size, raw_response,
                                        response_size + 1U);
    if (status != 0)
        return status;
    memcpy(response, &raw_response[1], response_size);
    return KSHIM_FTM5_OK;
}

static uint8_t ftm5_fifo_opcode(const kshim_ftm5_t *device)
{
    return device->transport.bus_kind == KSHIM_FTM5_BUS_SPI ?
           FTM5_FIFO_SPI : FTM5_FIFO_I2C;
}

static int ftm5_read_fifo(kshim_ftm5_t *device, uint8_t *events,
                          size_t size)
{
    uint8_t command = ftm5_fifo_opcode(device);

    return ftm5_transfer(device, &command, sizeof(command), events, size);
}

static int ftm5_wait_ready(kshim_ftm5_t *device)
{
    uint8_t event[KSHIM_FTM5_EVENT_SIZE];

    for (unsigned attempt = 0; attempt < KSHIM_FTM5_READY_ATTEMPTS;
         ++attempt) {
        int status = ftm5_read_fifo(device, event, sizeof(event));

        if (status != 0)
            return status;
        if (event[0] == FTM5_EVENT_READY)
            return KSHIM_FTM5_OK;
        if (event[0] == FTM5_EVENT_ERROR)
            return KSHIM_FTM5_ERR_PROTOCOL;
        if (attempt + 1U < KSHIM_FTM5_READY_ATTEMPTS)
            device->transport.delay_us(device->transport.context,
                                       KSHIM_FTM5_READY_DELAY_US);
    }
    return KSHIM_FTM5_ERR_TIMEOUT;
}

static int ftm5_read_chip_id(kshim_ftm5_t *device)
{
    uint8_t command[] = {
        device->transport.bus_kind == KSHIM_FTM5_BUS_SPI ?
            FTM5_HW_READ_SPI : FTM5_HW_READ_I2C,
        UINT8_C(0x20), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    };
    uint8_t chip_id[2];
    int status = ftm5_transfer(device, command, sizeof(command),
                               chip_id, sizeof(chip_id));

    if (status != 0)
        return status;
    if (chip_id[0] != FTM5_CHIP_ID_LOW ||
        chip_id[1] != FTM5_CHIP_ID_HIGH)
        return KSHIM_FTM5_ERR_NO_DEVICE;

    device->info.chip_id_low = chip_id[0];
    device->info.chip_id_high = chip_id[1];
    device->info.chip_id = ftm5_le16(chip_id);
    return KSHIM_FTM5_OK;
}

static int ftm5_read_system_info(kshim_ftm5_t *device)
{
    uint8_t command[] = {
        device->transport.bus_kind == KSHIM_FTM5_BUS_SPI ?
            FTM5_FRAMEBUFFER_SPI : FTM5_FRAMEBUFFER_I2C,
        UINT8_C(0x00), UINT8_C(0x00),
    };
    uint8_t data[KSHIM_FTM5_SYSTEM_INFO_SIZE];
    int status = ftm5_transfer(device, command, sizeof(command),
                               data, sizeof(data));

    if (status != 0)
        return status;
    if (data[0] != FTM5_SYSTEM_INFO_SIGNATURE ||
        data[1] != FTM5_SYSTEM_INFO_TYPE)
        return KSHIM_FTM5_ERR_PROTOCOL;

    device->info.firmware_version = ftm5_le16(&data[16]);
    device->info.config_version = ftm5_le16(&data[20]);
    device->info.resolution_x = ftm5_le16(&data[80]);
    device->info.resolution_y = ftm5_le16(&data[82]);
    return KSHIM_FTM5_OK;
}

static int ftm5_enable_scanning(kshim_ftm5_t *device)
{
    static const uint8_t command[] = {
        FTM5_SCAN_MODE, UINT8_C(0x00), UINT8_C(0x01),
    };

    /* The transport can report an error after the controller accepted the
     * write. Keep this state until a successful disable is acknowledged. */
    device->scanning_maybe = true;
    return ftm5_transfer(device, command, sizeof(command), NULL, 0);
}

static int ftm5_disable_scanning(kshim_ftm5_t *device)
{
    static const uint8_t command[] = {
        FTM5_SCAN_MODE, UINT8_C(0x00), UINT8_C(0x00),
    };

    return ftm5_transfer(device, command, sizeof(command), NULL, 0);
}

int kshim_ftm5_init(kshim_ftm5_t *device,
                    const kshim_ftm5_transport_t *transport,
                    kshim_touch_t *touch)
{
    int status;

    if (device == NULL || transport == NULL || touch == NULL ||
        !touch->initialized || transport->transfer == NULL ||
        transport->delay_us == NULL ||
        (transport->bus_kind != KSHIM_FTM5_BUS_I2C &&
         transport->bus_kind != KSHIM_FTM5_BUS_SPI))
        return KSHIM_FTM5_ERR_INVALID;

    memset(device, 0, sizeof(*device));
    device->transport = *transport;
    device->touch = touch;

    status = ftm5_wait_ready(device);
    if (status != 0)
        return ftm5_fail(device, status);
    status = ftm5_read_chip_id(device);
    if (status != 0)
        return ftm5_fail(device, status);
    status = ftm5_read_system_info(device);
    if (status != 0)
        return ftm5_fail(device, status);
    status = ftm5_enable_scanning(device);
    if (status != 0)
        return ftm5_fail(device, status);

    device->initialized = true;
    return KSHIM_FTM5_OK;
}

static int ftm5_qcom_i2c_transfer(void *context, const uint8_t *command,
                                  size_t command_size, uint8_t *response,
                                  size_t response_size)
{
    struct kshim_qcom_bus *bus = context;
    struct kshim_i2c_msg messages[2];
    uint8_t command_copy[FTM5_MAX_COMMAND_SIZE];
    size_t count = 1U;

    if (bus == NULL || command == NULL || command_size == 0U ||
        command_size > sizeof(command_copy))
        return KSHIM_FTM5_ERR_INVALID;

    memcpy(command_copy, command, command_size);
    messages[0] = (struct kshim_i2c_msg){
        .address = KSHIM_FTM5_I2C_ADDRESS,
        .read = false,
        .data = command_copy,
        .length = command_size,
    };
    if (response_size != 0U) {
        if (response == NULL)
            return KSHIM_FTM5_ERR_INVALID;
        messages[1] = (struct kshim_i2c_msg){
            .address = KSHIM_FTM5_I2C_ADDRESS,
            .read = true,
            .data = response,
            .length = response_size,
        };
        count = 2U;
    }
    return kshim_i2c_transfer(bus, messages, count, 0);
}

static int ftm5_qcom_spi_transfer(void *context, const uint8_t *command,
                                  size_t command_size, uint8_t *response,
                                  size_t response_size)
{
    struct kshim_qcom_bus *bus = context;
    struct kshim_spi_segment segments[2] = {
        {
            .tx = command,
            .rx = NULL,
            .length = command_size,
            .delay_us = response_size == 0U ?
                        KSHIM_FTM5_SPI_CS_HOLD_US : 0U,
        },
        {
            .tx = NULL,
            .rx = response,
            .length = response_size,
            .delay_us = KSHIM_FTM5_SPI_CS_HOLD_US,
        },
    };
    size_t count = response_size == 0U ? 1U : 2U;

    if (bus == NULL || command == NULL || command_size == 0U ||
        (response_size != 0U && response == NULL))
        return KSHIM_FTM5_ERR_INVALID;
    return kshim_spi_transfer(bus, segments, count, 0);
}

static void ftm5_qcom_delay(void *context, uint32_t delay_us)
{
    struct kshim_qcom_bus *bus = context;

    bus->io.delay_us(bus->io.context, delay_us);
}

int kshim_ftm5_init_i2c(kshim_ftm5_t *device,
                        struct kshim_qcom_bus *bus,
                        kshim_touch_t *touch)
{
    kshim_ftm5_transport_t transport = {
        .context = bus,
        .transfer = ftm5_qcom_i2c_transfer,
        .delay_us = ftm5_qcom_delay,
        .bus_kind = KSHIM_FTM5_BUS_I2C,
    };

    if (bus == NULL || !bus->initialized ||
        bus->config.protocol != KSHIM_GENI_I2C ||
        bus->config.speed_hz > KSHIM_FTM5_I2C_MAX_HZ ||
        bus->io.delay_us == NULL)
        return KSHIM_FTM5_ERR_INVALID;
    return kshim_ftm5_init(device, &transport, touch);
}

int kshim_ftm5_init_spi(kshim_ftm5_t *device,
                        struct kshim_qcom_bus *bus,
                        kshim_touch_t *touch)
{
    kshim_ftm5_transport_t transport = {
        .context = bus,
        .transfer = ftm5_qcom_spi_transfer,
        .delay_us = ftm5_qcom_delay,
        .bus_kind = KSHIM_FTM5_BUS_SPI,
    };

    if (bus == NULL || !bus->initialized ||
        bus->config.protocol != KSHIM_GENI_SPI ||
        bus->config.speed_hz > KSHIM_FTM5_SPI_MAX_HZ ||
        bus->config.spi_mode != KSHIM_FTM5_SPI_MODE ||
        bus->io.delay_us == NULL)
        return KSHIM_FTM5_ERR_INVALID;
    return kshim_ftm5_init(device, &transport, touch);
}

static int ftm5_decode_event(kshim_ftm5_t *device, const uint8_t *event)
{
    kshim_touch_contact_action_t action;
    uint8_t contact_id;
    uint32_t x;
    uint32_t y;

    /* A spontaneous reset loses the scan configuration and all contacts. */
    if (event[0] == FTM5_EVENT_ERROR || event[0] == FTM5_EVENT_READY) {
        device->initialized = false;
        return KSHIM_FTM5_ERR_PROTOCOL;
    }

    switch (event[0]) {
    case FTM5_EVENT_ENTER:
        action = KSHIM_TOUCH_CONTACT_ENTER;
        break;
    case FTM5_EVENT_MOVE:
        action = KSHIM_TOUCH_CONTACT_MOVE;
        break;
    case FTM5_EVENT_LEAVE:
        action = KSHIM_TOUCH_CONTACT_LEAVE;
        break;
    default:
        return KSHIM_FTM5_OK;
    }

    contact_id = event[1] >> 4;
    if (contact_id >= KSHIM_FTM5_MAX_CONTACTS)
        return KSHIM_FTM5_ERR_PROTOCOL;

    if (action != KSHIM_TOUCH_CONTACT_LEAVE) {
        /* fts_enter_pointer_event_handler: hover has BTN_TOUCH=0; invalid
         * types are not reported. Finger, glove, stylus and palm are down. */
        uint8_t type = event[1] & UINT8_C(0x0f);
        if (type == 5U)
            action = KSHIM_TOUCH_CONTACT_LEAVE;
        else if (type < 1U || type > 4U)
            return KSHIM_FTM5_OK;
        else if (!device->touch->contacts[contact_id].active)
            action = KSHIM_TOUCH_CONTACT_ENTER;
    }

    x = ((uint32_t)(event[3] & UINT8_C(0x0f)) << 8) | event[2];
    y = ((uint32_t)event[4] << 4) | (event[3] >> 4);
    if (kshim_touch_update(device->touch, contact_id, action, x, y) != 0)
        return KSHIM_FTM5_ERR_PROTOCOL;
    return KSHIM_FTM5_OK;
}

int kshim_ftm5_poll(kshim_ftm5_t *device)
{
    uint8_t events[KSHIM_FTM5_FIFO_DEPTH * KSHIM_FTM5_EVENT_SIZE];
    size_t remaining;
    int status;

    if (device == NULL || !device->initialized || device->touch == NULL)
        return KSHIM_FTM5_ERR_INVALID;

    status = ftm5_read_fifo(device, events, KSHIM_FTM5_EVENT_SIZE);
    if (status != 0)
        return ftm5_fail(device, status);
    if (events[0] == FTM5_EVENT_NONE)
        return KSHIM_FTM5_OK;

    remaining = events[7] & UINT8_C(0x1f);
    if (remaining > KSHIM_FTM5_FIFO_DEPTH - 1U)
        remaining = KSHIM_FTM5_FIFO_DEPTH - 1U;
    if (remaining != 0U) {
        status = ftm5_read_fifo(device, &events[KSHIM_FTM5_EVENT_SIZE],
                                 remaining * KSHIM_FTM5_EVENT_SIZE);
        if (status != 0)
            return ftm5_fail(device, status);
    }

    for (size_t index = 0; index <= remaining; ++index) {
        const uint8_t *event = &events[index * KSHIM_FTM5_EVENT_SIZE];

        if (event[0] == FTM5_EVENT_NONE)
            break;
        status = ftm5_decode_event(device, event);
        if (status != 0)
            return ftm5_fail(device, status);
    }
    return KSHIM_FTM5_OK;
}

int kshim_ftm5_quiesce(kshim_ftm5_t *device)
{
    int status = KSHIM_FTM5_OK;

    if (device == NULL)
        return KSHIM_FTM5_ERR_INVALID;
    if (device->scanning_maybe) {
        status = ftm5_disable_scanning(device);
        if (status == KSHIM_FTM5_OK) {
            device->initialized = false;
            device->scanning_maybe = false;
        }
    }

    if (device->touch != NULL)
        kshim_touch_cancel(device->touch);
    return status;
}
