/* SPDX-License-Identifier: MIT */
#pragma once

#include <qcom_bus.h>
#include <touch.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KSHIM_FTM5_I2C_ADDRESS UINT16_C(0x49)
#define KSHIM_FTM5_I2C_MAX_HZ UINT32_C(400000)
#define KSHIM_FTM5_SPI_MAX_HZ UINT32_C(7000000)
#define KSHIM_FTM5_SPI_MODE 0U
#define KSHIM_FTM5_SPI_CS_HOLD_US 10U

#define KSHIM_FTM5_NATIVE_WIDTH 1080U
#define KSHIM_FTM5_NATIVE_HEIGHT 2340U
#define KSHIM_FTM5_MAX_CONTACTS KSHIM_TOUCH_MAX_CONTACTS
#define KSHIM_FTM5_EVENT_SIZE 8U
#define KSHIM_FTM5_FIFO_DEPTH 32U
#define KSHIM_FTM5_SYSTEM_INFO_SIZE 208U
/* Vendor ftsTime.h: GENERAL_TIMEOUT=100 ms, TIMEOUT_RESOLUTION=2 ms.
 * Three reset retries in ftsCore.c are not three FIFO reads. */
#define KSHIM_FTM5_READY_DELAY_US UINT32_C(2000)
#define KSHIM_FTM5_READY_ATTEMPTS 51U

enum kshim_ftm5_result {
    KSHIM_FTM5_OK = 0,
    KSHIM_FTM5_ERR_NO_DEVICE = -19,
    KSHIM_FTM5_ERR_INVALID = -22,
    KSHIM_FTM5_ERR_PROTOCOL = -71,
    KSHIM_FTM5_ERR_UNSUPPORTED = -95,
    KSHIM_FTM5_ERR_TIMEOUT = -110,
};

typedef enum kshim_ftm5_bus_kind {
    KSHIM_FTM5_BUS_I2C = 0,
    KSHIM_FTM5_BUS_SPI,
} kshim_ftm5_bus_kind_t;

/* A transfer sends command first, then reads response without an intervening
 * I2C STOP or SPI chip-select deassertion. An SPI implementation must also
 * observe KSHIM_FTM5_SPI_CS_HOLD_US before releasing chip select. response_size
 * includes the raw SPI dummy byte; the controller driver removes it. */
typedef int (*kshim_ftm5_transfer_fn)(void *context,
                                      const uint8_t *command,
                                      size_t command_size,
                                      uint8_t *response,
                                      size_t response_size);
typedef void (*kshim_ftm5_delay_fn)(void *context, uint32_t delay_us);

typedef struct kshim_ftm5_transport {
    void *context;
    kshim_ftm5_transfer_fn transfer;
    kshim_ftm5_delay_fn delay_us;
    kshim_ftm5_bus_kind_t bus_kind;
} kshim_ftm5_transport_t;

typedef struct kshim_ftm5_info {
    uint16_t chip_id;
    uint16_t firmware_version;
    uint16_t config_version;
    uint16_t resolution_x;
    uint16_t resolution_y;
    uint8_t chip_id_low;
    uint8_t chip_id_high;
} kshim_ftm5_info_t;

typedef struct kshim_ftm5 {
    kshim_ftm5_transport_t transport;
    kshim_touch_t *touch;
    kshim_ftm5_info_t info;
    bool initialized;
    /* Set before the scan-enable transfer. A failed write may have reached
     * the controller, so quiesce must still issue a bounded stop command. */
    bool scanning_maybe;
} kshim_ftm5_t;

/* The platform must power the controller and pulse reset before this call. */
int kshim_ftm5_init(kshim_ftm5_t *device,
                    const kshim_ftm5_transport_t *transport,
                    kshim_touch_t *touch);

/* Convenience adapters for an initialized Qualcomm GENI serial engine. */
int kshim_ftm5_init_i2c(kshim_ftm5_t *device,
                        struct kshim_qcom_bus *bus,
                        kshim_touch_t *touch);
int kshim_ftm5_init_spi(kshim_ftm5_t *device,
                        struct kshim_qcom_bus *bus,
                        kshim_touch_t *touch);

/* Drain at most one 32-event hardware FIFO snapshot. */
int kshim_ftm5_poll(kshim_ftm5_t *device);

/* Stop scanning and release the reported touch before platform quiesce. */
int kshim_ftm5_quiesce(kshim_ftm5_t *device);
