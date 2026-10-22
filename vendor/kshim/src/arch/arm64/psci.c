#include <psci.h>

#include <libfdt.h>

#include <stddef.h>

#define PSCI_FN_VERSION            UINT32_C(0x84000000)
#define PSCI_FN_CPU_OFF            UINT32_C(0x84000002)
#define PSCI_FN64_CPU_ON           UINT32_C(0xc4000003)
#define PSCI_FN64_AFFINITY_INFO    UINT32_C(0xc4000004)
#define PSCI_FN_SYSTEM_OFF         UINT32_C(0x84000008)
#define PSCI_FN_SYSTEM_RESET       UINT32_C(0x84000009)
#define PSCI_FN_FEATURES           UINT32_C(0x8400000a)

typedef struct {
    kshim_psci_conduit_t conduit;
    uint32_t version;
    uint8_t initialized;
} kshim_psci_context_t;

static kshim_psci_context_t mPsci;

static int string_equal(const char *value, int length, const char *expected)
{
    int index = 0;

    if (value == NULL || expected == NULL || length <= 0)
        return 0;
    while (expected[index] != '\0') {
        if (index >= length || value[index] != expected[index])
            return 0;
        index++;
    }
    return index < length && value[index] == '\0';
}

static int node_available(const void *fdt, int node)
{
    int length;
    const char *status = fdt_getprop(fdt, node, "status", &length);

    if (status == NULL)
        return length == -FDT_ERR_NOTFOUND;
    return string_equal(status, length, "okay") ||
        string_equal(status, length, "ok");
}

__attribute__((weak, noinline)) int64_t kshim_psci_arch_smc(
    uint64_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
#if defined(__aarch64__)
    register uint64_t x0 __asm__("x0") = function_id;
    register uint64_t x1 __asm__("x1") = arg0;
    register uint64_t x2 __asm__("x2") = arg1;
    register uint64_t x3 __asm__("x3") = arg2;

    __asm__ volatile(
        "smc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",
          "x13", "x14", "x15", "x16", "x17", "memory");
    return (int64_t)x0;
#else
    (void)function_id;
    (void)arg0;
    (void)arg1;
    (void)arg2;
    return KSHIM_PSCI_NOT_SUPPORTED;
#endif
}

__attribute__((weak, noinline)) int64_t kshim_psci_arch_hvc(
    uint64_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
#if defined(__aarch64__)
    register uint64_t x0 __asm__("x0") = function_id;
    register uint64_t x1 __asm__("x1") = arg0;
    register uint64_t x2 __asm__("x2") = arg1;
    register uint64_t x3 __asm__("x3") = arg2;

    __asm__ volatile(
        "hvc #0"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",
          "x13", "x14", "x15", "x16", "x17", "memory");
    return (int64_t)x0;
#else
    (void)function_id;
    (void)arg0;
    (void)arg1;
    (void)arg2;
    return KSHIM_PSCI_NOT_SUPPORTED;
#endif
}

int64_t kshim_psci_call(
    uint32_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    if (!mPsci.initialized)
        return KSHIM_PSCI_NOT_SUPPORTED;
    if (mPsci.conduit == KSHIM_PSCI_CONDUIT_SMC)
        return kshim_psci_arch_smc(function_id, arg0, arg1, arg2);
    if (mPsci.conduit == KSHIM_PSCI_CONDUIT_HVC)
        return kshim_psci_arch_hvc(function_id, arg0, arg1, arg2);
    return KSHIM_PSCI_NOT_SUPPORTED;
}

int kshim_psci_init(const void *fdt)
{
    int node;
    int length;
    const char *method;
    int64_t result;
    uint32_t major;
    uint32_t minor;

    mPsci = (kshim_psci_context_t){0};
    if (fdt == NULL || fdt_check_header(fdt) != 0)
        return KSHIM_PSCI_INVALID_PARAMETERS;

    node = fdt_node_offset_by_compatible(fdt, -1, "arm,psci-1.0");
    if (node < 0)
        node = fdt_node_offset_by_compatible(fdt, -1, "arm,psci-0.2");
    if (node < 0 || !node_available(fdt, node))
        return KSHIM_PSCI_NOT_PRESENT;

    method = fdt_getprop(fdt, node, "method", &length);
    if (string_equal(method, length, "smc"))
        mPsci.conduit = KSHIM_PSCI_CONDUIT_SMC;
    else if (string_equal(method, length, "hvc"))
        mPsci.conduit = KSHIM_PSCI_CONDUIT_HVC;
    else
        return KSHIM_PSCI_INVALID_PARAMETERS;

    mPsci.initialized = 1U;
    result = kshim_psci_call(PSCI_FN_VERSION, 0U, 0U, 0U);
    if (result < 0) {
        mPsci = (kshim_psci_context_t){0};
        return (int)result;
    }

    mPsci.version = (uint32_t)result;
    major = mPsci.version >> 16;
    minor = mPsci.version & UINT32_C(0xffff);
    if (!((major == 0U && minor >= 2U) || major == 1U)) {
        mPsci = (kshim_psci_context_t){0};
        return KSHIM_PSCI_NOT_SUPPORTED;
    }
    return KSHIM_PSCI_SUCCESS;
}

kshim_psci_conduit_t kshim_psci_conduit(void)
{
    return mPsci.conduit;
}

uint32_t kshim_psci_version(void)
{
    return mPsci.version;
}

int64_t kshim_psci_features(uint32_t function_id)
{
    if ((mPsci.version >> 16) < 1U)
        return KSHIM_PSCI_NOT_SUPPORTED;
    return kshim_psci_call(PSCI_FN_FEATURES, function_id, 0U, 0U);
}

int64_t kshim_psci_cpu_on(
    uint64_t target_mpidr, uintptr_t entry_point, uintptr_t context_id)
{
    return kshim_psci_call(
        PSCI_FN64_CPU_ON, target_mpidr, (uint64_t)entry_point,
        (uint64_t)context_id);
}

int64_t kshim_psci_cpu_off(void)
{
    return kshim_psci_call(PSCI_FN_CPU_OFF, 0U, 0U, 0U);
}

int64_t kshim_psci_affinity_info(uint64_t target_mpidr, uint32_t level)
{
    return kshim_psci_call(PSCI_FN64_AFFINITY_INFO, target_mpidr, level, 0U);
}

int64_t kshim_psci_request_system_off(void)
{
    return kshim_psci_call(PSCI_FN_SYSTEM_OFF, 0U, 0U, 0U);
}

int64_t kshim_psci_request_system_reset(void)
{
    return kshim_psci_call(PSCI_FN_SYSTEM_RESET, 0U, 0U, 0U);
}

void kshim_psci_system_off(void)
{
    (void)kshim_psci_request_system_off();
    for (;;) {
#if defined(__aarch64__)
        __asm__ volatile("wfe" ::: "memory");
#endif
    }
}
