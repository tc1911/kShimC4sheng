#include <asm/asm_macro.h>

ASM_FUNC(arm64_read_cntfrq)
    mrs x0, cntfrq_el0 
    ret

/* Physical timer */
ASM_FUNC(arm64_read_cntpct)
    mrs x0, cntpct_el0
    ret

ASM_FUNC(arm64_read_cntpctl)
    mrs x0,  cntp_ctl_el0
    ret
    
ASM_FUNC(arm64_write_cntpctl)
    msr cntp_ctl_el0, x0
    ret

ASM_FUNC(arm64_read_cntpcval)
    mrs x0, cntp_cval_el0
    ret

ASM_FUNC(arm64_write_cntpcval)
    msr cntp_cval_el0, x0
    ret

ASM_FUNC(arm64_read_cntptval)
    mrs x0, cntp_tval_el0
    ret

ASM_FUNC(arm64_write_cntptval)
    msr cntp_tval_el0, x0
    ret

