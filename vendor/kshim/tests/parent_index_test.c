/* SPDX-License-Identifier: MIT */
/* Standalone regression test for the planned DT parent-index API.
 * The planned API is declared extern, so this TU links only once the
 * implementation exists. Compile/run only on the remote Docker host. */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <libfdt.h>

/* planned API: undefined references => link failure before impl lands */
extern void dt_parent_index_begin(const void *f);
extern void dt_parent_index_end(void);
extern int dt_parent_offset(const void *f, int node);

/* existing library helpers used by this test */
extern int dt_validate(const void *fdt, size_t available);
extern bool dt_available(const void *fdt, int node);

/* wrapping oracle: only the library's internal fallback hits __wrap */
extern int __real_fdt_parent_offset(const void *fdt, int node);
extern int __wrap_fdt_parent_offset(const void *fdt, int node);
static long g_calls;
int __wrap_fdt_parent_offset(const void *fdt, int node)
{
    g_calls++;
    return __real_fdt_parent_offset(fdt, node);
}

static long g_checks;
#define CHECK(c) do { g_checks++; assert(c); } while (0)

#define BUFSZ (1 << 20)
static _Alignas(64) char g_a[BUFSZ];
static _Alignas(64) char g_b[BUFSZ];
static _Alignas(64) char g_wide[BUFSZ];
static _Alignas(64) char g_deep[BUFSZ];
static int g_nodes[16384];

static int collect(const void *f)
{
    int off = -1, depth = -1, n = 0;
    while ((off = fdt_next_node(f, off, &depth)) >= 0 && depth >= 0) {
        if (n >= (int)(sizeof(g_nodes) / sizeof(g_nodes[0])))
            return -1;
        g_nodes[n++] = off;
    }
    return n;
}

int main(void)
{
    int n, i, got, exp;

    /* ---- tree A: root /disabled(status=disabled)/leaf, /enabled/leaf2,
     *      plus siblings; built linearly with the dynamic writer ---- */
    CHECK(fdt_create(g_a, BUFSZ) == 0);
    CHECK(fdt_finish_reservemap(g_a) == 0);
    CHECK(fdt_begin_node(g_a, "") == 0);
    CHECK(fdt_begin_node(g_a, "disabled") == 0);
    CHECK(fdt_property_string(g_a, "status", "disabled") == 0);
    CHECK(fdt_begin_node(g_a, "leaf") == 0);
    CHECK(fdt_end_node(g_a) == 0);
    CHECK(fdt_end_node(g_a) == 0);
    CHECK(fdt_begin_node(g_a, "enabled") == 0);
    CHECK(fdt_begin_node(g_a, "leaf2") == 0);
    CHECK(fdt_end_node(g_a) == 0);
    CHECK(fdt_end_node(g_a) == 0);
    for (i = 0; i < 40; i++) {
        char nm[16];
        snprintf(nm, sizeof(nm), "sib%d", i);
        CHECK(fdt_begin_node(g_a, nm) == 0);
        CHECK(fdt_end_node(g_a) == 0);
    }
    CHECK(fdt_end_node(g_a) == 0);
    CHECK(fdt_finish(g_a) == 0);
    CHECK(dt_validate(g_a, (size_t)BUFSZ) == 0);

    n = collect(g_a);
    CHECK(n > 5);
    printf("treeA_nodes=%d\n", n);

    /* every valid node cached while active, compared against the oracle */
    dt_parent_index_begin(g_a);
    for (i = 0; i < n; i++) {
        g_calls = 0;
        got = dt_parent_offset(g_a, g_nodes[i]);
        exp = __real_fdt_parent_offset(g_a, g_nodes[i]);
        CHECK(g_calls == 0);
        CHECK(got == exp);
    }
    printf("cached_lookups=%d\n", n);
    CHECK(dt_parent_offset(g_a, 0) == -FDT_ERR_NOTFOUND);

    /* availability while the index scope is active */
    {
        int dis = fdt_path_offset(g_a, "/disabled/leaf");
        int en = fdt_path_offset(g_a, "/enabled/leaf2");
        CHECK(dis > 0 && en > 0);
        CHECK(dt_available(g_a, dis) == false);
        CHECK(dt_available(g_a, en) == true);
    }

    /* invalid node: fallback exact, wrapper used */
    g_calls = 0;
    CHECK(dt_parent_offset(g_a, -12345) == __real_fdt_parent_offset(g_a, -12345));
    CHECK(g_calls > 0);
    g_calls = 0;
    CHECK(dt_parent_offset(g_a, 1) == __real_fdt_parent_offset(g_a, 1));
    CHECK(g_calls > 0);

    /* begin called twice resets cleanly */
    dt_parent_index_begin(g_a);
    dt_parent_index_begin(g_a);
    g_calls = 0;
    CHECK(dt_parent_offset(g_a, g_nodes[n - 1]) ==
          __real_fdt_parent_offset(g_a, g_nodes[n - 1]));
    CHECK(g_calls == 0);

    /* end clears active state: queries fall back via wrapper */
    dt_parent_index_end();
    g_calls = 0;
    CHECK(dt_parent_offset(g_a, g_nodes[n / 2]) ==
          __real_fdt_parent_offset(g_a, g_nodes[n / 2]));
    CHECK(g_calls > 0);
    printf("inactive_fallback=1\n");

    /* ---- tree B: a second valid blob ---- */
    CHECK(fdt_create(g_b, BUFSZ) == 0);
    CHECK(fdt_finish_reservemap(g_b) == 0);
    CHECK(fdt_begin_node(g_b, "") == 0);
    CHECK(fdt_begin_node(g_b, "only") == 0);
    CHECK(fdt_end_node(g_b) == 0);
    CHECK(fdt_end_node(g_b) == 0);
    CHECK(fdt_finish(g_b) == 0);
    CHECK(dt_validate(g_b, (size_t)BUFSZ) == 0);

    dt_parent_index_begin(g_a);
    g_calls = 0;
    CHECK(dt_parent_offset(g_a, g_nodes[1]) ==
          __real_fdt_parent_offset(g_a, g_nodes[1]));
    CHECK(g_calls == 0);
    {
        int only = fdt_path_offset(g_b, "/only");
        CHECK(only > 0);
        /* another valid blob falls back to exact */
        g_calls = 0;
        CHECK(dt_parent_offset(g_b, only) == __real_fdt_parent_offset(g_b, only));
        CHECK(g_calls > 0);
        /* begin on the other blob invalidates A's cache */
        dt_parent_index_begin(g_b);
        g_calls = 0;
        CHECK(dt_parent_offset(g_a, g_nodes[1]) ==
              __real_fdt_parent_offset(g_a, g_nodes[1]));
        CHECK(g_calls > 0);
        /* B is now the cached scope */
        g_calls = 0;
        CHECK(dt_parent_offset(g_b, only) == __real_fdt_parent_offset(g_b, only));
        CHECK(g_calls == 0);
    }
    dt_parent_index_end();

    /* ---- mutate A, rebegin, verify equality for every node ---- */
    CHECK(fdt_open_into(g_a, g_a, BUFSZ) == 0);
    CHECK(fdt_add_subnode(g_a, fdt_path_offset(g_a, "/"), "added") > 0);
    CHECK(dt_validate(g_a, (size_t)BUFSZ) == 0);
    n = collect(g_a);
    CHECK(n > 0);
    dt_parent_index_begin(g_a);
    for (i = 0; i < n; i++) {
        g_calls = 0;
        got = dt_parent_offset(g_a, g_nodes[i]);
        exp = __real_fdt_parent_offset(g_a, g_nodes[i]);
        CHECK(g_calls == 0);
        CHECK(got == exp);
    }
    dt_parent_index_end();
    printf("mutated_rebegin_equality=1\n");

    /* ---- capacity overflow: 9000 siblings > 8192 cap ---- */
    CHECK(fdt_create(g_wide, BUFSZ) == 0);
    CHECK(fdt_finish_reservemap(g_wide) == 0);
    CHECK(fdt_begin_node(g_wide, "") == 0);
    for (i = 0; i < 9000; i++) {
        char nm[16];
        snprintf(nm, sizeof(nm), "w%d", i);
        CHECK(fdt_begin_node(g_wide, nm) == 0);
        CHECK(fdt_end_node(g_wide) == 0);
    }
    CHECK(fdt_end_node(g_wide) == 0);
    CHECK(fdt_finish(g_wide) == 0);
    CHECK(dt_validate(g_wide, (size_t)BUFSZ) == 0);
    n = collect(g_wide);
    CHECK(n > 8192);
    dt_parent_index_begin(g_wide);
    for (i = 0; i < 8; i++) {
        g_calls = 0;
        got = dt_parent_offset(g_wide, g_nodes[i]);
        exp = __real_fdt_parent_offset(g_wide, g_nodes[i]);
        CHECK(got == exp);
        CHECK(g_calls > 0);
    }
    dt_parent_index_end();
    printf("capacity_overflow_fallback=1\n");

    /* ---- depth overflow: chain of depth 70 > 64 ---- */
    CHECK(fdt_create(g_deep, BUFSZ) == 0);
    CHECK(fdt_finish_reservemap(g_deep) == 0);
    CHECK(fdt_begin_node(g_deep, "") == 0);
    for (i = 0; i < 70; i++) {
        char nm[16];
        snprintf(nm, sizeof(nm), "d%d", i);
        CHECK(fdt_begin_node(g_deep, nm) == 0);
    }
    for (i = 0; i < 70; i++)
        CHECK(fdt_end_node(g_deep) == 0);
    CHECK(fdt_end_node(g_deep) == 0);
    CHECK(fdt_finish(g_deep) == 0);
    CHECK(dt_validate(g_deep, (size_t)BUFSZ) == 0);
    n = collect(g_deep);
    CHECK(n > 64);
    dt_parent_index_begin(g_deep);
    for (i = 0; i < n; i++) {
        g_calls = 0;
        got = dt_parent_offset(g_deep, g_nodes[i]);
        exp = __real_fdt_parent_offset(g_deep, g_nodes[i]);
        CHECK(got == exp);
        CHECK(g_calls > 0);
    }
    dt_parent_index_end();
    printf("depth_overflow_fallback=1\n");

    printf("checks=%ld\n", g_checks);
    printf("ALL PASS\n");
    return 0;
}
