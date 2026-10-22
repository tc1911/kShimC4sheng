/* tests/tb323fu_keys_test.c - red test: tb323fu SPMI regions + key descriptors */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qcom_dt.h>
#include <qcom_keys_fdt.h>
#include <dt.h>

#ifndef FIXTURE_PATH
#error "FIXTURE_PATH must be defined"
#endif
static void *load_fixture(void)
{
    FILE *fp = fopen(FIXTURE_PATH, "rb");
    assert(fp);
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    assert(n > 0);
    size_t total = (size_t)n + 4096;
    void *buf = malloc(total);
    assert(buf);
    assert(fread(buf, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    assert(fdt_open_into(buf, buf, (int)total) == 0);
    return buf;
}
static struct kshim_resources *new_resources(void)
{
    struct kshim_resources *r = calloc(1, sizeof(*r));
    assert(r);
    return r;
}
static void test_positive(void)
{
    void *fdt = load_fixture();
    struct kshim_resources *r = new_resources();
    assert(kshim_dt_spmi(fdt, r) == 0 && r->spmi_count == 1);
    const uint32_t exp[7] = { 0xc400000u, 0xc900000u, 0xc4c0000u, 0xc8c0000u,
                              0xc426000u, 0xc403000u, 0xc42a000u };
    for (int i = 0; i < 7; i++)
        assert(r->spmi[0].controller.Regions[i].BaseAddress == exp[i]);
    int pon = fdt_path_offset(fdt, "/arbiter@c400000/spmi@c426000/pmk8850@0/pon_hlos@1300");
    uint8_t ctrl = 0xff, bus = 0xff, sid = 0xff;
    uint16_t addr = 0xffff;
    assert(pon >= 0);
    assert(kshim_dt_spmi_identity(fdt, pon, &ctrl, &bus, &sid) == 0);
    assert(ctrl == 0 && bus == 0 && sid == 0);
    assert(kshim_dt_spmi_reg(fdt, pon, &addr) == 0 && addr == 0x1300);
    QcomKeyDescriptor keys[QCOM_KEYS_MAX];
    size_t count = 0;
    assert(QcomKeysBuildFromDeviceTree(fdt, keys, QCOM_KEYS_MAX, &count) == 0);
    assert(count == 3);
    const QcomKeyDescriptor *up = NULL, *down = NULL, *sel = NULL;
    for (size_t i = 0; i < count; i++) {
        if (keys[i].Action == QCOM_KEY_ACTION_UP) up = &keys[i];
        else if (keys[i].Action == QCOM_KEY_ACTION_DOWN) down = &keys[i];
        else if (keys[i].Action == QCOM_KEY_ACTION_SELECT) sel = &keys[i];
    }
    assert(up && down && sel);
    assert((uint64_t)up->Base + (uint64_t)up->RegisterOffset == 0xf165004u);
    assert(QCOM_KEY_IS_SPMI_TOKEN(sel->Base) && QCOM_KEY_SPMI_TOKEN_ADDRESS(sel->Base) == 0x1300);
    assert(QCOM_KEY_SPMI_TOKEN_SID(sel->Base) == 0 && sel->RegisterOffset == 0x10 && sel->Mask == 128);
    assert(QCOM_KEY_IS_SPMI_TOKEN(down->Base) && QCOM_KEY_SPMI_TOKEN_ADDRESS(down->Base) == 0x1300);
    assert(QCOM_KEY_SPMI_TOKEN_SID(down->Base) == 0 && down->RegisterOffset == 0x10 && down->Mask == 64);
    free(r);
    free(fdt);
}
static void assert_pon_descriptors(void *fdt)
{
    QcomKeyDescriptor keys[QCOM_KEYS_MAX];
    size_t count = 0;
    assert(QcomKeysBuildFromDeviceTree(fdt, keys, QCOM_KEYS_MAX, &count) == 0);
    assert(count == 3);
    const QcomKeyDescriptor *up = NULL, *down = NULL, *sel = NULL;
    for (size_t i = 0; i < count; i++) {
        if (keys[i].Action == QCOM_KEY_ACTION_UP) up = &keys[i];
        else if (keys[i].Action == QCOM_KEY_ACTION_DOWN) down = &keys[i];
        else if (keys[i].Action == QCOM_KEY_ACTION_SELECT) sel = &keys[i];
    }
    assert(up && down && sel);
    /* Literal PON descriptors: SPMI SID 0, base offset 0x1300, RT_STS 0x10. */
    assert(QCOM_KEY_IS_SPMI_TOKEN(sel->Base) && QCOM_KEY_SPMI_TOKEN_SID(sel->Base) == 0);
    assert(QCOM_KEY_SPMI_TOKEN_ADDRESS(sel->Base) == 0x1300);
    assert(sel->RegisterOffset == 0x10 && sel->Mask == 128);
    assert(QCOM_KEY_IS_SPMI_TOKEN(down->Base) && QCOM_KEY_SPMI_TOKEN_SID(down->Base) == 0);
    assert(QCOM_KEY_SPMI_TOKEN_ADDRESS(down->Base) == 0x1300);
    assert(down->RegisterOffset == 0x10 && down->Mask == 64);
}
static void test_positive_stock_alias(void)
{
    /* Gen4 TB322FC stock SunP .096 evidence names the HLOS cell "pon_hlos";
     * the fixture is reused from TB323FU but the alias evidence is from Gen4.
     * The reg pair is otherwise unchanged. */
    static const char names[] = "pon_hlos\0pon_pbs";
    void *fdt = load_fixture();
    int pon = fdt_path_offset(fdt, "/arbiter@c400000/spmi@c426000/pmk8850@0/pon_hlos@1300");
    uint16_t addr = 0xffff;
    assert(pon >= 0);
    assert(fdt_setprop(fdt, pon, "reg-names", names, sizeof(names)) == 0);
    assert(kshim_dt_spmi_reg(fdt, pon, &addr) == 0 && addr == 0x1300);
    assert_pon_descriptors(fdt);
    free(fdt);
}
static void test_positive_reordered_alias(void)
{
    /* The HLOS cell is selected by name index, so reordering the names and
     * reg cells together must still resolve to the 0x1300 cell. */
    static const char names[] = "pon_pbs\0pon_hlos";
    fdt32_t reg[2] = { cpu_to_fdt32(0x800), cpu_to_fdt32(0x1300) };
    void *fdt = load_fixture();
    int pon = fdt_path_offset(fdt, "/arbiter@c400000/spmi@c426000/pmk8850@0/pon_hlos@1300");
    uint16_t addr = 0xffff;
    assert(pon >= 0);
    assert(fdt_setprop(fdt, pon, "reg-names", names, sizeof(names)) == 0);
    assert(fdt_setprop(fdt, pon, "reg", reg, sizeof(reg)) == 0);
    assert(kshim_dt_spmi_reg(fdt, pon, &addr) == 0 && addr == 0x1300);
    assert_pon_descriptors(fdt);
    free(fdt);
}
static void test_negative_hlos_ambiguous(void)
{
    static const char dup_hlos[] = "hlos\0hlos";
    static const char dup_pon[] = "pon_hlos\0pon_hlos";
    static const char mixed[] = "hlos\0pon_hlos";
    static const char *const ambiguous[] = { dup_hlos, dup_pon, mixed };
    static const size_t sizes[] = { sizeof(dup_hlos), sizeof(dup_pon), sizeof(mixed) };
    for (size_t i = 0; i < sizeof(ambiguous) / sizeof(ambiguous[0]); i++) {
        void *fdt = load_fixture();
        int pon = fdt_path_offset(fdt, "/arbiter@c400000/spmi@c426000/pmk8850@0/pon_hlos@1300");
        uint16_t addr = 0xffff;
        assert(pon >= 0);
        assert(fdt_setprop(fdt, pon, "reg-names", ambiguous[i], (int)sizes[i]) == 0);
        assert(kshim_dt_spmi_reg(fdt, pon, &addr) != 0);
        free(fdt);
    }
}
static void test_indexed_descriptors_match(void)
{
    void *fdt = load_fixture();
    QcomKeyDescriptor baseline[QCOM_KEYS_MAX], indexed[QCOM_KEYS_MAX];
    QcomKeyFdtAdapter adapter;
    size_t baseline_count = 0, indexed_count = 0;

    assert(QcomKeysBuildFromDeviceTree(fdt, baseline, QCOM_KEYS_MAX,
                                     &baseline_count) == 0);
    assert(QcomKeyFdtAdapterInit(&adapter, fdt) == 0);
    dt_parent_index_begin(fdt);
    int status = QcomKeysBuildFromFdt(&adapter.Ops, indexed, QCOM_KEYS_MAX,
                                    &indexed_count);
    dt_parent_index_end();
    assert(status == 0 && baseline_count == 3 && indexed_count == baseline_count);
    for (size_t i = 0; i < baseline_count; i++) {
        assert(indexed[i].Base == baseline[i].Base);
        assert(indexed[i].RegisterOffset == baseline[i].RegisterOffset);
        assert(indexed[i].Mask == baseline[i].Mask);
        assert(indexed[i].Code == baseline[i].Code);
        assert(indexed[i].Action == baseline[i].Action);
        assert(indexed[i].Source == baseline[i].Source);
        assert(indexed[i].ActiveLow == baseline[i].ActiveLow);
        assert(indexed[i].DebounceSamples == baseline[i].DebounceSamples);
    }
    free(fdt);
}
static void test_negative_disabled(void)
{
    void *fdt = load_fixture();
    int arb = fdt_path_offset(fdt, "/arbiter@c400000");
    struct kshim_resources *r = new_resources();
    assert(arb >= 0);
    assert(fdt_setprop_string(fdt, arb, "status", "disabled") == 0);
    assert(kshim_dt_spmi(fdt, r) != 0);
    free(r);
    free(fdt);
    fdt = load_fixture();
    int spmi = fdt_path_offset(fdt, "/arbiter@c400000/spmi@c426000");
    r = new_resources();
    assert(spmi >= 0);
    assert(fdt_setprop_string(fdt, spmi, "status", "disabled") == 0);
    assert(kshim_dt_spmi(fdt, r) != 0);
    free(r);
    free(fdt);
}
static void test_negative_regnames(void)
{
    void *fdt = load_fixture();
    int arb = fdt_path_offset(fdt, "/arbiter@c400000");
    static const char bad[] = "missing\0chnls\0obsrvr\0chnl_map";
    assert(arb >= 0);
    assert(fdt_setprop(fdt, arb, "reg-names", bad, sizeof(bad)) == 0);
    struct kshim_resources *r = new_resources();
    assert(kshim_dt_spmi(fdt, r) != 0);
    free(r);
    free(fdt);
}
static void test_negative_pmic_reg(void)
{
    void *fdt = load_fixture();
    int pmic = fdt_path_offset(fdt, "/arbiter@c400000/spmi@c426000/pmk8850@0");
    int pon = fdt_path_offset(fdt, "/arbiter@c400000/spmi@c426000/pmk8850@0/pon_hlos@1300");
    fdt32_t reg[2] = { cpu_to_fdt32(16), 0 };
    uint8_t ctrl = 0xff, bus = 0xff, sid = 0xff;
    assert(pmic >= 0 && pon >= 0);
    assert(fdt_setprop(fdt, pmic, "reg", reg, sizeof(reg)) == 0);
    assert(kshim_dt_spmi_identity(fdt, pon, &ctrl, &bus, &sid) != 0);
    free(fdt);
}
static int fixture_up_present(uint32_t pin)
{
    void *fdt = load_fixture();
    int vol = fdt_path_offset(fdt, "/gpio_keys/vol_up");
    int has = 0, rc;
    size_t count = 0;
    QcomKeyDescriptor keys[QCOM_KEYS_MAX];
    assert(vol >= 0);
    const fdt32_t *gpios = fdt_getprop(fdt, vol, "gpios", NULL);
    assert(gpios);
    assert(fdt32_to_cpu(gpios[1]) == 101 && fdt32_to_cpu(gpios[2]) == 1);
    fdt32_t cells[3] = { gpios[0], cpu_to_fdt32(pin), gpios[2] };
    assert(fdt_setprop(fdt, vol, "gpios", cells, sizeof(cells)) == 0);
    rc = QcomKeysBuildFromDeviceTree(fdt, keys, QCOM_KEYS_MAX, &count);
    (void)rc; /* success with only the 2 PMIC keys is fine here */
    for (size_t i = 0; i < count; i++)
        if (keys[i].Action == QCOM_KEY_ACTION_UP)
            has = 1;
    free(fdt);
    return has;
}
static void test_gpio_boundary(void)
{
    assert(fixture_up_present(216) == 1);
    assert(fixture_up_present(217) == 0);
    assert(fixture_up_present(218) == 0);
}
static void test_layout_cases(void)
{
    /* Case 1: upstream kaanapali TLMM reg base f100000 (size 0x300000). The
     * kaanapali provider places the bank at f100000, so GPIO101 must resolve
     * to 0xf165004 with no additional bank bias folded in (no double bias). */
    void *fdt = load_fixture();
    int prov = fdt_path_offset(fdt, "/pinctrl@f000000");
    assert(prov >= 0);
    assert(fdt_setprop_string(fdt, prov, "compatible", "qcom,kaanapali-tlmm") == 0);
    fdt32_t reg[4] = { cpu_to_fdt32(0), cpu_to_fdt32(0xf100000),
                       cpu_to_fdt32(0), cpu_to_fdt32(0x300000) };
    assert(fdt_setprop(fdt, prov, "reg", reg, sizeof(reg)) == 0);
    QcomKeyDescriptor keys[QCOM_KEYS_MAX];
    size_t count = 0;
    assert(QcomKeysBuildFromDeviceTree(fdt, keys, QCOM_KEYS_MAX, &count) == 0);
    const QcomKeyDescriptor *up = NULL;
    for (size_t i = 0; i < count; i++)
        if (keys[i].Action == QCOM_KEY_ACTION_UP) up = &keys[i];
    assert(up);
    assert((uint64_t)up->Base + (uint64_t)up->RegisterOffset == 0xf165004u);
    free(fdt);

    /* Case 2: canoe original provider reg base f000000 with an insufficient
     * 0x100000 window cannot cover the biased GPIO101 bank register
     * (0xf165004); the VOLUMEUP descriptor must drop out while PMIC PON keys
     * may still parse. */
    fdt = load_fixture();
    prov = fdt_path_offset(fdt, "/pinctrl@f000000");
    assert(prov >= 0);
    fdt32_t reg2[4] = { cpu_to_fdt32(0), cpu_to_fdt32(0xf000000),
                        cpu_to_fdt32(0), cpu_to_fdt32(0x100000) };
    assert(fdt_setprop(fdt, prov, "reg", reg2, sizeof(reg2)) == 0);
    count = 0;
    int rc = QcomKeysBuildFromDeviceTree(fdt, keys, QCOM_KEYS_MAX, &count);
    (void)rc;
    for (size_t i = 0; i < count; i++)
        assert(keys[i].Action != QCOM_KEY_ACTION_UP);
    free(fdt);
}
int main(void)
{
    test_positive();
    test_positive_stock_alias();
    test_positive_reordered_alias();
    test_negative_hlos_ambiguous();
    test_indexed_descriptors_match();
    test_negative_disabled();
    test_negative_regnames();
    test_negative_pmic_reg();
    test_gpio_boundary();
    test_layout_cases();
    printf("tb323fu keys test: PASS\n");
    return 0;
}
