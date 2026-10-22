/* SPDX-License-Identifier: MIT */
#include <Library/cr_geni.h>
#if defined(__aarch64__)
static uint32_t native_read(void *context, uintptr_t address)
{
    uint32_t value;
    (void)context;
    __asm__ volatile("dsb sy" ::: "memory");
    value = *(volatile uint32_t *)address;
    __asm__ volatile("dmb sy" ::: "memory");
    return value;
}
static void native_write(void *context, uintptr_t address, uint32_t value)
{
    (void)context;
    __asm__ volatile("dmb sy" ::: "memory");
    *(volatile uint32_t *)address = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static uint64_t native_time(void *context)
{
    uint64_t count, frequency;
    (void)context;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(count));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (!frequency)
        return 0;
    return (count / frequency) * 1000000U +
           ((count % frequency) * 1000000U) / frequency;
}
static void native_delay(void *context, uint32_t us)
{
    uint64_t start = native_time(context);
    while (native_time(context) - start < us)
        __asm__ volatile("yield" ::: "memory");
}
#endif

const struct CrIo *CrIoDefault(void)
{
#if defined(__aarch64__)
    static const struct CrIo io = {
        .read32 = native_read, .write32 = native_write,
        .now_us = native_time, .delay_us = native_delay
    };
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (!frequency)
        return NULL;
    return &io;
#else
    return NULL;
#endif
}

bool CrIoValid(const struct CrIo *io)
{
    return io && io->read32 && io->write32 && io->now_us && io->delay_us;
}
