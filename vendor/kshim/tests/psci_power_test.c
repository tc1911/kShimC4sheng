/* SPDX-License-Identifier: MIT */
/*
 * Regression tests for returnable PSCI power requests.
 *
 * The two request APIs are declared in psci.h and return the raw firmware
 * status; neither is noreturn and neither spins on WFE.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <libfdt.h>
#include <psci.h>

#define PSCI_FN_VERSION      UINT32_C(0x84000000)
#define PSCI_FN_SYSTEM_OFF   UINT32_C(0x84000008)
#define PSCI_FN_SYSTEM_RESET UINT32_C(0x84000009)

static uint32_t gVersion;
static int64_t  gStatus;
static unsigned gSmcCalls;
static unsigned gHvcCalls;
static uint64_t gFid, gArg0, gArg1, gArg2;

static int64_t stub(uint64_t fid, uint64_t a0, uint64_t a1, uint64_t a2)
{
    /* No FEATURES probe: only VERSION and the two power requests may arrive. */
    assert(fid == PSCI_FN_VERSION || fid == PSCI_FN_SYSTEM_OFF ||
           fid == PSCI_FN_SYSTEM_RESET);
    gFid = fid;
    gArg0 = a0;
    gArg1 = a1;
    gArg2 = a2;
    if (fid == PSCI_FN_VERSION)
        return (int64_t)gVersion;
    return gStatus;
}

/* Strong replacements override the weak conduit calls in psci.c. */
int64_t kshim_psci_arch_smc(uint64_t f, uint64_t a0, uint64_t a1, uint64_t a2)
{
    gSmcCalls++;
    return stub(f, a0, a1, a2);
}

int64_t kshim_psci_arch_hvc(uint64_t f, uint64_t a0, uint64_t a1, uint64_t a2)
{
    gHvcCalls++;
    return stub(f, a0, a1, a2);
}

static void reset(void)
{
    gSmcCalls = 0;
    gHvcCalls = 0;
    gFid = 0;
    gArg0 = gArg1 = gArg2 = 0;
}

static void *make_tree(uint64_t *blob, size_t words, const char *compat,
                       const char *method)
{
    assert(!fdt_create_empty_tree(blob, (int)(words * sizeof(*blob))));
    int node = fdt_add_subnode(blob, 0, "psci");
    assert(node > 0);
    assert(!fdt_setprop_string(blob, node, "compatible", compat));
    assert(!fdt_setprop_string(blob, node, "method", method));
    return blob;
}

/* Init probes VERSION 0x84000000 with zero arguments on the chosen conduit. */
static void check_init(uint32_t version, kshim_psci_conduit_t conduit)
{
    assert(gFid == PSCI_FN_VERSION);
    assert(gArg0 == 0 && gArg1 == 0 && gArg2 == 0);
    if (conduit == KSHIM_PSCI_CONDUIT_SMC) {
        assert(gSmcCalls == 1 && gHvcCalls == 0);
    } else {
        assert(gHvcCalls == 1 && gSmcCalls == 0);
    }
    assert(kshim_psci_version() == version);
}

static void check_request(const char *name, int64_t (*request)(void),
                          uint32_t fid, kshim_psci_conduit_t conduit)
{
    unsigned before_smc = gSmcCalls;
    unsigned before_hvc = gHvcCalls;
    int64_t status = request();

    fprintf(stderr, "%s fid=0x%08x status=%lld\n", name, (unsigned)fid,
            (long long)status);
    assert(status == gStatus);
    assert(gFid == fid);
    assert(gArg0 == 0 && gArg1 == 0 && gArg2 == 0);
    if (conduit == KSHIM_PSCI_CONDUIT_SMC) {
        assert(gSmcCalls == before_smc + 1 && gHvcCalls == before_hvc);
    } else {
        assert(gHvcCalls == before_hvc + 1 && gSmcCalls == before_smc);
    }
}

static void test_before_init(void)
{
    reset();
    assert(kshim_psci_request_system_off() == KSHIM_PSCI_NOT_SUPPORTED);
    assert(kshim_psci_request_system_reset() == KSHIM_PSCI_NOT_SUPPORTED);
    assert(gSmcCalls == 0 && gHvcCalls == 0);
}

static void test_matrix(void)
{
    static const struct { const char *compat; uint32_t version; } versions[] = {
        { "arm,psci-0.2", 0x00000002U },
        { "arm,psci-1.0", 0x00010000U },
    };
    static const struct { const char *method; kshim_psci_conduit_t conduit; } conduits[] = {
        { "smc", KSHIM_PSCI_CONDUIT_SMC },
        { "hvc", KSHIM_PSCI_CONDUIT_HVC },
    };
    static const int64_t statuses[] = {
        KSHIM_PSCI_NOT_SUPPORTED, KSHIM_PSCI_DENIED, 0, 5,
    };

    for (size_t v = 0; v < sizeof(versions) / sizeof(versions[0]); ++v) {
        for (size_t c = 0; c < sizeof(conduits) / sizeof(conduits[0]); ++c) {
            for (size_t s = 0; s < sizeof(statuses) / sizeof(statuses[0]); ++s) {
                uint64_t blob[512];

                gVersion = versions[v].version;
                gStatus = statuses[s];
                reset();
                assert(!kshim_psci_init(make_tree(
                    blob, 512, versions[v].compat, conduits[c].method)));
                assert(kshim_psci_conduit() == conduits[c].conduit);
                check_init(versions[v].version, conduits[c].conduit);
                reset();
                check_request("off", kshim_psci_request_system_off,
                              PSCI_FN_SYSTEM_OFF, conduits[c].conduit);
                check_request("reset", kshim_psci_request_system_reset,
                              PSCI_FN_SYSTEM_RESET, conduits[c].conduit);
            }
        }
    }
}

static void test_failed_init_clears(void)
{
    uint64_t blob[512];

    gVersion = 0x00010000U;
    assert(!kshim_psci_init(make_tree(blob, 512, "arm,psci-1.0", "smc")));
    assert(kshim_psci_conduit() == KSHIM_PSCI_CONDUIT_SMC);
    reset();
    assert(kshim_psci_init(NULL) == KSHIM_PSCI_INVALID_PARAMETERS);
    assert(kshim_psci_conduit() == KSHIM_PSCI_CONDUIT_NONE);
    assert(kshim_psci_request_system_off() == KSHIM_PSCI_NOT_SUPPORTED);
    assert(kshim_psci_request_system_reset() == KSHIM_PSCI_NOT_SUPPORTED);
    assert(gSmcCalls == 0 && gHvcCalls == 0);
}

int main(void)
{
    test_before_init();
    test_matrix();
    test_failed_init_clears();
    puts("PSCI returnable power request tests passed");
    return 0;
}
