/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stddef.h>

#include "device_info.h"

/* Boot settings record and the pure decision helpers over it (kShim).
 *
 * The record is plain data: a device choice and a default boot target. It is
 * deliberately free of backend, disk-format or LVGL types so it can be stored,
 * compared and tested without any storage or hardware access. The two enums
 * use stable semantic IDs, not menu row indexes and not a serialized C-enum
 * layout, so persisted values keep their meaning when the menu is reordered.
 *
 * boot_settings_init() fills AUTO + MANIFEST. The record is then validated by
 * boot_settings_validate(), which only checks that both fields are inside
 * their enum ranges and never mutates the record.
 *
 * boot_settings_effective_board() turns the stored choice into the board that
 * will actually be used. AUTO follows the caller's already detected board,
 * UNKNOWN included. A manual choice always yields the requested board, even
 * when detection disagrees or is UNKNOWN, and the stored choice is never
 * rewritten by detection; it stays until the caller explicitly sets AUTO.
 *
 * boot_settings_resolve_default() maps the stored default target onto the
 * caller's current array of real boot system targets (ANDROID / INTERNAL / TF).
 * The MANIFEST target means "the caller's initial row", so it returns
 * manifest_initial once that index is proven in range. Any other target must
 * match exactly one candidate; zero or several matches request a new selection
 * by returning BOOT_SETTINGS_NEEDS_SELECTION and leave the output untouched.
 *
 * All helpers are pure: no heap, no storage and no hardware access, and none
 * of them mutate their inputs.
 */

enum boot_device_choice {
    BOOT_DEVICE_AUTO = 0,
    BOOT_DEVICE_TB322FC = 1,
    BOOT_DEVICE_TB323FU = 2,
};

enum boot_default_target {
    BOOT_TARGET_MANIFEST = 0,
    BOOT_TARGET_ANDROID = 1,
    BOOT_TARGET_INTERNAL = 2,
    BOOT_TARGET_TF = 3,
};

enum boot_settings_result {
    BOOT_SETTINGS_OK = 0,
    BOOT_SETTINGS_NEEDS_SELECTION = 1,
    BOOT_SETTINGS_INVALID = -1,
};

struct boot_settings {
    enum boot_device_choice device_choice;
    enum boot_default_target default_target;
};

/* Initialize out to the safe default record: AUTO + MANIFEST.
 * Returns BOOT_SETTINGS_OK, or BOOT_SETTINGS_INVALID when out is NULL. */
int boot_settings_init(struct boot_settings *out);

/* Check that both fields are inside their enum ranges. Does not mutate.
 * Returns BOOT_SETTINGS_OK, or BOOT_SETTINGS_INVALID for a NULL or
 * out-of-range record. */
int boot_settings_validate(const struct boot_settings *settings);

/* Resolve the effective board. AUTO yields detected; a manual choice yields the
 * requested board regardless of detected. detected is passed by value and is
 * never rewritten, settings is const. Returns BOOT_SETTINGS_INVALID, leaving
 * *out unchanged, when settings, out or detected is invalid. */
int boot_settings_effective_board(const struct boot_settings *settings,
                                  enum device_board detected,
                                  enum device_board *out);

/* Map the stored default target onto available, whose entries are only real
 * boot system targets (ANDROID / INTERNAL / TF) and whose indexes are the
 * caller's selectable target indexes. MANIFEST returns manifest_initial once
 * it is in range. Any other target returns the unique matching index, or
 * BOOT_SETTINGS_NEEDS_SELECTION when there is no match or more than one, in
 * which case *selected is left unchanged. Returns BOOT_SETTINGS_INVALID,
 * leaving *selected unchanged, for a NULL output, a nonempty NULL array, an
 * invalid record or candidate ID, or an out-of-range MANIFEST index. */
int boot_settings_resolve_default(const struct boot_settings *settings,
                                  const enum boot_default_target *available,
                                  size_t count,
                                  size_t manifest_initial,
                                  size_t *selected);
