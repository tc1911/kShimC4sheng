/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * initrd_test.c
 *
 * Focused host regression tests for kshim_initrd_apply() in src/dt/initrd.c.
 *
 * Every device tree is synthesized in-process with libfdt; there are no
 * external fixtures, no payload bytes are dereferenced, and no big physical
 * ranges are allocated. A 64 KiB static FDT buffer holds the handoff tree, a
 * 64-byte little-endian array stands in for the packed Image header and small
 * MANIFEST_ENTRY objects stand in for the manifest.
 *
 * Semantic rejections must leave the tree byte-identical (full-buffer snapshot
 * taken before the call); the successful path may only grow the tree by
 * adding the two advertised bounds, and must leave header/entry/context inputs
 * untouched.
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "initrd.h"
#include "dt.h"

#define FDT_BYTES (64u * 1024u)
#define FDT_WORDS (FDT_BYTES / sizeof(uint64_t))

#define INITRD_MAGIC 0x644d5241u           /* ARM64 "ARM\x64" */
#define BOOTARGS     "kshim-initrd-marker"

#define PACKED_BASE  0x80000000ULL
#define PAYLOAD_SIZE 0x08000000ULL
#define LINUX_OFF    0x200000ULL
#define LINUX_SIZE   0x200000ULL
#define IMAGE_SIZE   0x400000ULL

#define BLOB1_OFF    0x1000000ULL
#define BLOB1_SIZE   0x18001ULL            /* non page-aligned length */
#define BLOB2_OFF    0x1200000ULL
#define BLOB2_SIZE   0x2345ULL

#define BLOB1_START  (PACKED_BASE + BLOB1_OFF)   /* 0x81000000 */
#define BLOB1_END    (BLOB1_START + BLOB1_SIZE)  /* 0x81018001 */
#define BLOB2_START  (PACKED_BASE + BLOB2_OFF)   /* 0x81200000 */
#define BLOB2_END    (BLOB2_START + BLOB2_SIZE)  /* 0x81202345 */

static uint64_t g_fdt[FDT_WORDS];
static uint64_t g_snap[FDT_WORDS];

static uint8_t g_hdr[64];
static MANIFEST_ENTRY g_store[3];          /* [0] linux, [1] blob1, [2] blob2 */
static const MANIFEST_ENTRY *g_entries[3];
static struct kshim_initrd_context g_ctx;

static uint8_t g_hdr_snap[64];
static MANIFEST_ENTRY g_store_snap[3];
static struct kshim_initrd_context g_ctx_snap;

/* ---- little-endian header helpers --------------------------------------- */

static void write_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}

static void write_le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t page_size_of_kib(unsigned kib)
{
    return kib == 4 ? 4096ULL : kib == 16 ? 16384ULL : 65536ULL;
}

/* Raw Image flags[2:1]: 0 unspecified (64 KiB conservative), 1=4K, 2=16K, 3=64K. */
static uint64_t page_flags(unsigned kib)
{
    switch (kib) {
    case 4:  return 1ULL << 1;
    case 16: return 2ULL << 1;
    case 64: return 3ULL << 1;
    default: return 0;
    }
}

static uint64_t blob1_rounded_end(unsigned kib)
{
    uint64_t page = page_size_of_kib(kib);
    return (BLOB1_END + page - 1) & ~(page - 1);
}

/* ---- fixture ------------------------------------------------------------- */

static void fixture_reset(unsigned kib, uint64_t image_size)
{
    memset(g_hdr, 0, sizeof g_hdr);
    write_le32(g_hdr + 56, INITRD_MAGIC);
    write_le64(g_hdr + 16, image_size);
    write_le64(g_hdr + 24, page_flags(kib));

    memset(g_store, 0, sizeof g_store);
    g_store[0].type = MF_ENTRY_TYPE_LINUX;
    g_store[0].offset = LINUX_OFF;
    g_store[0].size = LINUX_SIZE;
    g_store[1].type = MF_ENTRY_TYPE_BLOB;
    g_store[1].offset = BLOB1_OFF;
    g_store[1].size = BLOB1_SIZE;
    g_store[2].type = MF_ENTRY_TYPE_BLOB;
    g_store[2].offset = BLOB2_OFF;
    g_store[2].size = BLOB2_SIZE;

    g_entries[0] = &g_store[0];
    g_entries[1] = &g_store[1];
    g_entries[2] = &g_store[2];

    g_ctx.entries = g_entries;
    g_ctx.entry_count = 3;
    g_ctx.packed_base = PACKED_BASE;
    g_ctx.packed_payload_size = PAYLOAD_SIZE;
    g_ctx.entry = &g_store[0];
    g_ctx.header = g_hdr;
    g_ctx.runtime.base = 0x82000000ULL;
    g_ctx.runtime.size = 0x400000ULL;
    g_ctx.runtime_fdt.base = 0x82400000ULL;
    g_ctx.runtime_fdt.size = 0x10000ULL;
    g_ctx.payload_fdt.base = 0x82800000ULL;
    g_ctx.payload_fdt.size = 0x10000ULL;
}

/* ---- libfdt tree helpers ------------------------------------------------- */

static void tree_begin(void)
{
    memset(g_fdt, 0, FDT_BYTES);
    assert(fdt_create_empty_tree(g_fdt, FDT_BYTES) == 0);
    assert(fdt_setprop_u32(g_fdt, 0, "#address-cells", 2) == 0);
    assert(fdt_setprop_u32(g_fdt, 0, "#size-cells", 2) == 0);
}

static void put_reg(int node, uint64_t base, uint64_t size, int ac, int sc)
{
    fdt32_t cells[4];
    int k = 0;

    for (int c = ac - 1; c >= 0; --c)
        cells[k++] = cpu_to_fdt32((uint32_t)(base >> (32 * c)));
    for (int c = sc - 1; c >= 0; --c)
        cells[k++] = cpu_to_fdt32((uint32_t)(size >> (32 * c)));
    assert(fdt_setprop(g_fdt, node, "reg", cells, k * (int)sizeof(fdt32_t)) == 0);
}

static int add_mem_node(const char *name, uint64_t base, uint64_t size,
                        int ac, int sc)
{
    int n = fdt_add_subnode(g_fdt, 0, name);

    assert(n >= 0);
    assert(fdt_setprop_string(g_fdt, n, "device_type", "memory") == 0);
    put_reg(n, base, size, ac, sc);
    return n;
}

static void add_default_mem(void)
{
    (void)add_mem_node("memory", PACKED_BASE, 0x10000000ULL, 2, 2);
}

static void add_chosen(int stale)
{
    int c = fdt_add_subnode(g_fdt, 0, "chosen");

    assert(c >= 0);
    assert(fdt_setprop_string(g_fdt, c, "bootargs", BOOTARGS) == 0);
    if (stale) {
        assert(fdt_setprop_u32(g_fdt, c, "linux,initrd-start", 0x1111) == 0);
        assert(fdt_setprop_u32(g_fdt, c, "linux,initrd-end", 0x2222) == 0);
    }
}

static void set_reference_u32(uint32_t index)
{
    int c = fdt_path_offset(g_fdt, "/chosen");

    assert(c >= 0);
    assert(fdt_setprop_u32(g_fdt, c, "kshim,initrd-entry", index) == 0);
}

static void set_reference_raw(const void *data, int len)
{
    int c = fdt_path_offset(g_fdt, "/chosen");

    assert(c >= 0);
    assert(fdt_setprop(g_fdt, c, "kshim,initrd-entry", data, len) == 0);
}

static void reserve_node(const char *name, uint64_t base, uint64_t size,
                         int nomap, int enabled)
{
    int p = fdt_subnode_offset(g_fdt, 0, "reserved-memory");
    int n;

    if (p < 0) {
        p = fdt_add_subnode(g_fdt, 0, "reserved-memory");
        assert(p >= 0);
        assert(fdt_setprop_u32(g_fdt, p, "#address-cells", 2) == 0);
        assert(fdt_setprop_u32(g_fdt, p, "#size-cells", 2) == 0);
        assert(fdt_setprop(g_fdt, p, "ranges", NULL, 0) == 0);
    }
    p = fdt_subnode_offset(g_fdt, 0, "reserved-memory");
    assert(p >= 0);
    n = fdt_add_subnode(g_fdt, p, name);
    assert(n >= 0);
    put_reg(n, base, size, 2, 2);
    if (nomap)
        assert(fdt_setprop(g_fdt, n, "no-map", NULL, 0) == 0);
    if (!enabled)
        assert(fdt_setprop_string(g_fdt, n, "status", "disabled") == 0);
}

/* Default valid tree plus a reference to manifest entry 1. */
static void setup_fixture(unsigned kib)
{
    fixture_reset(kib, IMAGE_SIZE);
    tree_begin();
    add_default_mem();
    add_chosen(0);
    set_reference_u32(1);
}

/* ---- call and assertion helpers ----------------------------------------- */

static int apply_cap(size_t cap)
{
    return kshim_initrd_apply(g_fdt, cap, &g_ctx);
}

/* Every semantic reject must precede the first tree mutation. */
static void expect_reject(void)
{
    memcpy(g_snap, g_fdt, FDT_BYTES);
    assert(apply_cap(FDT_BYTES) != 0);
    assert(memcmp(g_fdt, g_snap, FDT_BYTES) == 0);
}

static void get_initrd(uint64_t *start, uint64_t *end)
{
    int c = fdt_path_offset(g_fdt, "/chosen");
    int len;
    const void *p;
    fdt64_t v;

    assert(c >= 0);
    p = fdt_getprop(g_fdt, c, "linux,initrd-start", &len);
    assert(p && len == 8);
    memcpy(&v, p, sizeof v);
    *start = fdt64_to_cpu(v);
    p = fdt_getprop(g_fdt, c, "linux,initrd-end", &len);
    assert(p && len == 8);
    memcpy(&v, p, sizeof v);
    *end = fdt64_to_cpu(v);
}

static void assert_chosen_extras(uint32_t index)
{
    int c = fdt_path_offset(g_fdt, "/chosen");
    const char *ba;
    const fdt32_t *ref;

    assert(c >= 0);
    ba = fdt_getprop(g_fdt, c, "bootargs", NULL);
    assert(ba && strcmp(ba, BOOTARGS) == 0);
    ref = fdt_getprop(g_fdt, c, "kshim,initrd-entry", NULL);
    assert(ref && fdt32_to_cpu(*ref) == index);
}

static void expect_success(uint64_t start, uint64_t end, uint32_t index)
{
    uint64_t s, e;

    memcpy(g_hdr_snap, g_hdr, sizeof g_hdr);
    memcpy(g_store_snap, g_store, sizeof g_store);
    g_ctx_snap = g_ctx;

    assert(apply_cap(FDT_BYTES) == 0);
    assert(dt_validate(g_fdt, FDT_BYTES) == 0);
    assert(fdt_totalsize(g_fdt) <= (int)FDT_BYTES);

    get_initrd(&s, &e);
    assert(s == start && e == end);
    assert_chosen_extras(index);

    assert(memcmp(g_hdr, g_hdr_snap, sizeof g_hdr) == 0);
    assert(memcmp(g_store, g_store_snap, sizeof g_store) == 0);
    assert(memcmp(&g_ctx, &g_ctx_snap, sizeof g_ctx) == 0);
}

static void assert_initrd_absent(void)
{
    int c = fdt_path_offset(g_fdt, "/chosen");
    int len;

    assert(c >= 0);
    assert(fdt_getprop(g_fdt, c, "linux,initrd-start", &len) == NULL &&
           len == -FDT_ERR_NOTFOUND);
    assert(fdt_getprop(g_fdt, c, "linux,initrd-end", &len) == NULL &&
           len == -FDT_ERR_NOTFOUND);
    assert(strcmp(fdt_getprop(g_fdt, c, "bootargs", NULL), BOOTARGS) == 0);
}

/* ---- group 1: exact valid interval -------------------------------------- */

static void test_valid_exact(void)
{
    setup_fixture(4);
    assert(fdt_pack(g_fdt) == 0);
    expect_success(BLOB1_START, BLOB1_END, 1);
    /* Advertised end is the exact byte end, never the rounded copy end. */
    assert(BLOB1_END != blob1_rounded_end(4));
}

static void test_valid_nonpagealigned(void)
{
    uint64_t start = PACKED_BASE + BLOB1_OFF + 3;

    setup_fixture(4);
    g_store[1].offset = BLOB1_OFF + 3;   /* non page-aligned start */
    expect_success(start, start + BLOB1_SIZE, 1);
    assert((start & 0xFFF) != 0);
}

/* ---- group 2: independent selectors ------------------------------------- */

static void test_independent_selectors(void)
{
    uint64_t s, e;

    setup_fixture(4);
    expect_success(BLOB1_START, BLOB1_END, 1);

    /* Same tree, second selector: previous bounds are replaced. */
    set_reference_u32(2);
    expect_success(BLOB2_START, BLOB2_END, 2);
    get_initrd(&s, &e);
    assert(s == BLOB2_START && e == BLOB2_END);
    assert(s != BLOB1_START);
}

/* ---- group 3: legacy / no-reference and malformed selectors -------------- */

static void test_legacy_no_chosen(void)
{
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    memcpy(g_snap, g_fdt, FDT_BYTES);
    assert(kshim_initrd_apply(g_fdt, FDT_BYTES, NULL) == 0);
    assert(memcmp(g_fdt, g_snap, FDT_BYTES) == 0);
}

static void test_legacy_stale_removed(void)
{
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    add_default_mem();
    add_chosen(1);
    assert(kshim_initrd_apply(g_fdt, FDT_BYTES, NULL) == 0);
    assert_initrd_absent();
}

static void test_legacy_invalid_ctx_not_consulted(void)
{
    fixture_reset(4, IMAGE_SIZE);
    /* Would be rejected if the reference path ever consulted it. */
    g_ctx.header = NULL;
    g_ctx.entries = NULL;
    g_ctx.entry = NULL;
    tree_begin();
    add_default_mem();
    add_chosen(1);
    assert(kshim_initrd_apply(g_fdt, FDT_BYTES, &g_ctx) == 0);
    assert_initrd_absent();
}

static void test_selector_malformed(void)
{
    static const uint8_t two[2] = { 0, 0 };
    static const uint8_t eight[8] = { 0 };

    setup_fixture(4);
    set_reference_raw(NULL, 0);
    expect_reject();

    setup_fixture(4);
    set_reference_raw(two, 2);
    expect_reject();

    setup_fixture(4);
    set_reference_raw(eight, 8);
    expect_reject();

    setup_fixture(4);
    set_reference_u32(3);            /* index == entry_count */
    expect_reject();

    setup_fixture(4);
    g_entries[1] = NULL;             /* null entry */
    expect_reject();
}

/* ---- group 4: BLOB entry rejection -------------------------------------- */

static void test_blob_rejections(void)
{
    setup_fixture(4);
    g_store[1].type = MF_ENTRY_TYPE_DTB;
    expect_reject();

    setup_fixture(4);
    g_store[1].flags = MF_ENTRY_FLAG_EXECUTABLE;
    expect_reject();

    setup_fixture(4);
    g_store[1].flags = MF_ENTRY_FLAG_COPY;
    expect_reject();

    setup_fixture(4);
    g_store[1].size = 0;
    expect_reject();

    setup_fixture(4);
    g_store[1].entry_offset = 1;
    expect_reject();

    setup_fixture(4);
    g_store[1].device_tree_blob_entry = 1;
    expect_reject();

    setup_fixture(4);
    g_store[1].load_address = 1;
    expect_reject();

    setup_fixture(4);
    g_store[1].copy_size_max = 1;
    expect_reject();

    /* source offset past payload */
    setup_fixture(4);
    g_store[1].offset = PAYLOAD_SIZE + 1;
    expect_reject();

    /* source offset+size past payload */
    setup_fixture(4);
    g_store[1].size = PAYLOAD_SIZE;
    expect_reject();

    /* packed_base + payload overflow */
    setup_fixture(4);
    g_ctx.packed_base = UINT64_MAX - 0x10ULL;
    expect_reject();

    /* finite arithmetic: byte range, selected source and image all fit in 64
     * bits, but the page round-up of the BLOB byte end overflows */
    setup_fixture(4);
    g_ctx.packed_base = UINT64_MAX - 0x7FFFFFULL;
    g_ctx.packed_payload_size = 0x7FFFFFULL;
    g_store[1].offset = 0x7FFFFFULL - 0x1000ULL;
    g_store[1].size = 0xFFFULL;
    expect_reject();
}

/* ---- group 5: header / kernel rejection --------------------------------- */

static void test_kernel_rejections(void)
{
    /* selected entry is not a Linux entry */
    setup_fixture(4);
    g_store[0].type = MF_ENTRY_TYPE_BLOB;
    expect_reject();

    /* selected size smaller than a header */
    setup_fixture(4);
    g_store[0].size = 63;
    expect_reject();

    /* missing header */
    setup_fixture(4);
    g_ctx.header = NULL;
    expect_reject();

    /* bad Image magic */
    setup_fixture(4);
    write_le32(g_hdr + 56, 0x12345678u);
    expect_reject();

    /* zero image_size */
    setup_fixture(4);
    write_le64(g_hdr + 16, 0);
    expect_reject();

    /* COPY final address overflow */
    setup_fixture(4);
    g_store[0].flags = MF_ENTRY_FLAG_COPY;
    g_store[0].load_address = UINT64_MAX - 0x100ULL;
    write_le64(g_hdr + 16, IMAGE_SIZE);
    expect_reject();

    /* image_size reaching past the file and into the initrd interval */
    setup_fixture(4);
    write_le64(g_hdr + 16, 0x1000000ULL);
    expect_reject();

    /* COPY destination overlapping initrd although the packed source is elsewhere */
    setup_fixture(4);
    g_store[0].flags = MF_ENTRY_FLAG_COPY;
    g_store[0].load_address = BLOB1_START;
    expect_reject();

    /* packed source byte-overlap while the COPY kernel itself is safe */
    setup_fixture(4);
    g_store[0].flags = MF_ENTRY_FLAG_COPY;
    g_store[0].load_address = 0x84000000ULL;
    g_store[0].offset = BLOB1_OFF;   /* source range collides with the window */
    expect_reject();
}

static void test_copy_safe_success(void)
{
    setup_fixture(4);
    g_store[0].flags = MF_ENTRY_FLAG_COPY;
    g_store[0].load_address = 0x84000000ULL;
    expect_success(BLOB1_START, BLOB1_END, 1);
}

/* ---- group 6: page-rounded safety window -------------------------------- */

static void test_page_safety(void)
{
    static const unsigned kib[] = { 4, 16, 64, 0 };
    size_t i;

    for (i = 0; i < sizeof kib / sizeof kib[0]; ++i) {
        /* live span immediately after the byte end, inside the rounded end */
        setup_fixture(kib[i]);
        g_ctx.runtime.base = BLOB1_END;
        g_ctx.runtime.size = 0x1000ULL;
        expect_reject();

        /* exactly at the rounded end -> safe */
        setup_fixture(kib[i]);
        g_ctx.runtime.base = blob1_rounded_end(kib[i]);
        g_ctx.runtime.size = 0x1000ULL;
        expect_success(BLOB1_START, BLOB1_END, 1);
    }
}

static void test_span_variants(void)
{
    /* live span before the byte start, same page: byte-disjoint but window-live */
    setup_fixture(4);
    g_store[1].offset = BLOB1_OFF + 3;
    g_ctx.runtime.base = PACKED_BASE + BLOB1_OFF;
    g_ctx.runtime.size = 1;
    expect_reject();

    /* every protected span type just past the byte end -> reject */
    setup_fixture(4);
    g_ctx.runtime.base = BLOB1_END;
    g_ctx.runtime.size = 0x1000ULL;
    expect_reject();

    setup_fixture(4);
    g_ctx.runtime_fdt.base = BLOB1_END;
    g_ctx.runtime_fdt.size = 0x1000ULL;
    expect_reject();

    setup_fixture(4);
    g_ctx.payload_fdt.base = BLOB1_END;
    g_ctx.payload_fdt.size = 0x1000ULL;
    expect_reject();

    /* zero-size span is optional and accepted */
    setup_fixture(4);
    g_ctx.runtime.base = BLOB1_END;
    g_ctx.runtime.size = 0;
    expect_success(BLOB1_START, BLOB1_END, 1);

    /* overflowing non-empty span -> reject */
    setup_fixture(4);
    g_ctx.runtime.base = UINT64_MAX - 0x10ULL;
    g_ctx.runtime.size = 0x100ULL;
    expect_reject();
}

static void test_handoff_buffer_overlap(void)
{
    uint64_t ptr = (uint64_t)(uintptr_t)g_fdt;

    fixture_reset(4, IMAGE_SIZE);
    g_ctx.packed_base = ptr;
    g_ctx.packed_payload_size = 0x800000ULL;
    g_store[0].offset = 0x100000ULL;   /* kernel source away from the buffer */
    g_store[0].size = 0x1000ULL;
    g_store[1].offset = 0x1000ULL;     /* blob lands on the handoff buffer */
    g_store[1].size = 0x2000ULL;

    tree_begin();
    /* 64-bit RAM covering the host buffer address. */
    (void)add_mem_node("memory", ptr & ~0xFFFFFULL, 0x1000000ULL, 2, 2);
    add_chosen(0);
    set_reference_u32(1);
    expect_reject();
}

/* ---- group 7: usable RAM coverage --------------------------------------- */

static void test_ram_banks(void)
{
    /* fully inside one bank */
    setup_fixture(4);
    expect_success(BLOB1_START, BLOB1_END, 1);

    /* adjacent banks together covering the window */
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    (void)add_mem_node("memory@80000000", 0x80000000ULL, 0x01001000ULL, 2, 2);
    (void)add_mem_node("memory@81001000", 0x81001000ULL, 0x1000000ULL, 2, 2);
    add_chosen(0);
    set_reference_u32(1);
    expect_success(BLOB1_START, BLOB1_END, 1);

    /* overlapping banks */
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    (void)add_mem_node("memory@80000000", 0x80000000ULL, 0x01001000ULL, 2, 2);
    (void)add_mem_node("memory@81000800", 0x81000800ULL, 0x1000000ULL, 2, 2);
    add_chosen(0);
    set_reference_u32(1);
    expect_success(BLOB1_START, BLOB1_END, 1);

    /* a hole between two banks -> reject */
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    (void)add_mem_node("memory@80000000", 0x80000000ULL, 0x01001000ULL, 2, 2);
    (void)add_mem_node("memory@81002000", 0x81002000ULL, 0x1000000ULL, 2, 2);
    add_chosen(0);
    set_reference_u32(1);
    expect_reject();
}

static void test_ram_disabled_bank(void)
{
    int n;

    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    n = add_mem_node("memory", PACKED_BASE, 0x10000000ULL, 2, 2);
    assert(fdt_setprop_string(g_fdt, n, "status", "disabled") == 0);
    add_chosen(0);
    set_reference_u32(1);
    expect_reject();
}

static void test_ram_malformed_tuple(void)
{
    static const uint8_t reg12[12] = { 0 };
    int n;

    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    n = fdt_add_subnode(g_fdt, 0, "memory");
    assert(n >= 0);
    assert(fdt_setprop_string(g_fdt, n, "device_type", "memory") == 0);
    assert(fdt_setprop(g_fdt, n, "reg", reg12, (int)sizeof reg12) == 0);
    add_chosen(0);
    set_reference_u32(1);
    expect_reject();
}

static void test_ram_one_cell(void)
{
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    assert(fdt_setprop_u32(g_fdt, 0, "#address-cells", 1) == 0);
    assert(fdt_setprop_u32(g_fdt, 0, "#size-cells", 1) == 0);
    (void)add_mem_node("memory", 0x80000000ULL, 0x10000000ULL, 1, 1);
    add_chosen(0);
    set_reference_u32(1);
    expect_success(BLOB1_START, BLOB1_END, 1);
}

/* ---- group 8: firmware and reserved-memory reservations ------------------ */

static void test_reservations(void)
{
    /* header memory reservation overlapping the window */
    setup_fixture(4);
    assert(fdt_add_mem_rsv(g_fdt, BLOB1_START, 0x1000ULL) == 0);
    expect_reject();

    /* enabled reserved-memory node with no-map */
    setup_fixture(4);
    reserve_node("rm", BLOB1_START, 0x1000ULL, 1, 1);
    expect_reject();

    /* enabled reserved-memory node without no-map */
    setup_fixture(4);
    reserve_node("rm", BLOB1_START, 0x1000ULL, 0, 1);
    expect_reject();

    /* byte-disjoint but page-overlapping reservation */
    setup_fixture(4);
    reserve_node("rm", BLOB1_END, 0x10ULL, 0, 1);
    expect_reject();

    /* disabled reservation node is ignored */
    setup_fixture(4);
    reserve_node("rm", BLOB1_START, 0x1000ULL, 0, 0);
    expect_success(BLOB1_START, BLOB1_END, 1);
}

/* ---- group 9: ARM64 copy window ----------------------------------------- */

static void window_fixture(uint64_t load_addr, uint64_t ram_size)
{
    fixture_reset(4, IMAGE_SIZE);
    g_store[0].flags = MF_ENTRY_FLAG_COPY;
    g_store[0].load_address = load_addr;
    tree_begin();
    (void)add_mem_node("memory", PACKED_BASE, ram_size, 2, 2);
    add_chosen(0);
    set_reference_u32(1);
}

static void test_arm64_window(void)
{
    /* kernel more than 32 GiB away: rejected although byte/page ranges are disjoint */
    window_fixture(BLOB1_START + 0x900000000ULL, 0x10000000ULL);
    expect_reject();

    /* exactly 32 GiB from the aligned window base -> accepted */
    window_fixture(0x880000000ULL - IMAGE_SIZE, 0x900000000ULL);
    expect_success(BLOB1_START, BLOB1_END, 1);

    /* one byte beyond the 32 GiB span -> rejected */
    window_fixture(0x880000001ULL - IMAGE_SIZE, 0x900000000ULL);
    expect_reject();
}

/* ---- group 10: mutation capacity error ---------------------------------- */

static void test_pack_capacity(void)
{
    int cap;

    /* The two advertised u64 properties cannot fit a packed tree: nonzero,
     * and the (disposable) output carries no byte-identity guarantee. */
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    add_default_mem();
    add_chosen(0);
    set_reference_u32(1);
    assert(fdt_pack(g_fdt) == 0);
    cap = fdt_totalsize(g_fdt);
    assert(kshim_initrd_apply(g_fdt, (size_t)cap, &g_ctx) != 0);

    /* A packed tree without a reference still deletes in place. */
    fixture_reset(4, IMAGE_SIZE);
    tree_begin();
    add_default_mem();
    add_chosen(1);
    assert(fdt_pack(g_fdt) == 0);
    cap = fdt_totalsize(g_fdt);
    assert(kshim_initrd_apply(g_fdt, (size_t)cap, &g_ctx) == 0);
    assert_initrd_absent();
}

/* ---- group 11: actual packed-layout replay (tests only) ------------------ */

/*
 * Replay the byte intervals observed in a real packed image through the
 * production kshim_initrd_apply(). The actual Linux entry maps to fixture
 * entry 0 and the actual shared selected BLOB (real BLOB7) maps to fixture
 * entry 1; those two source intervals, the raw Image header flags/image_size
 * (header+24 / header+16), the selected payload DT source and the manifest
 * payload size are actual. Every other context range (runtime, runtime_fdt,
 * handoff buffer) keeps the synthetic fixture address and is not claimed to
 * be an actual physical address.
 *
 * This mode replays byte intervals on synthetic RAM only: it is neither a raw
 * current-ABL capture nor a hardware validation, and the full selector-7
 * decision is validated separately by the real packer.
 */
static int layout_replay(const uint64_t v[9])
{
    uint64_t blob_start = PACKED_BASE + v[4];
    uint64_t blob_end = blob_start + v[5];
    uint64_t dt_base = PACKED_BASE + v[6];
    uint64_t dt_end = dt_base + v[7];
    uint64_t s, e;
    int rc;

    fixture_reset(4, v[3]);           /* raw Image image_size, then raw flags */
    write_le64(g_hdr + 24, v[0]);
    g_store[0].offset = v[1];         /* actual Linux source interval */
    g_store[0].size = v[2];
    g_store[1].offset = v[4];         /* actual selected BLOB source interval */
    g_store[1].size = v[5];
    g_ctx.packed_payload_size = v[8]; /* manifest offset from the packed base */
    g_ctx.payload_fdt.base = dt_base;
    g_ctx.payload_fdt.size = v[7];

    tree_begin();
    add_default_mem();
    add_chosen(0);
    set_reference_u32(1);

    printf("layout flags=0x%" PRIx64 " blob=[0x%" PRIx64 ",0x%" PRIx64
           ") selected_dt=[0x%" PRIx64 ",0x%" PRIx64 ")\n",
           v[0], blob_start, blob_end, dt_base, dt_end);

    memcpy(g_snap, g_fdt, FDT_BYTES);
    memcpy(g_hdr_snap, g_hdr, sizeof g_hdr);
    memcpy(g_store_snap, g_store, sizeof g_store);
    g_ctx_snap = g_ctx;

    rc = apply_cap(FDT_BYTES);
    printf("layout rc=%d\n", rc);

    if (rc != 0) {
        /* Old packed layouts stay red: the production guard is not removed. */
        if (memcmp(g_fdt, g_snap, FDT_BYTES) != 0) {
            printf("layout FAIL: handoff tree mutated on rejection\n");
            return 1;
        }
        printf("layout REJECT: packed layout is not usable; guard kept\n");
        return 1;
    }

    if (dt_validate(g_fdt, FDT_BYTES) != 0 ||
        fdt_totalsize(g_fdt) > (int)FDT_BYTES) {
        printf("layout FAIL: invalid handoff tree\n");
        return 1;
    }
    get_initrd(&s, &e);
    if (s != blob_start || e != blob_end) {
        printf("layout FAIL: advertised [0x%" PRIx64 ",0x%" PRIx64
               ") != blob [0x%" PRIx64 ",0x%" PRIx64 ")\n",
               s, e, blob_start, blob_end);
        return 1;
    }
    assert_chosen_extras(1);
    if (memcmp(g_hdr, g_hdr_snap, sizeof g_hdr) != 0 ||
        memcmp(g_store, g_store_snap, sizeof g_store) != 0 ||
        memcmp(&g_ctx, &g_ctx_snap, sizeof g_ctx) != 0) {
        printf("layout FAIL: header/entries/context changed\n");
        return 1;
    }

    printf("layout PASS\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        uint64_t v[9];
        int i;

        if (argc != 11 || strcmp(argv[1], "--layout") != 0) {
            fprintf(stderr,
                    "usage: %s [--layout FLAGS LINUX_OFFSET LINUX_FILE_SIZE "
                    "IMAGE_SIZE BLOB_OFFSET BLOB_SIZE SELECTED_DTB_OFFSET "
                    "SELECTED_DTB_SIZE PACKED_PAYLOAD_SIZE]\n",
                    argv[0]);
            return 2;
        }
        for (i = 0; i < 9; ++i) {
            const char *arg = argv[2 + i];
            const char *p = arg;
            char *end = NULL;
            unsigned long long parsed;

            while (*p == ' ' || *p == '\t')
                ++p;
            errno = 0;
            parsed = strtoull(arg, &end, 0);
            if (*p == '-' || errno != 0 || end == arg || *end != '\0') {
                fprintf(stderr, "invalid numeric argument: %s\n", arg);
                return 2;
            }
            v[i] = (uint64_t)parsed;
        }
        return layout_replay(v);
    }

    test_valid_exact();
    test_valid_nonpagealigned();
    test_independent_selectors();
    test_legacy_no_chosen();
    test_legacy_stale_removed();
    test_legacy_invalid_ctx_not_consulted();
    test_selector_malformed();
    test_blob_rejections();
    test_kernel_rejections();
    test_copy_safe_success();
    test_page_safety();
    test_span_variants();
    test_handoff_buffer_overlap();
    test_ram_banks();
    test_ram_disabled_bank();
    test_ram_malformed_tuple();
    test_ram_one_cell();
    test_reservations();
    test_arm64_window();
    test_pack_capacity();

    printf("initrd_test PASS\n");
    return 0;
}
