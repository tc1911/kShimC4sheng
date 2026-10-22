#include <asm/asm_macro.h>
#include <asm/arm64_exception.h>
#include <arm64_sysreg.h>

// Read mpidr
// uint64_t arm64_read_mpidr(void)
ASM_FUNC(arm64_read_mpidr)
    mrs x0, mpidr_el1
    ret

// Read ID_AA64MMFR0_EL1
// uint64_t arm64_read_mmfr0(void)
ASM_FUNC(arm64_read_mmfr0_el1)
    mrs x0, id_aa64mmfr0_el1
    ret


// Enable interrupts
// void arm64_enable_interrupts(void)
ASM_FUNC(arm64_enable_interrupts)
    msr   daifclr, #DAIF_WR_ALL_BIT
    isb
    ret

// Disable interrupts
// void arm_disable_interrupts(void)
ASM_FUNC(arm64_disable_interrupts)
    msr   daifset, #DAIF_WR_INT_BIT
    isb
    ret

// void arm64_unset_alignment_check(void)
ASM_FUNC(arm64_unset_alignment_check)
    mrs     x0, sctlr_el1
    and     x0, x0, #~SCTLR_A_BIT  // Clear -> Disable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// void arm64_set_alignment_check(void)
ASM_FUNC(arm64_set_alignment_check)
    mrs     x0, sctlr_el1
    orr     x0, x0, #SCTLR_A_BIT   // Set -> Enable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// void arm64_unset_stack_alignment_check(void)
ASM_FUNC(arm64_unset_stack_alignment_check)
    mrs     x0, sctlr_el1
    and     x0, x0, #~SCTLR_SA_BIT  // Clear -> Disable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// void arm64_set_stack_alignment_check(void)
ASM_FUNC(arm64_set_stack_alignment_check)
    mrs     x0, sctlr_el1
    orr     x0, x0, #SCTLR_SA_BIT // Set -> Enable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// void arm64_set_instruction_cache(void)
ASM_FUNC(arm64_set_instruction_cache)
    mrs     x0, sctlr_el1
    orr     x0, x0, #SCTLR_I_BIT // Set -> Enable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// void arm64_unset_instruction_cache(void)
ASM_FUNC(arm64_unset_instruction_cache)
    mrs     x0, sctlr_el1
    and     x0, x0, #~SCTLR_I_BIT // Clear -> Disable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// void arm64_set_data_cache(void)
ASM_FUNC(arm64_set_data_cache)
    mrs     x0, sctlr_el1
    orr     x0, x0, #SCTLR_C_BIT // Set -> Enable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// void arm64_unset_data_cache(void)
ASM_FUNC(arm64_unset_data_cache)
    mrs     x0, sctlr_el1
    and     x0, x0, #~SCTLR_C_BIT // Clear -> Disable
    msr     sctlr_el1, x0
    dsb     sy
    isb
    ret

// uint64_t arm64_get_tcr(void)
ASM_FUNC(arm64_get_tcr)
    mrs     x0, tcr_el1
    isb
    ret

// void arm64_set_tcr(uint64_t tcr)
ASM_FUNC(arm64_set_tcr)
    msr     tcr_el1, x0
    isb
    ret

// uint64_t arm64_read_mair(void)
ASM_FUNC(arm64_read_mair)
    mrs     x0, mair_el1
    isb
    ret

// void arm64_set_mair(uint64_t mair)
ASM_FUNC(arm64_set_mair)
    msr     mair_el1, x0
    isb
    ret

// void arm64_enable_mmu(void)
ASM_FUNC(arm64_enable_mmu)
    mrs     x0, sctlr_el1 
    orr     x0, x0, #SCTLR_M_BIT
    tlbi    vmalle1
    dsb     nsh
    isb
    msr     sctlr_el1, x0
    isb
    ret


// void arm64_disable_mmu(void)
ASM_FUNC(arm64_disable_mmu)
    mrs     x0, sctlr_el1
    and     x0, x0, #~SCTLR_M_BIT
    msr     sctlr_el1, x0
    tlbi    vmalle1
    dsb     sy
    isb
    ret


/* Set up exception vector table */
// void arm64_install_exception_vector(void)
ASM_FUNC(arm64_install_exception_vector)
    adr x0, ARM64_EXCEPTION_VEC_TABLE
    msr VBAR_EL1, x0
    isb
    ret


/* Save contents
    GP registers
    x0 -- x31   > SP : 32*64/8 = 256 Bytes

    FP/SIMD registers
    V0 -- V31   > SP : 32*128/8 = 512 Bytes

    System registers
    elr_elx     > SP : 64/8 = 8 Bytes
    spsr_elx    > SP : 64/8 = 8 Bytes
    fpsr_elx    > SP : 64/8 = 8 Bytes
    esr_elx     > SP : 64/8 = 8 Bytes
    far_elx     > SP : 64/8 = 8 Bytes

    Align
    Padding     > SP : 8 Bytes 

    Total: 256+512+40+8 = 816 Bytes (0x330)
*/

#define EXCEPTION_CONTEXT_SIZE 0x330
#define EXCEPTION_GP_CONTEXT_SIZE 0x100
#define EXCEPTION_FP_CONTEXT_SIZE 0x200
#define EXCEPTION_SYS_CONTEXT_SIZE 0x30

/* Save Interrupt */
/* 
* ARM64 Exception table supports 128bytes (32 instructions) per entry
* As a common head here, it will jump to continuous handler once reach 32 instructions
*/
.macro ExceptionCommonSaveContext ExceptionType
    /* Pre allocate conext size in stack */
    sub sp, sp, #EXCEPTION_CONTEXT_SIZE
    // Current SP position:
    //  |-------------| <- Context top
    //  | SYS_CONTEXT |
    //  |-------------|
    //  | FP_CONTEXT  |
    //  |-------------|
    //  | GP_CONTEXT  |
    //  |-------------| <- SP is here

    /* Save all GP registers to stack */
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    stp x20, x21, [sp, #0xA0]
    stp x22, x23, [sp, #0xB0]
    stp x24, x25, [sp, #0xC0]
    stp x26, x27, [sp, #0xD0]
    stp x28, x29, [sp, #0xE0]
    // x30 -> Link Register
    // x31 -> SP
    // save lr & sp
    mov x0, sp
    add x0, x0, #EXCEPTION_CONTEXT_SIZE
    stp x30, x0, [sp, #0xF0]  // x0 -> x31 -> stack pointer
    mov sp, x0

    // Current SP position:
    //  |-------------| <- SP is here
    //  | SYS_CONTEXT |
    //  |-------------|
    //  | FP_CONTEXT  |
    //  |-------------|
    //  | GP_CONTEXT  |
    //  |-------------|
    
    // Save sys registers
    // The program is expected to be run on EL1
    // mrs x0, CurrentEL
    mrs x0, elr_el1         // Exception Link register
    mrs x1, spsr_el1        // Saved program status register
    stp x0, x1, [sp, #-EXCEPTION_SYS_CONTEXT_SIZE]! //  Save to stack
    mrs x0, fpsr            // Float point status register
    mrs x1, esr_el1         // Exception syndrome register
    stp x0, x1, [sp, #0x10] //  Save to stack
    mrs x1, far_el1         // Fault address register
    stp x0, xzr, [sp, #0x20]//  Save to stack

    // Current SP position:
    //  |-------------| <- Context top
    //  | SYS_CONTEXT |
    //  |-------------| <- SP is here
    //  | FP_CONTEXT  |
    //  |-------------|
    //  | GP_CONTEXT  |
    //  |-------------|

    // Jump to rest of our handler(no return)
    mov x1, \ExceptionType
    b ExceptioncontinuousSaveContext
.endm

/* ExceptioncontinuousSaveContext
*   Arg1: ExceptionType
*/
ExceptioncontinuousSaveContext:
    /* Save all FP registers to stack */
    stp q0, q1, [sp, #-EXCEPTION_FP_CONTEXT_SIZE]!
    stp q2, q3, [sp, #0x20]
    stp q4, q5, [sp, #0x40]
    stp q6, q7, [sp, #0x60]
    stp q8, q9, [sp, #0x80]
    stp q10, q11, [sp, #0xA0]
    stp q12, q13, [sp, #0xC0]
    stp q14, q15, [sp, #0xE0]
    stp q16, q17, [sp, #0x100]
    stp q18, q19, [sp, #0x120]
    stp q20, q21, [sp, #0x140]
    stp q22, q23, [sp, #0x160]
    stp q24, q25, [sp, #0x180]
    stp q26, q27, [sp, #0x1A0]
    stp q28, q29, [sp, #0x1C0]
    stp q30, q31, [sp, #0x1E0]

    //  |-------------| <- Context top
    //  | SYS_CONTEXT |
    //  |-------------|
    //  | FP_CONTEXT  |
    //  |-------------| <- SP is here
    //  | GP_CONTEXT  |
    //  |-------------|

    // Prepare SP for C
    sub sp, sp, #EXCEPTION_GP_CONTEXT_SIZE
    mov x0, sp

    //  |-------------| <- Context top
    //  | SYS_CONTEXT |
    //  |-------------|
    //  | FP_CONTEXT  |
    //  |-------------|
    //  | GP_CONTEXT  |
    //  |-------------| <- SP
    //  |   C Stuff   |
    //  |-------------|

    // Finished saving, jump to C handler
    // x0 = Context bottom
    // x1 = ExceptionType
    bl arm64_expetion_handler

    // Once C handler finished, restore everything we stored
    // Disable interrupts while restoring context
    // DAIF register will be automatically restored by hardware
    msr daifset, #DAIF_WR_INT_BIT
    isb

    // Current SP position:
    //  |-------------| <- Context top
    //  | SYS_CONTEXT |
    //  |-------------|
    //  | FP_CONTEXT  |
    //  |-------------|
    //  | GP_CONTEXT  |
    //  |-------------| <- SP
    //  |   C Stuff   |
    //  |-------------|
    // Restore GP registers from stack
    ldp x4, x5, [sp, #0x20]
    ldp x6, x7, [sp, #0x30]
    ldp x8, x9, [sp, #0x40]
    ldp x10, x11, [sp, #0x50]
    ldp x12, x13, [sp, #0x60]
    ldp x14, x15, [sp, #0x70]
    ldp x16, x17, [sp, #0x80]
    ldp x18, x19, [sp, #0x90]
    ldp x20, x21, [sp, #0xA0]
    ldp x22, x23, [sp, #0xB0]
    ldp x24, x25, [sp, #0xC0]
    ldp x26, x27, [sp, #0xD0]
    ldp x28, x29, [sp, #0xE0]
    ldr x30, [sp, #0xF0]
    ldp x0, x1, [sp], #EXCEPTION_GP_CONTEXT_SIZE

    // Current SP position:
    //  |-------------| <- Context top
    //  | SYS_CONTEXT |
    //  |-------------|
    //  | FP_CONTEXT  |
    //  |-------------| <- SP
    //  | GP_CONTEXT  |
    //  |-------------|
    // Restore FP registers from stack
    /* SIMD
    ld4 {q0,  q1,  q2,  q3},  [sp, 0x0]
    ld4 {q4,  q5,  q6,  q7},  [sp, 0x40]
    ld4 {q8,  q9,  q10, q11}, [sp, 0x80]
    ld4 {q12, q13, q14, q15}, [sp, 0xC0]
    ld4 {q16, q17, q18, q19}, [sp, 0x100]
    ld4 {q20, q21, q22, q23}, [sp, 0x140]
    ld4 {q24, q25, q26, q27}, [sp, 0x180]
    ld4 {q28, q29, q30, q31}, [sp, 0x1C0]
    */
    ldp q2, q3, [sp, #0x20]
    ldp q4, q5, [sp, #0x40]
    ldp q6, q7, [sp, #0x60]
    ldp q8, q9, [sp, #0x80]
    ldp q10, q11, [sp, #0xA0]
    ldp q12, q13, [sp, #0xC0]
    ldp q14, q15, [sp, #0xE0]
    ldp q16, q17, [sp, #0x100]
    ldp q18, q19, [sp, #0x120]
    ldp q20, q21, [sp, #0x140]
    ldp q22, q23, [sp, #0x160]
    ldp q24, q25, [sp, #0x180]
    ldp q26, q27, [sp, #0x1A0]
    ldp q28, q29, [sp, #0x1C0]
    ldp q30, q31, [sp, #0x1E0]
    ldp q0, q1, [sp], #EXCEPTION_FP_CONTEXT_SIZE
    
    // Restore sys registers from stack
    ldp x2, x3, [sp, #0x10] // Float point status register
    msr fpsr, x2            // Float point status register
    msr esr_el1, x3         // Exception syndrome register
    ldr x2, [sp, #0x20]     // Fault address register
    msr far_el1, x2         // Fault address register
    ldp x2, x3, [sp], #EXCEPTION_SYS_CONTEXT_SIZE
    msr elr_el1, x2         // Exception Link register
    msr spsr_el1, x3        // Saved program status register

    // Restore x2 x3
    sub sp, sp, #EXCEPTION_CONTEXT_SIZE
    ldp x2, x3, [sp, #0x10]
    add sp, sp, #EXCEPTION_CONTEXT_SIZE
    
    // Current SP position:
    //  |-------------| <- Context top  <- SP
    //  | SYS_CONTEXT |
    //  |-------------|
    //  | FP_CONTEXT  |
    //  |-------------|
    //  | GP_CONTEXT  |
    //  |-------------|
    
    // Exception Finished
    eret

/*
 *
 * ARM64 Exception vector table for ELx to ELx with SPx
 *   Here only consider EL1 to EL1 with SP_EL1, other cases are not supported
 *
*/


#define VECTOR_START(name) \
    .section .text.##name##, "ax"; \
    .align 11; \
    .org 0x000; \
    ASM_FUNC(name)

#define VECTOR_ENTRY(offset) \
    .org offset;

#define VECTOR_END(name) \
    .org 0x800; \
    .previous

VECTOR_START(ARM64_EXCEPTION_VEC_TABLE)
    /* Current Exception Level with SP_EL0 */
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SP0_SYNCHRONOUS) // 0x000: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SYNCHRONOUS    // 0x200: Synchronous exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SP0_IRQ)  // 0x080: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_IRQ    // 0x280: IRQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SP0_FIQ)  // 0x100: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_FIQ    // 0x300: FIQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SP0_SERROR)  // 0x180: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SERROR // 0x380: SError exception
    /* Current Exception Level with SP_ELx */
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SPX_SYNCHRONOUS) 
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SYNCHRONOUS    // 0x200: Synchronous exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SPX_IRQ) 
        ExceptionCommonSaveContext    EXCEPTION_TYPE_IRQ    // 0x280: IRQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SPX_FIQ) 
        ExceptionCommonSaveContext    EXCEPTION_TYPE_FIQ    // 0x300: FIQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_CURRENT_EL_SPX_SERROR) 
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SERROR // 0x380: SError exception
    /* Lower EL running aarch64 */
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH64_SYNCHRONOUS)  // 0x400: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SYNCHRONOUS    // 0x200: Synchronous exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH64_IRQ)  // 0x480: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_IRQ    // 0x280: IRQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH64_FIQ)  // 0x500: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_FIQ    // 0x300: FIQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH64_SERROR)  // 0x580: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SERROR // 0x380: SError exception
    /* Lower EL running aarch32 */
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH32_SYNCHRONOUS)  // 0x600: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SYNCHRONOUS    // 0x200: Synchronous exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH32_IRQ)  // 0x680: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_IRQ    // 0x280: IRQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH32_FIQ)  // 0x700: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_FIQ    // 0x300: FIQ exception
    VECTOR_ENTRY(EXCEPTION_OFFSET_LOWER_EL_AARCH32_SERROR)  // 0x780: Reserved
        ExceptionCommonSaveContext    EXCEPTION_TYPE_SERROR // 0x380: SError exception
VECTOR_END(ARM64_EXCEPTION_VEC_TABLE)