/*
 * tests/reserved_placeholder_test.c
 *
 * Regression test for reserved-memory children carrying (address, size)
 * cell tuples that are entirely zero ("zero placeholders") and a child
 * carrying a size but no reg.  Those are NOT reservations: they must not be
 * reported by dt_reservations() nor forwarded to kshim_mmu_configure().
 *
 * Plain self-test: no device actions, no MMU enable, no page-table prepare.
 * The image block below only satisfies symbols pulled in via kshim_test().
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dt.h"
#include "kshim_mmu.h"

__asm__(".pushsection .test_image,\"aw\",@progbits\n.balign 4096\n"
        ".global __image_start_address__\n__image_start_address__:\n.space 8192\n"
        ".global __image_end_address__\n__image_end_address__:\n.popsection\n");

extern unsigned char __image_start_address__;
extern unsigned char __image_end_address__;

#define FB_BASE      0xfc800000u
#define FB_SIZE      23152640u
#define SPLASH_BASE  0xfc800000u
#define SPLASH_SIZE  0x02b00000u

static uint64_t fdt_buf[8192];

/* Pack one big-endian (address, size) tuple as four cells (#address-cells=2,
 * #size-cells=2), converting host values with cpu_to_fdt32 before use. */
static void pack_tuple(uint32_t *cells, uint64_t address, uint64_t size)
{
    cells[0] = cpu_to_fdt32((uint32_t)(address >> 32));
    cells[1] = cpu_to_fdt32((uint32_t)address);
    cells[2] = cpu_to_fdt32((uint32_t)(size >> 32));
    cells[3] = cpu_to_fdt32((uint32_t)size);
}

static void *make_base_tree(void)
{
    int err;

    memset(fdt_buf, 0, sizeof(fdt_buf));
    err = fdt_create_empty_tree(fdt_buf, sizeof(fdt_buf));
    assert(err == 0);

    int root = fdt_path_offset(fdt_buf, "/");
    assert(root >= 0);
    assert(fdt_setprop_u32(fdt_buf, root, "#address-cells", 2) == 0);
    assert(fdt_setprop_u32(fdt_buf, root, "#size-cells", 2) == 0);

    int rm = fdt_add_subnode(fdt_buf, root, "reserved-memory");
    assert(rm >= 0);
    assert(fdt_setprop_u32(fdt_buf, rm, "#address-cells", 2) == 0);
    assert(fdt_setprop_u32(fdt_buf, rm, "#size-cells", 2) == 0);
    /* empty reserved ranges: no child of reserved-memory is "reserved" */
    assert(fdt_setprop(fdt_buf, rm, "ranges", NULL, 0) == 0);

    return fdt_buf;
}

/* Re-look-up the parent each time: offsets shift when subnodes are added. */
static int add_region_reg(void *fdt, const char *name,
                          const void *cells, size_t nbytes)
{
    int rm = fdt_path_offset(fdt, "/reserved-memory");
    assert(rm >= 0);
    int n = fdt_add_subnode(fdt, rm, name);
    assert(n >= 0);
    assert(fdt_setprop(fdt, n, "reg", cells, nbytes) == 0);
    return n;
}

static int add_region_noreg(void *fdt, const char *name,
                            const void *cells, size_t nbytes)
{
    int rm = fdt_path_offset(fdt, "/reserved-memory");
    assert(rm >= 0);
    int n = fdt_add_subnode(fdt, rm, name);
    assert(n >= 0);
    /* size present, reg deliberately absent */
    assert(fdt_setprop(fdt, n, "size", cells, nbytes) == 0);
    return n;
}

static void add_empty_prop(void *fdt, const char *path, const char *prop)
{
    int n = fdt_path_offset(fdt, path);
    assert(n >= 0);
    assert(fdt_setprop(fdt, n, prop, NULL, 0) == 0);
}

static void add_u32_prop(void *fdt, const char *path, const char *prop,
                         uint32_t value)
{
    int n = fdt_path_offset(fdt, path);
    assert(n >= 0);
    uint32_t cell = cpu_to_fdt32(value);
    assert(fdt_setprop(fdt, n, prop, &cell, sizeof(cell)) == 0);
}

/*
 * Stock-like reserved-memory tree:
 *   demura_region_0 / demura_region_1 -> four zero cells (placeholders)
 *   splash_region -> real range + empty no-map
 *   dynamic_region -> two big-endian size cells but no reg
 */
static void *make_stock_tree(void)
{
    uint32_t zero[4];
    uint32_t splash[4];
    uint32_t dynsize[2];
    void *fdt = make_base_tree();

    pack_tuple(zero, 0, 0);
    pack_tuple(splash, SPLASH_BASE, SPLASH_SIZE);
    dynsize[0] = cpu_to_fdt32(0);
    dynsize[1] = cpu_to_fdt32(0x1000u);

    add_region_reg(fdt, "demura_region_0", zero, sizeof(zero));
    add_region_reg(fdt, "demura_region_1", zero, sizeof(zero));

    add_region_reg(fdt, "splash_region", splash, sizeof(splash));
    add_empty_prop(fdt, "/reserved-memory/splash_region", "no-map");

    add_region_noreg(fdt, "dynamic_region", dynsize, sizeof(dynsize));

    return fdt;
}

/* ---- cases ----------------------------------------------------------- */

static void test_stock_zero_placeholders(void)
{
    struct dt_range ranges[16];
    bool no_map[16];
    size_t count = 0;
    void *fdt = make_stock_tree();

    int rc = dt_reservations(fdt, ranges, no_map, 16, &count);
    printf("stock_zero_placeholders: dt_reservations rc=%d count=%zu\n",
           rc, count);
    assert(rc == 0);
    assert(count == 1);
    assert(ranges[0].base == SPLASH_BASE);
    assert(ranges[0].size == SPLASH_SIZE);
    assert(no_map[0] == true);

    kshim_mmu_config_t cfg = {
        .fdt = fdt,
        .framebuffer_base = FB_BASE,
        .framebuffer_size = FB_SIZE,
    };
    int mrc = kshim_mmu_configure(&cfg);
    printf("stock_zero_placeholders: kshim_mmu_configure rc=%d\n", mrc);
    assert(mrc == 0);

    printf("PASS stock_zero_placeholders\n");
}

static void test_mixed_tuples_one_reg(void)
{
    struct dt_range ranges[16];
    bool no_map[16];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint32_t reg[12];

    pack_tuple(&reg[0], 0x81000000u, 0x1000u);  /* valid */
    pack_tuple(&reg[4], 0, 0);                  /* zero placeholder */
    pack_tuple(&reg[8], 0x81002000u, 0x1000u);  /* valid */

    add_region_reg(fdt, "mixed", reg, sizeof(reg));

    int rc = dt_reservations(fdt, ranges, no_map, 16, &count);
    printf("mixed_tuples_one_reg: rc=%d count=%zu\n", rc, count);
    assert(rc == 0);
    assert(count == 2);
    assert(ranges[0].base == 0x81000000u);
    assert(ranges[0].size == 0x1000u);
    assert(no_map[0] == false);
    assert(ranges[1].base == 0x81002000u);
    assert(ranges[1].size == 0x1000u);
    assert(no_map[1] == false);

    printf("PASS mixed_tuples_one_reg\n");
}

static void test_all_zero_cap0(void)
{
    struct dt_range ranges[1];
    bool no_map[1];
    size_t count = 123;
    void *fdt = make_base_tree();
    uint32_t zero[4];

    pack_tuple(zero, 0, 0);
    add_region_reg(fdt, "zerocap", zero, sizeof(zero));

    int rc = dt_reservations(fdt, ranges, no_map, 0, &count);
    printf("all_zero_cap0: rc=%d count=%zu\n", rc, count);
    assert(rc == 0);
    assert(count == 0);

    printf("PASS all_zero_cap0\n");
}

static void test_valid_then_zero_cap1(void)
{
    struct dt_range ranges[1];
    bool no_map[1];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint32_t reg[8];

    pack_tuple(&reg[0], 0x81000000u, 0x1000u);
    pack_tuple(&reg[4], 0, 0);

    add_region_reg(fdt, "validzero", reg, sizeof(reg));

    int rc = dt_reservations(fdt, ranges, no_map, 1, &count);
    printf("valid_then_zero_cap1: rc=%d count=%zu\n", rc, count);
    assert(rc == 0);
    assert(count == 1);
    assert(ranges[0].base == 0x81000000u);
    assert(ranges[0].size == 0x1000u);

    printf("PASS valid_then_zero_cap1\n");
}

static void test_nonzero_addr_zero_size(void)
{
    struct dt_range ranges[4];
    bool no_map[4];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint32_t reg[4];

    pack_tuple(reg, 0x81000000u, 0);
    add_region_reg(fdt, "badzero", reg, sizeof(reg));

    int rc = dt_reservations(fdt, ranges, no_map, 4, &count);
    printf("nonzero_addr_zero_size: rc=%d\n", rc);
    assert(rc != 0);

    printf("PASS nonzero_addr_zero_size\n");
}

static void test_malformed_len15(void)
{
    struct dt_range ranges[4];
    bool no_map[4];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint8_t reg[15];

    memset(reg, 0, sizeof(reg));
    add_region_reg(fdt, "badlen", reg, sizeof(reg));

    int rc = dt_reservations(fdt, ranges, no_map, 4, &count);
    printf("malformed_len15: rc=%d\n", rc);
    assert(rc != 0);

    printf("PASS malformed_len15\n");
}

static void test_nomap_u32(void)
{
    struct dt_range ranges[4];
    bool no_map[4];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint32_t empty_reg[4];

    pack_tuple(empty_reg, 0, 0);
    add_region_reg(fdt, "nomapu32", empty_reg, sizeof(empty_reg));
    add_u32_prop(fdt, "/reserved-memory/nomapu32", "no-map", 1u);

    int rc = dt_reservations(fdt, ranges, no_map, 4, &count);
    printf("nomap_u32: rc=%d\n", rc);
    assert(rc != 0);

    printf("PASS nomap_u32\n");
}

static void test_overflow_reg(void)
{
    struct dt_range ranges[4];
    bool no_map[4];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint32_t reg[4];

    pack_tuple(reg, UINT64_C(0xfffffffffffff000), 0x2000u);
    add_region_reg(fdt, "ovf", reg, sizeof(reg));

    int rc = dt_reservations(fdt, ranges, no_map, 4, &count);
    printf("overflow_reg: rc=%d\n", rc);
    assert(rc != 0);

    printf("PASS overflow_reg\n");
}

static void test_genuine_base0(void)
{
    struct dt_range ranges[4];
    bool no_map[4];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint32_t reg[4];

    /* base 0, size 0x1000: genuine reservation, not an empty tuple */
    pack_tuple(reg, 0, 0x1000u);
    add_region_reg(fdt, "base0", reg, sizeof(reg));

    int rc = dt_reservations(fdt, ranges, no_map, 4, &count);
    printf("genuine_base0: rc=%d count=%zu\n", rc, count);
    assert(rc == 0);
    assert(count == 1);
    assert(ranges[0].base == 0u);
    assert(ranges[0].size == 0x1000u);

    printf("PASS genuine_base0\n");
}

static void test_genuine_cap0(void)
{
    struct dt_range ranges[1];
    bool no_map[1];
    size_t count = 0;
    void *fdt = make_base_tree();
    uint32_t reg[4];

    pack_tuple(reg, 0, 0x1000u);
    add_region_reg(fdt, "genuinecap0", reg, sizeof(reg));

    /* a real reservation that cannot be stored must be an error */
    int rc = dt_reservations(fdt, ranges, no_map, 0, &count);
    printf("genuine_cap0: rc=%d\n", rc);
    assert(rc != 0);

    printf("PASS genuine_cap0\n");
}

/* ---- optional: run against a real stock DTB -------------------------- */

static int run_actual_dt(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(f);
        return 1;
    }
    long len = ftell(f);
    if (len <= 0) {
        fprintf(stderr, "empty dtb\n");
        fclose(f);
        return 1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        perror("fseek");
        fclose(f);
        return 1;
    }

    size_t sz = (size_t)len;
    /* malloc + manual round-up so the blob is >= 8 byte aligned */
    void *raw = malloc(sz + 8);
    if (!raw) {
        fclose(f);
        return 1;
    }
    void *blob = (void *)(((uintptr_t)raw + 7u) & ~(uintptr_t)7u);

    size_t got = fread(blob, 1, sz, f);
    fclose(f);
    if (got != sz) {
        fprintf(stderr, "short read %zu/%zu\n", got, sz);
        free(raw);
        return 1;
    }

    int vrc = dt_validate(blob, sz);
    printf("actual_dt dt_validate rc=%d\n", vrc);
    assert(vrc == 0);

    struct dt_range ranges[128];
    bool no_map[128];
    size_t count = 0;
    int rc = dt_reservations(blob, ranges, no_map, 128, &count);
    printf("actual_dt dt_reservations rc=%d count=%zu\n", rc, count);
    assert(rc == 0);

    for (size_t i = 0; i < count; i++) {
        printf("actual_dt range[%zu] base=0x%llx size=0x%llx no_map=%d\n",
               i, (unsigned long long)ranges[i].base,
               (unsigned long long)ranges[i].size, (int)no_map[i]);
    }

    kshim_mmu_config_t cfg = {
        .fdt = blob,
        .framebuffer_base = FB_BASE,
        .framebuffer_size = FB_SIZE,
    };
    int mrc = kshim_mmu_configure(&cfg);
    printf("actual_dt kshim_mmu_configure rc=%d\n", mrc);
    assert(mrc == 0);

    free(raw);
    return 0;
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);

    test_stock_zero_placeholders();
    test_mixed_tuples_one_reg();
    test_all_zero_cap0();
    test_valid_then_zero_cap1();
    test_nonzero_addr_zero_size();
    test_malformed_len15();
    test_nomap_u32();
    test_overflow_reg();
    test_genuine_base0();
    test_genuine_cap0();

    if (argc == 2) {
        printf("running optional actual DT integration: %s\n", argv[1]);
        return run_actual_dt(argv[1]);
    }

    printf("PASS reserved_placeholder_test\n");
    return 0;
}
