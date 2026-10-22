/* dt_diagnostic_report_test.c - regression contract tests for the
 * /chosen:bootargs diagnostic-report slot conversion.
 *
 * Target API (declared extern here; a production header is expected to add a
 * matching declaration):
 *
 *     int dt_diagnostic_report_slots(void *fdt, const char *probe,
 *                                    const char *s0);
 *
 * Contract under test.  /chosen:bootargs must carry two standalone source
 * tokens, each exactly once, each with an 80-byte value of exactly
 * "pending" + 73 underscores:
 *
 *     kshimreport.kshim_probe=<80 bytes>
 *     kshimreport.kshim_s0=<80 bytes>
 *
 * On success both are rewritten in place to the androidboot target form:
 *
 *     androidboot.kshim_probe=<80 bytes>
 *     androidboot.kshim_s0=<80 bytes>
 *
 * The two prefixes ("kshimreport." and "androidboot.") are both 12 bytes, but
 * the two token names are not: "kshim_probe=" is 12 bytes and "kshim_s0=" is
 * 9, so the probe span (12+12+80) and the s0 span (12+9+80) differ in length.
 * The two name lengths and the two span lengths are therefore derived from
 * their own string macros and checked independently.
 *
 * When both report arguments are 1..80 printable-ASCII bytes with no
 * whitespace, quote or backslash, and neither target token
 * (androidboot.kshim_probe= / androidboot.kshim_s0=) already exists as a
 * standalone token, success rewrites only the two 12-byte prefixes
 * ("kshimreport." -> "androidboot.") and the two 80-byte values (report value
 * padded with underscores).  All other input bytes and the FDT length are
 * preserved.  Every failure returns nonzero and leaves the whole FDT
 * byte-identical.  A repeated successful call refuses and changes nothing.
 * dt_probe_report() and dt_probe_report_slot() keep their original semantics.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libfdt.h>
#include <dt.h>

extern int dt_diagnostic_report_slots(void *fdt, const char *probe,
                                      const char *s0);

#define SRCPFX   "kshimreport."
#define TGTPFX   "androidboot."
#define N_PROBE  "kshim_probe="
#define N_S0     "kshim_s0="
#define SRC_PROBE SRCPFX N_PROBE
#define SRC_S0    SRCPFX N_S0
#define TGT_PROBE TGTPFX N_PROBE
#define TGT_S0    TGTPFX N_S0

#define PFXLEN  12                       /* "kshimreport." / "androidboot."    */
#define VALN    80                       /* fixed token value width            */
#define NAMELEN_PROBE ((int)sizeof(N_PROBE) - 1) /* "kshim_probe=" = 12        */
#define NAMELEN_S0    ((int)sizeof(N_S0) - 1)    /* "kshim_s0="    =  9        */
#define REGLEN_PROBE (PFXLEN + NAMELEN_PROBE + VALN) /* probe span = 104      */
#define REGLEN_S0    (PFXLEN + NAMELEN_S0 + VALN)    /* s0 span    = 101      */
#define BLOBSZ  4096

static char g_ph[VALN + 1];              /* "pending" + 73 '_' + NUL           */

static void init_ph(void)
{
    memcpy(g_ph, "pending", 7);
    memset(g_ph + 7, '_', VALN - 7);
    g_ph[VALN] = '\0';
}

static void build_tree(uint8_t *blob, int with_chosen)
{
    memset(blob, 0, BLOBSZ);
    assert(fdt_create_empty_tree(blob, BLOBSZ) == 0);
    if (with_chosen)
        assert(fdt_add_subnode(blob, 0, "chosen") >= 0);
}

static void set_args_raw(uint8_t *blob, const char *args, int alen)
{
    int c;
    build_tree(blob, 1);
    c = fdt_path_offset(blob, "/chosen");
    assert(c >= 0);
    assert(fdt_setprop(blob, c, "bootargs", args, alen) == 0);
}

static void set_args(uint8_t *blob, const char *args)
{
    set_args_raw(blob, args, (int)strlen(args) + 1);
}

/* b <- pre + SRC_PROBE:<ph> [space] SRC_S0:<ph> + suf, token order selectable. */
static void pair_str(char *b, const char *pre, int probe_first, const char *suf)
{
    b[0] = '\0';
    if (pre) strcat(b, pre);
    if (probe_first) {
        strcat(b, SRC_PROBE); strcat(b, g_ph); strcat(b, " ");
        strcat(b, SRC_S0);    strcat(b, g_ph);
    } else {
        strcat(b, SRC_S0);    strcat(b, g_ph); strcat(b, " ");
        strcat(b, SRC_PROBE); strcat(b, g_ph);
    }
    if (suf) strcat(b, suf);
}

static void set_pair(uint8_t *blob, const char *pre, int probe_first,
                     const char *suf)
{
    char b[1024];
    pair_str(b, pre, probe_first, suf);
    set_args(blob, b);
}

/* b <- t1 + v1 + " " + t2 + v2 */
static void two(char *b, const char *t1, const char *v1,
                const char *t2, const char *v2)
{
    b[0] = '\0';
    strcat(b, t1); strcat(b, v1);
    strcat(b, " ");
    strcat(b, t2); strcat(b, v2);
}

/* Byte offsets of the two source tokens inside the raw blob buffer. */
static void pair_offsets(const uint8_t *blob, size_t *op, size_t *os)
{
    int chosen = fdt_path_offset(blob, "/chosen");
    const char *ba, *p, *q;
    assert(chosen >= 0);
    ba = fdt_getprop(blob, chosen, "bootargs", NULL);
    assert(ba != NULL);
    p = strstr(ba, SRC_PROBE);
    q = strstr(ba, SRC_S0);
    assert(p != NULL && q != NULL);
    *op = (size_t)(p - (const char *)blob);
    *os = (size_t)(q - (const char *)blob);
}

/* Success: return 0, only the contracted bytes change, FDT length and
 * structural validity (fdt_check_full) preserved. */
static void expect_success(uint8_t *blob, const char *probe, const char *s0)
{
    static uint8_t snap[BLOBSZ];
    static uint8_t exp[BLOBSZ];
    char pv[VALN + 1], sv[VALN + 1];
    size_t op, os, pl = strlen(probe), sl = strlen(s0);

    assert(pl >= 1 && pl <= VALN && sl >= 1 && sl <= VALN);
    pair_offsets(blob, &op, &os);

    memcpy(pv, probe, pl); memset(pv + pl, '_', VALN - pl); pv[VALN] = '\0';
    memcpy(sv, s0, sl);    memset(sv + sl, '_', VALN - sl); sv[VALN] = '\0';

    memcpy(snap, blob, BLOBSZ);
    memcpy(exp, snap, BLOBSZ);
    memcpy(exp + op, TGTPFX, PFXLEN);
    memcpy(exp + op + PFXLEN + NAMELEN_PROBE, pv, VALN);
    memcpy(exp + os, TGTPFX, PFXLEN);
    memcpy(exp + os + PFXLEN + NAMELEN_S0, sv, VALN);

    assert(dt_diagnostic_report_slots(blob, probe, s0) == 0);
    assert(fdt_totalsize(blob) == fdt_totalsize(snap));
    assert(fdt_check_full(blob, BLOBSZ) == 0);
    assert(memcmp(blob, exp, BLOBSZ) == 0);
}

/* Failure: nonzero return with the entire FDT byte-identical. */
static void expect_fail(uint8_t *blob, const char *probe, const char *s0)
{
    static uint8_t snap[BLOBSZ];
    memcpy(snap, blob, BLOBSZ);
    assert(dt_diagnostic_report_slots(blob, probe, s0) != 0);
    assert(memcmp(blob, snap, BLOBSZ) == 0);
}

static void test_normal(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    char eighty[VALN + 1];

    memset(eighty, 'A', VALN);
    eighty[VALN] = '\0';

    /* probe token first / s0 token first, with and without neighbours. */
    set_pair(blob, NULL, 1, NULL);
    expect_success(blob, "on", "abc");

    set_pair(blob, NULL, 0, NULL);
    expect_success(blob, "midvalue", "tail");

    set_pair(blob, "foo=1 ", 1, " bar=2");
    expect_success(blob, "on", "x");

    set_pair(blob, "x=y z=w ", 0, " q=r");
    expect_success(blob, "a", "b");

    /* an 80-character report value is accepted with no underscore padding. */
    set_pair(blob, "pre ", 1, " post");
    expect_success(blob, eighty, eighty);
}

static void test_mutation_scope(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    static uint8_t snap[BLOBSZ];
    size_t op, os, lo, hi;

    set_pair(blob, "k=v ", 1, " t=u");
    pair_offsets(blob, &op, &os);
    assert(op < os);                     /* probe token precedes s0 token   */
    assert(os >= op + REGLEN_PROBE);     /* regions do not overlap          */
    memcpy(snap, blob, BLOBSZ);

    assert(dt_diagnostic_report_slots(blob, "p1", "s1") == 0);
    assert(fdt_check_full(blob, BLOBSZ) == 0);
    assert(fdt_totalsize(blob) == fdt_totalsize(snap));

    /* Every byte outside the two allowed regions is untouched. */
    assert(memcmp(blob, snap, op) == 0);
    lo = op + REGLEN_PROBE; hi = os;
    assert(memcmp(blob + lo, snap + lo, hi - lo) == 0);
    assert(memcmp(blob + os + REGLEN_S0, snap + os + REGLEN_S0,
                  BLOBSZ - (os + REGLEN_S0)) == 0);

    /* Only the 12-byte prefix and the 80-byte value of each token changed;
     * the "kshim_probe=" / "kshim_s0=" token names are preserved. */
    assert(memcmp(blob + op, TGTPFX, PFXLEN) == 0);
    assert(memcmp(blob + op + PFXLEN, N_PROBE, NAMELEN_PROBE) == 0);
    assert(memcmp(blob + op + PFXLEN + NAMELEN_PROBE, "p1", 2) == 0);
    assert(memcmp(blob + os, TGTPFX, PFXLEN) == 0);
    assert(memcmp(blob + os + PFXLEN, N_S0, NAMELEN_S0) == 0);
    assert(memcmp(blob + os + PFXLEN + NAMELEN_S0, "s1", 2) == 0);
}

static void test_embedded_target(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];

    /* Target-looking text embedded in another token's key ("foo=") or value
     * ("yandro...") is not a standalone token, so conversion still succeeds. */
    set_pair(blob, "foo=androidboot.kshim_probe=zz ", 1,
             " yandroidboot.kshim_s0=zz");
    expect_success(blob, "rp", "rs");
}

static void test_repeat(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    static uint8_t snap[BLOBSZ];

    set_pair(blob, "", 1, "");
    assert(dt_diagnostic_report_slots(blob, "p", "s") == 0);
    memcpy(snap, blob, BLOBSZ);
    /* The source tokens are gone; every repeat refuses and changes nothing. */
    assert(dt_diagnostic_report_slots(blob, "p", "s") != 0);
    assert(memcmp(blob, snap, BLOBSZ) == 0);
    assert(dt_diagnostic_report_slots(blob, "q", "r") != 0);
    assert(memcmp(blob, snap, BLOBSZ) == 0);
}

static void test_bad_sources(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    char b[2048], wr[VALN + 1];

    memcpy(wr, "done", 4);
    memset(wr + 4, '_', VALN - 4);
    wr[VALN] = '\0';

    /* short source value: 7 bytes, not 80. */
    two(b, SRC_PROBE, "pending", SRC_S0, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");
    two(b, SRC_PROBE, g_ph, SRC_S0, "pending");
    set_args(blob, b); expect_fail(blob, "ok", "ok");

    /* source already written: 80 bytes but no longer the pending marker. */
    two(b, SRC_PROBE, wr, SRC_S0, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");
    two(b, SRC_PROBE, g_ph, SRC_S0, wr);
    set_args(blob, b); expect_fail(blob, "ok", "ok");

    /* each source duplicated. */
    two(b, SRC_PROBE, g_ph, SRC_S0, g_ph);
    strcat(b, " "); strcat(b, SRC_PROBE); strcat(b, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");
    two(b, SRC_PROBE, g_ph, SRC_S0, g_ph);
    strcat(b, " "); strcat(b, SRC_S0); strcat(b, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");

    /* pre-existing standalone target token conflicts. */
    two(b, TGT_PROBE, g_ph, SRC_PROBE, g_ph);
    strcat(b, " "); strcat(b, SRC_S0); strcat(b, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");
    two(b, SRC_PROBE, g_ph, TGT_S0, g_ph);
    strcat(b, " "); strcat(b, SRC_S0); strcat(b, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");

    /* each source missing. */
    b[0] = '\0'; strcat(b, SRC_PROBE); strcat(b, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");
    b[0] = '\0'; strcat(b, SRC_S0); strcat(b, g_ph);
    set_args(blob, b); expect_fail(blob, "ok", "ok");
    set_args(blob, ""); expect_fail(blob, "ok", "ok");
}

static void test_bad_reports(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    char long81[VALN + 2];

    memset(long81, 'a', VALN + 1);
    long81[VALN + 1] = '\0';

    set_pair(blob, "", 1, "");
    expect_fail(blob, "", "s");          /* empty probe                       */
    expect_fail(blob, "p", "");          /* empty s0                          */
    expect_fail(blob, long81, "s");      /* probe 81 chars                    */
    expect_fail(blob, "p", long81);      /* s0 81 chars                       */
    expect_fail(blob, "a b", "s");       /* probe with whitespace             */
    expect_fail(blob, "p", "a b");       /* s0 with whitespace                */
    expect_fail(blob, "a\"b", "s");      /* probe with quote                  */
    expect_fail(blob, "p", "a\"b");      /* s0 with quote                     */
    expect_fail(blob, "a\\b", "s");      /* probe with backslash              */
    expect_fail(blob, "p", "a\\b");      /* s0 with backslash                 */
}

static void test_malformed(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    char b[1024];

    build_tree(blob, 0);                 /* no /chosen                        */
    expect_fail(blob, "p", "s");

    build_tree(blob, 1);                 /* /chosen without bootargs          */
    expect_fail(blob, "p", "s");

    pair_str(b, "", 1, "");              /* bootargs not NUL-terminated       */
    set_args_raw(blob, b, (int)strlen(b));
    expect_fail(blob, "p", "s");

    pair_str(b, "", 1, "");              /* NUL embedded inside bootargs      */
    {
        int n = (int)strlen(b);
        b[n] = '\0';
        memcpy(b + n + 1, "tail", 5);
        b[n + 6] = '\0';
        set_args_raw(blob, b, n + 7);
    }
    expect_fail(blob, "p", "s");
}

static void test_null_args(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];

    set_pair(blob, "", 1, "");
    assert(dt_diagnostic_report_slots(NULL, "p", "s") != 0);
    expect_fail(blob, NULL, "s");
    expect_fail(blob, "p", NULL);
    expect_fail(blob, NULL, NULL);
}

/* dt_probe_report()/dt_probe_report_slot() regression: on androidboot slots a
 * single write works and the repeat refuses while leaving the blob intact. */
static void test_original_apis(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    static uint8_t snap[BLOBSZ];
    char b[1024];

    two(b, TGT_PROBE, g_ph, TGT_S0, g_ph);
    set_args(blob, b);

    assert(dt_probe_report(blob, "on") == 0);
    memcpy(snap, blob, BLOBSZ);
    assert(dt_probe_report(blob, "on") != 0);
    assert(memcmp(blob, snap, BLOBSZ) == 0);

    assert(dt_probe_report_slot(blob, 0, "sv") == 0);
    memcpy(snap, blob, BLOBSZ);
    assert(dt_probe_report_slot(blob, 0, "sv") != 0);
    assert(memcmp(blob, snap, BLOBSZ) == 0);
}

/* Normal manifest runtime: the repacker leaves both source slots pending and
 * main() activates them before the first m14/m15 write. */
static void test_manifest_activation(void)
{
    _Alignas(8) static uint8_t blob[BLOBSZ];
    static uint8_t snap[BLOBSZ];
    int chosen, blen, alen;
    const char *ba;

    set_pair(blob, "console=tty0 ", 1, " q=1");
    chosen = fdt_path_offset(blob, "/chosen");
    assert(chosen >= 0);
    ba = fdt_getprop(blob, chosen, "bootargs", &blen);
    assert(ba != NULL && strstr(ba, SRC_PROBE) && strstr(ba, SRC_S0));
    memcpy(snap, blob, BLOBSZ);

    assert(dt_diagnostic_report_slots(blob, "pending", "pending") == 0);
    assert(fdt_totalsize(blob) == fdt_totalsize(snap));
    ba = fdt_getprop(blob, chosen, "bootargs", &alen);
    assert(ba != NULL && alen == blen);
    assert(strstr(ba, TGT_PROBE) && strstr(ba, TGT_S0));
    assert(strstr(ba, SRCPFX) == NULL);

    assert(dt_probe_report(blob, "m14:base=selected") == 0);
    memcpy(snap, blob, BLOBSZ);
    assert(dt_probe_report(blob, "m14:base=selected") != 0);
    assert(memcmp(blob, snap, BLOBSZ) == 0);

    assert(dt_probe_report_slot(blob, 0, "r22:cfg=0") == 0);
}

int main(void)
{
    init_ph();

    test_normal();
    test_mutation_scope();
    test_embedded_target();
    test_repeat();
    test_bad_sources();
    test_bad_reports();
    test_malformed();
    test_null_args();
    test_original_apis();
    test_manifest_activation();

    puts("dt_diagnostic_report_test: OK");
    return 0;
}
