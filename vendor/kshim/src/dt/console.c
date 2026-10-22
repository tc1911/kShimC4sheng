/* SPDX-License-Identifier: MIT */
#include <dt.h>
#include <kshim_console.h>
#include <string.h>
static int kind(const void *f, int node)
{
    if (!fdt_node_check_compatible(f, node, "qcom,msm-geni-console") ||
        !fdt_node_check_compatible(f, node, "qcom,geni-debug-uart")) return dt_available(f, node) ? 1 : -1;
    if (!fdt_node_check_compatible(f, node, "arm,pl011")) return dt_available(f, node) ? 0 : -1;
    return -1;
}
static int node_resource(const void *f, int node, struct kshim_console_resource *r)
{
    struct dt_range reg;
    int type = kind(f, node);
    if (type < 0 || dt_reg(f, node, 0, &reg) || reg.base > UINTPTR_MAX ||
        reg.size > SIZE_MAX || reg.size < (type ? 0x1000U : 0x100U)) return -1;
    *r = (struct kshim_console_resource){reg.base, reg.size, type != 0};
    return 0;
}
static int hex(const char *p, uint64_t *value)
{
    uint64_t v = 0; unsigned digits = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    for (; *p && *p != ' ' && *p != ','; ++p) {
        unsigned digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else return -1;
        if (v > (UINT64_MAX - digit) / 16) return -1;
        v = v * 16 + digit; ++digits;
    }
    if (!digits || !v) return -1;
    *value = v; return 0;
}
int kshim_console_probe(const void *f, struct kshim_console_resource *r)
{
    if (!f || !r) return -1;
    int chosen = fdt_path_offset(f, "/chosen");
    const char *args = chosen >= 0 ? dt_string(f, chosen, "bootargs") : NULL;
    const char *prefixes[] = {"earlycon=msm_geni_serial,", "earlycon=qcom_geni,", "earlycon=pl011,mmio32,", "earlycon=pl011,"};
    if (args) {
        for (const char *p = args; *p;) {
            for (unsigned i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
                size_t length = strlen(prefixes[i]);
                if (strncmp(p, prefixes[i], length)) continue;
                uint64_t base;
                if (hex(p + length, &base)) return -1;
                int found = -1;
                for (int n = fdt_next_node(f, -1, NULL); n >= 0; n = fdt_next_node(f, n, NULL)) {
                    struct kshim_console_resource candidate;
                    if (kind(f, n) != (i < 2) || node_resource(f, n, &candidate) || candidate.base != base) continue;
                    if (found >= 0) return -1;
                    found = n; *r = candidate;
                }
                return found >= 0 ? 0 : -1;
            }
            while (*p && *p != ' ') ++p;
            while (*p == ' ') ++p;
        }
    }
    const char *path = chosen >= 0 ? dt_string(f, chosen, "stdout-path") : NULL;
    if (path) {
        size_t length = strcspn(path, ":"); char name[256];
        if (!length || length >= sizeof(name)) return -1;
        memcpy(name, path, length); name[length] = 0;
        if (name[0] != '/') {
            int aliases = fdt_path_offset(f, "/aliases");
            path = aliases >= 0 ? dt_string(f, aliases, name) : NULL;
            if (!path) return -1;
        } else path = name;
        return node_resource(f, fdt_path_offset(f, path), r);
    }
    int found = -1;
    for (int n = fdt_next_node(f, -1, NULL); n >= 0; n = fdt_next_node(f, n, NULL)) {
        struct kshim_console_resource candidate;
        if (node_resource(f, n, &candidate)) continue;
        if (found >= 0) return -1;
        found = n; *r = candidate;
    }
    return found >= 0 ? 0 : -1;
}
