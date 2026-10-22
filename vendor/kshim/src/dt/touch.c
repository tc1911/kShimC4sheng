/* SPDX-License-Identifier: MIT */
#include <qcom_dt.h>
#include <Library/cr_android.h>
#include <string.h>
#include <stdio.h>

struct pin { int provider; unsigned number; uintptr_t address; uint32_t config; };
static int gpio_ref(const void *f, int node, const char *prop, struct pin *pin, uint32_t *flags)
{
    struct dt_spec s;
    if (dt_specifier(f, node, prop, "#gpio-cells", 0, &s) || s.count != 2 ||
        kshim_dt_gpio(f, s.node, s.args[0], &pin->address, NULL, NULL)) return -1;
    pin->provider = s.node; pin->number = s.args[0]; pin->config = 0;
    if (flags) *flags = s.args[1];
    return 0;
}
static int pinctrl(const void *f, int consumer, struct pin *pins, size_t capacity, size_t *count)
{
    int len;
    const fdt32_t *states = fdt_getprop(f, consumer, "pinctrl-0", &len);
    if (!states || len <= 0 || len % 4) return -1;
    for (int i = 0; i < len / 4; ++i) {
        int state = dt_phandle(f, fdt32_to_cpu(states[i])), provider = state;
        if (state < 0) return -1;
        while (provider > 0 && !fdt_getprop(f, provider, "gpio-controller", NULL))
            provider = fdt_parent_offset(f, provider);
        if (provider <= 0) return -1;
        int depth = 0;
        for (int n = state; n >= 0 && depth >= 0; n = fdt_next_node(f, n, &depth)) {
            if (!dt_available(f, n)) continue;
            int bytes;
            const char *names = fdt_getprop(f, n, "pins", &bytes);
            if (!names) continue;
            if (bytes <= 0 || names[bytes - 1]) return -1;
            for (int off = 0; off < bytes; off += (int)strlen(names + off) + 1) {
                const char *name = names + off;
                if (strncmp(name, "gpio", 4) || !name[4]) return -1;
                unsigned number = 0;
                for (const char *p = name + 4; *p; ++p) {
                    if (*p < '0' || *p > '9' || number > 1000) return -1;
                    number = number * 10 + *p - '0';
                }
                size_t slot;
                for (slot = 0; slot < *count; ++slot)
                    if (pins[slot].provider == provider && pins[slot].number == number) break;
                if (slot == *count) {
                    if (slot == capacity) return -1;
                    pins[slot] = (struct pin){.provider = provider, .number = number};
                    if (kshim_dt_gpio(f, provider, number, &pins[slot].address, NULL, NULL)) return -1;
                    ++*count;
                }
                uint32_t v;
                const char *function = dt_string(f, n, "function");
                if (function) {
                    if (kshim_dt_gpio(f, provider, number, &pins[slot].address, function, &v)) return -1;
                    pins[slot].config = (pins[slot].config & ~(15U << 2)) | (v << 2);
                }
                if (fdt_getprop(f, n, "drive-strength", NULL)) {
                    if (dt_u32(f, n, "drive-strength", &v) || v < 2 || v > 16 || (v & 1)) return -1;
                    pins[slot].config = (pins[slot].config & ~(7U << 6)) | ((v / 2 - 1) << 6);
                }
                const char *biases[] = {"bias-disable", "bias-pull-down", "bias-bus-hold", "bias-pull-up"};
                unsigned bias_count = 0;
                for (unsigned b = 0; b < 4; ++b) {
                    bool enabled;
                    if (dt_bool(f, n, biases[b], &enabled)) return -1;
                    if (enabled) { pins[slot].config = (pins[slot].config & ~3U) | b; ++bias_count; }
                }
                if (bias_count > 1) return -1;
                bool high, low;
                if (dt_bool(f, n, "output-high", &high) || dt_bool(f, n, "output-low", &low) || (high && low)) return -1;
                if (high || low) pins[slot].config |= 1U << 9;
            }
        }
    }
    return 0;
}
static int clocks(const void *f, int node, struct CrQupResources *r)
{
    const char *names[] = {"se-clk", "m-ahb", "s-ahb"};
    int provider = -1;
    struct dt_range range;
    for (unsigned i = 0; i < 3; ++i) {
        struct dt_spec s;
        int index = dt_string_index(f, node, "clock-names", names[i]);
        if (index < 0 || dt_specifier(f, node, "clocks", "#clock-cells", index, &s) || s.count != 1) return -1;
        const struct CrAndroidClock *c = CrAndroidClock(kshim_dt_soc(f, s.node), s.args[0]);
        if (!c || dt_reg(f, s.node, 0, &range) ||
            !dt_contains(range, range.base + c->vote, 4) || !dt_contains(range, range.base + c->halt, 4)) return -1;
        if (provider != -1 && (provider != s.node || r->vote != range.base + c->vote)) return -1;
        provider = s.node; r->vote = range.base + c->vote;
        if (!i) {
            if (!c->rcg || !dt_contains(range, range.base + c->rcg, 0x3c)) return -1;
            r->rcg = range.base + c->rcg; r->serial_halt = range.base + c->halt; r->serial_vote = c->mask;
        } else { r->ahb_halt[i - 1] = range.base + c->halt; r->ahb_votes |= c->mask; }
    }
    r->source_hz = 19200000; /* The shared core selects a verified undivided CXO slot. */
    r->speed_hz = 400000;
    if (fdt_getprop(f, node, "clock-frequency", NULL) && dt_u32(f, node, "clock-frequency", &r->speed_hz)) return -1;
    return 0;
}
static int rsc(const void *f, int n, struct CrQupResources *r)
{
    uint32_t drv;
    struct dt_range region, tcs;
    if (!dt_available(f, n) || dt_u32(f, n, "qcom,drv-id", &drv) || drv > 4) return -1;
    if (!fdt_node_check_compatible(f, n, "qcom,tcs-drv")) {
        if (dt_reg(f, n, 0, &region) || dt_reg(f, n, 1, &tcs) ||
            tcs.base <= region.base || tcs.base - region.base > UINT32_MAX) return -1;
        r->tcs_offset = tcs.base - region.base;
        r->rsc_size = r->tcs_offset + tcs.size;
    } else if (!fdt_node_check_compatible(f, n, "qcom,rpmh-rsc")) {
        char name[16]; snprintf(name, sizeof(name), "drv-%u", drv);
        if (dt_reg_named(f, n, name, &region) || dt_u32(f, n, "qcom,tcs-offset", &r->tcs_offset)) return -1;
        r->rsc_size = region.size;
    } else return -1;
    r->rsc_base = region.base; r->drv_id = drv;
    int len;
    const fdt32_t *p = fdt_getprop(f, n, "qcom,tcs-config", &len);
    if (!p || len != 32) return -1;
    unsigned offset = 0, seen = 0;
    for (int i = 0; i < 8; i += 2) {
        unsigned type = fdt32_to_cpu(p[i]), count = fdt32_to_cpu(p[i + 1]);
        if (type > 3 || (seen & (1U << type)) || count > 32 - offset) return -1;
        seen |= 1U << type;
        /* Android DT ACTIVE_TCS is 2; offsets follow the property order. */
        if (type == 2) { r->active_offset = offset; r->active_count = count; }
        offset += count;
    }
    return r->active_count && r->tcs_offset <= r->rsc_size &&
        offset * 0x2a0U <= r->rsc_size - r->tcs_offset ? 0 : -1;
}
static int supply(const void *f, int node, struct kshim_touch_resource *t, int *rsc_node)
{
    struct CrQupResources *r = &t->qup;
    if (!dt_available(f, node)) return -1;
    if (!fdt_node_check_compatible(f, node, "regulator-fixed")) {
        struct pin pin; uint32_t flags;
        bool active_high;
        if (r->gate_pin || gpio_ref(f, node, "gpio", &pin, &flags) ||
            dt_bool(f, node, "enable-active-high", &active_high) || !active_high || (flags & 1)) return -1;
        r->gate_pin = pin.address;
        if (fdt_getprop(f, node, "startup-delay-us", NULL) && dt_u32(f, node, "startup-delay-us", &r->gate_delay_us)) return -1;
        if (r->gate_delay_us > 1000000) return -1;
        return 0;
    }
    int parent = fdt_parent_offset(f, node);
    const char *name = dt_string(f, parent, "qcom,resource-name");
    bool xob = fdt_node_check_compatible(f, parent, "qcom,rpmh-xob-regulator") == 0;
    if (!name || !*name || strlen(name) > 8 || r->rail_count == 2 ||
        (!xob && fdt_node_check_compatible(f, parent, "qcom,rpmh-vrm-regulator"))) return -1;
    unsigned i = r->rail_count++;
    strcpy(t->rail_names[i], name); r->rails[i].name = t->rail_names[i];
    uint32_t min, max;
    if (dt_u32(f, node, "regulator-min-microvolt", &min) ||
        dt_u32(f, node, "regulator-max-microvolt", &max) || min > max ||
        min > 3300000 || min < 1000000 || min % 1000) return -1;
    if (!xob) {
        r->rails[i].millivolts = min / 1000;
        const char *type = dt_string(f, parent, "qcom,regulator-type");
        if (!type || strcmp(type, "pmic5-ldo")) return -1;
    }
    if (fdt_getprop(f, parent, "mboxes", NULL)) {
        struct dt_spec spec;
        if (dt_specifier(f, parent, "mboxes", "#mbox-cells", 0, &spec)) return -1;
        if (*rsc_node >= 0 && *rsc_node != spec.node) return -1;
        *rsc_node = spec.node;
    }
    return 0;
}
static bool selected_panel(const void *f, int node)
{
    int length;
    const fdt32_t *panels = fdt_getprop(f, node, "panel", &length);
    if (!panels) return length == -FDT_ERR_NOTFOUND;
    if (length <= 0 || length % 4) return false;
    for (int n = fdt_next_node(f, -1, NULL); n >= 0; n = fdt_next_node(f, n, NULL)) {
        bool active;
        if (dt_bool(f, n, "qcom,dsi-display-active", &active)) return false;
        if (!active || !dt_available(f, n)) continue;
        int panel = dt_reference(f, n, "qcom,dsi-panel", 0);
        for (int i = 0; i < length / 4; ++i)
            if (panel >= 0 && panel == dt_phandle(f, fdt32_to_cpu(panels[i]))) return true;
    }
    return false;
}
int kshim_dt_touch(const void *f, struct kshim_touch_resource *t)
{
    memset(t, 0, sizeof(*t)); t->node = -1;
    for (int n = fdt_node_offset_by_compatible(f, -1, "st,fts"); n >= 0;
         n = fdt_node_offset_by_compatible(f, n, "st,fts")) {
        if (!dt_available(f, n)) continue;
        int bus = fdt_parent_offset(f, n);
        const char *selection = dt_string(f, bus, "qcom,i2c-touch-active");
        if (fdt_getprop(f, bus, "qcom,i2c-touch-active", NULL) && (!selection || strcmp(selection, "st,fts"))) continue;
        if (!selected_panel(f, n)) continue;
        if (t->node >= 0) return -1;
        t->node = n;
    }
    if (t->node < 0) return -1;
    int n = t->node, bus = fdt_parent_offset(f, n);
    struct dt_range range;
    uint64_t address, size;
    if (fdt_node_check_compatible(f, bus, "qcom,i2c-geni") || dt_reg(f, bus, 0, &range) || range.size < 0x1000 ||
        dt_raw_reg(f, n, 0, &address, &size) || address > 0x7f || size) return -1;
    t->address = address; t->qup.geni = range.base; t->qup.geni_size = range.size;
    int wrapper = dt_reference(f, bus, "qcom,wrapper-core", 0);
    if (wrapper < 0 || dt_reg(f, wrapper, 0, &range)) return -1;
    /* The FTM4 property family (st,irq-gpio and friends) is not supported;
     * reject such a node instead of relabelling it as FTM5. */
    if (fdt_getprop(f, n, "st,irq-gpio", NULL)) return -1;
    if (!fdt_getprop(f, n, "fts,irq-gpio", NULL)) return -1;
    t->generation = 5;
    t->width = 1440; t->height = 2880;
    if (dt_u32(f, n, "fts,x-max", &t->width) || dt_u32(f, n, "fts,y-max", &t->height)) return -1;
    if (!t->width || !t->height || t->width > 8192 || t->height > 8192) return -1;
    if (dt_bool(f, n, "fts,x-flip", &t->flip_x) ||
        dt_bool(f, n, "fts,y-flip", &t->flip_y)) return -1;
    struct pin irq, reset, pins[8] = {0}; size_t count = 0; uint32_t flags;
    if (gpio_ref(f, n, "fts,irq-gpio", &irq, &flags) ||
        gpio_ref(f, n, "fts,reset-gpio", &reset, NULL) ||
        pinctrl(f, bus, pins, 8, &count) || count != 2) return -1;
    t->qup.irq_active_low = (flags & 0xf) == 8 || (flags & 1);
    for (size_t i = 0; i < 2; ++i) { t->qup.bus_pins[i] = pins[i].address; t->qup.bus_config[i] = pins[i].config; }
    count = 0;
    if (pinctrl(f, n, pins, 8, &count)) return -1;
    bool got_irq = false, got_reset = false;
    for (size_t i = 0; i < count; ++i) {
        if (pins[i].address == irq.address) { t->qup.irq_config = pins[i].config & ~(1U << 9); got_irq = true; }
        if (pins[i].address == reset.address) { t->qup.reset_config = pins[i].config | (1U << 9); got_reset = true; }
    }
    if (!got_irq || !got_reset || clocks(f, bus, &t->qup)) return -1;
    t->qup.irq_pin = irq.address; t->qup.reset_pin = reset.address;
    t->qup.reset_low_us = 10000;
    int controller = -1;
    if (supply(f, dt_reference(f, n, "avdd-supply", 0), t, &controller) ||
        supply(f, dt_reference(f, n, "vdd-supply", 0), t, &controller)) return -1;
    if (controller < 0) {
        for (int p = fdt_next_node(f, -1, NULL); p >= 0; p = fdt_next_node(f, p, NULL)) {
            const char *label = dt_string(f, p, "label");
            if (label && !strcmp(label, "apps_rsc") && dt_available(f, p)) {
                if (controller >= 0) return -1;
                controller = p;
            }
        }
    }
    if (controller < 0 || rsc(f, controller, &t->qup)) return -1;
    int db = -1;
    for (int p = fdt_node_offset_by_compatible(f, -1, "qcom,cmd-db"); p >= 0;
         p = fdt_node_offset_by_compatible(f, p, "qcom,cmd-db")) {
        if (!dt_available(f, p)) continue;
        if (db >= 0 || dt_reg(f, p, 0, &range)) return -1;
        db = p;
    }
    if (db < 0) return -1;
    if (range.size == 8) t->qup.cmd_db_dictionary = range.base;
    else { t->qup.cmd_db_base = range.base; t->qup.cmd_db_size = range.size; }
    return 0;
}
