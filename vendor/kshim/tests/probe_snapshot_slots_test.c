/*
 * Probe-slot reporting tests for dt_probe_report_slot().
 *
 * Builds a 4096-byte libfdt blob whose /chosen bootargs hold an original
 * probe token plus the nine androidboot.kshim_sN= slots, and checks that
 * reporting a slot rewrites only that slot's fixed 80-byte value.
 */
#include "dt.h"
#include "libfdt.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define FDT_SIZE      4096
#define SLOT_COUNT    9
#define VALUE_LEN     80
#define VALUE_BODY    72
#define MARKER        "pending"
#define MARKER_LEN    7
#define PROBE_KEY     "androidboot.kshim_probe="
#define SLOT_KEY      "androidboot.kshim_s"
#define BOOTARGS_MAX  1500

static void append(char *buf, size_t sz, const char *s)
{
    size_t len = strlen(buf);
    assert(len + strlen(s) + 1 <= sz);
    memcpy(buf + len, s, strlen(s) + 1);
}

/* 80-byte pending placeholder: "pending" plus 73 underscores. */
static void make_pending(char *dst)
{
    memcpy(dst, MARKER, MARKER_LEN);
    memset(dst + MARKER_LEN, '_', VALUE_LEN - MARKER_LEN);
    dst[VALUE_LEN] = 0;
}

/* 72-char distinct printable payload for a slot index. */
static void make_value(char *out, int idx)
{
    int n = snprintf(out, VALUE_BODY + 1, "slot%dreportvalue", idx);
    int i;
    assert(n > 0 && n < VALUE_BODY);
    for (i = n; i < VALUE_BODY; i++)
        out[i] = 'x';
    out[VALUE_BODY] = 0;
}

/* Expected stored bytes: 72-char payload plus 8 underscore pad. */
static void make_expected(char *out, const char *value)
{
    memcpy(out, value, VALUE_BODY);
    memset(out + VALUE_BODY, '_', VALUE_LEN - VALUE_BODY);
    out[VALUE_LEN] = 0;
}

static void slot_token(char *tok, size_t sz, int idx)
{
    snprintf(tok, sz, "%s%d=", SLOT_KEY, idx);
}

static void default_bootargs(char *out, size_t outsz)
{
    char ph[VALUE_LEN + 1];
    int i;
    make_pending(ph);
    out[0] = 0;
    append(out, outsz, "foo=1 ");
    append(out, outsz, PROBE_KEY);
    append(out, outsz, ph);
    for (i = 0; i < SLOT_COUNT; i++) {
        char tok[64];
        slot_token(tok, sizeof tok, i);
        append(out, outsz, " ");
        append(out, outsz, tok);
        append(out, outsz, ph);
    }
    append(out, outsz, " bar=2");
    assert(strlen(out) < BOOTARGS_MAX);
}

static void bootargs_with_slot0(char *out, size_t outsz, const char *slot0)
{
    char ph[VALUE_LEN + 1];
    int i;
    make_pending(ph);
    out[0] = 0;
    append(out, outsz, "foo=1 ");
    append(out, outsz, SLOT_KEY);
    append(out, outsz, "0=");
    append(out, outsz, slot0);
    for (i = 1; i < SLOT_COUNT; i++) {
        char tok[64];
        slot_token(tok, sizeof tok, i);
        append(out, outsz, " ");
        append(out, outsz, tok);
        append(out, outsz, ph);
    }
    append(out, outsz, " bar=2");
    assert(strlen(out) < BOOTARGS_MAX);
}

static void build_fdt(unsigned char *buf, const char *bootargs)
{
    int root, chosen;
    memset(buf, 0, FDT_SIZE);
    assert(fdt_create_empty_tree(buf, FDT_SIZE) == 0);
    root = fdt_path_offset(buf, "/");
    assert(root >= 0);
    chosen = fdt_path_offset(buf, "/chosen");
    if (chosen < 0)
        chosen = fdt_add_subnode(buf, root, "chosen");
    assert(chosen >= 0);
    assert(fdt_setprop(buf, chosen, "bootargs",
                       bootargs, (int)strlen(bootargs) + 1) == 0);
    assert(fdt_pack(buf) == 0);
}

static const char *bootargs_ptr(const void *fdt)
{
    int off = fdt_path_offset(fdt, "/chosen");
    int len;
    const char *p;
    assert(off >= 0);
    p = fdt_getprop(fdt, off, "bootargs", &len);
    assert(p != 0);
    return p;
}

/* Byte offset of the 80-byte value that follows token in the blob. */
static size_t value_offset(const void *fdt, const char *token)
{
    const char *p = bootargs_ptr(fdt);
    const char *found = strstr(p, token);
    assert(found != 0);
    return (size_t)(found - (const char *)fdt) + strlen(token);
}

static void test_report_each_slot_only_touches_its_value(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char work[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char before[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char value[VALUE_BODY + 1];
    char expected[VALUE_LEN + 1];
    char tok[64];
    char ph[VALUE_LEN + 1];
    size_t region, b;
    int i;

    default_bootargs(bootargs, sizeof bootargs);
    build_fdt(base, bootargs);
    memcpy(work, base, FDT_SIZE);
    make_pending(ph);

    for (i = 0; i < SLOT_COUNT; i++) {
        memcpy(before, work, FDT_SIZE);
        make_value(value, i);
        make_expected(expected, value);
        slot_token(tok, sizeof tok, i);

        assert(dt_probe_report_slot(work, (unsigned)i, value) == 0);

        region = value_offset(work, tok);
        assert(region + VALUE_LEN <= FDT_SIZE);
        assert(memcmp(work + region, expected, VALUE_LEN) == 0);

        for (b = 0; b < FDT_SIZE; b++) {
            if (b < region || b >= region + VALUE_LEN)
                assert(before[b] == work[b]);
        }
    }

    region = value_offset(work, PROBE_KEY);
    assert(memcmp(work + region, ph, VALUE_LEN) == 0);
}

static void test_original_report_leaves_slots(void)
{
    static unsigned char blob[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char before[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char value[VALUE_BODY + 1];
    char tok[64];
    size_t region, b;
    int i;

    default_bootargs(bootargs, sizeof bootargs);
    build_fdt(blob, bootargs);

    for (i = 0; i < SLOT_COUNT; i++) {
        make_value(value, i);
        assert(dt_probe_report_slot(blob, (unsigned)i, value) == 0);
    }

    memcpy(before, blob, FDT_SIZE);
    assert(dt_probe_report(blob, "original") == 0);

    for (i = 0; i < SLOT_COUNT; i++) {
        slot_token(tok, sizeof tok, i);
        region = value_offset(blob, tok);
        for (b = 0; b < VALUE_LEN; b++)
            assert(blob[region + b] == before[region + b]);
    }
}

static void test_invalid_slot_index(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char snapshot[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char value[VALUE_BODY + 1];

    default_bootargs(bootargs, sizeof bootargs);
    build_fdt(base, bootargs);
    memcpy(snapshot, base, FDT_SIZE);
    make_value(value, 0);
    assert(dt_probe_report_slot(base, 9u, value) != 0);
    assert(memcmp(base, snapshot, FDT_SIZE) == 0);
}

static void test_duplicate_slot_key(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char snapshot[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char ph[VALUE_LEN + 1];
    char value[VALUE_BODY + 1];

    make_pending(ph);
    bootargs[0] = 0;
    append(bootargs, sizeof bootargs, "foo=1 ");
    append(bootargs, sizeof bootargs, SLOT_KEY);
    append(bootargs, sizeof bootargs, "3=");
    append(bootargs, sizeof bootargs, ph);
    append(bootargs, sizeof bootargs, " ");
    append(bootargs, sizeof bootargs, SLOT_KEY);
    append(bootargs, sizeof bootargs, "3=");
    append(bootargs, sizeof bootargs, ph);
    append(bootargs, sizeof bootargs, " bar=2");

    build_fdt(base, bootargs);
    memcpy(snapshot, base, FDT_SIZE);
    make_value(value, 3);
    assert(dt_probe_report_slot(base, 3u, value) != 0);
    assert(memcmp(base, snapshot, FDT_SIZE) == 0);
}

static void test_wrong_length_placeholder(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char snapshot[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char short_value[VALUE_LEN];
    char value[VALUE_BODY + 1];

    memcpy(short_value, MARKER, MARKER_LEN);
    memset(short_value + MARKER_LEN, '_', (VALUE_LEN - 1) - MARKER_LEN);
    short_value[VALUE_LEN - 1] = 0;

    bootargs_with_slot0(bootargs, sizeof bootargs, short_value);
    build_fdt(base, bootargs);
    memcpy(snapshot, base, FDT_SIZE);
    make_value(value, 0);
    assert(dt_probe_report_slot(base, 0u, value) != 0);
    assert(memcmp(base, snapshot, FDT_SIZE) == 0);
}

static void test_wrong_marker_placeholder(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char snapshot[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char wrong[VALUE_LEN + 1];
    char value[VALUE_BODY + 1];

    memcpy(wrong, "xxxxxxx", MARKER_LEN);
    memset(wrong + MARKER_LEN, '_', VALUE_LEN - MARKER_LEN);
    wrong[VALUE_LEN] = 0;

    bootargs_with_slot0(bootargs, sizeof bootargs, wrong);
    build_fdt(base, bootargs);
    memcpy(snapshot, base, FDT_SIZE);
    make_value(value, 0);
    assert(dt_probe_report_slot(base, 0u, value) != 0);
    assert(memcmp(base, snapshot, FDT_SIZE) == 0);
}

static void test_missing_slot_key(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char snapshot[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char ph[VALUE_LEN + 1];
    char value[VALUE_BODY + 1];
    int i;

    make_pending(ph);
    bootargs[0] = 0;
    append(bootargs, sizeof bootargs, "foo=1 ");
    append(bootargs, sizeof bootargs, PROBE_KEY);
    append(bootargs, sizeof bootargs, ph);
    for (i = 1; i < SLOT_COUNT; i++) {
        char tok[64];
        slot_token(tok, sizeof tok, i);
        append(bootargs, sizeof bootargs, " ");
        append(bootargs, sizeof bootargs, tok);
        append(bootargs, sizeof bootargs, ph);
    }
    append(bootargs, sizeof bootargs, " bar=2");

    build_fdt(base, bootargs);
    memcpy(snapshot, base, FDT_SIZE);
    make_value(value, 0);
    assert(dt_probe_report_slot(base, 0u, value) != 0);
    assert(memcmp(base, snapshot, FDT_SIZE) == 0);
}

static void test_non_boundary_slot_key(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char snapshot[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];
    char ph[VALUE_LEN + 1];
    char value[VALUE_BODY + 1];
    int i;

    make_pending(ph);
    bootargs[0] = 0;
    append(bootargs, sizeof bootargs, "foo=1x");
    append(bootargs, sizeof bootargs, SLOT_KEY);
    append(bootargs, sizeof bootargs, "0=");
    append(bootargs, sizeof bootargs, ph);
    for (i = 1; i < SLOT_COUNT; i++) {
        char tok[64];
        slot_token(tok, sizeof tok, i);
        append(bootargs, sizeof bootargs, " ");
        append(bootargs, sizeof bootargs, tok);
        append(bootargs, sizeof bootargs, ph);
    }
    append(bootargs, sizeof bootargs, " bar=2");

    build_fdt(base, bootargs);
    memcpy(snapshot, base, FDT_SIZE);
    make_value(value, 0);
    assert(dt_probe_report_slot(base, 0u, value) != 0);
    assert(memcmp(base, snapshot, FDT_SIZE) == 0);
}

static void test_null_value(void)
{
    static unsigned char base[FDT_SIZE] __attribute__((aligned(4096)));
    static unsigned char snapshot[FDT_SIZE] __attribute__((aligned(4096)));
    char bootargs[BOOTARGS_MAX];

    default_bootargs(bootargs, sizeof bootargs);
    build_fdt(base, bootargs);
    memcpy(snapshot, base, FDT_SIZE);
    assert(dt_probe_report_slot(base, 0u, 0) != 0);
    assert(memcmp(base, snapshot, FDT_SIZE) == 0);
}

static void test_null_fdt(void)
{
    char value[VALUE_BODY + 1];
    make_value(value, 0);
    assert(dt_probe_report_slot(0, 0u, value) != 0);
}

int main(void)
{
    test_report_each_slot_only_touches_its_value();
    test_original_report_leaves_slots();
    test_invalid_slot_index();
    test_duplicate_slot_key();
    test_wrong_length_placeholder();
    test_wrong_marker_placeholder();
    test_missing_slot_key();
    test_non_boundary_slot_key();
    test_null_value();
    test_null_fdt();
    puts("probe_snapshot_slots_test: OK");
    return 0;
}
