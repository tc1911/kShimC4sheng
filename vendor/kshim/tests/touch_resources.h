/* SPDX-License-Identifier: MIT */
#pragma once
#include <qcom_bus.h>
#include <Library/cr_resources.h>
#define kshim_touch_platform CrQupContext
#define kshim_touch_platform_irq_asserted CrQupIrq
#define kshim_touch_platform_reset CrQupReset
#define kshim_touch_platform_quiesce CrQupQuiesce
#define kshim_cmd_db_lookup CrCmdDbLookupDictionary
#define KSHIM_TOUCH_STAGE_NONE CR_QUP_STAGE_NONE
#define KSHIM_TOUCH_STAGE_CMD_DB CR_QUP_STAGE_CMD_DB
#define KSHIM_TOUCH_STAGE_CLOCK CR_QUP_STAGE_CLOCK
#define KSHIM_TOUCH_STAGE_GENI CR_QUP_STAGE_GENI
#define KSHIM_TOUCH_STAGE_POWER CR_QUP_STAGE_POWER
#define KSHIM_TOUCH_STAGE_RESET CR_QUP_STAGE_RESET
#define KSHIM_TOUCH_STAGE_READY CR_QUP_STAGE_READY
static const struct CrQupResources andromeda = {
    .rsc_base = 0x18220000, .rsc_size = 0x10000, .tcs_offset = 0xd00,
    .drv_id = 2, .active_count = 2, .source_hz = 19200000, .speed_hz = 400000,
    .bus_config = {4, 4}, .irq_active_low = true,
    .cmd_db_dictionary = 0x0c3f000c,

    .geni_size = 0x4000, .geni = 0x00c80000U, .vote = 0x00152014U, .rcg = 0x0011e148U,
    .ahb_halt = {0x0011e004U, 0x0011e008U}, .serial_halt = 0x0011e144U,
    .ahb_votes = (UINT32_C(1) << 1) | (UINT32_C(1) << 2), .serial_vote = (UINT32_C(1) << 4),
    .bus_pins = {0x03d37000U, 0x03d38000U},
    .irq_pin = 0x03d7a000U, .reset_pin = 0x03d36000U, .gate_pin = 0x03925000U,
    /* sm8150-pinctrl.dtsi ts_active: both GPIOs use 8 mA and pull-up. */
    .irq_config = (3U << 6) | 3U,
    .reset_config = (UINT32_C(1) << 9) | (3U << 6) | 3U,
    .gate_delay_us = 4000U, .reset_low_us = 10000U,
    .rails = {{"ldoc1", 0, 0}}, .rail_count = 1,
};

static const struct CrQupResources hdk8250 = {
    .rsc_base = 0x18220000, .rsc_size = 0x10000, .tcs_offset = 0xd00,
    .drv_id = 2, .active_count = 2, .source_hz = 19200000, .speed_hz = 400000,
    .bus_config = {4, 4}, .irq_active_low = true,

    .geni_size = 0x4000, .geni = 0x00a94000U, /* QUPv3 wrapper1 SE5; downstream SE13. */
    .vote = 0x00152008U, .rcg = 0x00118600U,
    .ahb_halt = {0x00118004U, 0x00118008U}, .serial_halt = 0x001185fcU,
    .ahb_votes = (1U << 20) | (1U << 21), .serial_vote = 1U << 27,
    .bus_pins = {0x0f524000U, 0x0f525000U}, /* SOUTH GPIO36/37, qup13 mux1. */
    .reset_pin = 0x0f526000U, .irq_pin = 0x0f527000U,
    .irq_config = (3U << 6) | 3U, /* 8 mA, pull-up, GPIO input. */
    .reset_config = (1U << 9) | (3U << 6) | 3U,
    .reset_low_us = 20000U,
    .cmd_db_base = 0x80860000U, .cmd_db_size = 0x20000U,
    /* ST enables AVDD then DVDD. L13's 20 mA load selects HPM above the
     * documented 10 mA threshold; voltage is fixed at 3.008 V by the board DT.
     * Kona describes LDOC1 as VRM at 1.8 V (unlike Andromeda's XOB profile). */
    .rails = {{"ldoa13", 3008, 7}, {"ldoc1", 1800, 0}}, .rail_count = 2,
};


static inline int kshim_andromeda_touch_init(struct CrQupContext *p, const struct CrIo *io)
{ return CrQupInit(p, io, &andromeda); }
struct kshim_hdk8250_touch_platform { struct CrQupContext resources; };
static inline int kshim_hdk8250_touch_init(struct kshim_hdk8250_touch_platform *p, const struct CrIo *io)
{ return p ? CrQupInit(&p->resources, io, &hdk8250) : CR_BUS_INVALID; }
static inline int kshim_hdk8250_touch_irq_asserted(struct kshim_hdk8250_touch_platform *p)
{ return p ? CrQupIrq(&p->resources) : CR_BUS_INVALID; }
static inline int kshim_hdk8250_touch_reset(struct kshim_hdk8250_touch_platform *p)
{ return p ? CrQupReset(&p->resources) : CR_BUS_INVALID; }
static inline int kshim_hdk8250_touch_quiesce(struct kshim_hdk8250_touch_platform *p)
{ return p ? CrQupQuiesce(&p->resources) : CR_BUS_INVALID; }
