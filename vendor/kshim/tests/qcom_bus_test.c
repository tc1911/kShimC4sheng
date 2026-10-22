/* SPDX-License-Identifier: MIT */
#include <qcom_bus.h>
#include <Library/cr_geni_regs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BASE 0x00c80000U
struct model {
    uint32_t regs[0x4000 / 4];
    uint32_t commands[96];
    size_t command_count;
    uint8_t transmitted[4096];
    size_t transmitted_count;
    size_t tx_length, rx_length, tx_sent, rx_received;
    uint64_t time, last_data_done, cs_release_time;
    unsigned protocol, opcode, cancel_count, abort_count;
    uint32_t inject_error;
    bool active, hang, cancel_fail, abort_fail, short_read, bad_fifo;
    bool short_applied, cs;
    uint32_t latched;
};

static uint8_t pattern(size_t index) { return (uint8_t)(index * 17U + 7U); }

static uint32_t read32(void *context, uintptr_t address)
{
    struct model *m = context;
    assert(address >= BASE && address < BASE + 0x4000U && !(address & 3U));
    uint32_t reg = (uint32_t)(address - BASE);
    if (reg == GENI_SE_STATUS)
        return m->active ? 1U : 0;
    if (reg == GENI_SE_TX_FIFO_STATUS)
        return 0;
    if (reg == GENI_SE_RX_FIFO_STATUS) {
        size_t remaining = m->rx_length - m->rx_received;
        size_t chunk = remaining > 16 ? 16 : remaining;
        if (m->bad_fifo)
            return 5; /* Four-entry hardware FIFO cannot contain five words. */
        uint32_t value = (uint32_t)((chunk + 3U) / 4U);
        if (chunk == remaining && chunk) {
            value |= UINT32_C(1) << 31;
            value |= (uint32_t)(chunk % 4U) << 28;
        }
        return value;
    }
    if (reg == GENI_SE_RX_FIFOn) {
        uint32_t word = 0;
        for (unsigned b = 0; b < 4 && m->rx_received < m->rx_length; ++b)
            word |= (uint32_t)pattern(m->rx_received++) << (b * 8U);
        return word;
    }
    if (reg == GENI_SE_M_IRQ_STATUS) {
        if (m->latched)
            return m->latched;
        if (!m->active)
            return 0;
        if (m->opcode == 7 || m->opcode == 8 || m->opcode == 9) {
            m->active = false;
            if (m->opcode == 8) m->cs = true;
            if (m->opcode == 9) { m->cs = false; m->cs_release_time = m->time; }
            return GENI_SE_M_IRQ_CMD_DONE;
        }
        if (m->inject_error) {
            m->latched = m->inject_error;
            m->inject_error = 0;
            return m->latched;
        }
        if (m->hang)
            return 0;
        uint32_t irq = 0;
        if (m->tx_sent < m->tx_length)
            irq |= GENI_SE_M_IRQ_TX_FIFO_WATERMARK;
        if (m->rx_received < m->rx_length) {
            irq |= GENI_SE_M_IRQ_RX_FIFO_WATERMARK;
            if (m->rx_length - m->rx_received <= 16)
                irq |= GENI_SE_M_IRQ_RX_FIFO_LAST;
        }
        if (m->tx_sent == m->tx_length && m->rx_length - m->rx_received <= 16) {
            irq |= GENI_SE_M_IRQ_CMD_DONE;
            m->active = false;
            m->last_data_done = m->time;
        }
        return irq;
    }
    return m->regs[reg / 4U];
}

static void write32(void *context, uintptr_t address, uint32_t value)
{
    struct model *m = context;
    assert(address >= BASE && address < BASE + 0x4000U && !(address & 3U));
    uint32_t reg = (uint32_t)(address - BASE);
    if (reg == GENI_SE_M_IRQ_CLEAR) {
        m->latched &= ~value;
        return;
    }
    if (reg == GENI_SE_M_CMD_CTRL_REG) {
        if (value & GENI_SE_CMD_CTRL_GENI_CMD_CANCEL) {
            ++m->cancel_count;
            if (!m->cancel_fail) {
                m->active = false;
                m->latched |= GENI_SE_M_IRQ_CMD_CANCEL;
            }
        }
        if (value & GENI_SE_CMD_CTRL_GENI_CMD_ABORT) {
            ++m->abort_count;
            if (!m->abort_fail) {
                m->active = false;
                m->latched |= GENI_SE_M_IRQ_CMD_ABORT;
            }
        }
        return;
    }
    if (reg == GENI_SE_TX_FIFOn) {
        assert(m->active);
        for (unsigned b = 0; b < 4 && m->tx_sent < m->tx_length; ++b) {
            assert(m->transmitted_count < sizeof(m->transmitted));
            m->transmitted[m->transmitted_count++] = (uint8_t)(value >> (b * 8U));
            ++m->tx_sent;
        }
        return;
    }
    m->regs[reg / 4U] = value;
    if (reg == GENI_SE_M_CMD0) {
        assert(!m->active && m->command_count < 96);
        m->commands[m->command_count++] = value;
        m->opcode = value >> 27;
        m->tx_length = (m->opcode & 1U) && m->opcode < 4 ?
                       m->regs[I2C_GENI_SE_TX_TRANS_LEN / 4U] : 0;
        m->rx_length = (m->opcode & 2U) && m->opcode < 4 ?
                       m->regs[I2C_GENI_SE_RX_TRANS_LEN / 4U] : 0;
        if (m->protocol == KSHIM_GENI_SPI && m->opcode < 4)
            assert(m->cs && (value & SPI_GENI_M_CMD_PARAM_FRAGMENTATION));
        if (m->short_read && m->rx_length && !m->short_applied) {
            --m->rx_length;
            m->short_applied = true;
        }
        m->tx_sent = m->rx_received = 0;
        m->active = true;
    }
}

static uint64_t now_us(void *context) { return ((struct model *)context)->time; }
static void delay_us(void *context, uint32_t us) { ((struct model *)context)->time += us; }

static struct kshim_bus_io setup(struct model *m, unsigned protocol)
{
    memset(m, 0, sizeof(*m));
    m->protocol = protocol;
    m->regs[GENI_SE_FW_REVISION_RO / 4U] = protocol << 8;
    m->regs[GENI_SE_HW_PARAM_0 / 4U] = (32U << 24) | (4U << 16);
    m->regs[GENI_SE_HW_PARAM_1 / 4U] = (32U << 24) | (4U << 16);
    m->regs[GENI_SE_IRQ_EN / 4U] = 0x55;
    return (struct kshim_bus_io){m, read32, write32, now_us, delay_us};
}

static struct kshim_qcom_bus_config config(unsigned protocol)
{
    return (struct kshim_qcom_bus_config){
        .base = BASE, .size = 0x4000, .protocol = (enum kshim_qcom_protocol)protocol,
        .source_hz = 19200000U, .speed_hz = protocol == KSHIM_GENI_I2C ? 400000 : 1000000,
        .clock_index = 3, .spi_mode = 3, .chip_select = 1
    };
}

static void test_i2c(void)
{
    struct model m;
    struct kshim_bus_io io = setup(&m, KSHIM_GENI_I2C);
    struct kshim_qcom_bus_config cfg = config(KSHIM_GENI_I2C);
    struct kshim_qcom_bus bus = {0};
    assert(kshim_qcom_bus_init(&bus, &cfg, &io) == 0);
    assert(m.regs[GENI_SE_CLK_SEL / 4] == 3);
    assert(m.regs[I2C_GENI_SE_SCL_COUNTERS / 4] == ((5U << 20) | (12U << 10) | 24));
    const size_t lengths[] = {1, 2, 3, 4, 5, 15, 16, 17, 31, 32, 33, 256};
    for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
        uint8_t tx[256], rx[258];
        size_t length = lengths[n];
        for (size_t i = 0; i < length; ++i) tx[i] = (uint8_t)(i ^ 0xa5U);
        memset(rx, 0xee, sizeof(rx));
        struct kshim_i2c_msg messages[] = {
            {.address = 0x49, .data = tx, .length = length},
            {.address = 0x49, .read = true, .data = rx + 1, .length = length}
        };
        size_t commands = m.command_count, sent = m.transmitted_count;
        assert(kshim_i2c_transfer(&bus, messages, 2, 1000) == 0);
        assert(m.command_count == commands + 2);
        assert((m.commands[commands] & 0x7ffffffU) == ((0x49U << 9) | 4U));
        assert((m.commands[commands + 1] & 0x7ffffffU) == (0x49U << 9));
        assert(memcmp(tx, m.transmitted + sent, length) == 0);
        for (size_t i = 0; i < length; ++i) assert(rx[i + 1] == pattern(i));
        assert(rx[0] == 0xee && rx[length + 1] == 0xee);
        assert(!bus.i2c_held);
    }
    assert(kshim_qcom_bus_quiesce(&bus) == 0);
    assert(m.regs[GENI_SE_IRQ_EN / 4] == 0x55 && !bus.initialized);
}

static void test_spi(void)
{
    struct model m;
    struct kshim_bus_io io = setup(&m, KSHIM_GENI_SPI);
    struct kshim_qcom_bus_config cfg = config(KSHIM_GENI_SPI);
    struct kshim_qcom_bus bus = {0};
    uint8_t header[] = {0xa5, 0, 0x12}, data[37], rx[37];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
    assert(kshim_qcom_bus_init(&bus, &cfg, &io) == 0);
    assert(m.regs[SPI_GENI_SE_CPOL / 4] == 4 && m.regs[SPI_GENI_SE_CPHA / 4] == 1);
    assert(m.regs[GENI_SE_SER_M_CLK_CFG / 4] == (20U << 4 | 1));
    struct kshim_spi_segment segments[] = {
        {.tx = header, .length = sizeof(header)},
        {.tx = data, .rx = rx, .length = sizeof(data), .delay_us = 10}
    };
    assert(kshim_spi_transfer(&bus, segments, 2, 1000) == 0);
    assert(m.command_count == 4 && (m.commands[0] >> 27) == 8 &&
           (m.commands[3] >> 27) == 9 && !m.cs);
    assert(m.cs_release_time - m.last_data_done >= 10);
    assert(memcmp(m.transmitted, header, sizeof(header)) == 0);
    assert(memcmp(m.transmitted + sizeof(header), data, sizeof(data)) == 0);
    for (size_t i = 0; i < sizeof(rx); ++i) assert(rx[i] == pattern(i));
    assert(kshim_qcom_bus_quiesce(&bus) == 0);
}

static void test_faults(void)
{
    struct model m;
    struct kshim_bus_io io;
    struct kshim_qcom_bus_config cfg = config(KSHIM_GENI_I2C);
    struct kshim_qcom_bus bus;
    uint8_t data[5];
    struct kshim_i2c_msg message = {.address = 0x49, .read = true, .data = data, .length = sizeof(data)};
    const uint32_t errors[] = {I2C_GENI_M_IRQ_NACK, I2C_GENI_M_IRQ_ARB_LOST,
                              I2C_GENI_M_IRQ_BUS_PROTO, GENI_SE_M_IRQ_TX_FIFO_WR_ERR};
    const int expected[] = {KSHIM_BUS_NACK, KSHIM_BUS_AGAIN, KSHIM_BUS_IO, KSHIM_BUS_IO};
    for (unsigned i = 0; i < 4; ++i) {
        bus = (struct kshim_qcom_bus){0}; io = setup(&m, KSHIM_GENI_I2C);
        assert(kshim_qcom_bus_init(&bus, &cfg, &io) == 0);
        m.inject_error = errors[i];
        assert(kshim_i2c_transfer(&bus, &message, 1, 100) == expected[i]);
        assert(!bus.i2c_held && m.cancel_count == 1);
        assert(kshim_i2c_transfer(&bus, &message, 1, 100) == 0);
    }
    for (unsigned i = 0; i < 2; ++i) {
        bus = (struct kshim_qcom_bus){0}; io = setup(&m, KSHIM_GENI_I2C);
        assert(kshim_qcom_bus_init(&bus, &cfg, &io) == 0);
        m.short_read = i == 0; m.bad_fifo = i == 1;
        assert(kshim_i2c_transfer(&bus, &message, 1, 100) == KSHIM_BUS_IO);
    }
    bus = (struct kshim_qcom_bus){0}; io = setup(&m, KSHIM_GENI_I2C);
    assert(kshim_qcom_bus_init(&bus, &cfg, &io) == 0);
    m.hang = true; m.cancel_fail = true;
    assert(kshim_i2c_transfer(&bus, &message, 1, 50) == KSHIM_BUS_TIMEOUT);
    assert(m.cancel_count == 1 && m.abort_count == 1 && m.time <= 1100 && !bus.poisoned);
    m.hang = false;
    assert(kshim_i2c_transfer(&bus, &message, 1, 50) == 0);
    m.hang = true; m.abort_fail = true;
    assert(kshim_i2c_transfer(&bus, &message, 1, 50) == KSHIM_BUS_TIMEOUT);
    assert(bus.poisoned && kshim_i2c_transfer(&bus, &message, 1, 50) == KSHIM_BUS_IO);
    assert(kshim_qcom_bus_quiesce(&bus) == KSHIM_BUS_TIMEOUT);
}

static void test_reject(void)
{
    struct model m;
    struct kshim_bus_io io;
    struct kshim_qcom_bus_config cfg = config(KSHIM_GENI_I2C);
    struct kshim_qcom_bus bus;
    for (unsigned i = 0; i < 3; ++i) {
        bus = (struct kshim_qcom_bus){0}; io = setup(&m, KSHIM_GENI_I2C);
        if (i == 0) m.regs[GENI_SE_IF_DISABLE_RO / 4] = 1;
        if (i == 1) m.regs[GENI_SE_FW_REVISION_RO / 4] = KSHIM_GENI_SPI << 8;
        if (i == 2) m.regs[GENI_SE_HW_PARAM_0 / 4] = 32U << 24;
        assert(kshim_qcom_bus_init(&bus, &cfg, &io) == KSHIM_BUS_UNSUPPORTED);
        assert(m.regs[GENI_SE_IRQ_EN / 4] == 0x55 && !m.command_count);
    }
    bus = (struct kshim_qcom_bus){0}; io = setup(&m, KSHIM_GENI_I2C);
    assert(kshim_qcom_bus_init(&bus, &cfg, &io) == 0);
    uint8_t byte = 1;
    struct kshim_i2c_msg msgs[] = {{0x49, false, &byte, 1}, {0x80, true, &byte, 1}};
    assert(kshim_i2c_transfer(&bus, msgs, 2, 100) == KSHIM_BUS_INVALID);
    assert(!m.command_count);
}

int main(void)
{
    test_i2c(); test_spi(); test_faults(); test_reject();
    puts("GENI I2C/SPI FIFO, repeated START, CS, and failure tests passed");
    return 0;
}
