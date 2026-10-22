/* Exercise the production recorder/exporter against a deterministic clock
 * and real libfdt; no production reset or test-only state API is needed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libfdt.h>
#include <boot_metrics.h>

#if defined(CONFIG_KSHIM_BOOT_TIMING) && CONFIG_KSHIM_BOOT_TIMING
static uint64_t Now;
static unsigned ClockReads;

uint64_t kshim_boot_metrics_now_ms(void)
{
    ++ClockReads;
    return Now;
}

static uint64_t Read64(const void *Fdt, const char *Name, unsigned Index,
                       unsigned Count)
{
    int Length = 0;
    int Chosen = fdt_path_offset(Fdt, "/chosen");
    const unsigned char *Values = fdt_getprop(Fdt, Chosen, Name, &Length);
    assert(Values != NULL && Length == (int)(Count * sizeof(fdt64_t)));
    /* A DT property may be only four-byte aligned. */
    fdt64_t Value;
    memcpy(&Value, Values + Index * sizeof(Value), sizeof(Value));
    return fdt64_to_cpu(Value);
}

static void CreateTree(void *Fdt, size_t Size)
{
    assert(fdt_create_empty_tree(Fdt, (int)Size) == 0);
    assert(fdt_add_subnode(Fdt, 0, "chosen") >= 0);
    int Memory = fdt_add_subnode(Fdt, 0, "memory@80000000");
    assert(Memory >= 0);
    const fdt32_t Reg[] = {cpu_to_fdt32(0x80000000U), cpu_to_fdt32(0x40000000U)};
    assert(fdt_setprop(Fdt, Memory, "reg", Reg, sizeof(Reg)) == 0);
    assert(fdt_add_mem_rsv(Fdt, UINT64_C(0x90000000), UINT64_C(0x100000)) == 0);
}

int main(void)
{
    _Alignas(8) unsigned char Fdt[2048];
    _Alignas(8) unsigned char Small[2048];
    CreateTree(Fdt, sizeof(Fdt));

    /* An absent entry timestamp must not turn zero-initialized state into
     * apparently valid phase times. */
    Now = 900U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_MMU);
    assert(kshim_boot_metrics_export(Fdt) == 0);
    for (unsigned Index = 0; Index < KSHIM_BOOT_STAGE_COUNT; ++Index)
        assert(Read64(Fdt, "kshim,boot-timing-ms", Index,
                      KSHIM_BOOT_STAGE_COUNT) == UINT64_MAX);
    assert(Read64(Fdt, "kshim,boot-entry-counter-ms", 0, 1) == UINT64_MAX);

    Now = 1000U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_ENTRY);
    Now = 1017U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_RESOURCES);
    Now = 1052U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_FONT);
    Now = 1070U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_UI);
    Now = 1084U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_ENTRIES);
    Now = 1112U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_FIRST_FRAME);
    unsigned Before = ClockReads;
    Now = 8000U;
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_ENTRY);
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_FIRST_FRAME);
    kshim_boot_metrics_record_once((kshim_boot_stage_t)-1);
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_COUNT);
    assert(ClockReads == Before);
    assert(kshim_boot_metrics_export(Fdt) == 0);
    const uint64_t Expected[] = {0, 17, UINT64_MAX, 52, 70, 84, 112, UINT64_MAX};
    for (unsigned Index = 0; Index < KSHIM_BOOT_STAGE_COUNT; ++Index)
        assert(Read64(Fdt, "kshim,boot-timing-ms", Index,
                      KSHIM_BOOT_STAGE_COUNT) == Expected[Index]);
    assert(Read64(Fdt, "kshim,boot-entry-counter-ms", 0, 1) == 1000U);
    int Length;
    const fdt32_t *Version = fdt_getprop(Fdt, fdt_path_offset(Fdt, "/chosen"),
                                       "kshim,boot-timing-version", &Length);
    assert(Version != NULL && Length == 4 && fdt32_to_cpu(*Version) == 1U);

    /* Later handoff export adds the real dwell time without replacing the
     * recorded first frame. It must preserve memory and reservations. */
    kshim_boot_metrics_record_once(KSHIM_BOOT_STAGE_HANDOFF);
    assert(kshim_boot_metrics_export(Fdt) == 0);
    assert(Read64(Fdt, "kshim,boot-timing-ms", KSHIM_BOOT_STAGE_HANDOFF,
                  KSHIM_BOOT_STAGE_COUNT) == 7000U);
    const fdt32_t *Reg = fdt_getprop(Fdt, fdt_path_offset(Fdt, "/memory@80000000"),
                                   "reg", &Length);
    assert(Reg != NULL && Length == 8);
    assert(fdt32_to_cpu(Reg[0]) == 0x80000000U);
    assert(fdt32_to_cpu(Reg[1]) == 0x40000000U);
    uint64_t Address, Size;
    assert(fdt_num_mem_rsv(Fdt) == 1);
    assert(fdt_get_mem_rsv(Fdt, 0, &Address, &Size) == 0);
    assert(Address == UINT64_C(0x90000000) && Size == UINT64_C(0x100000));

    /* A packed tree has no room for these diagnostic properties. */
    CreateTree(Small, sizeof(Small));
    assert(fdt_pack(Small) == 0);
    assert(kshim_boot_metrics_export(Small) == -FDT_ERR_NOSPACE);
    assert(kshim_boot_metrics_export(NULL) < 0);
    assert(ClockReads == Before + 1U);
    puts("PASS: boot timing stages, record-once clock, DT export and no-space");
    return 0;
}
#else
int main(void)
{
    unsigned char Fdt[64], Before[64];
    memset(Fdt, 0xa5, sizeof(Fdt));
    memcpy(Before, Fdt, sizeof(Fdt));
    for (unsigned Index = 0; Index < KSHIM_BOOT_STAGE_COUNT; ++Index)
        kshim_boot_metrics_record_once((kshim_boot_stage_t)Index);
    assert(kshim_boot_metrics_export(Fdt) == 0);
    assert(memcmp(Fdt, Before, sizeof(Fdt)) == 0);
    assert(kshim_boot_metrics_export(NULL) == 0);
    puts("PASS: disabled boot timing is a no-op");
    return 0;
}
#endif
