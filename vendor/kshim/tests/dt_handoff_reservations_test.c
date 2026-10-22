#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "dt.h"
#include "dt_handoff.h"


static uint64_t rt[8192], pl[8192], out[8192];

static void init(void *f)
{
    assert(fdt_create_empty_tree(f, sizeof(rt)) == 0);
    assert(fdt_setprop_u32(f, 0, "#address-cells", 2) == 0);
    assert(fdt_setprop_u32(f, 0, "#size-cells", 2) == 0);
}

static void reg(void *f, int node, uint64_t base, uint64_t size)
{
    fdt64_t cells[2] = { cpu_to_fdt64(base), cpu_to_fdt64(size) };
    assert(fdt_setprop(f, node, "reg", cells, sizeof(cells)) == 0);
}

static int reserve(void *f, const char *name, uint64_t base, uint64_t size, int nomap)
{
    int p = fdt_subnode_offset(f, 0, "reserved-memory");
    if (p < 0) {
        p = fdt_add_subnode(f, 0, "reserved-memory");
        assert(p >= 0);
        assert(fdt_setprop_u32(f, p, "#address-cells", 2) == 0);
        assert(fdt_setprop_u32(f, p, "#size-cells", 2) == 0);
        assert(fdt_setprop(f, p, "ranges", NULL, 0) == 0);
    }
    p = fdt_subnode_offset(f, 0, "reserved-memory");
    assert(p >= 0);
    int node = fdt_add_subnode(f, p, name);
    assert(node >= 0);
    reg(f, node, base, size);
    if (nomap)
        assert(fdt_setprop(f, node, "no-map", NULL, 0) == 0);
    return node;
}

static void setup(void)
{
    init(rt);
    init(pl);
    int m = fdt_add_subnode(rt, 0, "memory");
    assert(m >= 0);
    assert(fdt_setprop_string(rt, m, "device_type", "memory") == 0);
    reg(rt, m, 0x80000000, 0x40000000);
}

static void test1(void)
{
    setup();
    int src = reserve(rt, "src", 0x90000000, 0x4000, 1);
    assert(fdt_setprop_u32(rt, src, "phandle", 77) == 0);
    reserve(rt, "zeros", 0, 0, 1);
    int rm = fdt_subnode_offset(rt, 0, "reserved-memory");
    int sz = fdt_add_subnode(rt, rm, "sizeonly");
    assert(sz >= 0);
    assert(fdt_setprop_u64(rt, sz, "size", 0x1000) == 0);
    int d = reserve(rt, "disabled", 0x92000000, 0x1000, 1);
    assert(fdt_setprop_string(rt, d, "status", "disabled") == 0);
    assert(fdt_add_mem_rsv(rt, 0x91000000, 0x1000) == 0);
    int ex = reserve(pl, "existing", 0x90001000, 0x1000, 1);
    assert(fdt_setprop_u32(pl, ex, "phandle", 9) == 0);
    assert(fdt_setprop_string(pl, ex, "compatible", "test") == 0);

    struct dt_range ranges[16];
    bool nomap[16];
    size_t n = 0;
    assert(dt_handoff_memory(rt, sizeof(rt), pl, sizeof(pl), out, sizeof(out)) == 0);
    assert(dt_reservations(out, ranges, nomap, 16, &n) == 0);
    assert(n == 4);

    bool m0 = false, m1 = false, m2 = false, m3 = false;
    for (size_t i = 0; i < n; i++) {
        if (ranges[i].base == 0x90001000 && ranges[i].size == 0x1000) m0 = true;
        if (ranges[i].base == 0x90000000 && ranges[i].size == 0x1000) m1 = true;
        if (ranges[i].base == 0x90002000 && ranges[i].size == 0x2000) m2 = true;
        if (ranges[i].base == 0x91000000 && ranges[i].size == 0x1000) m3 = true;
        if (ranges[i].base == 0x90000000 || ranges[i].base == 0x90002000)
            assert(nomap[i] == true);
    }
    assert(m0 && m1 && m2 && m3);

    assert(fdt_num_mem_rsv(out) == 1);
    uint64_t a = 0, s = 0;
    assert(fdt_get_mem_rsv(out, 0, &a, &s) == 0);
    assert(a == 0x91000000 && s == 0x1000);
    int n9 = fdt_node_offset_by_phandle(out, 9);
    assert(n9 >= 0);
    const char *c = fdt_getprop(out, n9, "compatible", NULL);
    assert(c != NULL && strcmp(c, "test") == 0);
    assert(fdt_node_offset_by_phandle(out, 77) < 0);
}

static void test2(void)
{
    setup();
    reserve(rt, "src", 0x90000000, 0x4000, 1);
    reserve(pl, "tgt", 0x90001000, 0x1000, 0);
    assert(dt_handoff_memory(rt, sizeof(rt), pl, sizeof(pl), out, sizeof(out)) < 0);
}

static void test3(void)
{
    setup();
    int src = reserve(rt, "src", 0x91000000, 0x1000, 0);
    assert(fdt_setprop_string(rt, src, "compatible", "src") == 0);
    assert(fdt_setprop_u32(rt, src, "phandle", 55) == 0);
    assert(dt_handoff_memory(rt, sizeof(rt), pl, sizeof(pl), out, sizeof(out)) == 0);
    assert(fdt_num_mem_rsv(out) == 1);
    uint64_t a = 0, s = 0;
    assert(fdt_get_mem_rsv(out, 0, &a, &s) == 0);
    assert(a == 0x91000000 && s == 0x1000);
    assert(fdt_node_offset_by_phandle(out, 55) < 0);
    for (int off = -1; (off = fdt_next_node(out, off, NULL)) >= 0; ) {
        const char *cp = fdt_getprop(out, off, "compatible", NULL);
        assert(cp == NULL || strcmp(cp, "src") != 0);
    }
    int rm = fdt_subnode_offset(out, 0, "reserved-memory");
    if (rm >= 0) {
        for (int ch = fdt_first_subnode(out, rm); ch >= 0; ch = fdt_next_subnode(out, ch))
            assert(fdt_getprop(out, ch, "no-map", NULL) == NULL);
    }
}

static void test4(void)
{
    setup();
    assert(fdt_setprop_u32(rt, 0, "#address-cells", 1) == 0);
    assert(fdt_setprop_u32(rt, 0, "#size-cells", 1) == 0);
    int m = fdt_path_offset(rt, "/memory");
    assert(m >= 0);
    fdt32_t cells[2] = { cpu_to_fdt32(0x40000000), cpu_to_fdt32(0x10000000) };
    assert(fdt_setprop(rt, m, "reg", cells, sizeof(cells)) == 0);
    assert(dt_handoff_memory(rt, sizeof(rt), pl, sizeof(pl), out, sizeof(out)) == 0);
    int mo = fdt_path_offset(out, "/memory");
    assert(mo >= 0);
    int len = 0;
    assert(fdt_getprop(out, mo, "reg", &len) != NULL && len == 16);
    uint64_t a = 0, s = 0;
    assert(dt_raw_reg(out, mo, 0, &a, &s) == 0);
    assert(a == 0x40000000 && s == 0x10000000);
    const fdt32_t *ac = fdt_getprop(out, 0, "#address-cells", NULL);
    const fdt32_t *sc = fdt_getprop(out, 0, "#size-cells", NULL);
    assert(ac != NULL && fdt32_to_cpu(*ac) == 2);
    assert(sc != NULL && fdt32_to_cpu(*sc) == 2);
}

int main(void)
{
    test1();
    test2();
    test3();
    test4();
    printf("dt_handoff_reservations_test PASS\n");
    return 0;
}
