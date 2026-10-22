#include <asm/asm_macro.h>
#include <arm64_sysreg.h>

/* PSCI CPU_ON enters with x0 holding the caller supplied context ID. */
ASM_FUNC(kshim_smp_secondary_entry)
    mov x9, x0
    mrs x10, CurrentEL
    cmp x10, #0x4
    b.eq 1f
    cmp x10, #0x8
    b.eq 2f
    b 4f

1:
    mrs x11, sctlr_el1
    orr x11, x11, #SCTLR_SPAN_BIT
    msr sctlr_el1, x11
    mrs x11, cpacr_el1
    orr x11, x11, #CPACR_FPEN_BIT
    msr cpacr_el1, x11
    b 3f

2:
    mrs x11, sctlr_el2
    orr x11, x11, #SCTLR_SPAN_BIT
    msr sctlr_el2, x11
    mrs x11, cptr_el2
    bic x11, x11, #0x400
    msr cptr_el2, x11

3:
    isb
    ldr x11, [x9]
    and x11, x11, #-16
    mov sp, x11
    mov x0, x9
    mov x1, x10
    bl kshim_smp_secondary_main

4:
    wfe
    b 4b

/* Keep the historical symbol for out-of-tree users of the old entry point. */
.global arm64_idle_processor_startup
.type arm64_idle_processor_startup, %function
arm64_idle_processor_startup:
    b kshim_smp_secondary_entry
