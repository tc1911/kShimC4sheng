/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read-only device information derived from an already validated runtime FDT.
 *
 * device_info_read() fills a fixed-size, caller-owned record. It never
 * allocates, never writes to the device tree and never touches hardware or
 * MMIO; the blob is strictly read-only input and must already have been
 * accepted by dt_validate() before the call. The record is fully
 * zero-initialized first, then populated, so an unknown or missing value is
 * always observable as an explicit unknown state:
 *   - board / soc: DEVICE_BOARD_UNKNOWN / DEVICE_SOC_UNKNOWN
 *   - model / compatible: empty string, *_known == false
 *   - dt_reported_ram_bytes == 0 with ram_known == false
 *
 * Board identity uses exact root-compatible evidence and, for the measured
 * stock profile below, the active primary-panel reference; the model text, a
 * generic SoC identifier alone, a prefix or a case-folded match never
 * identifies a board:
 *   - DEVICE_BOARD_TB322FC: exact "lenovo,elden" together with exact
 *     "qcom,sm8750" (the trusted Linux TB322FC reference), or the alternate
 *     strict stock fingerprint described below.
 *   - DEVICE_BOARD_TB323FU: exact "lenovo,tb323fu" or exact "lenovo,baldur".
 *   - "qcom,canoep" alone is a SoC marker, not a board identity.
 * Conflicting board evidence leave the board UNKNOWN; unrelated
 * devices that share a SoC keep the board UNKNOWN.
 *
 * The alternate strict TB322FC stock fingerprint is the measured .096
 * active-reference shape only. It requires an exact root "qcom,sunp" with no
 * SM8850-family marker, plus an enabled exact "/soc/qcom,dsi-display-primary"
 * (single "qcom,dsi-display" compatible, "primary" label) whose single-u32
 * "qcom,dsi-default-panel" uniquely resolves to the enabled exact Elden
 * NT36523 3K panel node with the exact "qcom,mdss-dsi-panel-name". The .259 BL
 * runtime FDT stays unverified, any unmatched generic-SoC tree stays UNKNOWN,
 * and a mere standby panel never counts.
 *
 * The SoC is classified independently from exact entries:
 *   - DEVICE_SOC_SM8750: "qcom,sun", "qcom,sunp" or "qcom,sm8750".
 *   - DEVICE_SOC_SM8850: "qcom,canoe", "qcom,canoep", "qcom,kaanapali" or
 *     "qcom,sm8850".
 * Entries from both groups leave the SoC UNKNOWN.
 *
 * The compatible field preserves the original root compatible list verbatim
 * as a NUL-separated list: whole entries are copied while they fit and
 * compatible_length counts every copied byte including the NUL separators. If
 * one entry does not fit, the copy stops and compatible_truncated is set;
 * classification always uses the complete original list, never this copy. A
 * malformed list (empty entries or a missing terminal NUL) yields an empty
 * compatible, compatible_known == false and no classifications at all. The
 * model copy may likewise truncate; it stays NUL-terminated and sets
 * model_truncated.
 *
 * dt_reported_ram_bytes is an unadjusted sum of the sizes of the reg entries
 * of all enabled root-level memory nodes, decoded with the root
 * #address-cells / #size-cells (default 2/1, only 1 or 2 supported). It is DT
 * reported RAM only: overlapping or duplicate ranges are not normalized,
 * reservations are not subtracted, and it is neither available nor physical
 * RAM. Any enabled memory node with a missing or malformed reg, an unsupported
 * cell count, a zero-sized tuple, an address-end overflow, or a sum overflow
 * makes the whole total unknown (ram_known == false, zero bytes). Disabled
 * memory nodes are excluded; with no enabled memory node the total is unknown.
 */
enum device_board {
    DEVICE_BOARD_UNKNOWN = 0,
    DEVICE_BOARD_TB322FC,
    DEVICE_BOARD_TB323FU,
};

enum device_soc {
    DEVICE_SOC_UNKNOWN = 0,
    DEVICE_SOC_SM8750,
    DEVICE_SOC_SM8850,
};

struct device_info {
    enum device_board board;
    enum device_soc soc;
    char model[128];
    bool model_known;
    bool model_truncated;
    char compatible[256];
    size_t compatible_length;
    bool compatible_known;
    bool compatible_truncated;
    uint64_t dt_reported_ram_bytes;
    bool ram_known;
};

/* Populate info from validated_fdt. validated_fdt must have passed
 * dt_validate(); info is caller-owned and fully overwritten. */
void device_info_read(const void *validated_fdt, struct device_info *info);
