#pragma once

#include <kshim.h>
#include <arm64_exception.h>
/** Macros **/
/* GICD Registers */
#define ARM_GICD_CTLR 0x0
#define ARM_GICD_TYPER 0x4
#define ARM_GICD_IIDR 0x8

#define ARM_GICD_IGROUPR 0x80
#define ARM_GICD_ISENABLER 0x100
#define ARM_GICD_ICENABLER 0x180
#define ARM_GICD_ISPENDR 0x200
#define ARM_GICD_ICPENDR 0x280
#define ARM_GICD_ISACTIVER 0x300
#define ARM_GICD_ICACTIVER 0x380

#define ARM_GICD_IPRIORITYR 0x400

#define ARM_GICD_ITARGETSR 0x800
#define ARM_GICD_ICFGR 0xC00
#define ARM_GICD_IGRPMODR 0xD00
#define ARM_GICD_IROUTER 0x6100

/* GICR Registers */
#define ARM_GICR_TYPER 0x8
#define ARM_GICR_WAKER 0x14
#define ARM_GICR_IGROUPR 0x80
#define ARM_GICR_ISENABLER 0x100
#define ARM_GICR_ICENABLER 0x180
#define ARM_GICR_IPRIORITYR 0x400

// GICD CTLR
#define BIT_ARM_GICD_CTLR_ENABLE_GRP0   BIT(0)
#define BIT_ARM_GICD_CTLR_ENABLE_GRP1NS BIT(1)
#define BIT_ARM_GICD_CTLR_ENABLE_GRP1S       BIT(2)
#define BIT_ARM_GICD_CTLR_ARES          BIT(4)
#define BIT_ARM_GICD_CTLR_ARE_NS          BIT(4)
#define BIT_ARM_GICD_CTLR_DS            BIT(6)

// GICD TYPER
#define BIT_ARM_GICD_TYPER_ITLINE_NUM GENBITS(4, 0)

// GICR TYPER
#define BIT_ARM_GICR_TYPER_PLPIS BIT(0)
#define BIT_ARM_GICR_TYPER_VLPIS BIT(1)
#define BIT_ARM_GICR_TYPER_DIRECTLPI BIT(3)
#define BIT_ARM_GICR_TYPER_LAST BIT(4)
#define BIT_ARM_GICR_TYPER_AFFINITY GENBITS(63, 32)
#define BIT_ARM_GICR_TYPER_AFFINITY_SHT 32
#define ARM_GIC_INTERRUPT_DEFAULT_PRIORITY 0x80

// GICR WAKER
#define BIT_ARM_GICR_WAKER_PROCESSOR_SLEEP BIT(1)
#define BIT_ARM_GICR_WAKER_CHILDREN_ASLEEP BIT(2)

#define ARM_GICR_CTLR_FRAME_SIZE SIZE_K(64)    // 64KB
#define ARM_GICR_SGI_PPI_FRAME_SIZE SIZE_K(64) // 64KB

// MPIDR_EL1
#define BIT_ARM_MPIDR_EL1_AFF0 GENBITS(7, 0)
#define BIT_ARM_MPIDR_EL1_AFF0_SHT 0
#define BIT_ARM_MPIDR_EL1_AFF1 GENBITS(15, 8)
#define BIT_ARM_MPIDR_EL1_AFF1_SHT 8
#define BIT_ARM_MPIDR_EL1_AFF2 GENBITS(23, 16)
#define BIT_ARM_MPIDR_EL1_AFF2_SHT 16
#define BIT_ARM_MPIDR_EL1_AFF3 GENBITS(39, 32)
#define BIT_ARM_MPIDR_EL1_AFF3_SHT 24
/// squash mpidr to 32 bit affinity
#define MPIDR_TO_AFF(mpidr)                                                                        \
    ((mpidr) & ((BIT_ARM_MPIDR_EL1_AFF0) | (BIT_ARM_MPIDR_EL1_AFF1) | (BIT_ARM_MPIDR_EL1_AFF2))) | \
        ((mpidr) & BIT_ARM_MPIDR_EL1_AFF3) >> (BIT_ARM_MPIDR_EL1_AFF3_SHT - BIT_ARM_MPIDR_EL1_AFF2_SHT)

extern uint64_t arm64_read_mpidr(void);                     // Read MPIDR_EL1 register
extern uint64_t arm64_gicv3_acknowledge_interrupt(void);    // GICv3 acknowledge interrupt function
extern void arm64_gicv3_set_pmr(uint64_t pmr);              // Set GIC PMR register
extern void arm64_gicv3_set_bpr(uint64_t bpr);              // Read MPIDR_EL1 register
extern void arm64_gicv3_write_eoir1(uint64_t interrupt_no); // GICv3 end of interrupt function
extern void arm64_gicv3_enable_gicc(void);                  // Enable GIC CPU interface
/** Enums **/
enum INT_TYPE
{
    GIC_IT_SGI,      // Software Generated Interrupt 0-15
    GIC_IT_PPI,      // Private Peripheral Interrupt 16-31
    GIC_IT_SPI,      // Shared Peripheral Interrupt 32-1020
    GIC_IT_LPI,      // Locality Specific Peripheral Interrupt > 8192
    GIC_IT_RESERVED, // Reserved 1020-1023
    GIC_IT_UNK       // Unknown Interrupt Type
};

/** Types **/
typedef void (*GICV3_INTERRUPT_HANDLER)(PARM64_EXCEPTION_CONTEXT context, uint64_t exception_type, uint16_t int_id); // Interrupt handler array
typedef struct arm64_gicv3_context
{
    uint64_t gicd_base;                                // GIC Distributor base address
    uint64_t gicd_size;                                // GIC Distributor size
    uint64_t gicr_base;                                // GIC Redistributor base address
    uint64_t gicr_size;                                // GIC Redistributor size
    uint64_t gicr_stride;                              // GIC Redistributor stride size
    uint64_t gicr_base_this_cpu;                       // GIC Redistributor base address for this CPU
    uint64_t max_interrupt_num;                        // Maximum number of interrupts supported
    GICV3_INTERRUPT_HANDLER *gicv3_interrupt_handlers; // Interrupt handler pool
} ARM64_GIC_V3_CONTEXT, *PARM64_GIC_V3_CONTEXT;

/** Functions **/
void gicv3_init(
    uint64_t gicd_base,
    uint64_t gicr_base,
    uint64_t gicr_stride);

void gicv3_register_interrupt(
    uint16_t interrupt_no,
    GICV3_INTERRUPT_HANDLER handler);

void gicv3_irq_handler(
    ARM64_EXCEPTION_CONTEXT *context,
    uint64_t exception_type);

void gicv3_end_of_interrupt(
    uint64_t interrupt_no);
