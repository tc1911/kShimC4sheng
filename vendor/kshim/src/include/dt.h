/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <libfdt.h>
struct dt_range { uint64_t base, size; };
struct dt_spec { int node; unsigned count; uint32_t args[8]; };
/* All other APIs require a blob accepted by dt_validate first. */
int dt_validate(const void *fdt, size_t available);
bool dt_available(const void *fdt, int node);
int dt_u32(const void *fdt, int node, const char *property, uint32_t *value);
int dt_bool(const void *fdt, int node, const char *property, bool *value);
const char *dt_string(const void *fdt, int node, const char *property);
int dt_string_index(const void *fdt, int node, const char *property, const char *name);
int dt_phandle(const void *fdt, uint32_t handle);
int dt_reference(const void *fdt, int node, const char *property, int index);
int dt_specifier(const void *fdt, int node, const char *property,
                 const char *cells_property, unsigned index, struct dt_spec *spec);
int dt_reg(const void *fdt, int node, unsigned index, struct dt_range *range);
int dt_reg_named(const void *fdt, int node, const char *name, struct dt_range *range);
int dt_raw_reg(const void *fdt, int node, unsigned index,
               uint64_t *address, uint64_t *size);
bool dt_contains(struct dt_range range, uint64_t address, uint64_t size);
int dt_reservations(const void *fdt, struct dt_range *ranges, bool *no_map,
                    size_t capacity, size_t *count);
/* Contract: callers must dt_validate the blob first. The tree must stay
 * immutable and initialization is single-threaded, non-nested scope; call
 * dt_parent_index_end before any mutation. Larger or deeper trees fall back
 * unchanged; 8192 nodes / 64 levels are performance caps only. */
void dt_parent_index_begin(const void *fdt);
void dt_parent_index_end(void);
int dt_parent_offset(const void *fdt, int offset);
/* Shares the parent-index scope: uses the index only when one is active for
 * this exact blob, otherwise falls back to libfdt. Matches
 * fdt_node_offset_by_compatible semantics exactly. */
int dt_node_offset_by_compatible(const void *fdt, int start, const char *compatible);

int dt_probe_report(void *fdt, const char *value);
int dt_probe_report_slot(void *fdt, unsigned slot, const char *value);

/* Two-slot DT-only diagnostic report.
 *
 * Rewrites the two standalone /chosen:bootargs source tokens
 *
 *     kshimreport.kshim_probe=<80 bytes>
 *     kshimreport.kshim_s0=<80 bytes>
 *
 * in place to their androidboot target form, in one fully-preflighted step:
 *
 *     androidboot.kshim_probe=<80 bytes>
 *     androidboot.kshim_s0=<80 bytes>
 *
 * The blob must already be a mutable blob accepted by dt_validate.  It is
 * edited in place and is never grown, shrunk, repacked or reallocated, so
 * fdt_totalsize and every byte outside the two contracted spans stay as they
 * were.
 *
 * probe / s0 carry the report strings to store: each must be 1..80 printable
 * ASCII bytes with no whitespace, '"' or '\' (the existing report_slot
 * policy; both report arguments are validated together).  Each source token
 * must occur exactly once at a token boundary and hold exactly "pending"
 * followed by 73 underscores; a standalone target token
 * (androidboot.kshim_probe= / androidboot.kshim_s0=) already present in the
 * source is rejected.  Text that only resembles a key inside another token's
 * name or value is not a key.  Both source name lengths
 * ("kshim_probe="=12, "kshim_s0="=9) are derived from their own strings.
 * Every check runs for both slots before any byte is written, so any failure
 * (nonzero return) leaves the whole blob byte-identical.  On success only the
 * two 12-byte prefixes ("kshimreport." -> "androidboot.") and the two 80-byte
 * values (report value padded with underscores) change; a repeated call
 * refuses (the source tokens are gone) and changes nothing.
 *
 * dt_probe_report() and dt_probe_report_slot() keep their original semantics.
 */
int dt_diagnostic_report_slots(void *fdt, const char *probe, const char *s0);
