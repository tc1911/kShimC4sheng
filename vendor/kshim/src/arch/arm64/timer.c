// Arm64 Generic Timer
#include <timer.h>
#include <gicv3.h>
#include <asm/asm_macro.h>
#include <memops.h>
#include "pl011_uart.h"
#include <lib/stdport.h>

typedef struct ARM64_ARCH_TIMER_CONTEXT
{
    /* IRQ ID */
    uint8_t virt_ppi;
    uint8_t hyp_ppi;
    uint8_t nonsec_ppi;
    uint8_t sec_ppi;
    /* Timer frequency */
    uint64_t cntfrq; // Hz
    /* Int period */
    uint64_t period; // micro seconds
} ARM64_ARCH_TIMER_CONTEXT, *PARM64_ARCH_TIMER_CONTEXT;

// Driver context
ARM64_ARCH_TIMER_CONTEXT *timer_context = NULL;

uint64_t delay(uint64_t micro_seconds)
{
    uint64_t target_time = arm64_read_cntpct() + (arm64_read_cntfrq() * (micro_seconds) / 1000000);
    uint64_t start = arm64_read_cntpct();
    while (arm64_read_cntpct() < target_time)
    {
        // TODO
        // Implement vDelay
    }
    return arm64_read_cntpct() - start;
}

// @Return: unit milli seconds
uint64_t get_current_time()
{
    uint64_t current_time = arm64_read_cntpct() * 1000 / arm64_read_cntfrq();
    return current_time;
}

static uint64_t time = 0;
// Timer interrupt handler
void arm64_arch_timer_interrupt_handler(
    PARM64_EXCEPTION_CONTEXT context, // Not used here
    uint64_t exception_type,
    uint16_t int_id)
{
    // Mark interrupt as handled
    gicv3_end_of_interrupt(int_id);

    // Reload timer
    uint64_t period_ticks = (arm64_read_cntfrq() * timer_context->period / 1000000);
    uint64_t target_ticks = arm64_read_cntpct() + period_ticks;
    while ((target_ticks + period_ticks) < arm64_read_cntpct())
    {
        target_ticks += period_ticks; // Missing interrupts ?
    }
    arm64_write_cntpcval(target_ticks);

    printf("Timer interrupt: %ld\n", time++);

    asm volatile("isb sy" ::: "memory"); // Memory barrier
}

// arm64 arch timer interrupt register
void arm64_arch_timer_init(
    // PPI numbers
    uint8_t arch_timer_virt_irq_no,
    uint8_t arch_timer_hyp_irq_no,
    uint8_t arch_timer_nonsec_irq_no,
    uint8_t arch_timer_sec_irq_no)
{
    // Initialize timer context
    timer_context = (PARM64_ARCH_TIMER_CONTEXT)calloc(1, sizeof(ARM64_ARCH_TIMER_CONTEXT));
    timer_context->virt_ppi = arch_timer_virt_irq_no;
    timer_context->hyp_ppi = arch_timer_hyp_irq_no;
    timer_context->nonsec_ppi = arch_timer_nonsec_irq_no;
    timer_context->sec_ppi = arch_timer_sec_irq_no;
    timer_context->cntfrq = arm64_read_cntfrq(); // Get timer frequency
    timer_context->period = 1000 * 1000;         // Default period is 1 sec

    // 1. Disable timer
    uint32_t cntp_ctlr = arm64_read_cntpctl();
    REG32_MSK_SET(&cntp_ctlr, CNT_CTL_IMASK);
    REG32_MSK_CLR(&cntp_ctlr, CNT_CTL_ENABLE);
    arm64_write_cntpctl(cntp_ctlr);

    // // 2. Register timer interrupt handler
    gicv3_register_interrupt(arch_timer_virt_irq_no, arm64_arch_timer_interrupt_handler);
    gicv3_register_interrupt(arch_timer_nonsec_irq_no, arm64_arch_timer_interrupt_handler);
    gicv3_register_interrupt(arch_timer_sec_irq_no, arm64_arch_timer_interrupt_handler);
    gicv3_register_interrupt(arch_timer_hyp_irq_no, arm64_arch_timer_interrupt_handler);

    // 3. Set period, assume 1s here.
    uint64_t target_tick = arm64_read_cntpct() + (arm64_read_cntfrq() * timer_context->period / 1000000);
    arm64_write_cntpcval(target_tick);

    // 4. Re-enable timer
    cntp_ctlr = CNT_CTL_ENABLE;
    arm64_write_cntpctl(cntp_ctlr);
}
