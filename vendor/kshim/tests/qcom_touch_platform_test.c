/* SPDX-License-Identifier: MIT */
#include "touch_resources.h"
#include <Library/cr_geni_regs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DICT 0x0c3f000cU
#define DB 0x85f20000U
#define DRV 0x18220000U
#define TCS (DRV + 0xd00U)
#define SE 0x00c80000U
#define GCC 0x00100000U
#define RCG (GCC + 0x1e148U)
#define VOTE (GCC + 0x52014U)
#define GPIO37 0x03925000U
#define GPIO54 0x03d36000U
#define GPIO55 0x03d37000U
#define GPIO56 0x03d38000U
#define GPIO122 0x03d7a000U
#define LDO 0x000400a0U
#undef BIT
#define BIT(n) (UINT32_C(1) << (n))
struct cell { uintptr_t address; uint32_t value; };
struct model {
    struct cell cells[512];
    unsigned count, writes, tcs_writes, triggers, selected;
    uint64_t time, gate_time, vote_time, reset_low_time, reset_high_time;
    bool gate_seen, reset_low_seen, hang, incomplete, rcg_stuck, triggered;
    unsigned major;
};

static uint32_t *cell(struct model *m, uintptr_t address)
{
    assert(!(address & 3U));
    for (unsigned i = 0; i < m->count; ++i)
        if (m->cells[i].address == address)
            return &m->cells[i].value;
    assert(m->count < sizeof(m->cells) / sizeof(m->cells[0]));
    m->cells[m->count].address = address;
    return &m->cells[m->count++].value;
}
static uint32_t control_offset(struct model *m) { return m->major == 2 ? 0x14 : 0x24; }
static uint32_t command_offset(struct model *m) { return m->major == 2 ? 0x30 : 0x34; }
static uint32_t read32(void *context, uintptr_t address)
{
    struct model *m = context;
    if (address == TCS + 4U && m->triggered && !m->hang) {
        uintptr_t base = TCS + m->selected * 0x2a0U;
        *cell(m, TCS + 4U) |= BIT(m->selected);
        *cell(m, base + command_offset(m) + 12U) = BIT(8) | (m->incomplete ? 0 : BIT(16));
    }
    return *cell(m, address);
}
static void write32(void *context, uintptr_t address, uint32_t value)
{
    struct model *m = context;
    ++m->writes;
    if (address >= TCS && address < TCS + 9U * 0x2a0U) {
        assert(address < TCS + 2U * 0x2a0U); /* No sleep/wake/control writes. */
        ++m->tcs_writes;
    }
    if (address == TCS + 8U) {
        *cell(m, TCS + 4U) &= ~value;
        m->triggered = false;
        return;
    }
    if (address == RCG && (value & 1U) && !m->rcg_stuck)
        value &= ~1U;
    for (unsigned slot = 0; slot < 2; ++slot)
        if (address == TCS + slot * 0x2a0U + control_offset(m) &&
            (*cell(m, address) & BIT(24)))
            assert(value & BIT(16)); /* Trigger must clear before AMC mode. */
    *cell(m, address) = value;
    if (address == GPIO122 && (value & 3U))
        assert(m->triggers == 1 && !m->triggered && !m->hang);
    if (address == GPIO37 + 4U && (value & 2U)) {
        m->gate_time = m->time; m->gate_seen = true;
    }
    if (address == GPIO54 + 4U) {
        assert(m->triggers && !m->hang);
        if (value & 2U) {
            assert(m->reset_low_seen && m->time - m->reset_low_time >= 10000U);
            m->reset_high_time = m->time;
        } else {
            m->reset_low_seen = true; m->reset_low_time = m->time;
        }
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        uintptr_t base = TCS + slot * 0x2a0U;
        if (address == base + control_offset(m) && (value & BIT(24))) {
            assert(m->gate_seen && m->time - m->gate_time >= 4000U);
            uint32_t command = command_offset(m);
            assert(*cell(m, base + command + 4U) == LDO + 4U);
            assert(*cell(m, base + command + 8U) == 1);
            assert(*cell(m, base + command) == (8U | BIT(8) | BIT(16)));
            assert(*cell(m, base + control_offset(m) + 8U) == 1);
            assert(!(*cell(m, TCS) & BIT(slot)));
            ++m->triggers; m->triggered = true; m->selected = slot;
            m->vote_time = m->time;
        }
    }
}
static uint64_t now_us(void *context) { return ((struct model *)context)->time; }
static void delay_us(void *context, uint32_t us) { ((struct model *)context)->time += us; }

static struct kshim_bus_io setup(struct model *m, unsigned major)
{
    memset(m, 0, sizeof(*m)); m->major = major;
    *cell(m, DICT) = DB; *cell(m, DICT + 4U) = 0x1000;
    *cell(m, DB) = 1; *cell(m, DB + 4U) = 0x0c0330db;
    *cell(m, DB + 8U) = 4; /* slave 4, header offset 0 */
    *cell(m, DB + 12U) = (1U << 16) | 24U; /* count 1, aux offset 24 */
    *cell(m, DB + 144U) = 0x636f646c; /* "ldoc" */
    *cell(m, DB + 148U) = '1';
    *cell(m, DB + 160U) = LDO;
    *cell(m, DRV) = (major << 16) | (7U << 8);
    *cell(m, DRV + 0xcU) = (16U << 27) | (9U << 12);
    *cell(m, TCS) = 0xffU;
    for (unsigned slot = 0; slot < 2; ++slot) {
        uintptr_t base = TCS + slot * 0x2a0U;
        uint32_t control = control_offset(m), command = command_offset(m);
        *cell(m, base + control + 4U) = 1; /* idle */
        *cell(m, base + control - 4U) = 0x80;
        *cell(m, base + control + 8U) = 0x40;
        *cell(m, base + command) = 0x10055;
        *cell(m, base + command + 4U) = 0x400b0;
        *cell(m, base + command + 8U) = 77;
    }
    *cell(m, VOTE) = BIT(28);
    *cell(m, RCG + 4U) = (1U << 8) | (2U << 12) | 5U;
    *cell(m, GPIO55) = 0x180;
    *cell(m, GPIO56) = 0x181;
    *cell(m, GPIO122) = 0x100;
    *cell(m, GPIO122 + 4U) = 1;
    *cell(m, SE + GENI_SE_FW_REVISION_RO) = KSHIM_GENI_I2C << 8;
    *cell(m, SE + GENI_SE_HW_PARAM_0) = (32U << 24) | (16U << 16);
    *cell(m, SE + GENI_SE_HW_PARAM_1) = (32U << 24) | (16U << 16);
    return (struct kshim_bus_io){m, read32, write32, now_us, delay_us};
}

static void assert_restored(struct model *m)
{
    assert(*cell(m, VOTE) == BIT(28));
    assert(*cell(m, RCG + 4U) == ((1U << 8) | (2U << 12) | 5U));
    assert(*cell(m, GPIO55) == 0x180 && *cell(m, GPIO56) == 0x181);
    assert(*cell(m, GPIO122) == 0x100);
    assert(*cell(m, TCS) == 0xff);
    uintptr_t base = TCS + m->selected * 0x2a0U;
    uint32_t command = command_offset(m), control = control_offset(m);
    assert(*cell(m, base + command) == 0x10055);
    assert(*cell(m, base + command + 4U) == 0x400b0);
    assert(*cell(m, base + command + 8U) == 77);
    assert(*cell(m, base + control - 4U) == 0x80);
    assert(*cell(m, base + control + 8U) == 0x40);
}

static void test_success(void)
{
    for (unsigned major = 2; major <= 3; ++major) {
        struct model m;
        struct kshim_bus_io io = setup(&m, major);
        struct kshim_touch_platform p = {0};
        assert(kshim_andromeda_touch_init(&p, &io) == 0);
        assert(p.stage == KSHIM_TOUCH_STAGE_READY && p.initialized);
        assert(m.triggers == 1 && m.vote_time >= m.gate_time + 4000U);
        assert(m.reset_high_time >= m.reset_low_time + 10000U);
        /* Active GPIO122/54: 8 mA, pull-up; only reset is an output. */
        assert((*cell(&m, GPIO122) & 0x3ffU) == 0xc3U);
        assert((*cell(&m, GPIO54) & 0x3ffU) == 0x2c3U);
        assert(kshim_touch_platform_irq_asserted(&p) == 0);
        *cell(&m, GPIO122 + 4U) = 0;
        assert(kshim_touch_platform_irq_asserted(&p) == 1);
        assert(kshim_touch_platform_quiesce(&p) == 0);
        assert_restored(&m);
        assert(*cell(&m, GPIO37 + 4U) & 2U); /* rail gate stays enabled */
        assert(*cell(&m, GPIO54 + 4U) & 2U); /* reset stays released */
    }
}

static void test_database_and_preflight(void)
{
    for (unsigned fault = 0; fault < 6; ++fault) {
        struct model m;
        struct kshim_bus_io io = setup(&m, 2);
        struct kshim_touch_platform p = {0};
        switch (fault) {
        case 0: *cell(&m, DB + 4U) = 0; break;
        case 1: *cell(&m, DICT) = 0x1000; break;
        case 2: *cell(&m, DB + 12U) = 0xffff0018; break;
        case 3: *cell(&m, DB + 148U) = '2'; break;
        case 4: *cell(&m, TCS + 0x18) = 0; *cell(&m, TCS + 0x2a0 + 0x18) = 0; break;
        case 5: *cell(&m, DB + 164U) = (0xfff0U << 16) | 0x40; break;
        }
        assert(kshim_andromeda_touch_init(&p, &io) != 0);
        assert(!m.writes && !m.gate_seen && !m.reset_low_seen);
    }
}

static void test_dfs_and_failure(void)
{
    struct model m;
    struct kshim_bus_io io = setup(&m, 2);
    struct kshim_touch_platform p = {0};
    *cell(&m, RCG + 0x14U) = 1;
    for (unsigned i = 0; i < 8; ++i) *cell(&m, RCG + 0x1cU + i * 4U) = (2U << 8) | 1;
    *cell(&m, RCG + 0x1cU + 3U * 4U) = 1;
    assert(kshim_andromeda_touch_init(&p, &io) == 0);
    assert(p.i2c.config.clock_index == 3 && !p.rcg_changed);
    assert(kshim_touch_platform_quiesce(&p) == 0);
    assert_restored(&m);
    p = (struct kshim_touch_platform){0}; io = setup(&m, 2);
    *cell(&m, TCS + 0x18) = 0; /* select second active TCS */
    assert(kshim_andromeda_touch_init(&p, &io) == 0 && m.selected == 1);
    assert(kshim_touch_platform_quiesce(&p) == 0); assert_restored(&m);
    p = (struct kshim_touch_platform){0}; io = setup(&m, 2);
    *cell(&m, SE + GENI_SE_IF_DISABLE_RO) = 1;
    assert(kshim_andromeda_touch_init(&p, &io) == KSHIM_BUS_UNSUPPORTED);
    assert(!m.triggers && !m.gate_seen); assert_restored(&m);
    p = (struct kshim_touch_platform){0}; io = setup(&m, 2); m.hang = true;
    assert(kshim_andromeda_touch_init(&p, &io) == KSHIM_BUS_TIMEOUT);
    assert(p.tcs_pending && !m.reset_low_seen);
    assert(kshim_touch_platform_quiesce(&p) == KSHIM_BUS_TIMEOUT);
    m.hang = false; /* eventual ACK is retired without submitting a second vote */
    assert(kshim_touch_platform_quiesce(&p) == 0 && m.triggers == 1);
    assert_restored(&m);
    p = (struct kshim_touch_platform){0}; io = setup(&m, 2); m.incomplete = true;
    assert(kshim_andromeda_touch_init(&p, &io) == KSHIM_BUS_IO);
    assert(!p.tcs_pending && !m.reset_low_seen); assert_restored(&m);
}

int main(void)
{
    test_success(); test_database_and_preflight(); test_dfs_and_failure();
    puts("Andromeda clocks, bounded CmdDB, RPMh ownership, and power sequence tests passed");
    return 0;
}
