#pragma once

#include <stdint.h>

typedef enum {
    KSHIM_PSCI_CONDUIT_NONE = 0,
    KSHIM_PSCI_CONDUIT_SMC,
    KSHIM_PSCI_CONDUIT_HVC,
} kshim_psci_conduit_t;

enum {
    KSHIM_PSCI_SUCCESS = 0,
    KSHIM_PSCI_NOT_SUPPORTED = -1,
    KSHIM_PSCI_INVALID_PARAMETERS = -2,
    KSHIM_PSCI_DENIED = -3,
    KSHIM_PSCI_ALREADY_ON = -4,
    KSHIM_PSCI_ON_PENDING = -5,
    KSHIM_PSCI_INTERNAL_FAILURE = -6,
    KSHIM_PSCI_NOT_PRESENT = -7,
    KSHIM_PSCI_DISABLED = -8,
    KSHIM_PSCI_INVALID_ADDRESS = -9,
};

enum {
    KSHIM_PSCI_AFFINITY_ON = 0,
    KSHIM_PSCI_AFFINITY_OFF = 1,
    KSHIM_PSCI_AFFINITY_ON_PENDING = 2,
};

int kshim_psci_init(const void *fdt);
kshim_psci_conduit_t kshim_psci_conduit(void);
uint32_t kshim_psci_version(void);

int64_t kshim_psci_call(
    uint32_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2);
int64_t kshim_psci_features(uint32_t function_id);
int64_t kshim_psci_cpu_on(
    uint64_t target_mpidr, uintptr_t entry_point, uintptr_t context_id);
int64_t kshim_psci_cpu_off(void);
int64_t kshim_psci_affinity_info(uint64_t target_mpidr, uint32_t level);
void kshim_psci_system_off(void) __attribute__((noreturn));

/*
 * Returnable power requests. The caller initializes PSCI independently before
 * calling these. A successful SYSTEM_OFF/SYSTEM_RESET does not return; any
 * return, including zero or positive, means the firmware did not perform the
 * requested transition. The raw firmware status is returned unchanged for the
 * caller to report, and the caller treats every return as a failure.
 */
int64_t kshim_psci_request_system_off(void);
int64_t kshim_psci_request_system_reset(void);

/* Weak architecture calls let host tests replace the firmware conduit. */
int64_t kshim_psci_arch_smc(
    uint64_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2);
int64_t kshim_psci_arch_hvc(
    uint64_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2);
