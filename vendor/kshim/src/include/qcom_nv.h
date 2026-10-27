/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

struct kshim_nv_selection {
    uint32_t fingerprint;
    uint8_t index;
    uint8_t language;      /* kshim_menu_language_t */
    uint8_t timeout_s;     /* 0 disables the countdown */
    uint8_t has_settings;  /* timeout_s/language are valid */
};

/* Remember the last menu choice in the low bits of the battery-backed PMIC
 * PMIC SDAM. Both calls return 0 on success and -1 when unavailable. */
int kshim_nv_load(const void *hardware_fdt, struct kshim_nv_selection *out);
int kshim_nv_store(const void *hardware_fdt, uint8_t index, uint32_t fingerprint);
/* Persist the Settings page language and boot delay without disturbing the
 * remembered boot selection. */
int kshim_nv_store_settings(const void *hardware_fdt, uint8_t language,
                            uint8_t timeout_s);

/* Diagnostic for the UI: 0 = a record was read, -1 = read failed,
 * -2 = no valid record. slot is the SDAM base + offset of the record
 * marker (0x71BC) or -1. */
int kshim_nv_last_status(uint8_t *index, int *slot);
