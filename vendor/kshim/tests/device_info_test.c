/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * device_info_test.c
 *
 * Host tests for device_info_read(const void *, struct device_info *).
 *
 * All device trees are synthesized in-process with libfdt; there are no
 * external fixtures. The function is a pure read-only decoder: the input blob
 * is byte-compared before and after the representative call, and the whole
 * record must be reset on every call. The only exception is the opt-in
 * --stock-tb323fu <dtb> acceptance run, which reads a caller-named blob and
 * performs the same byte comparison.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>
#include "dt.h"
#include "device_info.h"

#define BYTES (64u * 1024u)
#define WORDS (BYTES / sizeof(uint64_t))

static uint64_t g_blob[WORDS];
static uint64_t g_snap[WORDS];

static void *new_tree_cells(int ac, int sc)
{
    memset(g_blob, 0, BYTES);
    assert(fdt_create_empty_tree(g_blob, BYTES) == 0);
    if (ac)
        assert(fdt_setprop_u32(g_blob, 0, "#address-cells", (uint32_t)ac) == 0);
    if (sc)
        assert(fdt_setprop_u32(g_blob, 0, "#size-cells", (uint32_t)sc) == 0);
    return g_blob;
}

static void *new_tree(void)
{
    return new_tree_cells(2, 2);
}

static void set_model(void *f, const char *model)
{
    assert(fdt_setprop_string(f, 0, "model", model) == 0);
}

static void set_compat(void *f, const char *const *entries, size_t count)
{
    char buf[1024];
    size_t off = 0;

    for (size_t i = 0; i < count; ++i) {
        size_t len = strlen(entries[i]) + 1;

        assert(off + len <= sizeof(buf));
        memcpy(buf + off, entries[i], len);
        off += len;
    }
    assert(fdt_setprop(f, 0, "compatible", buf, (int)off) == 0);
}

static void set_compat_raw(void *f, const void *data, int len)
{
    assert(fdt_setprop(f, 0, "compatible", data, len) == 0);
}

static void set_model_raw(void *f, const void *data, int len)
{
    assert(fdt_setprop(f, 0, "model", data, len) == 0);
}

static void set_reg(void *f, int node, const uint64_t *pairs, size_t count,
                    int ac, int sc)
{
    fdt32_t cells[128];
    int k = 0;

    for (size_t i = 0; i < count; ++i) {
        uint64_t address = pairs[i * 2];
        uint64_t size = pairs[i * 2 + 1];

        for (int c = ac - 1; c >= 0; --c)
            cells[k++] = cpu_to_fdt32((uint32_t)(address >> (32 * c)));
        for (int c = sc - 1; c >= 0; --c)
            cells[k++] = cpu_to_fdt32((uint32_t)(size >> (32 * c)));
    }
    assert(k <= (int)(sizeof(cells) / sizeof(cells[0])));
    assert(fdt_setprop(f, node, "reg", cells, (int)(k * sizeof(fdt32_t))) == 0);
}

/* device_type: 0 = omit, 1 = "memory", 2 = something else. */
static int add_mem(void *f, const char *name, const uint64_t *pairs,
                   size_t count, int ac, int sc, int device_type, int enabled)
{
    int node = fdt_add_subnode(f, 0, name);

    assert(node >= 0);
    if (device_type == 1)
        assert(fdt_setprop_string(f, node, "device_type", "memory") == 0);
    else if (device_type == 2)
        assert(fdt_setprop_string(f, node, "device_type", "other") == 0);
    set_reg(f, node, pairs, count, ac, sc);
    if (!enabled)
        assert(fdt_setprop_string(f, node, "status", "disabled") == 0);
    return node;
}

static void run(void *f, struct device_info *info)
{
    assert(dt_validate(f, BYTES) == 0);
    device_info_read(f, info);
}

/* ---- board and SoC classification --------------------------------------- */

static void test_tb322fc_exact(void)
{
    static const char *const compat[] = { "lenovo,elden", "qcom,sm8750" };
    struct device_info info;
    void *f = new_tree();

    set_model(f, "TB322FC");
    set_compat(f, compat, 2);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB322FC);
    assert(info.soc == DEVICE_SOC_SM8750);
    assert(info.model_known && !info.model_truncated);
    assert(strcmp(info.model, "TB322FC") == 0);
    assert(info.compatible_known && !info.compatible_truncated);
    assert(info.compatible_length == 25);
    assert(memcmp(info.compatible, "lenovo,elden", 12) == 0);
    assert(info.compatible[12] == '\0');
    assert(memcmp(info.compatible + 13, "qcom,sm8750", 11) == 0);
    assert(info.compatible[24] == '\0');
    assert(!info.ram_known && info.dt_reported_ram_bytes == 0);
}

static void test_tb323fu_identities(void)
{
    static const char *const tb323fu[] = { "lenovo,tb323fu" };
    static const char *const baldur[] = { "lenovo,baldur", "qcom,canoep" };
    struct device_info info;
    void *f;

    f = new_tree();
    set_compat(f, tb323fu, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB323FU);
    assert(info.soc == DEVICE_SOC_UNKNOWN);

    f = new_tree();
    set_compat(f, baldur, 2);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB323FU);
    assert(info.soc == DEVICE_SOC_SM8850);
}

static void test_same_soc_unrelated_stays_unknown(void)
{
    static const char *const a[] = { "qcom,sm8750", "vendor,other" };
    static const char *const b[] = { "qcom,kaanapali", "vendor,foo" };
    static const char *const c[] = { "lenovo,elden" };  /* missing qcom,sm8750 */
    struct device_info info;
    void *f;

    f = new_tree();
    set_compat(f, a, 2);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8750);

    f = new_tree();
    set_compat(f, b, 2);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    f = new_tree();
    set_compat(f, c, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_UNKNOWN);
}

/* "qcom,sunp" is a generic SM8750-family SoC identifier: the exact entry
 * marks the SoC but never the board, and only an exact match counts. */
static void test_qcom_sunp_exact_soc_only(void)
{
    static const char *const exact[] = { "qcom,sunp" };
    static const char *const prefixed[] = { "vendor,qcom,sunp" };
    static const char *const suffixed[] = { "qcom,sunp-extra" };
    struct device_info info;
    void *f;

    f = new_tree();
    set_compat(f, exact, 1);
    run(f, &info);
    assert(info.soc == DEVICE_SOC_SM8750);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    f = new_tree();
    set_compat(f, prefixed, 1);
    run(f, &info);
    assert(info.soc == DEVICE_SOC_UNKNOWN);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    f = new_tree();
    set_compat(f, suffixed, 1);
    run(f, &info);
    assert(info.soc == DEVICE_SOC_UNKNOWN);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

/* Exact, case-sensitive compatible matches only: no prefix, model or
 * case-folded path may ever identify a board. */
static void test_negative_identification(void)
{
    static const char *const canoep[] = { "qcom,canoep" };
    static const char *const unrelated[] = { "vendor,other" };
    static const char *const extra[] = { "lenovo,tb323fu-extra", "qcom,canoep" };
    static const char *const mixed_case[] = { "Lenovo,TB323FU", "qcom,canoep" };
    static const char raw_model[] = "Lenovo TB323FU\0second";
    struct device_info info;
    void *f;

    /* "qcom,canoep" alone is a SoC marker, never a board identity. */
    f = new_tree();
    set_compat(f, canoep, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* A TB323FU-looking model string with an unrelated compatible does not
     * determine the board: only exact compatible entries do. */
    f = new_tree();
    set_model(f, "Lenovo TB323FU");
    set_compat(f, unrelated, 1);
    run(f, &info);
    assert(info.model_known && strcmp(info.model, "Lenovo TB323FU") == 0);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* An entry that only shares a prefix ("lenovo,tb323fu-extra") is not an
     * exact match and must not identify the board. */
    f = new_tree();
    set_compat(f, extra, 2);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* A case-mismatched Lenovo identifier stays unknown (exact match only). */
    f = new_tree();
    set_compat(f, mixed_case, 2);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* A missing root model never determines a board. */
    f = new_tree();
    set_compat(f, canoep, 1);
    run(f, &info);
    assert(!info.model_known && info.model[0] == '\0');
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* A malformed root model never determines a board either. */
    f = new_tree();
    set_model_raw(f, raw_model, (int)sizeof(raw_model));
    set_compat(f, canoep, 1);
    run(f, &info);
    assert(!info.model_known && info.model[0] == '\0');
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

static void test_conflicting_board_identifiers(void)
{
    static const char *const c[] = {
        "lenovo,elden", "qcom,sm8750", "lenovo,tb323fu"
    };
    struct device_info info;
    void *f = new_tree();

    set_compat(f, c, 3);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8750);
}

static void test_conflicting_soc_identifiers(void)
{
    static const char *const c[] = { "qcom,sm8750", "qcom,canoep" };
    struct device_info info;
    void *f = new_tree();

    set_compat(f, c, 2);
    run(f, &info);
    assert(info.soc == DEVICE_SOC_UNKNOWN);
}

static void test_multi_compatible_order(void)
{
    static const char *const a[] = { "vendor,x", "qcom,sm8750", "lenovo,elden" };
    static const char *const b[] = { "lenovo,elden", "vendor,x", "qcom,sm8750" };
    static const char *const c[] = { "qcom,canoep", "lenovo,baldur", "vendor,y" };
    struct device_info info;
    void *f;

    f = new_tree();
    set_compat(f, a, 3);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB322FC && info.soc == DEVICE_SOC_SM8750);

    f = new_tree();
    set_compat(f, b, 3);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB322FC && info.soc == DEVICE_SOC_SM8750);

    f = new_tree();
    set_compat(f, c, 3);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB323FU && info.soc == DEVICE_SOC_SM8850);
}

/* ---- stock TB322FC active-panel fingerprint ------------------------------ */

#define DSI_PRIMARY  "qcom,dsi-display-primary"
#define MDSS_MDP     "qcom,mdss_mdp@ae00000"
#define DSI_DISPLAY  "qcom,dsi-display"
#define DSI_DEFAULT  "qcom,dsi-default-panel"
#define ELDEN_NODE   "qcom,mdss_dsi_elden_nt36523_csot_3k_dsc_vid"
#define ELDEN_PANEL  "nt36523 3k fhd plus 165Hz CPHY video panel"

static int add_child(void *f, int parent, const char *name)
{
    int node = fdt_add_subnode(f, parent, name);

    assert(node >= 0);
    return node;
}

/* phandle 0 omits the property; panel_name NULL omits the name. */
static int add_panel(void *f, int parent, const char *name, uint32_t phandle,
                     const char *panel_name, int enabled)
{
    int node = add_child(f, parent, name);

    if (phandle)
        assert(fdt_setprop_u32(f, node, "phandle", phandle) == 0);
    if (panel_name)
        assert(fdt_setprop_string(f, node, "qcom,mdss-dsi-panel-name", panel_name) == 0);
    if (!enabled)
        assert(fdt_setprop_string(f, node, "status", "disabled") == 0);
    return node;
}

/* compat/label NULL omit the property; cells NULL omits the default-panel. */
static int add_primary(void *f, int soc, const char *compat, const char *label,
                       const uint32_t *cells, int ncell, int enabled)
{
    int node = add_child(f, soc, DSI_PRIMARY);

    if (compat)
        assert(fdt_setprop_string(f, node, "compatible", compat) == 0);
    if (label)
        assert(fdt_setprop_string(f, node, "label", label) == 0);
    if (cells) {
        fdt32_t encoded[2];

        assert(ncell >= 1 && ncell <= 2);
        for (int i = 0; i < ncell; ++i)
            encoded[i] = cpu_to_fdt32(cells[i]);
        assert(fdt_setprop(f, node, DSI_DEFAULT, encoded,
                           ncell * (int)sizeof(fdt32_t)) == 0);
    }
    if (!enabled)
        assert(fdt_setprop_string(f, node, "status", "disabled") == 0);
    return node;
}

/* Canonical measured .096 layout: an enabled /soc/<DSI_PRIMARY> whose
 * single-cell qcom,dsi-default-panel targets the enabled Elden panel under
 * the mdss_mdp parent. Root properties must already be set (offsets shift
 * when a root property is added after /soc). */
static void build_stock_display(void *f)
{
    uint32_t one[1] = { 1 };
    int soc = add_child(f, 0, "soc");
    int mdp = add_child(f, soc, MDSS_MDP);

    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
}

static void test_stock_panel_exact(void)
{
    static const char *const compat[] = { "qcom,sunp" };
    struct device_info info;
    void *f = new_tree();

    set_compat(f, compat, 1);
    build_stock_display(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB322FC);
    assert(info.soc == DEVICE_SOC_SM8750);
}

/* A standby Elden panel is never used: only the referenced target counts. */
static void test_stock_panel_standby_rejected(void)
{
    static const char *const compat[] = { "qcom,sunp" };
    uint32_t other[1] = { 2 };
    struct device_info info;
    void *f = new_tree();
    int soc, mdp;

    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);        /* standby */
    add_panel(f, mdp, "qcom,mdss_dsi_other_panel", 2, "other panel", 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", other, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8750);
}

static void test_stock_panel_bad_reference(void)
{
    static const char *const compat[] = { "qcom,sunp" };
    uint32_t one[1] = { 1 };
    uint32_t two[2] = { 1, 1 };
    uint32_t dangling[1] = { 99 };
    struct device_info info;
    void *f;
    int soc, mdp;

    /* missing qcom,dsi-default-panel */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", NULL, 0, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* dangling phandle */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", dangling, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* two-cell (multiple-cell) reference is rejected before any lookup */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", two, 2, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* a duplicated target phandle is ambiguous: never resolved */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_panel(f, mdp, "qcom,mdss_dsi_dup_panel", 1, "dup", 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

static void test_stock_panel_disabled(void)
{
    static const char *const compat[] = { "qcom,sunp" };
    uint32_t one[1] = { 1 };
    struct device_info info;
    void *f;
    int soc, mdp;

    /* disabled primary */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 0);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* disabled target */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 0);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

static void test_stock_panel_root_gating(void)
{
    static const char *const canoep[] = { "qcom,canoep" };
    static const char *const mixed[] = { "qcom,sunp", "qcom,canoep" };
    static const char *const sunp_only[] = { "qcom,sunp" };
    struct device_info info;
    void *f;

    /* no qcom,sunp root: the fingerprint is never consulted */
    f = new_tree();
    set_compat(f, canoep, 1);
    build_stock_display(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* sunp plus an SM8850-family marker: fingerprint suppressed, SoC mixed */
    f = new_tree();
    set_compat(f, mixed, 2);
    build_stock_display(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_UNKNOWN);

    /* root-only sunp with no display node stays board UNKNOWN */
    f = new_tree();
    set_compat(f, sunp_only, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8750);
}

static void test_stock_panel_wrong_names(void)
{
    static const char *const compat[] = { "qcom,sunp" };
    uint32_t one[1] = { 1 };
    struct device_info info;
    void *f;
    int soc, mdp;

    /* wrong target node name */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, "qcom,mdss_dsi_other_panel", 1, ELDEN_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* wrong target panel name */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, "wrong panel name", 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* wrong primary compatible */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_primary(f, soc, "qcom,dsi-display-secondary", "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* wrong primary label */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP);
    add_panel(f, mdp, ELDEN_NODE, 1, ELDEN_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "secondary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

/* The stock fingerprint reuses the exact-match conflict handling: an exact
 * lenovo,tb323fu alongside it leaves the board UNKNOWN. */
static void test_stock_panel_tb323fu_conflict(void)
{
    static const char *const compat[] = { "qcom,sunp", "lenovo,tb323fu" };
    struct device_info info;
    void *f = new_tree();

    set_compat(f, compat, 2);
    build_stock_display(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8750);
}

/* ---- stock Gen5 TB323FU active-panel fingerprint ------------------------- */

/* Measured DTB12+DTBO47 shape: an exact root "qcom,canoep" with no
 * SM8750-family marker, and an enabled exact /soc/qcom,dsi-display-primary
 * (single "qcom,dsi-display" compatible, "primary" label) whose single-cell
 * qcom,dsi-default-panel resolves to the one enabled Baldur NT36523 panel
 * under /soc/qcom,mdss_mdp@9800000. The reference is symbolic on purpose: the
 * captured phandle (0x7ec) is not part of the fingerprint. */
#define MDSS_MDP_GEN5  "qcom,mdss_mdp@9800000"
#define BALDUR_NODE    "qcom,mdss_dsi_baldur_nt36523_csot_3k_dsc_vid"
#define BALDUR_PANEL   "nt36523 3k fhd plus 165Hz DPHY video panel"
#define BALDUR_PHANDLE 7u
#define GEN5_COMPAT    "qcom,canoep"

static void disable_node(void *f, int node)
{
    assert(fdt_setprop_string(f, node, "status", "disabled") == 0);
}

/* Root properties must already be set (offsets shift when a root property is
 * added after /soc). */
static void build_stock_display_gen5(void *f)
{
    uint32_t one[1] = { BALDUR_PHANDLE };
    int soc = add_child(f, 0, "soc");
    int mdp = add_child(f, soc, MDSS_MDP_GEN5);

    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
}

/* The regression this file exists for: a stock Gen5 tree is currently read
 * as board UNKNOWN even though the root identifies the SoC as SM8850. */
static void test_gen5_stock_panel_exact(void)
{
    static const char *const compat[] = { GEN5_COMPAT };
    struct device_info info;
    void *f = new_tree();

    set_compat(f, compat, 1);
    build_stock_display_gen5(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB323FU);
    assert(info.soc == DEVICE_SOC_SM8850);
}

/* Only the actively referenced panel counts: the node name and the panel name
 * must both be exact, and a standby Baldur panel is never used. */
static void test_gen5_stock_panel_wrong_panel(void)
{
    static const char *const compat[] = { GEN5_COMPAT };
    uint32_t baldur[1] = { BALDUR_PHANDLE };
    uint32_t other[1] = { 8 };
    struct device_info info;
    void *f;
    int soc, mdp;

    /* wrong target node name */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, "qcom,mdss_dsi_other_panel", BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", baldur, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* wrong target panel name */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, "wrong panel name", 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", baldur, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* the reference aims at another enabled panel while Baldur is standby */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1); /* standby */
    add_panel(f, mdp, "qcom,mdss_dsi_other_panel", 8, "other panel", 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", other, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

/* Missing, multi-cell and ambiguous references never resolve. */
static void test_gen5_stock_panel_bad_reference(void)
{
    static const char *const compat[] = { GEN5_COMPAT };
    uint32_t one[1] = { BALDUR_PHANDLE };
    uint32_t two[2] = { BALDUR_PHANDLE, BALDUR_PHANDLE };
    struct device_info info;
    void *f;
    int soc, mdp;

    /* missing qcom,dsi-default-panel */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", NULL, 0, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* two-cell (multiple-cell) reference is rejected before any lookup */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", two, 2, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* a duplicated target phandle is ambiguous: never resolved */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_panel(f, mdp, "qcom,mdss_dsi_dup_panel", BALDUR_PHANDLE, "dup", 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

/* A disabled primary, target or ancestor makes the layout unavailable. */
static void test_gen5_stock_panel_disabled(void)
{
    static const char *const compat[] = { GEN5_COMPAT };
    uint32_t one[1] = { BALDUR_PHANDLE };
    struct device_info info;
    void *f;
    int soc, mdp;

    /* disabled primary */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 0);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* disabled target */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 0);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* disabled target ancestor (mdss_mdp) */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    disable_node(f, mdp);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);

    /* disabled primary ancestor (/soc) */
    f = new_tree();
    set_compat(f, compat, 1);
    soc = add_child(f, 0, "soc");
    disable_node(f, soc);
    mdp = add_child(f, soc, MDSS_MDP_GEN5);
    add_panel(f, mdp, BALDUR_NODE, BALDUR_PHANDLE, BALDUR_PANEL, 1);
    add_primary(f, soc, DSI_DISPLAY, "primary", one, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
}

/* The fingerprint is gated on the exact qcom,canoep root: other SM8850-family
 * markers never substitute for it, and an SM8750-family entry suppresses it. */
static void test_gen5_stock_panel_root_gating(void)
{
    static const char *const canoep[] = { GEN5_COMPAT };
    static const char *const canoe[] = { "qcom,canoe" };
    static const char *const kaanapali[] = { "qcom,kaanapali" };
    static const char *const mixed[] = { "qcom,sunp", "qcom,canoep" };
    struct device_info info;
    void *f;

    /* root-only qcom,canoep without the display fingerprint stays UNKNOWN */
    f = new_tree();
    set_compat(f, canoep, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* qcom,canoe alone is not the measured root */
    f = new_tree();
    set_compat(f, canoe, 1);
    build_stock_display_gen5(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* qcom,kaanapali alone is not the measured root either */
    f = new_tree();
    set_compat(f, kaanapali, 1);
    build_stock_display_gen5(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_SM8850);

    /* sunp plus canoep: the fingerprint is suppressed, SoC stays mixed */
    f = new_tree();
    set_compat(f, mixed, 2);
    build_stock_display_gen5(f);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_UNKNOWN);
}

static void test_missing_properties(void)
{
    struct device_info info;
    void *f = new_tree();

    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN);
    assert(info.soc == DEVICE_SOC_UNKNOWN);
    assert(!info.model_known && !info.model_truncated && info.model[0] == '\0');
    assert(!info.compatible_known && !info.compatible_truncated);
    assert(info.compatible[0] == '\0' && info.compatible_length == 0);
    assert(!info.ram_known && info.dt_reported_ram_bytes == 0);
}

static void test_malformed_string_list(void)
{
    struct device_info info;
    char raw[64];
    void *f;

    /* interior empty entry: "lenovo,elden\0\0qcom,sm8750\0" */
    f = new_tree();
    memset(raw, 0, sizeof(raw));
    memcpy(raw, "lenovo,elden", 12);
    memcpy(raw + 14, "qcom,sm8750", 11);
    set_compat_raw(f, raw, 26);
    run(f, &info);
    assert(!info.compatible_known && info.compatible[0] == '\0');
    assert(info.compatible_length == 0);
    assert(info.board == DEVICE_BOARD_UNKNOWN && info.soc == DEVICE_SOC_UNKNOWN);

    /* missing terminal NUL */
    f = new_tree();
    memset(raw, 0, sizeof(raw));
    memcpy(raw, "lenovo,elden", 12);
    memcpy(raw + 12, "qcom,sm8750", 11);
    set_compat_raw(f, raw, 23);
    run(f, &info);
    assert(!info.compatible_known && info.board == DEVICE_BOARD_UNKNOWN);

    /* a single terminal NUL, i.e. one empty entry */
    f = new_tree();
    memset(raw, 0, sizeof(raw));
    set_compat_raw(f, raw, 1);
    run(f, &info);
    assert(!info.compatible_known && info.board == DEVICE_BOARD_UNKNOWN);

    /* leading empty entry before a real one */
    f = new_tree();
    memset(raw, 0, sizeof(raw));
    memcpy(raw + 1, "lenovo,tb323fu", 14);
    set_compat_raw(f, raw, 16);
    run(f, &info);
    assert(!info.compatible_known && info.board == DEVICE_BOARD_UNKNOWN);
}

static void test_truncated_display(void)
{
    char long_entry[300 + 1];
    char model[200];
    const char *const entries[4] = {
        "vendor,prefix", long_entry, "lenovo,tb323fu", "qcom,canoep"
    };
    size_t prefix = sizeof("vendor,prefix");
    struct device_info info;
    void *f;

    /* The second entry cannot fit the 256-byte compatible copy; both identity
     * entries follow it, so a copy-based classification would miss them. */
    memset(long_entry, 'a', sizeof(long_entry) - 1);
    long_entry[sizeof(long_entry) - 1] = '\0';

    f = new_tree();
    set_compat(f, entries, 4);

    memset(model, 'M', sizeof(model) - 1);
    model[sizeof(model) - 1] = '\0';
    set_model(f, model);

    run(f, &info);
    assert(info.compatible_known && info.compatible_truncated);

    /* The display copy keeps only the entry before the oversized one. */
    assert(info.compatible_length == prefix);
    assert(memcmp(info.compatible, "vendor,prefix", prefix) == 0);
    assert(info.compatible[prefix - 1] == '\0');
    assert(fdt_stringlist_contains(info.compatible, (int)info.compatible_length,
                                   "lenovo,tb323fu") == 0);
    assert(fdt_stringlist_contains(info.compatible, (int)info.compatible_length,
                                   "qcom,canoep") == 0);

    /* Classification must use the complete original list, not this copy. */
    assert(info.board == DEVICE_BOARD_TB323FU && info.soc == DEVICE_SOC_SM8850);
    assert(info.model_known && info.model_truncated);
    assert(strlen(info.model) == 127 && info.model[127] == '\0');
    assert(info.model[0] == 'M');
}

static void test_model_bounds(void)
{
    struct device_info info;
    char model[129];
    void *f;

    memset(model, 'x', 127);
    model[127] = '\0';
    f = new_tree();
    set_model(f, model);
    run(f, &info);
    assert(info.model_known && !info.model_truncated && strlen(info.model) == 127);

    memset(model, 'y', 128);
    model[128] = '\0';
    f = new_tree();
    set_model(f, model);
    run(f, &info);
    assert(info.model_known && info.model_truncated && strlen(info.model) == 127);
}

static void test_model_empty_and_malformed(void)
{
    static const char raw_model[] = "TB323FU\0second";
    struct device_info info;
    void *f;

    /* An empty root model string yields no known model and an empty buffer. */
    f = new_tree();
    set_model(f, "");
    run(f, &info);
    assert(!info.model_known && !info.model_truncated);
    assert(info.model[0] == '\0');

    /* A model property holding several NUL-separated strings is malformed:
     * dt_string rejects it, so the model is never marked known. */
    f = new_tree();
    set_model_raw(f, raw_model, (int)sizeof(raw_model));
    run(f, &info);
    assert(!info.model_known && !info.model_truncated);
    assert(info.model[0] == '\0');
}

/* ---- DT reported RAM ----------------------------------------------------- */

static void test_ram_2cell(void)
{
    uint64_t banks[4] = {
        0x80000000ULL, 0x40000000ULL,
        0x100000000ULL, 0x10000000ULL,
    };
    struct device_info info;
    void *f = new_tree();

    add_mem(f, "memory", banks, 2, 2, 2, 1, 1);
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x50000000ULL);
}

static void test_ram_1cell(void)
{
    uint64_t bank[2] = { 0x40000000ULL, 0x10000000ULL };
    struct device_info info;
    void *f = new_tree_cells(1, 1);

    add_mem(f, "memory", bank, 1, 1, 1, 1, 1);
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x10000000ULL);
}

static void test_ram_default_cells(void)
{
    /* no #address-cells / #size-cells properties: defaults are 2/1 */
    uint64_t bank[2] = { 0x80000000ULL, 0x40000000ULL };
    struct device_info info;
    void *f = new_tree_cells(0, 0);

    add_mem(f, "memory", bank, 1, 2, 1, 1, 1);
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x40000000ULL);
}

static void test_ram_multiple_nodes(void)
{
    uint64_t b0[2] = { 0x80000000ULL, 0x40000000ULL };
    uint64_t b1[2] = { 0x100000000ULL, 0x10000000ULL };
    struct device_info info;
    void *f = new_tree();

    add_mem(f, "memory@80000000", b0, 1, 2, 2, 1, 1);
    add_mem(f, "memory@100000000", b1, 1, 2, 2, 1, 1);
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x50000000ULL);
}

static void test_ram_disabled_excluded(void)
{
    uint64_t b0[2] = { 0x80000000ULL, 0x40000000ULL };
    uint64_t b1[2] = { 0x100000000ULL, 0x10000000ULL };
    struct device_info info;
    void *f;

    f = new_tree();
    add_mem(f, "memory@80000000", b0, 1, 2, 2, 1, 0);   /* disabled */
    add_mem(f, "memory@100000000", b1, 1, 2, 2, 1, 1);  /* enabled */
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x10000000ULL);

    f = new_tree();
    add_mem(f, "memory", b0, 1, 2, 2, 1, 0);            /* only a disabled node */
    run(f, &info);
    assert(!info.ram_known && info.dt_reported_ram_bytes == 0);
}

static void test_memory_node_matching(void)
{
    uint64_t b[2] = { 0x80000000ULL, 0x40000000ULL };
    struct device_info info;
    void *f;

    f = new_tree();
    add_mem(f, "mem1", b, 1, 2, 2, 1, 1);               /* device_type=memory */
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x40000000ULL);

    f = new_tree();
    add_mem(f, "memory", b, 1, 2, 2, 0, 1);             /* name fallback */
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x40000000ULL);

    f = new_tree();
    add_mem(f, "memory@0", b, 1, 2, 2, 0, 1);           /* memory@... fallback */
    run(f, &info);
    assert(info.ram_known && info.dt_reported_ram_bytes == 0x40000000ULL);

    f = new_tree();
    add_mem(f, "memory", b, 1, 2, 2, 2, 1);             /* device_type=other */
    run(f, &info);
    assert(!info.ram_known);

    f = new_tree();
    add_mem(f, "sram", b, 1, 2, 2, 0, 1);               /* unrelated name */
    run(f, &info);
    assert(!info.ram_known);
}

static void test_ram_malformed(void)
{
    uint64_t zero[2] = { 0x80000000ULL, 0x0ULL };
    uint8_t three[3] = { 0x00, 0x00, 0x00 };
    struct device_info info;
    void *f;
    int node;

    /* reg length is not a whole number of tuples */
    f = new_tree();
    node = fdt_add_subnode(f, 0, "memory");
    assert(node >= 0);
    assert(fdt_setprop_string(f, node, "device_type", "memory") == 0);
    assert(fdt_setprop(f, node, "reg", three, sizeof(three)) == 0);
    run(f, &info);
    assert(!info.ram_known && info.dt_reported_ram_bytes == 0);

    /* reg missing entirely */
    f = new_tree();
    node = fdt_add_subnode(f, 0, "memory");
    assert(node >= 0);
    assert(fdt_setprop_string(f, node, "device_type", "memory") == 0);
    run(f, &info);
    assert(!info.ram_known);

    /* zero-sized tuple */
    f = new_tree();
    add_mem(f, "memory", zero, 1, 2, 2, 1, 1);
    run(f, &info);
    assert(!info.ram_known);

    /* unsupported #size-cells */
    f = new_tree();
    assert(fdt_setprop_u32(f, 0, "#size-cells", 3) == 0);
    add_mem(f, "memory", zero, 1, 2, 2, 1, 1);
    run(f, &info);
    assert(!info.ram_known);

    /* unsupported #address-cells */
    f = new_tree();
    assert(fdt_setprop_u32(f, 0, "#address-cells", 0) == 0);
    add_mem(f, "memory", zero, 1, 2, 2, 1, 1);
    run(f, &info);
    assert(!info.ram_known);

    /* no memory node at all */
    f = new_tree();
    run(f, &info);
    assert(!info.ram_known);
}

static void test_ram_overflow(void)
{
    uint64_t end_overflow[2] = { 0xFFFFFFFFFFFFF000ULL, 0x4000ULL };
    uint64_t sum_overflow[4] = {
        0x0ULL, 0x8000000000000000ULL,
        0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL,
    };
    struct device_info info;
    void *f;

    f = new_tree();
    add_mem(f, "memory", end_overflow, 1, 2, 2, 1, 1);
    run(f, &info);
    assert(!info.ram_known && info.dt_reported_ram_bytes == 0);

    f = new_tree();
    add_mem(f, "memory", sum_overflow, 2, 2, 2, 1, 1);
    run(f, &info);
    assert(!info.ram_known && info.dt_reported_ram_bytes == 0);
}

/* ---- record lifecycle ---------------------------------------------------- */

static void test_record_reset_across_calls(void)
{
    static const char *const compat[] = { "lenovo,tb323fu", "qcom,canoep" };
    uint64_t bank[2] = { 0x80000000ULL, 0x40000000ULL };
    struct device_info info;
    void *f;

    f = new_tree();
    set_model(f, "TB323FU");
    set_compat(f, compat, 2);
    add_mem(f, "memory", bank, 1, 2, 2, 1, 1);
    run(f, &info);
    assert(info.board == DEVICE_BOARD_TB323FU && info.soc == DEVICE_SOC_SM8850);
    assert(info.model_known && info.ram_known);

    /* a second call on an empty tree must clear every field */
    f = new_tree();
    run(f, &info);
    assert(info.board == DEVICE_BOARD_UNKNOWN && info.soc == DEVICE_SOC_UNKNOWN);
    assert(!info.model_known && !info.model_truncated && info.model[0] == '\0');
    assert(!info.compatible_known && !info.compatible_truncated);
    assert(info.compatible[0] == '\0' && info.compatible_length == 0);
    assert(!info.ram_known && info.dt_reported_ram_bytes == 0);
}

static void test_read_only(void)
{
    static const char *const compat[] = { "lenovo,elden", "qcom,sm8750" };
    uint64_t bank[2] = { 0x80000000ULL, 0x40000000ULL };
    struct device_info info;
    void *f = new_tree();

    set_model(f, "TB322FC");
    set_compat(f, compat, 2);
    add_mem(f, "memory", bank, 1, 2, 2, 1, 1);
    assert(dt_validate(f, BYTES) == 0);

    memcpy(g_snap, g_blob, BYTES);
    device_info_read(f, &info);
    assert(info.board == DEVICE_BOARD_TB322FC && info.ram_known);
    assert(memcmp(g_blob, g_snap, BYTES) == 0);
}

/* ---- optional real-blob acceptance --------------------------------------- */

/* Not a harness: only the no-argument run above is the test suite. This reads
 * a captured stock DTB the caller names explicitly, validates it, checks the
 * measured board/SoC classification and that the blob was not modified. */
static int stock_accept(const char *path)
{
    struct device_info info;
    unsigned char *blob, *copy;
    long length;
    size_t size;
    FILE *fp = fopen(path, "rb");

    if (!fp) {
        fprintf(stderr, "stock: cannot open %s\n", path);
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) || (length = ftell(fp)) <= 0 ||
        fseek(fp, 0, SEEK_SET)) {
        fclose(fp);
        fprintf(stderr, "stock: cannot size %s\n", path);
        return 1;
    }
    size = (size_t)length;
    blob = malloc(size);
    copy = malloc(size);
    if (!blob || !copy || fread(blob, 1, size, fp) != size) {
        fclose(fp);
        free(blob);
        free(copy);
        fprintf(stderr, "stock: cannot read %s\n", path);
        return 1;
    }
    fclose(fp);
    memcpy(copy, blob, size);
    if (dt_validate(blob, size)) {
        fprintf(stderr, "stock: %s is not a valid FDT\n", path);
        free(blob);
        free(copy);
        return 1;
    }
    device_info_read(blob, &info);
    if (info.board != DEVICE_BOARD_TB323FU ||
        info.soc != DEVICE_SOC_SM8850 || memcmp(blob, copy, size)) {
        fprintf(stderr, "stock: %s board/soc/mutation mismatch\n", path);
        free(blob);
        free(copy);
        return 1;
    }
    free(blob);
    free(copy);
    printf("stock-tb323fu PASS\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (argc == 3 && strcmp(argv[1], "--stock-tb323fu") == 0)
            return stock_accept(argv[2]);
        fprintf(stderr, "usage: %s [--stock-tb323fu <dtb>]\n", argv[0]);
        return 2;
    }

    test_tb322fc_exact();
    test_tb323fu_identities();
    test_same_soc_unrelated_stays_unknown();
    test_qcom_sunp_exact_soc_only();
    test_negative_identification();
    test_conflicting_board_identifiers();
    test_conflicting_soc_identifiers();
    test_multi_compatible_order();
    test_stock_panel_exact();
    test_stock_panel_standby_rejected();
    test_stock_panel_bad_reference();
    test_stock_panel_disabled();
    test_stock_panel_root_gating();
    test_stock_panel_wrong_names();
    test_stock_panel_tb323fu_conflict();
    test_gen5_stock_panel_exact();
    test_gen5_stock_panel_wrong_panel();
    test_gen5_stock_panel_bad_reference();
    test_gen5_stock_panel_disabled();
    test_gen5_stock_panel_root_gating();
    test_missing_properties();
    test_malformed_string_list();
    test_truncated_display();
    test_model_bounds();
    test_model_empty_and_malformed();
    test_ram_2cell();
    test_ram_1cell();
    test_ram_default_cells();
    test_ram_multiple_nodes();
    test_ram_disabled_excluded();
    test_memory_node_matching();
    test_ram_malformed();
    test_ram_overflow();
    test_record_reset_across_calls();
    test_read_only();
    printf("device_info_test PASS\n");
    return 0;
}
