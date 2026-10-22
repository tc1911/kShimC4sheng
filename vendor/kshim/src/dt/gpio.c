/* SPDX-License-Identifier: MIT */
#include <qcom_dt.h>
#include <Library/cr_android.h>
#include <string.h>
unsigned kshim_dt_soc(const void *f, int node)
{
    if (!fdt_node_check_compatible(f, node, "qcom,gcc-sm8150-v2") ||
        !fdt_node_check_compatible(f, node, "qcom,sm8150-pinctrl") ||
        !fdt_node_check_compatible(f, node, "qcom,gcc-sm8150")) return 8150;
    if (!fdt_node_check_compatible(f, node, "qcom,kona-pinctrl") ||
        !fdt_node_check_compatible(f, node, "qcom,sm8250-tlmm") ||
        !fdt_node_check_compatible(f, node, "qcom,gcc-kona") ||
        !fdt_node_check_compatible(f, node, "qcom,gcc-sm8250")) return 8250;
    return 0;
}
int kshim_dt_gpio(const void *f, int provider, uint32_t pin,
                  uintptr_t *address, const char *function, uint32_t *mux)
{
    struct dt_range range;
    const struct CrAndroidPin *layout = CrAndroidPin(kshim_dt_soc(f, provider), pin);
    if (!layout || !layout->functions[0] || !address || dt_reg(f, provider, 0, &range) ||
        !dt_contains(range, range.base + layout->offset, 0x10)) return -1;
    *address = range.base + layout->offset;
    if (function) {
        if (!mux) return -1;
        for (unsigned i = 0; i < 10; ++i)
            if (layout->functions[i] && !strcmp(layout->functions[i], function)) { *mux = i; return 0; }
        return -1;
    }
    return 0;
}
