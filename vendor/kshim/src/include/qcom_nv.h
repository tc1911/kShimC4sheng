/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

struct kshim_nv_selection {
    uint32_t fingerprint;
    uint8_t index;
};

/* Remember the last menu choice in the low bits of the battery-backed PMIC
 * PMIC SDAM. Both calls return 0 on success and -1 when unavailable. */
int kshim_nv_load(const void *hardware_fdt, struct kshim_nv_selection *out);
int kshim_nv_store(const void *hardware_fdt, uint8_t index, uint32_t fingerprint);

/* Diagnostic for the UI: 0 = a record was read, -1 = read failed,
 * -2 = no valid record. slot is the SDAM base + offset of the record
 * marker (0x71BC) or -1. */
int kshim_nv_last_status(uint8_t *index, int *slot);
