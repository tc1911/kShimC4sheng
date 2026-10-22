#pragma once
#include <stdint.h>
#include <stddef.h>
#include <menu_locale.h>
#include <device_info.h>

/* device_info.h provides both `struct device_info` (read by platform.c, which
 * never calls the device_info API itself) and the stable `enum device_board`
 * used by the session-settings entry point below. */

int kshim_platform_main(uint64_t fdt, uint64_t arg1, uint64_t arg2, uint64_t arg3);
/* Names must be in runtime-owned memory. Returns -1 when UI is unavailable,
 * -2 on unsafe shutdown (caller must not boot), or a selected index via out. */
int kshim_platform_select(uint64_t fdt, const char *const *names, size_t count,
                          size_t initial, uint32_t timeout_ms, size_t *selected);
/* Each set bit moves that original payload index into the advanced page.
 * Confirmed payloads still return their original manifest index. Page changes
 * cancel auto boot and keep the UI alive; the old API always remains flat. */
int kshim_platform_select_advanced(uint64_t fdt, const char *const *names,
                                  size_t count, size_t initial,
                                  uint32_t timeout_ms, uint64_t advanced_mask,
                                  size_t *selected);
/* Optional runtime-owned descriptions follow the same original payload indexes
 * as names. The array and individual entries may be NULL. Without advanced
 * actions this uses a flat menu while retaining the description metadata. */
int kshim_platform_select_described(uint64_t fdt, const char *const *names,
                                   const char *const *descriptions, size_t count,
                                   size_t initial, uint32_t timeout_ms,
                                   uint64_t advanced_mask, size_t *selected);
/* Localized variant: `roles` carries one semantic role per original payload
 * index (NULL means all UNKNOWN). Known roles pick their label/description from
 * the runtime locale table, while UNKNOWN entries keep the caller's exact
 * strings. Under the language-switch config this entry point and the
 * described/advanced wrappers all offer the runtime language page; only the
 * simple kshim_platform_select compatibility wrapper stays flat with no
 * language. Advanced layout and handoff are otherwise identical. */
int kshim_platform_select_localized(uint64_t fdt, const char *const *names,
                                    const char *const *descriptions,
                                    const kshim_menu_role_t *roles, size_t count,
                                    size_t initial, uint32_t timeout_ms,
                                    uint64_t advanced_mask, size_t *selected);
/* Localized variant that additionally offers a read-only Device info page fed
 * from a caller-owned record. A non-NULL `info` adds one main-menu row (before
 * the power rows) and a read-only field subpage; NULL keeps the exact
 * kshim_platform_select_localized behavior with no info row. */
int kshim_platform_select_localized_info(uint64_t fdt, const char *const *names,
                                         const char *const *descriptions,
                                         const kshim_menu_role_t *roles,
                                         const struct device_info *info,
                                         size_t count, size_t initial,
                                         uint32_t timeout_ms,
                                         uint64_t advanced_mask, size_t *selected);
/* Session-settings entry point. Adds the Device / Default pages and a runtime
 * Settings page on top of the localized+info menu. `payload_boards[i]` is the
 * board associated with the DTB of payload i (DEVICE_BOARD_UNKNOWN when the
 * caller has no matching evidence); INTERNAL/TF/DIAGNOSTIC payloads only stay
 * selectable when that board is known and equals the session's effective
 * board. `selected` is always an original caller index. Only this entry point
 * enables the session settings; every other entry point keeps settings off and
 * behaves exactly as before. */
int kshim_platform_select_localized_settings(uint64_t fdt, const char *const *names,
                                             const char *const *descriptions,
                                             const kshim_menu_role_t *roles,
                                             const struct device_info *info,
                                             const enum device_board *payload_boards,
                                             size_t count, size_t initial,
                                             uint32_t timeout_ms,
                                             uint64_t advanced_mask, size_t *selected);
int kshim_platform_prepare_handoff(void);
