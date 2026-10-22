/* SPDX-License-Identifier: MIT */
#include "touch_resources.h"
#include <Library/cr_geni_regs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DB 0x80860000U
#define DRV 0x18220000U
#define TCS (DRV + 0xd00U)
#define SE 0x00a94000U
#define RCG 0x00118600U
#define VOTE 0x00152008U
#define GPIO36 0x0f524000U
#define GPIO37 0x0f525000U
#define RESET 0x0f526000U
#define IRQ 0x0f527000U
#define AVDD 0x000400a0U
#define DVDD 0x000400c0U
#undef BIT
#define BIT(n) (UINT32_C(1) << (n))

struct cell { uintptr_t address; uint32_t value; };
struct model {
    struct cell cells[512];
    unsigned count, writes, triggers, selected, major, fail_at;
    uint64_t time, reset_low_time;
    bool triggered, hang, incomplete, reset_low_seen, reset_high_seen;
};

static uint32_t *cell(struct model *m, uintptr_t address)
{
    assert(!(address & 3U));
    /* The HDK profile must never access an SM8150 GPIO, SE, or dictionary. */
    assert((address >= 0x00100000U && address < 0x00153000U) ||
           (address >= SE && address < SE + 0x4000U) ||
           (address >= DB && address < DB + 0x20000U) ||
           (address >= DRV && address < DRV + 0x10000U) ||
           (address >= GPIO36 && address < GPIO36 + 0x4000U));
    for (unsigned i = 0; i < m->count; ++i)
        if (m->cells[i].address == address)
            return &m->cells[i].value;
    assert(m->count < sizeof(m->cells) / sizeof(m->cells[0]));
    m->cells[m->count].address = address;
    return &m->cells[m->count++].value;
}
static uint32_t control(struct model *m) { return m->major == 2 ? 0x14 : 0x24; }
static uint32_t command(struct model *m) { return m->major == 2 ? 0x30 : 0x34; }
static uint32_t read32(void *context, uintptr_t address)
{
    struct model *m = context;
    if (address == TCS + 4U && m->triggered && !m->hang) {
        uintptr_t base = TCS + m->selected * 0x2a0U;
        *cell(m, TCS + 4U) |= BIT(m->selected);
        *cell(m, base + command(m) + 12U) = BIT(8) | (m->incomplete ? 0 : BIT(16));
    }
    return *cell(m, address);
}
static void write32(void *context, uintptr_t address, uint32_t value)
{
    static const uint32_t rail_addresses[] = {AVDD, AVDD + 8, AVDD + 4, DVDD, DVDD + 4};
    static const uint32_t rail_values[] = {3008, 7, 1, 1800, 1};
    struct model *m = context;
    ++m->writes;
    if (address >= TCS && address < TCS + 9U * 0x2a0U)
        assert(address < TCS + 2U * 0x2a0U); /* Never sleep/wake/control. */
    if (address == TCS + 8U) {
        *cell(m, TCS + 4U) &= ~value;
        m->triggered = false;
        return;
    }
    if (address == RCG && (value & 1U))
        value &= ~1U;
    for (unsigned slot = 0; slot < 2; ++slot)
        if (address == TCS + slot * 0x2a0U + control(m) &&
            (*cell(m, address) & BIT(24)))
            assert(value & BIT(16)); /* Trigger must clear before AMC mode. */
    *cell(m, address) = value;
    if (address == IRQ && (value & 0x3ffU) == 0xc3U)
        assert(m->triggers == 5 && !m->hang && !m->triggered);
    if (address == RESET + 4U) {
        assert(m->triggers == 5 && !m->hang);
        if (value & 2U) {
            assert(m->reset_low_seen && m->time - m->reset_low_time >= 20000U);
            m->reset_high_seen = true;
        } else {
            m->reset_low_seen = true;
            m->reset_low_time = m->time;
        }
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        uintptr_t base = TCS + slot * 0x2a0U;
        if (address != base + control(m) || !(value & BIT(24)))
            continue;
        assert(m->triggers < 5 && !m->triggered);
        assert(*cell(m, base + command(m) + 4U) == rail_addresses[m->triggers]);
        assert(*cell(m, base + command(m) + 8U) == rail_values[m->triggers]);
        assert(*cell(m, base + command(m)) == (8U | BIT(8) | BIT(16)));
        assert(*cell(m, base + control(m) + 8U) == 1);
        assert(!(*cell(m, TCS) & BIT(slot)));
        ++m->triggers;
        m->triggered = true;
        m->selected = slot;
        if (m->triggers == m->fail_at)
            m->hang = true;
    }
}
static uint64_t now_us(void *context) { return ((struct model *)context)->time; }
static void delay_us(void *context, uint32_t us) { ((struct model *)context)->time += us; }

static struct kshim_bus_io setup(struct model *m, unsigned major)
{
    memset(m, 0, sizeof(*m)); m->major = major;
    *cell(m, DB) = 1; *cell(m, DB + 4U) = 0x0c0330db;
    *cell(m, DB + 8U) = 4; /* VRM slave 4, data offset 0. */
    *cell(m, DB + 12U) = (2U << 16) | 48U; /* Two entries, aux offset 48. */
    *cell(m, DB + 144U) = 0x616f646c; /* "ldoa13" */
    *cell(m, DB + 148U) = 0x00003331;
    *cell(m, DB + 160U) = AVDD;
    *cell(m, DB + 168U) = 0x636f646c; /* "ldoc1" */
    *cell(m, DB + 172U) = '1';
    *cell(m, DB + 184U) = DVDD;
    *cell(m, DRV) = (major << 16) | (7U << 8);
    *cell(m, DRV + 0xcU) = (16U << 27) | (9U << 12);
    *cell(m, TCS) = 0xffU;
    for (unsigned slot = 0; slot < 2; ++slot) {
        uintptr_t base = TCS + slot * 0x2a0U;
        *cell(m, base + control(m) + 4U) = 1;
        *cell(m, base + control(m) - 4U) = 0x80;
        *cell(m, base + control(m) + 8U) = 0x40;
        *cell(m, base + command(m)) = 0x10055;
        *cell(m, base + command(m) + 4U) = 0x400b0;
        *cell(m, base + command(m) + 8U) = 77;
    }
    *cell(m, VOTE) = BIT(28);
    *cell(m, RCG + 4U) = (1U << 8) | (2U << 12) | 5U;
    *cell(m, GPIO36) = 0x180;
    *cell(m, GPIO37) = 0x181;
    *cell(m, IRQ) = 0x100;
    *cell(m, IRQ + 4U) = 1;
    *cell(m, SE + GENI_SE_FW_REVISION_RO) = KSHIM_GENI_I2C << 8;
    *cell(m, SE + GENI_SE_HW_PARAM_0) = (32U << 24) | (16U << 16);
    *cell(m, SE + GENI_SE_HW_PARAM_1) = (32U << 24) | (16U << 16);
    return (struct kshim_bus_io){m, read32, write32, now_us, delay_us};
}

static void assert_restored(struct model *m)
{
    assert(*cell(m, VOTE) == BIT(28));
    assert(*cell(m, RCG + 4U) == ((1U << 8) | (2U << 12) | 5U));
    assert(*cell(m, GPIO36) == 0x180 && *cell(m, GPIO37) == 0x181);
    assert(*cell(m, IRQ) == 0x100 && *cell(m, TCS) == 0xff);
    uintptr_t base = TCS + m->selected * 0x2a0U;
    assert(*cell(m, base + command(m)) == 0x10055);
    assert(*cell(m, base + command(m) + 4U) == 0x400b0);
    assert(*cell(m, base + command(m) + 8U) == 77);
    assert(*cell(m, base + control(m) - 4U) == 0x80);
    assert(*cell(m, base + control(m) + 8U) == 0x40);
}

static void test_success(void)
{
    for (unsigned major = 2; major <= 3; ++major) {
        struct model m;
        struct kshim_bus_io io = setup(&m, major);
        struct kshim_hdk8250_touch_platform p = {0};
        assert(kshim_hdk8250_touch_quiesce(&p) == 0);
        assert(kshim_hdk8250_touch_init(&p, &io) == 0);
        assert(p.resources.stage == KSHIM_TOUCH_STAGE_READY && p.resources.initialized);
        assert(p.resources.i2c.config.base == SE && p.resources.i2c.config.speed_hz == 400000U);
        assert(m.triggers == 5 && m.reset_high_seen);
        assert(*cell(&m, GPIO36) == 4 && *cell(&m, GPIO37) == 4);
        assert(*cell(&m, IRQ) == 0xc3 && *cell(&m, RESET) == 0x2c3);
        assert(kshim_hdk8250_touch_irq_asserted(&p) == 0);
        *cell(&m, IRQ + 4U) = 0;
        assert(kshim_hdk8250_touch_irq_asserted(&p) == 1);
        assert(kshim_hdk8250_touch_reset(&p) == 0);
        assert(kshim_hdk8250_touch_quiesce(&p) == 0);
        assert_restored(&m);
        assert(*cell(&m, RESET + 4U) & 2U);
    }
}

static void test_preflight(void)
{
    for (unsigned fault = 0; fault < 5; ++fault) {
        struct model m;
        struct kshim_bus_io io = setup(&m, 2);
        struct kshim_hdk8250_touch_platform p = {0};
        switch (fault) {
        case 0: *cell(&m, DB + 4U) = 0; break;
        case 1: *cell(&m, DB + 148U) = 'x'; break;
        case 2: *cell(&m, DB + 172U) = 'x'; break;
        case 3: *cell(&m, TCS + 0x18) = 0; *cell(&m, TCS + 0x2a0 + 0x18) = 0; break;
        case 4: *cell(&m, DB + 184U) = 0xfff4fffcU; break;
        }
        assert(kshim_hdk8250_touch_init(&p, &io) != 0);
        assert(!m.writes && !m.reset_low_seen);
    }
    struct model m;
    struct kshim_bus_io io = setup(&m, 2);
    struct kshim_hdk8250_touch_platform p = {0};
    *cell(&m, SE + GENI_SE_IF_DISABLE_RO) = 1;
    assert(kshim_hdk8250_touch_init(&p, &io) == KSHIM_BUS_UNSUPPORTED);
    assert(!m.triggers && !m.reset_low_seen);
    assert_restored(&m);
}

static void test_ack_failure(void)
{
    for (unsigned vote = 1; vote <= 5; ++vote) {
        struct model m;
        struct kshim_bus_io io = setup(&m, 2);
        struct kshim_hdk8250_touch_platform p = {0};
        m.fail_at = vote;
        assert(kshim_hdk8250_touch_init(&p, &io) == KSHIM_BUS_TIMEOUT);
        assert(m.triggers == vote && p.resources.tcs_pending && !m.reset_low_seen);
        assert(kshim_hdk8250_touch_quiesce(&p) == KSHIM_BUS_TIMEOUT);
        m.hang = false;
        assert(kshim_hdk8250_touch_quiesce(&p) == 0 && m.triggers == vote);
        assert_restored(&m);
    }
    struct model m;
    struct kshim_bus_io io = setup(&m, 2);
    struct kshim_hdk8250_touch_platform p = {0};
    m.incomplete = true;
    assert(kshim_hdk8250_touch_init(&p, &io) == KSHIM_BUS_IO);
    assert(!p.resources.tcs_pending && !m.reset_low_seen && m.triggers == 1);
    assert_restored(&m);
}

static void test_dfs_and_ownership(void)
{
    struct model m;
    struct kshim_bus_io io = setup(&m, 2);
    struct kshim_hdk8250_touch_platform p = {0};
    *cell(&m, TCS + 0x18) = 0;
    assert(kshim_hdk8250_touch_init(&p, &io) == 0 && m.selected == 1);
    assert(kshim_hdk8250_touch_quiesce(&p) == 0);
    assert_restored(&m);
    p = (struct kshim_hdk8250_touch_platform){0}; io = setup(&m, 2);
    *cell(&m, RCG + 0x14) = 1;
    for (unsigned i = 0; i < 8; ++i)
        *cell(&m, RCG + 0x1cU + i * 4U) = (2U << 8) | 1;
    *cell(&m, RCG + 0x1cU + 5U * 4U) = 1;
    assert(kshim_hdk8250_touch_init(&p, &io) == 0);
    assert(p.resources.i2c.config.clock_index == 5 && !p.resources.rcg_changed);
    assert(kshim_hdk8250_touch_quiesce(&p) == 0);
    assert_restored(&m);
    p = (struct kshim_hdk8250_touch_platform){0}; io = setup(&m, 2);
    *cell(&m, RCG + 0x14) = 1;
    for (unsigned i = 0; i < 8; ++i)
        *cell(&m, RCG + 0x1cU + i * 4U) = (2U << 8) | 1;
    assert(kshim_hdk8250_touch_init(&p, &io) == KSHIM_BUS_UNSUPPORTED);
    assert(!m.triggers && !m.reset_low_seen);
    assert_restored(&m);
    p = (struct kshim_hdk8250_touch_platform){0}; io = setup(&m, 2);
    *cell(&m, SE + 0x40) = 1; /* Never retune or cancel somebody else's active SE. */
    assert(kshim_hdk8250_touch_init(&p, &io) == KSHIM_BUS_BUSY);
    assert(!m.triggers && !m.reset_low_seen);
    assert_restored(&m);
}

int main(void)
{
    test_success(); test_preflight(); test_ack_failure(); test_dfs_and_ownership();
    puts("HDK8250 independent pins/clocks, two VRM rails, ACK ordering and cleanup passed");
    return 0;
}
