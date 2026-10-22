#include "pl011_uart.h"
#include <bitops.h>
#include <memops.h>
#include <gicv3.h>

typedef struct PL011_UART_CONTEXT
{
    uint64_t mmio_base;
} PL011_UART_CONTEXT, *PPL011_UART_CONTEXT;

PPL011_UART_CONTEXT pl011_uart_context = NULL;

void pl011_isr(
    PARM64_EXCEPTION_CONTEXT context, // Not used here
    uint64_t exception_type,
    uint16_t int_id)
{
    // Mark interrupt as handled
    gicv3_end_of_interrupt(int_id);

    // Read masked interrupt status register
    uint32_t misr = REG32_OUT(pl011_uart_context->mmio_base + 0x40);

    if (misr & BIT(4)) // RX
    {
        // Handle RX interrupt
        do
        {
            char c = REG32_OUT(pl011_uart_context->mmio_base + 0x00) & 0xFF; // Read data from RX FIFO
            pl011_write(pl011_uart_context->mmio_base, &c, 1);               // Echo back the received character
        } while (!(REG32_OUT(pl011_uart_context->mmio_base + 0x18) & (1 << 4)));
        // Clear the interrupt
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, BIT(4));
    }
    else if (misr & BIT(5)) // TX
    {
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, BIT(5));
    }
    else if (misr & BIT(6)) // RT (Receive Timeout)
    {
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, BIT(6));
    }
    else if (misr & BIT(7)) // FEMIS (Frame Error)
    {
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, BIT(7));
    }
    else if (misr & BIT(8)) // PE (Parity Error)
    {
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, BIT(8));
    }
    else if (misr & BIT(9)) // BE (Break Error)
    {
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, BIT(9));
    }
    else if (misr & BIT(10)) // OE (Overrun Error)

    {
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, BIT(10));
    }
    else
    {
        // Handle other interrupts
        // Clear the interrupt
        REG32_MSK_SET(pl011_uart_context->mmio_base + 0x44, misr);
    }
}

void pl011_init(uint64_t mmio_base)
{
    // Alloc driver context
    pl011_uart_context = (PPL011_UART_CONTEXT)malloc(sizeof(PL011_UART_CONTEXT));
    if (pl011_uart_context == NULL)
    {
        return;
    }
    pl011_uart_context->mmio_base = mmio_base;

    // Enable UART
    REG32_IN(mmio_base + 0x30, BIT(9) | BIT(8) | BIT(0));
    REG32_IN(mmio_base + 0x38, 0);
}

void pl011_write(
    uint64_t mmio_base, char *str, int len)
{
    int i = 0;
    while (i < len)
    {
        // Write the character to the UART data register
        *(volatile uint32_t *)(mmio_base) = str[i];
        i++;
    }
}

int pl011_try_read(uint64_t mmio_base, char *character)
{
    if (REG32_OUT(mmio_base + 0x18) & BIT(4))
        return 0;

    *character = REG32_OUT(mmio_base) & 0xff;
    return 1;
}
