#include <dt.h>
#include <libfdt.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

static unsigned char blob[8192] __attribute__((aligned(8)));
static int prop_reads;

const void *__real_fdt_getprop(const void *, int, const char *, int *);
const void *__wrap_fdt_getprop(const void *f, int off, const char *name, int *lenp)
{
    if (name && (!strcmp(name, "phandle") || !strcmp(name, "linux,phandle")))
        prop_reads++;
    return __real_fdt_getprop(f, off, name, lenp);
}

static void build(void)
{
    assert(fdt_create(blob, sizeof(blob)) == 0);
    assert(fdt_finish_reservemap(blob) == 0);
    assert(fdt_begin_node(blob, "") == 0);
    assert(fdt_begin_node(blob, "provider") == 0);
    assert(fdt_property_u32(blob, "phandle", 7) == 0);
    assert(fdt_property_u32(blob, "linux,phandle", 7) == 0);
    assert(fdt_end_node(blob) == 0);
    assert(fdt_begin_node(blob, "other") == 0);
    assert(fdt_property_u32(blob, "phandle", 8) == 0);
    assert(fdt_end_node(blob) == 0);
    assert(fdt_end_node(blob) == 0);
    assert(fdt_finish(blob) == 0);
}

static void test_clean(void)
{
    build();
    assert(dt_validate(blob, sizeof(blob)) == 0);
    prop_reads = 0;
    int off = dt_phandle(blob, 7);
    int prov = fdt_path_offset(blob, "/provider");
    assert(prov >= 0 && off == prov);
    assert(prop_reads == 6);
}

static void test_handles(void)
{
    build();
    assert(dt_validate(blob, sizeof(blob)) == 0);
    prop_reads = 0;
    assert(dt_phandle(blob, 0) == -1);
    assert(dt_phandle(blob, UINT32_MAX) == -1);
    assert(prop_reads == 0);
}

static void test_mutations(void)
{
    build();
    assert(fdt_open_into(blob, blob, sizeof(blob)) == 0);

    int oth = fdt_path_offset(blob, "/other");
    assert(oth >= 0);
    assert(fdt_setprop_u32(blob, oth, "phandle", 7) == 0);
    assert(dt_validate(blob, sizeof(blob)) == 0);
    assert(dt_phandle(blob, 7) == -1);

    oth = fdt_path_offset(blob, "/other");
    assert(oth >= 0);
    assert(fdt_setprop_u32(blob, oth, "phandle", 8) == 0);

    int prov = fdt_path_offset(blob, "/provider");
    assert(prov >= 0);
    assert(fdt_setprop_u32(blob, prov, "linux,phandle", 9) == 0);
    assert(dt_validate(blob, sizeof(blob)) == 0);
    assert(dt_phandle(blob, 7) == -1);

    prov = fdt_path_offset(blob, "/provider");
    assert(prov >= 0);
    assert(fdt_setprop_u32(blob, prov, "linux,phandle", 7) == 0);
}

static void test_bad_node(void)
{
    build();
    assert(fdt_open_into(blob, blob, sizeof(blob)) == 0);

    int bad = fdt_path_offset(blob, "/other");
    assert(bad >= 0);
    unsigned char raw[2] = {0, 0};
    assert(fdt_setprop(blob, bad, "phandle", raw, sizeof(raw)) == 0);
    assert(dt_validate(blob, sizeof(blob)) == 0);
    assert(dt_phandle(blob, 7) == -1);

    bad = fdt_path_offset(blob, "/other");
    assert(bad >= 0);
    assert(fdt_delprop(blob, bad, "phandle") == 0);

    int prov = fdt_path_offset(blob, "/provider");
    assert(prov >= 0);
    assert(fdt_setprop(blob, prov, "status", "disabled", 9) == 0);
    assert(dt_validate(blob, sizeof(blob)) == 0);
    assert(dt_phandle(blob, 7) == -1);
}

int main(void)
{
    test_clean();
    test_handles();
    test_mutations();
    test_bad_node();
    printf("PASS\n");
    return 0;
}
