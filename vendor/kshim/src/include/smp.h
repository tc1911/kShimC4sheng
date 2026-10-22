#pragma once

#include <stddef.h>
#include <stdint.h>

#define KSHIM_SMP_MAX_CPUS 8U
#define KSHIM_SMP_DEFAULT_TIMEOUT_US UINT64_C(1000000)

typedef uint64_t kshim_smp_ticket_t;
typedef int (*kshim_smp_callback_t)(void *context);

enum {
    KSHIM_SMP_OK = 0,
    KSHIM_SMP_INVALID = -1,
    KSHIM_SMP_NOT_READY = -2,
    KSHIM_SMP_BUSY = -3,
    KSHIM_SMP_TIMEOUT = -4,
    KSHIM_SMP_PSCI_ERROR = -5,
    KSHIM_SMP_CPU_ERROR = -6,
};

int kshim_smp_init(const void *fdt);
int kshim_smp_start_workers(uint64_t timeout_us);
size_t kshim_smp_cpu_count(void);
size_t kshim_smp_online_count(void);
size_t kshim_smp_available_count(void);
uint64_t kshim_smp_cpu_mpidr(size_t cpu_index);

int kshim_smp_submit(
    size_t worker_index, kshim_smp_callback_t callback, void *context,
    kshim_smp_ticket_t *ticket);
int kshim_smp_join(
    size_t worker_index, kshim_smp_ticket_t ticket, uint64_t timeout_us,
    int *result);

/* Invoked while a worker is idle. It must return promptly. NULL removes it. */
int kshim_smp_set_permanent_callback(
    size_t worker_index, kshim_smp_callback_t callback, void *context);

int kshim_smp_shutdown(uint64_t timeout_us);
int kshim_smp_prepare_handoff(uint64_t timeout_us);

uint64_t kshim_smp_time_ticks(void);
uint64_t kshim_smp_time_frequency(void);
uint64_t kshim_smp_time_us(void);

void kshim_smp_secondary_main(uintptr_t boot_context, uint64_t entry_el)
    __attribute__((noreturn));
void kshim_smp_secondary_entry(void);
