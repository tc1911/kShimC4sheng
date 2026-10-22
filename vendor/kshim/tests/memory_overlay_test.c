#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dt.h"
#include "dt_handoff.h"
#include "dt_simplefb.h"

/*
 * memory_overlay_test.c
 *
 * Tests for the dt_handoff.h API:
 *   int dt_memory_capture(const void *runtime, size_t runtime_size,
 *                         void *saved, size_t capacity);
 *   int dt_memory_apply(const void *saved, size_t saved_size,
 *                       const void *payload, size_t payload_size,
 *                       void *output, size_t capacity,
 *                       void *overlay, size_t overlay_capacity);
 *
 * All trees are synthesized in-process with libfdt; there are no external
 * fixtures and no large stack frames. Buffers are 64 KiB each.
 *
 * The one opt-in exception is the optional
 *   --fdt-pair <runtime.dtb> <payload.dtb>
 * mode, which replays capture/apply over two trusted local blobs using the
 * exact production buffer sizes. The no-argument run never touches files.
 *
 * The second opt-in mode,
 *   --fdt-pair-simplefb <old-accepted-source.dtb> <payload.dtb>
 * runs the same capture/apply replay and then promotes the target's disabled
 * simple-framebuffer template using a descriptor decoded from the source's
 * /chosen node. It writes no DT file; the target output stays in-process.
 */

#define WORDS 8192u
#define BYTES (WORDS * sizeof(uint64_t))

static uint64_t rt[WORDS];    /* runtime input */
static uint64_t pl[WORDS];    /* payload input */
static uint64_t sv[WORDS];    /* saved (capture output / apply input) */
static uint64_t out[WORDS];   /* apply output */
static uint64_t ov[WORDS];    /* overlay workspace (destructively consumed) */
static uint64_t out2[WORDS];  /* second apply output (snapshot reuse check) */

static uint64_t rt_keep[WORDS];
static uint64_t pl_keep[WORDS];
static uint64_t sv_keep[WORDS];

static void keep(uint64_t *dst, const uint64_t *src)
{
    memcpy(dst, src, BYTES);
}

static void same(const char *what, const uint64_t *a, const uint64_t *b)
{
    if (memcmp(a, b, BYTES) != 0) {
        printf("immutability violation: %s\n", what);
        assert(0);
    }
}

static void init(void *f)
{
    assert(fdt_create_empty_tree(f, BYTES) == 0);
    assert(fdt_setprop_u32(f, 0, "#address-cells", 2) == 0);
    assert(fdt_setprop_u32(f, 0, "#size-cells", 2) == 0);
}

static void reg(void *f, int node, uint64_t base, uint64_t size)
{
    fdt64_t cells[2] = { cpu_to_fdt64(base), cpu_to_fdt64(size) };
    assert(fdt_setprop(f, node, "reg", cells, sizeof(cells)) == 0);
}

/* banks is a flat array of (base,size) pairs; nbank pairs. */
static int mem_node(void *f, const char *name, const uint64_t *banks, size_t nbank)
{
    int m = fdt_subnode_offset(f, 0, name);
    if (m < 0)
        m = fdt_add_subnode(f, 0, name);
    assert(m >= 0);
    assert(fdt_setprop_string(f, m, "device_type", "memory") == 0);
    assert(nbank * 2 <= 16);
    fdt64_t cells[16];
    for (size_t i = 0; i < nbank; i++) {
        cells[i * 2 + 0] = cpu_to_fdt64(banks[i * 2 + 0]);
        cells[i * 2 + 1] = cpu_to_fdt64(banks[i * 2 + 1]);
    }
    assert(fdt_setprop(f, m, "reg", cells, nbank * 2 * sizeof(fdt64_t)) == 0);
    return m;
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

static void check_root_22(void *f)
{
    const fdt32_t *ac = fdt_getprop(f, 0, "#address-cells", NULL);
    const fdt32_t *sc = fdt_getprop(f, 0, "#size-cells", NULL);
    assert(ac != NULL && fdt32_ld(ac) == 2);
    assert(sc != NULL && fdt32_ld(sc) == 2);
}

/* ---- valid snapshot independence, overlay consumed ---------------------- */

static void test_snapshot_independence(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);

    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    same("runtime after capture", rt, rt_keep);   /* runtime immutable */

    keep(sv_keep, sv);
    /* Overwrite the runtime input; saved must be fully independent. */
    init(rt);
    same("saved independent of runtime", sv, sv_keep);

    keep(pl_keep, pl);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);
    same("saved after apply", sv, sv_keep);       /* saved immutable */
    same("payload after apply", pl, pl_keep);     /* payload immutable */

    /* output shape */
    check_root_22(out);
    int mo = fdt_path_offset(out, "/memory");
    assert(mo >= 0);
    const char *dt = fdt_getprop(out, mo, "device_type", NULL);
    assert(dt != NULL && strcmp(dt, "memory") == 0);
    uint64_t a = 0, s = 0;
    assert(dt_raw_reg(out, mo, 0, &a, &s) == 0);
    assert(a == 0x80000000ULL && s == 0x40000000ULL);

    /* overlay workspace was destructively consumed */
    assert(fdt_check_header(ov) != 0);
}

/* ---- two RAM banks including one above 4 GiB ---------------------------- */

static void test_banks_over_4g(void)
{
    init(rt);
    init(pl);
    uint64_t banks[4] = {
        0x80000000ULL, 0x80000000ULL,
        0x100000000ULL, 0x10000000ULL,
    };
    mem_node(rt, "memory", banks, 2);

    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    same("runtime", rt, rt_keep);

    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);
    int mo = fdt_path_offset(out, "/memory");
    assert(mo >= 0);
    int len = 0;
    assert(fdt_getprop(out, mo, "reg", &len) != NULL);
    assert(len == 4 * (int)sizeof(fdt64_t));
    uint64_t a = 0, s = 0;
    assert(dt_raw_reg(out, mo, 1, &a, &s) == 0);
    assert(a == 0x100000000ULL && s == 0x10000000ULL);
}

/* ---- optional exact ddr_device_type ------------------------------------ */

static void test_ddr_bytes_exact(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    int m = mem_node(rt, "memory", banks, 1);
    assert(fdt_setprop_u32(rt, m, "ddr_device_type", 0x12345678) == 0);

    keep(sv_keep, sv);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);
    int mo = fdt_path_offset(out, "/memory");
    assert(mo >= 0);
    const fdt32_t *d = fdt_getprop(out, mo, "ddr_device_type", NULL);
    assert(d != NULL && fdt32_ld(d) == 0x12345678);
}

static void test_no_ddr_is_valid(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x20000000ULL };
    mem_node(rt, "memory", banks, 1);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);
    int mo = fdt_path_offset(out, "/memory");
    assert(mo >= 0);
    assert(fdt_getprop(out, mo, "ddr_device_type", NULL) == NULL);
}

/* ---- 1/1 source cells normalized to root 2/2 ---------------------------- */

static void test_source_1_1_normalization(void)
{
    assert(fdt_create_empty_tree(rt, BYTES) == 0);
    assert(fdt_setprop_u32(rt, 0, "#address-cells", 1) == 0);
    assert(fdt_setprop_u32(rt, 0, "#size-cells", 1) == 0);
    int m = fdt_add_subnode(rt, 0, "memory");
    assert(m >= 0);
    assert(fdt_setprop_string(rt, m, "device_type", "memory") == 0);
    fdt32_t cells[2] = { cpu_to_fdt32(0x40000000), cpu_to_fdt32(0x10000000) };
    assert(fdt_setprop(rt, m, "reg", cells, sizeof(cells)) == 0);

    init(pl);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);

    check_root_22(out);
    int mo = fdt_path_offset(out, "/memory");
    assert(mo >= 0);
    int len = 0;
    assert(fdt_getprop(out, mo, "reg", &len) != NULL && len == 16);
    uint64_t a = 0, s = 0;
    assert(dt_raw_reg(out, mo, 0, &a, &s) == 0);
    assert(a == 0x40000000ULL && s == 0x10000000ULL);
}

/* ---- payload phandle 22 stays, source phandle 22 is never copied -------- */

static void test_payload_phandle_and_source_dropped(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);
    /* unrelated source carveout re-uses phandle value 22; must not leak */
    int src = reserve(rt, "src", 0x90000000ULL, 0x4000ULL, 1);
    assert(fdt_setprop_u32(rt, src, "phandle", 22) == 0);

    int pr = reserve(pl, "tgt", 0x90000000ULL, 0x4000ULL, 1);
    assert(fdt_setprop_u32(pl, pr, "phandle", 22) == 0);
    int soc = fdt_add_subnode(pl, 0, "soc");
    assert(soc >= 0);
    int dev = fdt_add_subnode(pl, soc, "device");
    assert(dev >= 0);
    assert(fdt_setprop_u32(pl, dev, "memory-region", 22) == 0);

    keep(pl_keep, pl);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);
    same("payload intact", pl, pl_keep);

    /* exactly one node carries phandle 22, and it is the payload's */
    int n22 = fdt_node_offset_by_phandle(out, 22);
    assert(n22 >= 0);
    int odev = fdt_path_offset(out, "/soc/device");
    assert(odev >= 0);
    const fdt32_t *mr = fdt_getprop(out, odev, "memory-region", NULL);
    assert(mr != NULL && fdt32_ld(mr) == 22);
}

/* ---- map reservations preserved, source no-map added -------------------- */

static void test_reservations_preserved_and_nomap_added(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);
    reserve(rt, "map", 0x91000000ULL, 0x1000ULL, 0);      /* mapped */
    reserve(rt, "nomap", 0x92000000ULL, 0x1000ULL, 1);    /* no-map */
    assert(fdt_add_mem_rsv(rt, 0x93000000ULL, 0x1000ULL) == 0);

    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    same("runtime", rt, rt_keep);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);

    /* reservation table (fdt_add_mem_rsv path) must carry both mapped ones */
    int have_910 = 0, have_930 = 0;
    int nrsv = fdt_num_mem_rsv(out);
    assert(nrsv >= 0);
    for (int i = 0; i < nrsv; i++) {
        uint64_t ra = 0, rs = 0;
        assert(fdt_get_mem_rsv(out, i, &ra, &rs) == 0);
        if (ra == 0x91000000ULL && rs == 0x1000ULL)
            have_910 = 1;
        if (ra == 0x93000000ULL && rs == 0x1000ULL)
            have_930 = 1;
    }
    assert(have_910 && have_930);

    /* source no-map region must appear as a reserved-memory child with no-map */
    int rm = fdt_subnode_offset(out, 0, "reserved-memory");
    assert(rm >= 0);
    int found = 0;
    for (int ch = fdt_first_subnode(out, rm); ch >= 0; ch = fdt_next_subnode(out, ch)) {
        uint64_t a = 0, s = 0;
        if (dt_raw_reg(out, ch, 0, &a, &s) == 0 && a == 0x92000000ULL && s == 0x1000ULL) {
            assert(fdt_getprop(out, ch, "no-map", NULL) != NULL);
            found = 1;
        }
    }
    assert(found);
}

/* ---- target reusable vs source no-map overlap rejected ------------------ */

static void test_conflict_rejected_inputs_immutable(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);
    reserve(rt, "nm", 0x90000000ULL, 0x4000ULL, 1);      /* source no-map */
    int reuse = reserve(pl, "reuse", 0x90001000ULL, 0x1000ULL, 0);
    assert(fdt_setprop(pl, reuse, "reusable", NULL, 0) == 0); /* target reusable overlap */

    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    same("runtime after capture", rt, rt_keep);

    keep(sv_keep, sv);
    keep(pl_keep, pl);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) < 0);
    same("saved on failure", sv, sv_keep);
    same("payload on failure", pl, pl_keep);
    same("runtime on failure", rt, rt_keep);
}

/* ---- invalid source reg rejection -------------------------------------- */

static void test_zero_and_overflow_reg_rejected(void)
{
    /* zero size */
    init(rt);
    int m = fdt_add_subnode(rt, 0, "memory");
    assert(m >= 0);
    assert(fdt_setprop_string(rt, m, "device_type", "memory") == 0);
    reg(rt, m, 0, 0);
    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) < 0);
    same("runtime zero-size", rt, rt_keep);

    /* overflowing base+size */
    init(rt);
    m = fdt_add_subnode(rt, 0, "memory");
    assert(m >= 0);
    assert(fdt_setprop_string(rt, m, "device_type", "memory") == 0);
    reg(rt, m, 0xFFFFFFFFFFFFF000ULL, 0x4000ULL);
    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) < 0);
    same("runtime overflow", rt, rt_keep);
}

/* ---- no /memory at all -------------------------------------------------- */

static void test_no_memory(void)
{
    init(rt);
    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) < 0);
    same("runtime no-memory", rt, rt_keep);
}

/* ---- conflicting ddr_device_type across memory nodes -------------------- */

static void test_conflicting_ddr_rejected(void)
{
    init(rt);
    uint64_t b0[2] = { 0x80000000ULL, 0x40000000ULL };
    uint64_t b1[2] = { 0x100000000ULL, 0x10000000ULL };
    mem_node(rt, "memory", b0, 1);
    mem_node(rt, "memory1", b1, 1);
    /* Node offsets shift after sibling insertion; refresh before each set. */
    int m0 = fdt_path_offset(rt, "/memory");
    assert(m0 >= 0);
    assert(fdt_setprop_u32(rt, m0, "ddr_device_type", 1) == 0);
    int m1 = fdt_path_offset(rt, "/memory1");
    assert(m1 >= 0);
    assert(fdt_setprop_u32(rt, m1, "ddr_device_type", 2) == 0);
    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) < 0);
    same("runtime conflicting ddr", rt, rt_keep);
}

/* ---- tiny buffers ------------------------------------------------------- */

static void test_tiny_buffers(void)
{
    init(rt);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);
    init(pl);

    assert(dt_memory_capture(rt, BYTES, sv, 0) < 0);
    assert(dt_memory_capture(rt, BYTES, sv, 16) < 0);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);

    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, 0, ov, BYTES) < 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, 16, ov, BYTES) < 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, 0) < 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, 16) < 0);
}

/* ---- output/input/overlay aliasing rejection ---------------------------- */

static void test_alias_rejected(void)
{
    init(rt);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);
    init(pl);

    /* saved aliases runtime */
    assert(dt_memory_capture(rt, BYTES, rt, BYTES) < 0);

    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);

    /* output aliases saved */
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, sv, BYTES, ov, BYTES) < 0);
    /* output aliases payload */
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, pl, BYTES, ov, BYTES) < 0);
    /* overlay aliases output */
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, out, BYTES) < 0);
    /* overlay aliases saved */
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, sv, BYTES) < 0);
}

/* ---- ddr regions: source copied, stale target region dropped ----------- */

static void test_ddr_regions(void)
{
    init(rt);
    init(pl);

    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    int m = mem_node(rt, "memory", banks, 1);
    assert(fdt_setprop_u32(rt, m, "ddr_device_type", 0x12345678) == 0);

    /* source /ddr-regions: region0 and region2 are direct 16-byte PROPERTIES */
    int dr = fdt_add_subnode(rt, 0, "ddr-regions");
    assert(dr >= 0);
    fdt64_t s0reg[2] = { cpu_to_fdt64(0x1111111122220000ULL),
                         cpu_to_fdt64(0x1000ULL) };
    assert(fdt_setprop(rt, dr, "region0", s0reg, sizeof s0reg) == 0);
    fdt64_t s2reg[2] = { cpu_to_fdt64(0x3333333344440000ULL),
                         cpu_to_fdt64(0x2000ULL) };
    assert(fdt_setprop(rt, dr, "region2", s2reg, sizeof s2reg) == 0);
    assert(fdt_setprop_u32(rt, dr, "phandle", 55) == 0);
    assert(fdt_setprop_string(rt, dr, "compatible", "vendor") == 0);

    /* target: stale region9 PROPERTY plus phandle 66 on the SAME target node */
    int tdr = fdt_add_subnode(pl, 0, "ddr-regions");
    assert(tdr >= 0);
    fdt64_t t9reg[2] = { cpu_to_fdt64(0x9999999999990000ULL),
                         cpu_to_fdt64(0x9000ULL) };
    assert(fdt_setprop(pl, tdr, "region9", t9reg, sizeof t9reg) == 0);
    assert(fdt_setprop_u32(pl, tdr, "phandle", 66) == 0);

    /* target /chosen and model preserved by the generic apply */
    int chosen = fdt_path_offset(pl, "/chosen");
    if (chosen < 0)
        chosen = fdt_add_subnode(pl, 0, "chosen");
    assert(chosen >= 0);
    assert(fdt_setprop_string(pl, chosen, "bootargs", "payload") == 0);
    assert(fdt_setprop_u32(pl, chosen, "linux,initrd-start", 0xdead0000) == 0);
    assert(fdt_setprop_u32(pl, chosen, "linux,initrd-end", 0xdead1000) == 0);
    assert(fdt_setprop_string(pl, 0, "model", "target") == 0);

    keep(rt_keep, rt);
    keep(pl_keep, pl);

    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    same("runtime after capture", rt, rt_keep);

    keep(sv_keep, sv);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);
    same("saved after apply", sv, sv_keep);
    same("payload after apply", pl, pl_keep);

    /* snapshot is reusable: a second apply must also succeed */
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out2, BYTES, ov, BYTES) == 0);
    same("saved after second apply", sv, sv_keep);

    int odr = fdt_path_offset(out, "/ddr-regions");
    assert(odr >= 0);
    assert(fdt_getprop(out, odr, "region9", NULL) == NULL);

    int l0 = 0, l2 = 0;
    const void *p0 = fdt_getprop(out, odr, "region0", &l0);
    const void *p2 = fdt_getprop(out, odr, "region2", &l2);
    assert(p0 != NULL && l0 == 16);
    assert(p2 != NULL && l2 == 16);
    assert(memcmp(p0, s0reg, 16) == 0);
    assert(memcmp(p2, s2reg, 16) == 0);

    /* source-only phandle 55 and compatible are not propagated */
    assert(fdt_node_offset_by_phandle(out, 55) < 0);
    assert(fdt_getprop(out, odr, "compatible", NULL) == NULL);

    /* target phandle 66 on the same target ddr-regions node survives */
    const fdt32_t *oph = fdt_getprop(out, odr, "phandle", NULL);
    assert(oph != NULL && fdt32_ld(oph) == 66);

    /* /chosen bootargs, initrd bounds and model intact */
    int oc = fdt_path_offset(out, "/chosen");
    assert(oc >= 0);
    const char *ba = fdt_getprop(out, oc, "bootargs", NULL);
    assert(ba != NULL && strcmp(ba, "payload") == 0);
    const fdt32_t *is = fdt_getprop(out, oc, "linux,initrd-start", NULL);
    const fdt32_t *ie = fdt_getprop(out, oc, "linux,initrd-end", NULL);
    assert(is != NULL && fdt32_ld(is) == 0xdead0000);
    assert(ie != NULL && fdt32_ld(ie) == 0xdead1000);
    const char *md = fdt_getprop(out, 0, "model", NULL);
    assert(md != NULL && strcmp(md, "target") == 0);
}

/* ---- invalid snapshot inputs ------------------------------------------- */

static void test_invalid_snapshot_inputs(void)
{
    /* malformed ddr_device_type (3 bytes) */
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    int m = mem_node(rt, "memory", banks, 1);
    uint8_t bad[3] = { 0x01, 0x02, 0x03 };
    assert(fdt_setprop(rt, m, "ddr_device_type", bad, sizeof bad) == 0);
    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) < 0);
    same("runtime malformed ddr", rt, rt_keep);

    /* malformed region0: 3-byte PROPERTY directly on /ddr-regions, no child */
    init(rt);
    init(pl);
    mem_node(rt, "memory", banks, 1);
    int dr = fdt_add_subnode(rt, 0, "ddr-regions");
    assert(dr >= 0);
    uint8_t three[3] = { 0x00, 0x00, 0x00 };
    assert(fdt_setprop(rt, dr, "region0", three, sizeof three) == 0);
    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) < 0);
    same("runtime region0 len3", rt, rt_keep);

    /* overlapping RAM banks in /memory */
    init(rt);
    init(pl);
    uint64_t obank[4] = { 0x80000000ULL, 0x40000000ULL,
                          0x90000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", obank, 2);
    keep(rt_keep, rt);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) < 0);
    same("runtime overlapping banks", rt, rt_keep);
}

/* ---- disabled target reserved-memory rejects a no-map gap -------------- */

static void test_disabled_target_reserved_memory_rejected(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);
    reserve(rt, "nm", 0x90000000ULL, 0x4000ULL, 1);  /* source no-map gap */

    int trm = fdt_add_subnode(pl, 0, "reserved-memory");
    assert(trm >= 0);
    assert(fdt_setprop_u32(pl, trm, "#address-cells", 2) == 0);
    assert(fdt_setprop_u32(pl, trm, "#size-cells", 2) == 0);
    assert(fdt_setprop(pl, trm, "ranges", NULL, 0) == 0);
    assert(fdt_setprop_string(pl, trm, "status", "disabled") == 0);

    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) < 0);
}

/* ---- target child name collision rejects the no-map gap ---------------- */

static void test_target_gap_child_collision_rejected(void)
{
    init(rt);
    init(pl);
    uint64_t banks[2] = { 0x80000000ULL, 0x40000000ULL };
    mem_node(rt, "memory", banks, 1);
    reserve(rt, "nm", 0x90000000ULL, 0x4000ULL, 1);  /* source no-map gap */

    int trm = fdt_add_subnode(pl, 0, "reserved-memory");
    assert(trm >= 0);
    assert(fdt_setprop_u32(pl, trm, "#address-cells", 2) == 0);
    assert(fdt_setprop_u32(pl, trm, "#size-cells", 2) == 0);
    assert(fdt_setprop(pl, trm, "ranges", NULL, 0) == 0);
    int col = fdt_add_subnode(pl, trm, "kshim-firmware@90000000");
    assert(col >= 0);
    reg(pl, col, 0x90000000ULL, 0x4000ULL);
    assert(fdt_setprop_string(pl, col, "status", "disabled") == 0);

    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == -FDT_ERR_EXISTS);
}

/* ---- zero-size placeholder banks are skipped ---------------------------- */

static void test_zero_placeholders_skipped(void)
{
    init(rt);
    init(pl);
    uint64_t banks[10] = {
        0xd8800000ULL, 0x0ULL,
        0x80000000ULL, 0x40000000ULL,
        0xc0000000ULL, 0x0ULL,
        0x100000000ULL, 0x10000000ULL,
        0xe0000000ULL, 0x0ULL,
    };
    int m = mem_node(rt, "memory", banks, 5);
    assert(fdt_setprop_u32(rt, m, "ddr_device_type", 0x12345678) == 0);

    keep(rt_keep, rt);
    keep(pl_keep, pl);
    assert(dt_memory_capture(rt, BYTES, sv, BYTES) == 0);
    same("runtime after capture", rt, rt_keep);

    keep(sv_keep, sv);
    assert(dt_memory_apply(sv, BYTES, pl, BYTES, out, BYTES, ov, BYTES) == 0);
    same("saved after apply", sv, sv_keep);
    same("payload after apply", pl, pl_keep);

    int mo = fdt_path_offset(out, "/memory");
    assert(mo >= 0);
    int len = 0;
    assert(fdt_getprop(out, mo, "reg", &len) != NULL);
    assert(len == 4 * (int)sizeof(fdt64_t));   /* exactly two banks, 32 bytes */

    uint64_t a = 0, s = 0;
    assert(dt_raw_reg(out, mo, 0, &a, &s) == 0);
    assert(a == 0x80000000ULL && s == 0x40000000ULL);
    assert(dt_raw_reg(out, mo, 1, &a, &s) == 0);
    assert(a == 0x100000000ULL && s == 0x10000000ULL);

    const fdt32_t *d = fdt_getprop(out, mo, "ddr_device_type", NULL);
    assert(d != NULL && fdt32_ld(d) == 0x12345678);
}

/* ---- optional real FDT pair mode ---------------------------------------- */

/*
 * Production-sized workspaces, exactly as the device path sizes them, so a
 * pass here can never be an artifact of a roomier test-only capacity. The
 * three arrays are uint64_t, hence 8-byte aligned, and static, so no large
 * stack frames are created. Capture/apply must not touch the two blobs; each
 * blob is copied and compared after the calls.
 */
#define PAIR_SNAPSHOT_BYTES DT_MEMORY_SNAPSHOT_BYTES           /* 32 KiB */
#define PAIR_OUTPUT_BYTES   (256u * 1024u)
#define PAIR_OVERLAY_BYTES  DT_MEMORY_OVERLAY_BYTES            /* 64 KiB */

static uint64_t pair_snapshot[PAIR_SNAPSHOT_BYTES / sizeof(uint64_t)];
static uint64_t pair_output[PAIR_OUTPUT_BYTES / sizeof(uint64_t)];
static uint64_t pair_overlay[PAIR_OVERLAY_BYTES / sizeof(uint64_t)];

static int read_blob(const char *path, void **blob, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *buf;

    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "cannot size %s\n", path);
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)size);
    if (buf == NULL) {
        fprintf(stderr, "out of memory for %s\n", path);
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "short read on %s\n", path);
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    *blob = buf;
    *len = (size_t)size;
    return 0;
}

/*
 * Build the simplefb descriptor from the old accepted source tree. The FDT
 * reg property supplies a 64-bit physical address and a 64-bit visible span;
 * both are decoded from the four big-endian cells without unaligned casts.
 * This mode models the visible-span bound recorded by the old accepted FDT,
 * not the current raw ABL framebuffer allocation, so buffer_size is only a
 * known lower-bound backing and not a claim about the live reservation.
 */
static int simplefb_source_descriptor(const void *source, size_t len,
                                      kshim_framebuffer_config_t *fb)
{
    int chosen, node, found = -1, reg_len = 0;
    uint32_t ac, sc, width, height, stride;
    const fdt32_t *reg;
    const char *format;
    uint64_t physical, span;

    if (dt_validate(source, len) != 0)
        return -1;
    chosen = fdt_path_offset(source, "/chosen");
    if (chosen < 0)
        return -1;
    if (dt_u32(source, chosen, "#address-cells", &ac) != 0 || ac != 2)
        return -1;
    if (dt_u32(source, chosen, "#size-cells", &sc) != 0 || sc != 2)
        return -1;

    for (node = fdt_first_subnode(source, chosen); node >= 0;
         node = fdt_next_subnode(source, node)) {
        if (fdt_node_check_compatible(source, node, "simple-framebuffer") == 0) {
            if (found >= 0)
                return -1;
            found = node;
        }
    }
    if (found < 0)
        return -1;

    reg = fdt_getprop(source, found, "reg", &reg_len);
    if (reg == NULL || reg_len != 16)
        return -1;
    physical = ((uint64_t)fdt32_ld(reg + 0) << 32) | fdt32_ld(reg + 1);
    span = ((uint64_t)fdt32_ld(reg + 2) << 32) | fdt32_ld(reg + 3);
    if (span == 0 || (uint64_t)(size_t)span != span)
        return -1;

    if (dt_u32(source, found, "width", &width) != 0 || width == 0)
        return -1;
    if (dt_u32(source, found, "height", &height) != 0 || height == 0)
        return -1;
    if (dt_u32(source, found, "stride", &stride) != 0 || stride == 0)
        return -1;
    format = dt_string(source, found, "format");
    if (format == NULL || strcmp(format, "a8r8g8b8") != 0)
        return -1;

    memset(fb, 0, sizeof(*fb));
    fb->render_address = (uintptr_t)physical;
    fb->width = width;
    fb->height = height;
    fb->stride = stride;
    fb->bpp = 32;
    fb->format = KSHIM_FB_FORMAT_ARGB8888;
    fb->buffer_size = (size_t)span;
    return 0;
}

/* The promoted node must keep the descriptor's geometry and physical base. */
static int simplefb_output_ok(const void *blob,
                              const kshim_framebuffer_config_t *fb)
{
    int chosen, node, found = -1, reg_len = 0;
    const fdt32_t *reg;
    const char *status, *format;
    uint32_t width, height, stride;
    uint64_t physical;

    chosen = fdt_path_offset(blob, "/chosen");
    if (chosen < 0)
        return -1;
    for (node = fdt_first_subnode(blob, chosen); node >= 0;
         node = fdt_next_subnode(blob, node)) {
        if (fdt_node_check_compatible(blob, node, "simple-framebuffer") == 0) {
            if (found >= 0)
                return -1;
            found = node;
        }
    }
    if (found < 0)
        return -1;

    status = dt_string(blob, found, "status");
    if (status == NULL || strcmp(status, "okay") != 0)
        return -1;
    if (dt_u32(blob, found, "width", &width) != 0 || width != fb->width)
        return -1;
    if (dt_u32(blob, found, "height", &height) != 0 || height != fb->height)
        return -1;
    if (dt_u32(blob, found, "stride", &stride) != 0 || stride != fb->stride)
        return -1;
    format = dt_string(blob, found, "format");
    if (format == NULL || strcmp(format, "a8r8g8b8") != 0)
        return -1;
    reg = fdt_getprop(blob, found, "reg", &reg_len);
    if (reg == NULL || reg_len != 16)
        return -1;
    physical = ((uint64_t)fdt32_ld(reg + 0) << 32) | fdt32_ld(reg + 1);
    if (physical != (uint64_t)fb->render_address)
        return -1;
    return 0;
}

/*
 * Replays the real device sequence over an accepted runtime tree and a
 * candidate target tree: capture(source) then apply(snapshot, target).
 * Emits the numeric statuses as "c=<capture> a=<apply>" and only the literal
 * marker on success. Returns 0 only when both calls return 0 and neither blob
 * was modified.
 *
 * The simplefb variant additionally promotes the target template with a
 * descriptor decoded from the source tree, then reports sf and the
 * output/pack/validate statuses. The source and target blobs remain immutable;
 * only pair_output is mutated.
 */
static int fdt_pair(const char *runtime_path, const char *payload_path,
                    int with_simplefb)
{
    void *runtime = NULL;
    void *payload = NULL;
    void *runtime_keep = NULL;
    void *payload_keep = NULL;
    size_t runtime_len = 0;
    size_t payload_len = 0;
    int cap = 0;
    int app = 0;
    int sf_ok = 1;
    int status = 1;

    if (read_blob(runtime_path, &runtime, &runtime_len) != 0 ||
        read_blob(payload_path, &payload, &payload_len) != 0) {
        printf("real-fdt-pair FAIL (read)\n");
        goto out;
    }

    if (dt_validate(runtime, runtime_len) != 0 ||
        dt_validate(payload, payload_len) != 0) {
        printf("real-fdt-pair FAIL (validate)\n");
        goto out;
    }

    runtime_keep = malloc(runtime_len);
    payload_keep = malloc(payload_len);
    if (runtime_keep == NULL || payload_keep == NULL) {
        printf("real-fdt-pair FAIL (read)\n");
        goto out;
    }
    memcpy(runtime_keep, runtime, runtime_len);
    memcpy(payload_keep, payload, payload_len);

    cap = dt_memory_capture(runtime, runtime_len, pair_snapshot,
                            sizeof(pair_snapshot));
    if (cap == 0) {
        app = dt_memory_apply(pair_snapshot, sizeof(pair_snapshot),
                              payload, payload_len,
                              pair_output, sizeof(pair_output),
                              pair_overlay, sizeof(pair_overlay));
        printf("real-fdt-pair c=%d a=%d\n", cap, app);
    } else {
        printf("real-fdt-pair c=%d a=not-run\n", cap);
    }

    if (with_simplefb) {
        int sf = -1;

        if (cap != 0) {
            printf("real-fdt-pair-simplefb c=%d a=not-run sf=not-run\n", cap);
            sf_ok = 0;
        } else if (app != 0) {
            printf("real-fdt-pair-simplefb c=%d a=%d sf=not-run\n", cap, app);
            sf_ok = 0;
        } else {
            kshim_framebuffer_config_t fb;

            if (simplefb_source_descriptor(runtime, runtime_len, &fb) == 0)
                sf = dt_simplefb_apply(pair_output, sizeof(pair_output), &fb);
            printf("real-fdt-pair-simplefb c=%d a=%d sf=%d\n", cap, app, sf);

            if (sf == 0) {
                int out_ok = simplefb_output_ok(pair_output, &fb);
                int packed = fdt_pack(pair_output);
                int valid = dt_validate(pair_output, sizeof(pair_output));

                printf("real-fdt-pair-simplefb out=%d pack=%d valid=%d\n",
                       out_ok, packed, valid);
                if (out_ok != 0 || packed != 0 || valid != 0)
                    sf_ok = 0;
            } else {
                sf_ok = 0;
            }
        }
    }

    status = 0;
    if (memcmp(runtime, runtime_keep, runtime_len) != 0) {
        printf("immutability violation: runtime\n");
        status = 1;
    }
    if (memcmp(payload, payload_keep, payload_len) != 0) {
        printf("immutability violation: payload\n");
        status = 1;
    }
    if (cap != 0 || app != 0 || !sf_ok)
        status = 1;

    if (status == 0)
        printf("real-fdt-pair%s PASS\n", with_simplefb ? "-simplefb" : "");

out:
    free(runtime);
    free(payload);
    free(runtime_keep);
    free(payload_keep);
    return status;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (argc == 4 && strcmp(argv[1], "--fdt-pair") == 0)
            return fdt_pair(argv[2], argv[3], 0);
        if (argc == 4 && strcmp(argv[1], "--fdt-pair-simplefb") == 0)
            return fdt_pair(argv[2], argv[3], 1);
        fprintf(stderr,
                "usage: %s [--fdt-pair <runtime.dtb> <payload.dtb>]\n"
                "       %s --fdt-pair-simplefb <old-accepted-source.dtb>"
                " <payload.dtb>\n",
                argv[0], argv[0]);
        return 2;
    }

    test_zero_placeholders_skipped();
    test_snapshot_independence();
    test_banks_over_4g();
    test_ddr_bytes_exact();
    test_no_ddr_is_valid();
    test_source_1_1_normalization();
    test_payload_phandle_and_source_dropped();
    test_reservations_preserved_and_nomap_added();
    test_conflict_rejected_inputs_immutable();
    test_zero_and_overflow_reg_rejected();
    test_no_memory();
    test_conflicting_ddr_rejected();
    test_tiny_buffers();
    test_alias_rejected();
    test_ddr_regions();
    test_invalid_snapshot_inputs();
    test_disabled_target_reserved_memory_rejected();
    test_target_gap_child_collision_rejected();
    printf("memory_overlay_test PASS\n");
    return 0;
}
