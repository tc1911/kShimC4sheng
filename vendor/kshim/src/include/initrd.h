/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <dt.h>
#include <manifest.h>

/*
 * External-initramfs runtime handoff.
 *
 * kshim_initrd_apply() runs on an already validated, already normalized
 * handoff FDT and reads only the existing /chosen/kshim,initrd-entry property:
 *
 *  - no /chosen, or no such property: the historical builtin-initramfs
 *    profile is kept by deleting linux,initrd-start / linux,initrd-end
 *    (a missing node or property is ignored, any other error is returned);
 *  - a present reference: the referenced manifest BLOB entry is validated
 *    against the supplied context and, only if every check passes, its exact
 *    BLOB byte interval is advertised through linux,initrd-start /
 *    linux,initrd-end (page rounding is used only for internal safety checks,
 *    so no payload padding is exposed to Linux).
 *
 * The context is consulted only on the reference path; a caller with no
 * reference may pass NULL. Every semantic check runs before the first tree
 * mutation, so a non-zero return means the handoff FDT is not usable as output
 * and the caller must fall back. The helper never logs, never allocates, never
 * reads BLOB payload bytes and never mutates the runtime/linker, runtime or
 * payload inputs.
 */
struct kshim_initrd_context {
    const MANIFEST_ENTRY *const *entries; /* validated entry pointer array */
    uint32_t entry_count;
    uint64_t packed_base;         /* physical packed image base (linux_base) */
    uint64_t packed_payload_size; /* manifest_address - linux_base */
    const MANIFEST_ENTRY *entry;  /* selected Linux manifest entry */
    const void *header;           /* 64 header bytes at the packed source */
    struct dt_range runtime;      /* runtime image .. __runtime_end_address__ */
    struct dt_range runtime_fdt;  /* original runtime bootloader DT */
    struct dt_range payload_fdt;  /* selected payload DT source */
};

/* Returns 0 on success, non-zero on rejection or mutation failure. */
int kshim_initrd_apply(void *handoff, size_t capacity,
                       const struct kshim_initrd_context *ctx);
