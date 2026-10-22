/* probe_report_test.c - regression tests for dt_probe_report(void*, const char*).
 * Contract: success (0) rewrites only the 80 bytes of the fixed token value in
 * /chosen:bootargs; every failure returns nonzero and leaves the blob intact. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libfdt.h>
#include <dt.h>

extern int dt_probe_report(void *fdt, const char *value);

#define KEY    "androidboot.kshim_probe="
#define KEYLEN (sizeof(KEY) - 1)
#define VALN   80
#define BLOBSZ 4096
#define SETS(b, s) set_bootargs((b), (s), (int)strlen(s) + 1)

static char g_init[VALN + 1];

static void init_value(void) {
    memcpy(g_init, "pending", 7);
    memset(g_init + 7, '_', VALN - 7);
    g_init[VALN] = '\0';
}

/* buf <- pre + KEY + first vlen bytes of val + suf */
static const char *tok_with(char *buf, const char *pre, const char *val, size_t vlen, const char *suf) {
    size_t n = 0;
    if (pre) n += (size_t)sprintf(buf + n, "%s", pre);
    memcpy(buf + n, KEY, KEYLEN); n += KEYLEN;
    memcpy(buf + n, val, vlen);   n += vlen;
    if (suf) n += (size_t)sprintf(buf + n, "%s", suf);
    buf[n] = '\0';
    return buf;
}

static void build_tree(uint8_t *blob, int with_chosen) {
    memset(blob, 0, BLOBSZ);
    assert(fdt_create_empty_tree(blob, BLOBSZ) == 0);
    if (with_chosen) assert(fdt_add_subnode(blob, 0, "chosen") >= 0);
}

static void set_bootargs(uint8_t *blob, const char *args, int alen) {
    build_tree(blob, 1);
    int c = fdt_path_offset(blob, "/chosen");
    assert(c >= 0);
    assert(fdt_setprop(blob, c, "bootargs", args, alen) == 0);
}

static void test_ok(uint8_t *blob, const char *pre, const char *suf, const char *argval) {
    static uint8_t snap[BLOBSZ];
    char a[512], expv[VALN + 1];
    size_t al = strlen(argval), prelen = pre ? strlen(pre) : 0;
    assert(al <= VALN);
    SETS(blob, tok_with(a, pre, g_init, VALN, suf));
    memcpy(snap, blob, BLOBSZ);
    memcpy(expv, argval, al);
    memset(expv + al, '_', VALN - al);
    expv[VALN] = '\0';
    assert(dt_probe_report(blob, argval) == 0);
    int c = fdt_path_offset(blob, "/chosen");
    assert(c >= 0);
    const char *ba = fdt_getprop(blob, c, "bootargs", NULL);
    assert(ba != NULL);
    const char *v = strstr(ba, KEY) + KEYLEN;
    size_t voff = (size_t)(v - (const char *)blob);
    assert(voff == (size_t)(ba - (const char *)blob) + prelen + KEYLEN);
    assert(memcmp(blob, snap, voff) == 0);
    assert(memcmp(blob + voff + VALN, snap + voff + VALN, BLOBSZ - voff - VALN) == 0);
    assert(memcmp(v, expv, VALN) == 0);
}

static void test_fail(uint8_t *blob, const char *argval) {
    static uint8_t snap[BLOBSZ];
    memcpy(snap, blob, BLOBSZ);
    assert(dt_probe_report(blob, argval) != 0);
    assert(memcmp(blob, snap, BLOBSZ) == 0);
}

int main(void) {
    _Alignas(8) static uint8_t blob[BLOBSZ];
    char a[512], w[VALN + 2];
    init_value();

    /* token at start, middle, end; value shorter than 80 gets underscore padded */
    test_ok(blob, "", "", "on");
    test_ok(blob, "foo=1 ", " bar=2", "midvalue");
    test_ok(blob, "x=y z=w ", "", "tail");
    test_ok(blob, NULL, NULL, "abc");

    /* invalid value arguments */
    SETS(blob, tok_with(a, "", g_init, VALN, ""));
    memset(w, 'x', VALN + 1); w[VALN + 1] = '\0';
    test_fail(blob, w);
    test_fail(blob, "");
    test_fail(blob, "   ");

    /* malformed blob / missing anchor points */
    build_tree(blob, 0);
    test_fail(blob, "ok");
    build_tree(blob, 1);
    test_fail(blob, "ok");
    SETS(blob, "foo=1 bar=2");
    test_fail(blob, "ok");

    /* key present but not on a token boundary */
    SETS(blob, tok_with(a, "x", g_init, VALN, ""));
    test_fail(blob, "ok");

    /* duplicated key */
    tok_with(a, "", g_init, VALN, "");
    strcat(a, " "); strcat(a, KEY); strncat(a, g_init, VALN);
    SETS(blob, a);
    test_fail(blob, "ok");

    /* initial value 79 then 81 chars */
    memset(w, '_', 79); w[79] = '\0';
    SETS(blob, tok_with(a, "", w, 79, ""));
    test_fail(blob, "ok");
    memset(w, '_', 81); w[81] = '\0';
    SETS(blob, tok_with(a, "", w, 81, ""));
    test_fail(blob, "ok");

    /* wrong 80-char initial value */
    memset(w, 'z', VALN); w[VALN] = '\0';
    SETS(blob, tok_with(a, "", w, VALN, ""));
    test_fail(blob, "ok");

    /* bootargs property not NUL terminated */
    tok_with(a, "", g_init, VALN, "");
    set_bootargs(blob, a, (int)strlen(a));
    test_fail(blob, "ok");

    /* embedded NUL inside bootargs */
    tok_with(a, "", g_init, VALN, "");
    { int n = (int)strlen(a); a[n++] = '\0'; memcpy(a + n, "tail", 5); n += 5;
      set_bootargs(blob, a, n); }
    test_fail(blob, "ok");

    puts("probe_report_test: OK");
    return 0;
}
