#include <asm/arm64_exception.h>
#include <arm64_exception.h>
#include <arm64_sysreg.h>
#include <lib/stdport.h>
#include <gicv3.h>
#include <bitops.h>
#include <memops.h>
#include <error_no.h>

/*
 * Acknowledgements:
 *   mu_silicon_arm_tiano/ArmPkg/Drivers/ArmGic/GicV3/ArmGicV3Dxe.c
 *   HI0069H_b_gic_architecture_specification.pdf
 *   linux/drivers/irqchip/gic-v3.c
 */

PARM64_GIC_V3_CONTEXT gicv3_driver_context = NULL;

/**
 * @brief   Get the maximum number of interrupts supported by the GICv3
 *
 * @param gicd_base
 * @return uint16_t max interrupt number
 */
uint16_t gicv3_get_max_interrupts(
    uint64_t gicd_base)
{
    // Get the number of interrupts
    uint16_t interrupt_num = ((REG32_OUT(gicd_base + ARM_GICD_TYPER) & BIT_ARM_GICD_TYPER_ITLINE_NUM) + 1) * 32;
    return interrupt_num > 1020 ? 1020 : interrupt_num;
}

/**
 * @brief   Get the type of interrupt
 *
 * @param gicd_base
 * @param interrupt_num
 * @return enum INT_TYPE
 */
enum INT_TYPE gicv3_get_interrupt_type(
    uint16_t interrupt_num)
{
    if (interrupt_num < 16)
    {
        return GIC_IT_SGI;
    }
    else if (interrupt_num < 32)
    {
        return GIC_IT_PPI;
    }
    else if (interrupt_num < 1020)
    {
        return GIC_IT_SPI;
    }
    else if (interrupt_num < 1024)
    {
        return GIC_IT_RESERVED;
    }
    else if (interrupt_num > 8192)
    {
        return GIC_IT_LPI;
    }
    else
    {
        return GIC_IT_UNK;
    }
}

uint64_t gicv3_find_gicr_addr(
    uint64_t gicr_base,
    uint64_t gicr_stride)
{
    uint64_t gicr_base_this_cpu = gicr_base;
    // Get mpidr
    uint64_t cpu_aff = arm64_read_mpidr();
    cpu_aff = MPIDR_TO_AFF(cpu_aff);

    if (gicr_stride == 0)
    {
        gicr_stride = SIZE_K(64) * 2; // Skip CTLR Frame size and SGI/PPI Frame size
    }

    // Go through all gicr and find base address for this cpu
    uint64_t gicr_type_register = REG64_OUT(gicr_base_this_cpu + ARM_GICR_TYPER);

    do
    {
        // Compare affinity
        if (((gicr_type_register & BIT_ARM_GICR_TYPER_AFFINITY) >> BIT_ARM_GICR_TYPER_AFFINITY_SHT) == cpu_aff)
        {
            // Found the gicr base address for this cpu
            return gicr_base_this_cpu;
        }
        else
        {
            // Not found, go to next gicr
            gicr_base_this_cpu += gicr_stride;
            gicr_type_register = REG64_OUT(gicr_base_this_cpu + ARM_GICR_TYPER);
        }
    } while (gicr_type_register & BIT_ARM_GICR_TYPER_LAST);
    // Not found, return 0
    printf("GICR base address not found for this cpu\n");
    return 0;
}

void gicv3_init(
    uint64_t gicd_base,
    uint64_t gicr_base,
    uint64_t gicr_stride)
{
    // Initialize driver context
    gicv3_driver_context = (ARM64_GIC_V3_CONTEXT *)calloc(1, sizeof(ARM64_GIC_V3_CONTEXT));
    gicv3_driver_context->gicd_base = gicd_base;
    gicv3_driver_context->gicr_base = gicr_base;
    gicv3_driver_context->gicr_stride = gicr_stride;

    // Reset GICD
    /// 1. Ensure ARES is set in GICD_CTLR
    REG32_MSK_SET(gicd_base + ARM_GICD_CTLR, BIT_ARM_GICD_CTLR_ARES);

    /// 2. Clear all interrupts
    gicv3_driver_context->max_interrupt_num = gicv3_get_max_interrupts(gicd_base);
    printf("max_interrupt_num: %lx\n", gicv3_driver_context->max_interrupt_num);
    //// Clear SPI
    for (int i = 32; i < gicv3_driver_context->max_interrupt_num; i++)
    {
        // Disable interrupt source
        REG32_IN(gicd_base + ARM_GICD_ICENABLER + (i / 32) * 4, BIT(i % 32));

        // Clear priority
        REG32_MSK_CLR_SET(gicd_base + ARM_GICD_IPRIORITYR + (i / 4) * 4, (0xff << (i % 4) * 8),
                          (ARM_GIC_INTERRUPT_DEFAULT_PRIORITY << (i % 4) * 8));
    }

    //// Clear SGI & PPI
    gicv3_driver_context->gicr_base_this_cpu = gicv3_find_gicr_addr(gicr_base, gicr_stride);
    ///// Ensure GICR_WAKER is set to 0
    REG32_MSK_CLR(gicv3_driver_context->gicr_base_this_cpu + ARM_GICR_WAKER, BIT_ARM_GICR_WAKER_PROCESSOR_SLEEP);
    while (REG32_OUT(gicv3_driver_context->gicr_base_this_cpu + ARM_GICR_WAKER) & BIT_ARM_GICR_WAKER_CHILDREN_ASLEEP)
    {
        // Wait for children to wake up
    };
    /// Do gicr reset if gicr base address is found
    if (gicv3_driver_context->gicr_base_this_cpu)
    {
        for (int i = 0; i < 32; i++)
        {
            // Disable interrupt for this cpu
            REG32_MSK_SET(gicv3_driver_context->gicr_base_this_cpu + ARM_GICR_ICENABLER + ARM_GICR_CTLR_FRAME_SIZE + ((i / 32) * 4), BIT(i % 32));

            // Clear priority
            REG32_MSK_CLR_SET(gicv3_driver_context->gicr_base_this_cpu + ARM_GICR_IPRIORITYR + ARM_GICR_CTLR_FRAME_SIZE + (i / 4) * 4, (0xff << (i % 4) * 8),
                              (ARM_GIC_INTERRUPT_DEFAULT_PRIORITY << (i % 4) * 8));
        }
    }

    /// 3. Reconfigure GICD/GICR
    //// Check DS bit in GICD_CTLR
    if (REG32_OUT(gicd_base + ARM_GICD_CTLR) & BIT_ARM_GICD_CTLR_DS)
    {
        REG32_IN(gicv3_driver_context->gicr_base_this_cpu + ARM_GICR_CTLR_FRAME_SIZE + ARM_GICR_IGROUPR, 0xFFFFFFFF);

        for (int i = 32; i < gicv3_driver_context->max_interrupt_num; i += 32)
        {
            REG32_IN(gicd_base + ARM_GICD_IGROUPR + i / 8, 0xFFFFFFFF);
        }
    }

    //// Route all SPI interrupts to this cpu
    uint64_t cpu_iroute_target = arm64_read_mpidr() & // Only needs AFF
                                 (BIT_ARM_MPIDR_EL1_AFF0 | BIT_ARM_MPIDR_EL1_AFF1 | BIT_ARM_MPIDR_EL1_AFF2 | BIT_ARM_MPIDR_EL1_AFF3);

    for (int i = 0; i < (gicv3_driver_context->max_interrupt_num - 32); i++)
    {
        REG64_IN(gicd_base + ARM_GICD_IROUTER + (i * 8), cpu_iroute_target);
    }

    /// 4. Configure GICC
    //// Disable interrupt preempt (binary point register)
    arm64_gicv3_set_bpr(0x7);
    //// Allow all interrupts (priority mask register)
    arm64_gicv3_set_pmr(0xFF);
    //// Enable GICC ()
    arm64_gicv3_enable_gicc();

    /// 5. Enable GICD
    REG32_MSK_SET(gicd_base + ARM_GICD_CTLR, BIT_ARM_GICD_CTLR_ENABLE_GRP1NS | BIT_ARM_GICD_CTLR_ENABLE_GRP0);

    // Allocate interrupt handler pool
    gicv3_driver_context->gicv3_interrupt_handlers = (GICV3_INTERRUPT_HANDLER *)calloc(gicv3_driver_context->max_interrupt_num, sizeof(GICV3_INTERRUPT_HANDLER));

    // Install Vector table
    arm64_install_exception_vector();

    // Enable interrupt(DAIF)
    arm64_enable_interrupts();
}

// Enable interrupt
void gicv3_enable_interrupt(
    uint16_t interrupt_no)
{
    // Enable the interrupt
    // SPI
    if (gicv3_get_interrupt_type(interrupt_no) == GIC_IT_SPI)
    {
        REG32_IN(gicv3_driver_context->gicd_base + ARM_GICD_ISENABLER + (interrupt_no / 32) * 4, BIT(interrupt_no % 32));
    }
    // SGI or PPI
    else
    {
        REG32_MSK_SET(gicv3_driver_context->gicr_base_this_cpu + ARM_GICR_ISENABLER + ARM_GICR_CTLR_FRAME_SIZE + (interrupt_no / 32) * 4, BIT(interrupt_no % 32));
    }
}

// Disable interrupt
void gicv3_disable_interrupt(
    uint16_t interrupt_no)
{
    // Disable the interrupt
    // SPI
    if (gicv3_get_interrupt_type(interrupt_no) == GIC_IT_SPI)
    {
        REG32_IN(gicv3_driver_context->gicd_base + ARM_GICD_ICENABLER + (interrupt_no / 32) * 4, BIT(interrupt_no % 32));
    }
    // SGI or PPI
    else
    {
        REG32_IN(gicv3_driver_context->gicr_base_this_cpu + ARM_GICR_ICENABLER + ARM_GICR_CTLR_FRAME_SIZE + (interrupt_no / 32) * 4, BIT(interrupt_no % 32));
    }
}

// Register interrupt
void gicv3_register_interrupt(
    uint16_t interrupt_no,
    GICV3_INTERRUPT_HANDLER handler)
{
    // Check if the interrupt number is valid
    if (interrupt_no >= gicv3_driver_context->max_interrupt_num)
    {
        printf("%s: Invalid interrupt number %d\n", __func__, interrupt_no);
        return;
    }

    // Register the handler
    gicv3_driver_context->gicv3_interrupt_handlers[interrupt_no] = handler;

    // Enable the interrupt
    gicv3_enable_interrupt(interrupt_no);
}

// Mark interrupt is finished
void gicv3_end_of_interrupt(
    uint64_t interrupt_no)
{
    // End of interrupt
    arm64_gicv3_write_eoir1(interrupt_no);
}

// Get IRQ number and call its handler
void gicv3_irq_handler(
    ARM64_EXCEPTION_CONTEXT *context,
    uint64_t exception_type)
{
    // Acknowledge the interrupt
    uint64_t irq_number = arm64_gicv3_acknowledge_interrupt();
    if ((irq_number & ICC_IAR_INTID) > gicv3_driver_context->max_interrupt_num)
    {
        printf("%s: invalid interrupt %ld\n", __func__, irq_number);
        return; // Invalid interrupt number
    }

    // Validate the interrupt number
    if (gicv3_driver_context->gicv3_interrupt_handlers[irq_number] == NULL)
    {
        printf("%s: No handler registered for interrupt %ld\n", __func__, irq_number);
        gicv3_end_of_interrupt(irq_number);
        return; // No handler registered for this interrupt
    }

    // Call the interrupt handler
    gicv3_driver_context->gicv3_interrupt_handlers[irq_number](context, exception_type, irq_number);

    // End of interrupt, call in isr to finish irq as soon as possible
    // gicv3_end_of_interrupt(irq_number);
}
