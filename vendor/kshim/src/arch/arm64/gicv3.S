#include <asm/asm_macro.h>
// Enable GIC cpu interface
ASM_FUNC(arm64_gicv3_enable_gicc)
    mov x0, #0x1
    msr ICC_IGRPEN1_EL1, x0
    isb
    ret

// Disable GIC cpu interface
ASM_FUNC(arm64_gicv3_disable_gicc)
    mov x0, #0
    msr ICC_IGRPEN1_EL1, x0
    isb
    ret

// Set binary point register
// void arm64_set_bpr(uint64_t bpr)
ASM_FUNC(arm64_gicv3_set_bpr)
    msr ICC_BPR1_EL1, x0
    isb
    ret

// Set priority mask register
// void arm64_set_pmr(uint64_t pmr)
ASM_FUNC(arm64_gicv3_set_pmr)
    msr ICC_PMR_EL1, x0
    ret

// Write end of interrupt register
// void arm64_write_eoi(uint64_t interrupt_no)
ASM_FUNC(arm64_gicv3_write_eoir1)
    msr ICC_EOIR1_EL1, x0
    ret

// Read end of interrupt register
// uint64_t arm64_read_eoi(void)
ASM_FUNC(arm64_gicv3_acknowledge_interrupt)
    mrs x0, ICC_IAR1_EL1
    ret