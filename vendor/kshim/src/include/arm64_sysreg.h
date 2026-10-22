#pragma once
#include <bitops.h>

// DAIF bit definitions for writing through msr daifclr/sr daifset
#define DAIF_WR_FIQ_BIT     BIT(0)
#define DAIF_WR_IRQ_BIT     BIT(1)
#define DAIF_WR_ABORT_BIT   BIT(2)
#define DAIF_WR_DEBUG_BIT   BIT(3)
#define DAIF_WR_INT_BIT     (DAIF_WR_FIQ_BIT | DAIF_WR_IRQ_BIT)
#define DAIF_WR_ALL_BIT     GENBITS(3, 0)

// ICC IAR bit definition
#define ICC_IAR_INTID       GENBITS(10, 0)
#define ICC_IAR_INTID_CPUID GENBITS(12, 10)

// cntp ctl register
#define CNT_CTL_ENABLE      BIT(0)
#define CNT_CTL_IMASK       BIT(1)
#define CNT_CTL_ISTATUS     BIT(2)


// SCTLR bits
#define SCTLR_M_BIT     BIT(0)
#define SCTLR_A_BIT     BIT(1)
#define SCTLR_C_BIT     BIT(2)
#define SCTLR_SA_BIT    BIT(3)
#define SCTLR_I_BIT     BIT(12)
#define SCTLR_SPAN_BIT  BIT(23)

// CPACR bits
#define CPACR_FPEN_BIT GENBITS(21, 20)