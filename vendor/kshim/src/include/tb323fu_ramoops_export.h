/* SPDX-License-Identifier: MIT */
/*
 * TB323FU M2 persistent-console offline: Android-FDT report export.
 *
 * tb323fu_ramoops_export() writes the fixed ten-slot report (probe + s0..s8)
 * into an existing Android runtime FDT through the real dt_probe_report() and
 * dt_probe_report_slot() helpers declared in dt.h and implemented by
 * inputs/probe.c.  Those helpers replace the body of an already present
 * 80-byte "pending" slot in /chosen bootargs in place, so the export never
 * resizes bootargs and never touches an unrelated property.
 *
 * The function is deliberately side-effect free outside those slots: it never
 * writes RAM, never adds a persistent region, never issues MMIO and never runs
 * its own ring decoder or ECC correction.  The console is read only through
 * the existing tb323fu_ramoops_read_console(); when capture failed or the
 * layout is invalid the region is not dereferenced at all and the slots record
 * the capture/reader error instead of a fabricated RAW_LOG state.
 *
 * Return value is the bitmask of slots written successfully:
 *   bit 0        probe
 *   bit 1 .. 9   s0 .. s8
 * A NULL fdt yields 0 (nothing written).  Each bit is set only when the
 * matching report helper returned success; a missing or duplicated bootargs
 * slot simply drops its bit.
 */
#ifndef TB323FU_RAMOOPS_EXPORT_H
#define TB323FU_RAMOOPS_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#include "tb323fu_ramoops.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Slot bit indices in the returned mask. */
#define TB323FU_RAMOOPS_EXPORT_BIT_PROBE 0u
#define TB323FU_RAMOOPS_EXPORT_BIT_S0    1u
#define TB323FU_RAMOOPS_EXPORT_BIT_S1    2u
#define TB323FU_RAMOOPS_EXPORT_BIT_S2    3u
#define TB323FU_RAMOOPS_EXPORT_BIT_S3    4u
#define TB323FU_RAMOOPS_EXPORT_BIT_S4    5u
#define TB323FU_RAMOOPS_EXPORT_BIT_S5    6u
#define TB323FU_RAMOOPS_EXPORT_BIT_S6    7u
#define TB323FU_RAMOOPS_EXPORT_BIT_S7    8u
#define TB323FU_RAMOOPS_EXPORT_BIT_S8    9u

/* All ten slots written (positive run). */
#define TB323FU_RAMOOPS_EXPORT_MASK_ALL  0x3ffu

/*
 * Export the current run into android_fdt.
 *
 * fdt               the Android runtime FDT to edit in place (NULL => 0).
 * region            console region base; read via read_console only.
 * len               bytes available at region (must cover the whole region).
 * layout            captured layout; valid==false means "do not read".
 * capture_status    return code of the earlier capture() call (0 == success).
 * expected_candidate  the single identity token to look for (may be NULL).
 *
 * Slot grammar (fixed, 80-byte, ASCII, no space/quote/backslash):
 *   s0       pc1:st=<dec>:a=<hex>:z=<hex>:c=<hex>:n=<hex>:id=<match|unknown>
 *   s1       pc1:stage=<hex of up to 32 stage bytes>
 *   s2..s8   pc1:tN=<hex of up to 36 raw tail bytes>  (7*36 = 252 bytes)
 *   probe    pc1:cap=<dec>:rc=<dec>:m=<9-slot mask hex>:raw=1:ecc=0
 *
 * The probe slot is written last because it carries the mask of the nine data
 * slots.  The identity check scans the verified circular log in logical order
 * with an ASCII value boundary on both sides of the token; no match (or an
 * unusable token) is reported as "unknown".
 */
int tb323fu_ramoops_export(void *fdt, const volatile uint8_t *region, size_t len,
			   const tb323fu_ramoops_layout *layout,
			   int capture_status, const char *expected_candidate);

#ifdef __cplusplus
}
#endif

#endif /* TB323FU_RAMOOPS_EXPORT_H */
