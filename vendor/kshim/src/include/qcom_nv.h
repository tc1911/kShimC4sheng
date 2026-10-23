/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

struct kshim_nv_selection {
    uint32_t fingerprint;
    uint8_t index;
};

/* Remember the last menu choice in the PMIC SDAM scratch area. The record is
 * double-buffered so an interrupted write cannot destroy the previous value.
 * Both calls return 0 on success and -1 when persistence is unavailable. */
int kshim_nv_load(const void *hardware_fdt, struct kshim_nv_selection *out);
int kshim_nv_store(const void *hardware_fdt, uint8_t index, uint32_t fingerprint);
