/* SPDX-License-Identifier: GPL-3.0-only */
#include "device_info.h"
#include "dt.h"
#include <string.h>

/* Root #address-cells / #size-cells. Only 1 or 2 are supported; the DT
 * defaults are 2/1. Returns -1 for a malformed or unsupported value. */
static int root_cells(const void *fdt, const char *property, int fallback)
{
    int len;
    uint32_t value;
    const fdt32_t *p = fdt_getprop(fdt, 0, property, &len);

    if (!p)
        return len == -FDT_ERR_NOTFOUND ? fallback : -1;
    if (len != 4)
        return -1;
    value = fdt32_to_cpu(*p);
    if (value < 1 || value > 2)
        return -1;
    return (int)value;
}

/* A root child is a memory node when it carries a valid device_type = "memory"
 * or, only when device_type is absent, when its node name is "memory" or a
 * "memory@..." variant. A present but non-memory device_type never falls back
 * to the node name. */
static bool is_memory_node(const void *fdt, int node)
{
    int len;
    const char *name;
    const void *device_type = fdt_getprop(fdt, node, "device_type", &len);

    if (device_type && len >= 0) {
        const char *s = dt_string(fdt, node, "device_type");
        return s && strcmp(s, "memory") == 0;
    }
    name = fdt_get_name(fdt, node, NULL);
    if (!name)
        return false;
    return strcmp(name, "memory") == 0 || strncmp(name, "memory@", 7) == 0;
}

static bool list_contains(const char *list, int len, const char *entry)
{
    return fdt_stringlist_contains(list, len, entry) > 0;
}

/* True when list is a well formed nonempty string list: at least one entry,
 * no empty entry and a terminal NUL. */
static bool compatible_list_valid(const char *list, int len)
{
    int off;

    if (!list || len <= 0 || list[len - 1] != '\0')
        return false;
    for (off = 0; off < len;) {
        size_t entry = strlen(list + off);
        if (!entry)
            return false;
        off += (int)entry + 1;
    }
    return off == len;
}

/* Sum the reg sizes of every enabled root-level memory node. Any malformed
 * input, unsupported cell count, zero-sized tuple, address-end overflow, or
 * sum overflow makes the whole total unknown. Returns 0 with *total set on
 * success, -1 when unknown. */
static int memory_total(const void *fdt, uint64_t *total)
{
    int ac = root_cells(fdt, "#address-cells", 2);
    int sc = root_cells(fdt, "#size-cells", 1);
    int tuple, node;
    uint64_t sum = 0;
    bool any = false;

    if (ac < 0 || sc < 0)
        return -1;
    tuple = ac + sc;

    fdt_for_each_subnode(node, fdt, 0) {
        int reg_len, banks, i;
        const fdt32_t *reg;

        if (!is_memory_node(fdt, node) || !dt_available(fdt, node))
            continue;
        any = true;
        reg = fdt_getprop(fdt, node, "reg", &reg_len);
        if (!reg || reg_len <= 0 || reg_len % (tuple * 4))
            return -1;
        banks = reg_len / (tuple * 4);
        for (i = 0; i < banks; ++i) {
            uint64_t address, size;

            if (dt_raw_reg(fdt, node, (unsigned)i, &address, &size))
                return -1;
            if (!size)
                return -1;
            if (sum > UINT64_MAX - size)
                return -1;
            sum += size;
        }
    }
    if (!any)
        return -1;

    *total = sum;
    return 0;
}

/* The measured stock active-reference fingerprint shared by the two stock
 * board callers: the .096 TB322FC (Elden, CPHY) and the DTB12+DTBO47 TB323FU
 * (Baldur, DPHY). Only this exact shape has evidence; the .259 BL runtime FDT
 * is unverified and any other layout stays UNKNOWN. The caller supplies the
 * exact panel node name and qcom,mdss-dsi-panel-name it measured. The
 * fingerprint requires the exact /soc/qcom,dsi-display-primary node (single
 * "qcom,dsi-display" compatible, "primary" label, itself and its ancestors
 * available) to resolve its single-cell nonzero qcom,dsi-default-panel, via
 * dt_reference index 0, to one unique enabled node whose name and panel name
 * match. No alternative primary and no standby panel is ever searched. */
static bool stock_active_panel(const void *fdt, const char *node_name,
                               const char *panel_name)
{
    int primary, target;
    uint32_t phandle;
    const char *name, *s;

    primary = fdt_path_offset(fdt, "/soc/qcom,dsi-display-primary");
    if (primary < 0 || !dt_available(fdt, primary))
        return false;
    s = dt_string(fdt, primary, "compatible");
    if (!s || strcmp(s, "qcom,dsi-display") != 0)
        return false;
    s = dt_string(fdt, primary, "label");
    if (!s || strcmp(s, "primary") != 0)
        return false;

    if (dt_u32(fdt, primary, "qcom,dsi-default-panel", &phandle) || !phandle)
        return false;
    target = dt_reference(fdt, primary, "qcom,dsi-default-panel", 0);
    if (target < 0)
        return false;
    name = fdt_get_name(fdt, target, NULL);
    if (!name || strcmp(name, node_name) != 0)
        return false;
    s = dt_string(fdt, target, "qcom,mdss-dsi-panel-name");
    if (!s || strcmp(s, panel_name) != 0)
        return false;
    return true;
}

void device_info_read(const void *validated_fdt, struct device_info *info)
{
    const char *model, *compatible;
    int compat_len, off, rc;
    size_t cap, used;
    bool truncated, tb322, tb323, soc8750, soc8850;
    uint64_t ram = 0;

    if (!info)
        return;
    memset(info, 0, sizeof(*info));
    info->board = DEVICE_BOARD_UNKNOWN;
    info->soc = DEVICE_SOC_UNKNOWN;
    if (!validated_fdt)
        return;

    /* An empty model string is an unknown model: only a nonempty, well formed
     * single dt_string marks the model known. */
    model = dt_string(validated_fdt, 0, "model");
    if (model && model[0]) {
        size_t length = strlen(model);

        cap = sizeof(info->model) - 1;
        used = length < cap ? length : cap;
        memcpy(info->model, model, used);
        info->model[used] = '\0';
        info->model_known = true;
        info->model_truncated = length > cap;
    }

    compatible = fdt_getprop(validated_fdt, 0, "compatible", &compat_len);
    if (compatible && compatible_list_valid(compatible, compat_len)) {
        cap = sizeof(info->compatible);
        used = 0;
        truncated = false;
        for (off = 0; off < compat_len;) {
            size_t entry = strlen(compatible + off) + 1;

            if (used + entry > cap) {
                truncated = true;
                break;
            }
            memcpy(info->compatible + used, compatible + off, entry);
            used += entry;
            off += (int)entry;
        }
        info->compatible_length = used;
        info->compatible_known = true;
        info->compatible_truncated = truncated;

        soc8750 = list_contains(compatible, compat_len, "qcom,sun") ||
                  list_contains(compatible, compat_len, "qcom,sunp") ||
                  list_contains(compatible, compat_len, "qcom,sm8750");
        soc8850 = list_contains(compatible, compat_len, "qcom,canoe") ||
                  list_contains(compatible, compat_len, "qcom,canoep") ||
                  list_contains(compatible, compat_len, "qcom,kaanapali") ||
                  list_contains(compatible, compat_len, "qcom,sm8850");

        /* The trusted Linux reference pairing (elden + sm8750) is unchanged.
         * A stock TB322FC is additionally accepted from the measured .096
         * panel fingerprint, but only for an exact qcom,sunp root and never
         * when an SM8850-family marker is also present; the fingerprint is
         * not consulted otherwise (sun/sm8750 shapes are not measured). */
        tb322 = (list_contains(compatible, compat_len, "lenovo,elden") &&
                 list_contains(compatible, compat_len, "qcom,sm8750")) ||
                (list_contains(compatible, compat_len, "qcom,sunp") && !soc8850 &&
                 stock_active_panel(
                     validated_fdt,
                     "qcom,mdss_dsi_elden_nt36523_csot_3k_dsc_vid",
                     "nt36523 3k fhd plus 165Hz CPHY video panel"));

        /* A stock TB323FU carries the exact qcom,canoep root without any
         * SM8750-family marker; its board identity comes only from the
         * measured Baldur panel fingerprint. Named Lenovo entries are
         * accepted as before. */
        tb323 = list_contains(compatible, compat_len, "lenovo,tb323fu") ||
                list_contains(compatible, compat_len, "lenovo,baldur") ||
                (list_contains(compatible, compat_len, "qcom,canoep") && !soc8750 &&
                 stock_active_panel(
                     validated_fdt,
                     "qcom,mdss_dsi_baldur_nt36523_csot_3k_dsc_vid",
                     "nt36523 3k fhd plus 165Hz DPHY video panel"));
        if (tb322 != tb323)
            info->board = tb322 ? DEVICE_BOARD_TB322FC : DEVICE_BOARD_TB323FU;

        if (soc8750 != soc8850)
            info->soc = soc8750 ? DEVICE_SOC_SM8750 : DEVICE_SOC_SM8850;
    }

    rc = memory_total(validated_fdt, &ram);
    if (!rc) {
        info->ram_known = true;
        info->dt_reported_ram_bytes = ram;
    }
}
