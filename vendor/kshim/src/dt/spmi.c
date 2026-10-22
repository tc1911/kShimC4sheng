/* SPDX-License-Identifier: MIT */
#include <qcom_dt.h>
#include <string.h>

/* A recognized SPMI controller bus.  Two layouts exist:
 *
 *   - generic: a "qcom,spmi-pmic-arb" node is itself the bus.
 *   - canoe:   a "qcom,canoe-spmi-pmic-arb" arbiter owns the shared
 *              regions and the ee/channel selection, while the actual
 *              bus is the arbiter's enabled "spmi@..." child.
 *
 * Disabled nodes and disabled canoe arbiters are never treated as buses.
 */
struct kshim_spmi_bus {
    int bus;
    int arb;
    bool nested;
};

static bool kshim_spmi_bus_at(const void *f, int n, struct kshim_spmi_bus *b)
{
    if (!f || n < 0) return false;

    if (fdt_node_check_compatible(f, n, "qcom,spmi-pmic-arb") == 0) {
        if (!dt_available(f, n)) return false;
        if (b) { b->bus = n; b->arb = n; b->nested = false; }
        return true;
    }

    const char *name = fdt_get_name(f, n, NULL);
    if (!name || strncmp(name, "spmi", 4) != 0) return false;
    if (name[4] != '\0' && name[4] != '@') return false;

    int parent = dt_parent_offset(f, n);
    if (parent < 0) return false;
    if (fdt_node_check_compatible(f, parent, "qcom,canoe-spmi-pmic-arb") != 0)
        return false;
    if (!dt_available(f, n)) return false;

    if (b) { b->bus = n; b->arb = parent; b->nested = true; }
    return true;
}

/* Region name for a type.  Selected explicitly from the layout rather
 * than guessed by falling back between the two spellings. */
static const char *kshim_spmi_region_name(unsigned i, bool nested)
{
    switch (i) {
    case SPMI_MEMORY_REGION_TYPE_CORE:      return "core";
    case SPMI_MEMORY_REGION_TYPE_CH_SLAVES: return "chnls";
    case SPMI_MEMORY_REGION_TYPE_OBSERVER:  return "obsrvr";
    case SPMI_MEMORY_REGION_TYPE_INTERRUPT: return "intr";
    case SPMI_MEMORY_REGION_TYPE_CONFIG:    return "cnfg";
    case SPMI_MEMORY_REGION_TYPE_CH_MAP:
        return nested ? "chnl_map" : "chnls-map";
    case SPMI_MEMORY_REGION_TYPE_CH_OWNER:
        return nested ? "chnl_owner" : "chnls-owner";
    default:
        return NULL;
    }
}

/* Node that owns a region: shared regions live on the arbiter for the
 * nested layout, bus-local regions on the bus.  Generic layouts keep
 * everything on their single node. */
static int kshim_spmi_region_src(unsigned i, bool nested, int arb, int bus)
{
    if (!nested) return arb;
    switch (i) {
    case SPMI_MEMORY_REGION_TYPE_CORE:
    case SPMI_MEMORY_REGION_TYPE_CH_SLAVES:
    case SPMI_MEMORY_REGION_TYPE_OBSERVER:
    case SPMI_MEMORY_REGION_TYPE_CH_MAP:
        return arb;
    default:
        return bus;
    }
}

static bool kshim_spmi_region_required(unsigned i, bool nested)
{
    if (nested) return true;
    return i == SPMI_MEMORY_REGION_TYPE_CORE ||
           i == SPMI_MEMORY_REGION_TYPE_CH_SLAVES ||
           i == SPMI_MEMORY_REGION_TYPE_OBSERVER;
}

struct kshim_spmi_sel {
    uint32_t ee;
    uint32_t channel;
    uint32_t bus;
};

/* Optional bus-id: read from the bus node when present, else from the
 * arbiter, else zero.  Absence is not an error; a malformed or
 * out-of-range value is. */
static int kshim_spmi_bus_id(const void *f, const struct kshim_spmi_bus *b,
                             uint32_t *bus)
{
    *bus = 0;
    if (fdt_getprop(f, b->bus, "qcom,bus-id", NULL)) {
        if (dt_u32(f, b->bus, "qcom,bus-id", bus)) return -1;
    } else if (fdt_getprop(f, b->arb, "qcom,bus-id", NULL)) {
        if (dt_u32(f, b->arb, "qcom,bus-id", bus)) return -1;
    }
    if (*bus > 3) return -1;
    return 0;
}

/* Resource enumeration selection.  ee/channel come from the arbiter for
 * the nested layout and from the bus node for the generic layout; the
 * optional bus-id is resolved through kshim_spmi_bus_id. */
static int kshim_spmi_select(const void *f, const struct kshim_spmi_bus *b,
                             struct kshim_spmi_sel *sel)
{
    int owner = b->nested ? b->arb : b->bus;
    sel->channel = 0;
    if (dt_u32(f, owner, "qcom,ee", &sel->ee) || sel->ee > 5) return -1;
    if (fdt_getprop(f, owner, "qcom,channel", NULL) &&
        dt_u32(f, owner, "qcom,channel", &sel->channel)) return -1;
    if (sel->channel > 5) return -1;
    return kshim_spmi_bus_id(f, b, &sel->bus);
}

int kshim_dt_spmi(const void *f, struct kshim_resources *r)
{
    r->spmi_count = 0;
    for (int n = fdt_next_node(f, -1, NULL); n >= 0; n = fdt_next_node(f, n, NULL)) {
        struct kshim_spmi_bus b;
        if (!kshim_spmi_bus_at(f, n, &b)) continue;
        if (r->spmi_count == KSHIM_SPMI_MAX) return -1;
        struct kshim_spmi_resource *s = &r->spmi[r->spmi_count];
        memset(s, 0, sizeof(*s));
        s->node = b.bus;
        bool nested = b.nested;
        for (unsigned i = 0; i < SPMI_MEMORY_REGION_TYPE_MAX; ++i) {
            struct dt_range range;
            const char *name = kshim_spmi_region_name(i, nested);
            bool required = kshim_spmi_region_required(i, nested);
            if (!name) { if (required) return -1; continue; }
            if (dt_reg_named(f, kshim_spmi_region_src(i, nested, b.arb, b.bus),
                             name, &range)) {
                if (required) return -1;
                continue;
            }
            if ((range.base & 3) || range.base > UINTPTR_MAX || range.size > SIZE_MAX)
                return -1;
            s->controller.Regions[i] = (SpmiMemoryRegion){range.base, range.size};
        }
        struct kshim_spmi_sel sel;
        if (kshim_spmi_select(f, &b, &sel)) return -1;
        s->controller.ActiveEE = sel.ee;
        s->controller.Channel = sel.channel;
        s->controller.BusId = sel.bus;
        ++r->spmi_count;
    }
    return r->spmi_count ? 0 : -1;
}

int kshim_dt_spmi_identity(const void *f, int node, uint8_t *controller,
                           uint8_t *bus, uint8_t *sid)
{
    if (!f || node < 0 || !controller || !bus || !sid) return -1;
    if (!dt_available(f, node)) return -1;

    /* Walk ancestors until the PMIC's parent is an enabled recognized bus. */
    struct kshim_spmi_bus b;
    int pmic = node;
    bool found = false;
    for (; pmic > 0; pmic = dt_parent_offset(f, pmic)) {
        int parent = dt_parent_offset(f, pmic);
        if (parent >= 0 && kshim_spmi_bus_at(f, parent, &b)) { found = true; break; }
    }
    if (!found) return -1;

    int len;
    const fdt32_t *reg = fdt_getprop(f, pmic, "reg", &len);
    if (!reg || len != 8 || fdt32_to_cpu(reg[0]) > 15 || fdt32_to_cpu(reg[1]))
        return -1;

    /* The enumeration index and bus must match the resource record. */
    unsigned index = 0;
    for (int n = fdt_next_node(f, -1, NULL); n >= 0; n = fdt_next_node(f, n, NULL)) {
        struct kshim_spmi_bus cand;
        if (!kshim_spmi_bus_at(f, n, &cand)) continue;
        if (cand.bus == b.bus) {
            uint32_t bus_id;
            if (index >= KSHIM_SPMI_MAX) return -1;
            if (kshim_spmi_bus_id(f, &cand, &bus_id)) return -1;
            *controller = (uint8_t)index;
            *bus = (uint8_t)bus_id;
            *sid = (uint8_t)fdt32_to_cpu(reg[0]);
            return 0;
        }
        ++index;
    }
    return -1;
}

/* Downstream Qualcomm encodes PMIC peripheral reg as <offset length> with
 * address-cells=2, size-cells=0. Mainline uses <offset>, size-cells=0.
 * This is a bus-specific tuple, not a 64-bit physical MMIO address.
 *
 * A size-cells=0 PMIC with address-cells=1 may also expose a multi-cell
 * reg list; in that case only a well-formed, uniquely named "hlos" entry
 * (or its stock alias "pon_hlos") is accepted and the address is taken
 * from its cell. */
int kshim_dt_spmi_reg(const void *f, int node, uint16_t *offset)
{
    uint8_t controller, bus, sid;
    if (!offset || kshim_dt_spmi_identity(f, node, &controller, &bus, &sid)) return -1;

    int parent = dt_parent_offset(f, node), len;
    uint32_t ac, sc;
    const fdt32_t *p = fdt_getprop(f, node, "reg", &len);
    if (!p || dt_u32(f, parent, "#address-cells", &ac) ||
        dt_u32(f, parent, "#size-cells", &sc) || sc ||
        (ac != 1 && ac != 2)) return -1;

    uint32_t a;
    if (ac == 2) {
        if (len != 8) return -1;
        uint32_t size = fdt32_to_cpu(p[1]);
        a = fdt32_to_cpu(p[0]);
        if ((a & 0xffU) || a > UINT16_MAX || size > 0x10000U - a) return -1;
    } else {
        if (len == 4) {
            a = fdt32_to_cpu(p[0]);
        } else if (len > 4 && (len % 4) == 0) {
            int count = fdt_stringlist_count(f, node, "reg-names");
            if (count != len / 4) return -1;
            /* Locate the single HLOS cell by its name index.  Either the
             * exact name "hlos" or the stock alias "pon_hlos" is accepted;
             * a repeated name or both aliases together are ambiguous. */
            int index = -1;
            for (int i = 0; i < count; ++i) {
                const char *name = fdt_stringlist_get(f, node, "reg-names", i, NULL);
                if (!name || !*name) return -1;
                if (strcmp(name, "hlos") && strcmp(name, "pon_hlos")) continue;
                if (index >= 0) return -1;
                index = i;
            }
            if (index < 0) return -1;
            a = fdt32_to_cpu(p[index]);
        } else {
            return -1;
        }
        if ((a & 0xffU) || a > UINT16_MAX) return -1;
    }
    *offset = (uint16_t)a;
    return 0;
}
