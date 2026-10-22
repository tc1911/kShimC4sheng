/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Host tests for the kShim boot settings API (boot_settings.h).
 *
 * These are plain assertion-based tests over the pure decision helpers. They
 * assert only the documented success and error contracts, never a test-only
 * stub return, so the same file links straight against the real decision
 * module (src/core/boot_settings.c) with no stub and no storage/UFS backend.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "boot_settings.h"

/* First positive test: the safe defaults and the frozen numeric IDs. */
static void test_defaults(void)
{
    struct boot_settings settings;

    fprintf(stderr, "boot_settings: defaults and fixed IDs\n");

    assert(BOOT_DEVICE_AUTO == 0);
    assert(BOOT_DEVICE_TB322FC == 1);
    assert(BOOT_DEVICE_TB323FU == 2);
    assert(BOOT_TARGET_MANIFEST == 0);
    assert(BOOT_TARGET_ANDROID == 1);
    assert(BOOT_TARGET_INTERNAL == 2);
    assert(BOOT_TARGET_TF == 3);
    assert(BOOT_SETTINGS_OK == 0);
    assert(BOOT_SETTINGS_NEEDS_SELECTION == 1);
    assert(BOOT_SETTINGS_INVALID == -1);

    memset(&settings, 0xAA, sizeof(settings));
    assert(boot_settings_init(&settings) == BOOT_SETTINGS_OK);
    assert(settings.device_choice == BOOT_DEVICE_AUTO);
    assert(settings.default_target == BOOT_TARGET_MANIFEST);
}

/* A valid record stays untouched by every read-only helper. */
static void test_valid_record_unchanged(void)
{
    struct boot_settings settings = { BOOT_DEVICE_TB322FC, BOOT_TARGET_TF };
    const enum boot_default_target available[] = {
        BOOT_TARGET_ANDROID, BOOT_TARGET_INTERNAL, BOOT_TARGET_TF,
    };
    enum device_board board = DEVICE_BOARD_UNKNOWN;
    size_t selected = 42;

    assert(boot_settings_validate(&settings) == BOOT_SETTINGS_OK);
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_TB323FU,
                                         &board) == BOOT_SETTINGS_OK);
    assert(boot_settings_resolve_default(&settings, available, 3, 0,
                                         &selected) == BOOT_SETTINGS_OK);

    assert(settings.device_choice == BOOT_DEVICE_TB322FC);
    assert(settings.default_target == BOOT_TARGET_TF);
    assert(board == DEVICE_BOARD_TB322FC);
    assert(selected == 2);
}

/* AUTO follows detection (UNKNOWN included); a manual choice survives changed,
 * mismatched and UNKNOWN detection; restoring AUTO follows detection again. */
static void test_effective_board(void)
{
    struct boot_settings settings;
    enum device_board out = DEVICE_BOARD_TB323FU;

    assert(boot_settings_init(&settings) == BOOT_SETTINGS_OK);

    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_TB322FC,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_TB322FC);
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_TB323FU,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_TB323FU);
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_UNKNOWN,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_UNKNOWN);

    settings.device_choice = BOOT_DEVICE_TB322FC;
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_TB323FU,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_TB322FC);
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_UNKNOWN,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_TB322FC);

    settings.device_choice = BOOT_DEVICE_TB323FU;
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_TB322FC,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_TB323FU);
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_UNKNOWN,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_TB323FU);
    /* The manual choice is stored, not rewritten by a detection mismatch. */
    assert(settings.device_choice == BOOT_DEVICE_TB323FU);

    settings.device_choice = BOOT_DEVICE_AUTO;
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_TB322FC,
                                         &out) == BOOT_SETTINGS_OK);
    assert(out == DEVICE_BOARD_TB322FC);
}

/* Negative and upper out-of-range enum values are rejected for both fields;
 * NULL is rejected. */
static void test_validate_rejects_out_of_range(void)
{
    struct boot_settings valid = { BOOT_DEVICE_TB322FC, BOOT_TARGET_TF };
    struct boot_settings bad;

    assert(boot_settings_validate(NULL) == BOOT_SETTINGS_INVALID);

    bad = valid;
    bad.device_choice = (enum boot_device_choice)-1;
    assert(boot_settings_validate(&bad) == BOOT_SETTINGS_INVALID);
    bad.device_choice = (enum boot_device_choice)3;
    assert(boot_settings_validate(&bad) == BOOT_SETTINGS_INVALID);

    bad = valid;
    bad.default_target = (enum boot_default_target)-1;
    assert(boot_settings_validate(&bad) == BOOT_SETTINGS_INVALID);
    bad.default_target = (enum boot_default_target)4;
    assert(boot_settings_validate(&bad) == BOOT_SETTINGS_INVALID);

    bad = valid;
    bad.device_choice = (enum boot_device_choice)-5;
    bad.default_target = (enum boot_default_target)7;
    assert(boot_settings_validate(&bad) == BOOT_SETTINGS_INVALID);
}

/* MANIFEST may resolve to a nonzero initial index; explicit targets follow the
 * candidate array as it reorders. */
static void test_resolve_default_mapping(void)
{
    struct boot_settings settings;
    const enum boot_default_target order_a[] = {
        BOOT_TARGET_TF, BOOT_TARGET_INTERNAL, BOOT_TARGET_ANDROID,
    };
    size_t selected = 99;

    assert(boot_settings_init(&settings) == BOOT_SETTINGS_OK);
    assert(settings.default_target == BOOT_TARGET_MANIFEST);

    assert(boot_settings_resolve_default(&settings, order_a, 3, 0,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 0);
    selected = 99;
    assert(boot_settings_resolve_default(&settings, order_a, 3, 2,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 2);

    settings.default_target = BOOT_TARGET_TF;
    selected = 99;
    assert(boot_settings_resolve_default(&settings, order_a, 3, 0,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 0);

    settings.default_target = BOOT_TARGET_INTERNAL;
    selected = 99;
    assert(boot_settings_resolve_default(&settings, order_a, 3, 0,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 1);

    settings.default_target = BOOT_TARGET_ANDROID;
    selected = 99;
    assert(boot_settings_resolve_default(&settings, order_a, 3, 0,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 2);
}

/* A missing saved target requests a new selection and stops auto-boot; the
 * sentinel output and the record are untouched. A duplicate saved target also
 * requests selection. An unrelated duplicate leaves a unique match usable. */
static void test_resolve_needs_selection(void)
{
    struct boot_settings settings;
    const enum boot_default_target no_match[] = {
        BOOT_TARGET_ANDROID, BOOT_TARGET_INTERNAL,
    };
    const enum boot_default_target dup_saved[] = {
        BOOT_TARGET_ANDROID, BOOT_TARGET_TF, BOOT_TARGET_TF,
    };
    const enum boot_default_target unrelated_dup[] = {
        BOOT_TARGET_ANDROID, BOOT_TARGET_INTERNAL, BOOT_TARGET_ANDROID,
    };
    size_t selected = 555;

    memset(&settings, 0, sizeof(settings));
    settings.device_choice = BOOT_DEVICE_AUTO;
    settings.default_target = BOOT_TARGET_TF;

    assert(boot_settings_resolve_default(&settings, no_match, 2, 0,
                                         &selected) ==
           BOOT_SETTINGS_NEEDS_SELECTION);
    assert(selected == 555);
    assert(settings.device_choice == BOOT_DEVICE_AUTO);
    assert(settings.default_target == BOOT_TARGET_TF);

    assert(boot_settings_resolve_default(&settings, dup_saved, 3, 0,
                                         &selected) ==
           BOOT_SETTINGS_NEEDS_SELECTION);
    assert(selected == 555);
    assert(settings.default_target == BOOT_TARGET_TF);

    settings.default_target = BOOT_TARGET_INTERNAL;
    assert(boot_settings_resolve_default(&settings, unrelated_dup, 3, 0,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 1);
}

/* Boundary checks implied by the header: empty candidates, NULL and invalid
 * inputs, MANIFEST index bounds, and an explicit target that ignores the
 * unused MANIFEST index. */
static void test_resolve_boundaries(void)
{
    struct boot_settings settings;
    struct boot_settings invalid_record = { (enum boot_device_choice)9,
                                            BOOT_TARGET_ANDROID };
    const enum boot_default_target available[] = {
        BOOT_TARGET_ANDROID, BOOT_TARGET_INTERNAL, BOOT_TARGET_TF,
    };
    const enum boot_default_target bad_candidate[] = {
        BOOT_TARGET_ANDROID, (enum boot_default_target)9, BOOT_TARGET_TF,
    };
    const enum boot_default_target manifest_candidate[] = {
        BOOT_TARGET_ANDROID, BOOT_TARGET_MANIFEST,
    };
    enum device_board board = DEVICE_BOARD_TB323FU;
    size_t selected = 777;

    assert(boot_settings_init(&settings) == BOOT_SETTINGS_OK);

    /* Empty candidate array with an explicit saved target: stop and select. */
    settings.default_target = BOOT_TARGET_ANDROID;
    selected = 777;
    assert(boot_settings_resolve_default(&settings, NULL, 0, 0, &selected) ==
           BOOT_SETTINGS_NEEDS_SELECTION);
    assert(selected == 777);

    /* Nonempty NULL array, NULL output and NULL record are invalid. */
    assert(boot_settings_resolve_default(&settings, NULL, 3, 0, &selected) ==
           BOOT_SETTINGS_INVALID);
    assert(selected == 777);
    assert(boot_settings_resolve_default(&settings, available, 3, 0, NULL) ==
           BOOT_SETTINGS_INVALID);
    assert(boot_settings_resolve_default(NULL, available, 3, 0, &selected) ==
           BOOT_SETTINGS_INVALID);
    assert(selected == 777);

    /* Invalid record and invalid candidate IDs are rejected. */
    assert(boot_settings_resolve_default(&invalid_record, available, 3, 0,
                                         &selected) == BOOT_SETTINGS_INVALID);
    assert(selected == 777);
    assert(boot_settings_resolve_default(&settings, bad_candidate, 3, 0,
                                         &selected) == BOOT_SETTINGS_INVALID);
    assert(selected == 777);
    assert(boot_settings_resolve_default(&settings, manifest_candidate, 2, 0,
                                         &selected) == BOOT_SETTINGS_INVALID);
    assert(selected == 777);

    /* MANIFEST index bounds. */
    settings.default_target = BOOT_TARGET_MANIFEST;
    assert(boot_settings_resolve_default(&settings, available, 3, 0,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 0);
    selected = 777;
    assert(boot_settings_resolve_default(&settings, available, 3, 3,
                                         &selected) == BOOT_SETTINGS_INVALID);
    assert(selected == 777);
    assert(boot_settings_resolve_default(&settings, available, 3, 99,
                                         &selected) == BOOT_SETTINGS_INVALID);
    assert(selected == 777);

    /* An explicit target ignores the unused MANIFEST index entirely. */
    settings.default_target = BOOT_TARGET_INTERNAL;
    selected = 777;
    assert(boot_settings_resolve_default(&settings, available, 3, 999,
                                         &selected) == BOOT_SETTINGS_OK);
    assert(selected == 1);

    /* effective_board boundary input errors leave the output unchanged. */
    assert(boot_settings_init(&settings) == BOOT_SETTINGS_OK);
    board = DEVICE_BOARD_TB323FU;
    assert(boot_settings_effective_board(&settings, (enum device_board)-1,
                                         &board) == BOOT_SETTINGS_INVALID);
    assert(board == DEVICE_BOARD_TB323FU);
    assert(boot_settings_effective_board(&settings, (enum device_board)3,
                                         &board) == BOOT_SETTINGS_INVALID);
    assert(board == DEVICE_BOARD_TB323FU);
    assert(boot_settings_effective_board(NULL, DEVICE_BOARD_UNKNOWN, &board) ==
           BOOT_SETTINGS_INVALID);
    assert(board == DEVICE_BOARD_TB323FU);
    assert(boot_settings_effective_board(&settings, DEVICE_BOARD_UNKNOWN,
                                         NULL) == BOOT_SETTINGS_INVALID);
    assert(board == DEVICE_BOARD_TB323FU);

    /* init with a NULL output is invalid. */
    assert(boot_settings_init(NULL) == BOOT_SETTINGS_INVALID);
}

int main(void)
{
    test_defaults();
    test_valid_record_unchanged();
    test_effective_board();
    test_validate_rejects_out_of_range();
    test_resolve_default_mapping();
    test_resolve_needs_selection();
    test_resolve_boundaries();

    fprintf(stderr, "boot_settings: all tests passed\n");
    return 0;
}
