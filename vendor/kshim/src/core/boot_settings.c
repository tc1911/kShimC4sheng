/* SPDX-License-Identifier: GPL-3.0-only */

#include "boot_settings.h"

#include <stdbool.h>

/* Exact membership tests: never rely on the signed representation or the
 * ordering of the enum values, and never rewrite an out-of-range field. */

static bool device_choice_valid(enum boot_device_choice choice)
{
    switch (choice) {
    case BOOT_DEVICE_AUTO:
    case BOOT_DEVICE_TB322FC:
    case BOOT_DEVICE_TB323FU:
        return true;
    default:
        return false;
    }
}

static bool default_target_valid(enum boot_default_target target)
{
    switch (target) {
    case BOOT_TARGET_MANIFEST:
    case BOOT_TARGET_ANDROID:
    case BOOT_TARGET_INTERNAL:
    case BOOT_TARGET_TF:
        return true;
    default:
        return false;
    }
}

/* Only real boot system targets may appear in the caller's candidate array. */
static bool candidate_valid(enum boot_default_target target)
{
    switch (target) {
    case BOOT_TARGET_ANDROID:
    case BOOT_TARGET_INTERNAL:
    case BOOT_TARGET_TF:
        return true;
    default:
        return false;
    }
}

static bool detected_valid(enum device_board board)
{
    switch (board) {
    case DEVICE_BOARD_UNKNOWN:
    case DEVICE_BOARD_TB322FC:
    case DEVICE_BOARD_TB323FU:
        return true;
    default:
        return false;
    }
}

int boot_settings_init(struct boot_settings *out)
{
    if (out == NULL) {
        return BOOT_SETTINGS_INVALID;
    }

    out->device_choice = BOOT_DEVICE_AUTO;
    out->default_target = BOOT_TARGET_MANIFEST;
    return BOOT_SETTINGS_OK;
}

int boot_settings_validate(const struct boot_settings *settings)
{
    if (settings == NULL) {
        return BOOT_SETTINGS_INVALID;
    }

    if (!device_choice_valid(settings->device_choice) ||
        !default_target_valid(settings->default_target)) {
        return BOOT_SETTINGS_INVALID;
    }

    return BOOT_SETTINGS_OK;
}

int boot_settings_effective_board(const struct boot_settings *settings,
                                  enum device_board detected,
                                  enum device_board *out)
{
    if (out == NULL || !detected_valid(detected) ||
        boot_settings_validate(settings) != BOOT_SETTINGS_OK) {
        return BOOT_SETTINGS_INVALID;
    }

    switch (settings->device_choice) {
    case BOOT_DEVICE_AUTO:
        *out = detected;
        break;
    case BOOT_DEVICE_TB322FC:
        *out = DEVICE_BOARD_TB322FC;
        break;
    case BOOT_DEVICE_TB323FU:
        *out = DEVICE_BOARD_TB323FU;
        break;
    default:
        return BOOT_SETTINGS_INVALID;
    }

    return BOOT_SETTINGS_OK;
}

int boot_settings_resolve_default(const struct boot_settings *settings,
                                  const enum boot_default_target *available,
                                  size_t count,
                                  size_t manifest_initial,
                                  size_t *selected)
{
    size_t i;
    size_t matches = 0;
    size_t first = 0;

    if (selected == NULL || boot_settings_validate(settings) != BOOT_SETTINGS_OK ||
        (count > 0 && available == NULL)) {
        return BOOT_SETTINGS_INVALID;
    }

    for (i = 0; i < count; i++) {
        if (!candidate_valid(available[i])) {
            return BOOT_SETTINGS_INVALID;
        }
    }

    if (settings->default_target == BOOT_TARGET_MANIFEST) {
        if (manifest_initial >= count) {
            return BOOT_SETTINGS_INVALID;
        }
        *selected = manifest_initial;
        return BOOT_SETTINGS_OK;
    }

    for (i = 0; i < count; i++) {
        if (available[i] != settings->default_target) {
            continue;
        }
        if (matches == 0) {
            first = i;
        }
        matches++;
        if (matches > 1) {
            return BOOT_SETTINGS_NEEDS_SELECTION;
        }
    }

    if (matches == 0) {
        return BOOT_SETTINGS_NEEDS_SELECTION;
    }

    *selected = first;
    return BOOT_SETTINGS_OK;
}
