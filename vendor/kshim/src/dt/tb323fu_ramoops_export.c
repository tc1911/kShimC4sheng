/* SPDX-License-Identifier: MIT */
/*
 * TB323FU M2 persistent-console offline: Android-FDT report export.
 *
 * Writes the fixed ten-slot report (probe + s0..s8) into an existing Android
 * runtime FDT using the real dt_probe_report() / dt_probe_report_slot()
 * helpers (inputs/probe.c).  It never writes RAM, never adds a persistent
 * region, never issues MMIO, runs no second ring decoder and performs no ECC
 * correction; the console is read solely through the frozen
 * tb323fu_ramoops_read_console() contract.
 *
 * Slot grammar (all ASCII, no space/quote/backslash, 80-byte value):
 *   s0        pc1:st=<dec>:a=<hex>:z=<hex>:c=<hex>:n=<hex>:id=<match|unknown>
 *   s1        pc1:stage=<hex of up to 32 stage bytes>
 *   s2..s8    pc1:tN=<hex of up to 36 raw tail bytes>   (7*36 = 252 bytes)
 *   probe     pc1:cap=<dec>:rc=<dec>:m=<9-slot mask hex>:raw=1:ecc=0
 * probe is written last because it carries the successful mask of the nine
 * data slots.  Each slot is emitted once; the snprintf result must satisfy
 * 0 < n < 81 or the slot is skipped (its mask bit stays clear).
 *
 * The region is read only when capture succeeded (capture_status == 0) and the
 * layout is valid; otherwise it is never dereferenced and st/rc record the
 * capture/reader error rather than a fabricated RAW_LOG state.  Candidate
 * identity is a bounded, read-only scan of the verified circular log
 * (logical[i] = data[(start + i) % size], the same source formula as
 * read_console) that requires an ASCII value boundary on both sides of the
 * token, so a longer token sharing the expected prefix is never mis-counted.
 */
#include "tb323fu_ramoops_export.h"

#include "dt.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define EXPORT_SLOT_LEN   80u   /* report slot value length enforced by probe.c */
#define EXPORT_SEG_BYTES  36u   /* raw tail bytes per segment */
#define EXPORT_SEG_COUNT  7u    /* 7 * 36 = 252 == TB323FU_RAMOOPS_TAIL_MAX */
#define EXPORT_TOKEN_MAX  64u   /* maximum expected-candidate token length */

/* ASCII value boundary: NUL or any ASCII whitespace. */
static bool exp_boundary(uint8_t b)
{
	return b == 0 || b == ' ' || b == '\t' || b == '\n' ||
	       b == '\r' || b == '\v' || b == '\f';
}

/* Lowercase hex encode; dst must hold 2*n bytes (no NUL written). */
static size_t exp_hex(char *dst, const uint8_t *src, size_t n)
{
	static const char digits[] = "0123456789abcdef";
	size_t j = 0;

	for (size_t i = 0; i < n; i++) {
		dst[j++] = digits[src[i] >> 4];
		dst[j++] = digits[src[i] & 0x0f];
	}
	return j;
}

/* stage[] is NUL-terminated; bound the length without strnlen. */
static size_t exp_stage_len(const char *s)
{
	size_t n = 0;

	while (n < TB323FU_RAMOOPS_STAGE_MAX && s[n] != '\0')
		n++;
	return n;
}

/* A usable candidate token: non-empty, <=64 bytes, printable ASCII no space. */
static bool exp_token_ok(const char *tok, size_t *len)
{
	size_t n;

	if (tok == NULL)
		return false;
	n = strlen(tok);
	if (n == 0 || n > EXPORT_TOKEN_MAX)
		return false;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)tok[i];

		if (c < 0x21 || c > 0x7e)
			return false;
	}
	*len = n;
	return true;
}

/*
 * Bounded, boundary-checked search for tok in the verified circular log.
 * data is region + console_off + header and logical[i] = data[(start+i)%size],
 * exactly the read_console reconstruction.  A match additionally requires the
 * byte before and after to be an ASCII value boundary or the log endpoint.
 */
static bool exp_ring_match(const volatile uint8_t *data, uint32_t start,
			   uint32_t size, const uint8_t *tok, size_t tlen)
{
	if (size == 0 || tlen == 0 || tlen > size)
		return false;

	for (uint32_t pos = 0; pos + tlen <= size; pos++) {
		bool ok = true;

		for (size_t j = 0; j < tlen; j++) {
			uint32_t idx = (uint32_t)(((uint64_t)pos + j) % size);

			if ((uint8_t)data[((uint64_t)start + idx) % size] !=
			    tok[j]) {
				ok = false;
				break;
			}
		}
		if (!ok)
			continue;

		if (pos != 0) {
			uint32_t pidx = (uint32_t)((uint64_t)pos - 1) % size;

			if (!exp_boundary((uint8_t)
			    data[((uint64_t)start + pidx) % size]))
				continue;
		}
		if ((uint32_t)(pos + tlen) != size) {
			uint32_t aidx = (uint32_t)(pos + tlen) % size;

			if (!exp_boundary((uint8_t)
			    data[((uint64_t)start + aidx) % size]))
				continue;
		}
		return true;
	}
	return false;
}

/*
 * Emit one slot through the real probe helpers.  slot 0 is the probe; slots
 * 1..9 map to s0..s8.  Returns bit if the write succeeded, else 0.
 */
static unsigned exp_emit(void *fdt, unsigned slot, const char *value,
			 unsigned bit)
{
	if (slot == 0)
		return dt_probe_report(fdt, value) == 0 ? bit : 0u;
	return dt_probe_report_slot(fdt, slot - 1u, value) == 0 ? bit : 0u;
}

int tb323fu_ramoops_export(void *fdt, const volatile uint8_t *region, size_t len,
			   const tb323fu_ramoops_layout *layout,
			   int capture_status, const char *expected_candidate)
{
	tb323fu_console_summary summary;
	bool read_ok = false, raw = false;
	int rc = TB323FU_RAMOOPS_ERR_INVALID;
	unsigned mask = 0;
	const char *id = "unknown";
	char slot[EXPORT_SLOT_LEN + 1u];
	int n;

	if (fdt == NULL)
		return 0;

	memset(&summary, 0, sizeof(summary));

	/* Read the console only on a successful capture with a valid layout. */
	if (capture_status == 0 && layout != NULL && layout->valid) {
		rc = tb323fu_ramoops_read_console(region, len, layout, &summary);
		if (rc == TB323FU_RAMOOPS_OK) {
			read_ok = true;
			raw = (summary.state == TB323FU_CONSOLE_RAW_LOG);
		}
	}

	/* Identity: only a decoded raw log can carry the token. */
	if (read_ok && raw) {
		size_t tlen = 0;

		if (exp_token_ok(expected_candidate, &tlen)) {
			const volatile uint8_t *data =
				region + TB323FU_RAMOOPS_CONSOLE_OFF +
				TB323FU_PRZ_HEADER_SIZE;

			if (exp_ring_match(data, summary.start, summary.size,
					   (const uint8_t *)expected_candidate,
					   tlen))
				id = "match";
		}
	}

	/* s0: console state, header echo and identity. */
	n = snprintf(slot, sizeof(slot),
		     "pc1:st=%d:a=%x:z=%x:c=%x:n=%zx:id=%s",
		     read_ok ? (int)summary.state : -1,
		     (unsigned)(read_ok ? summary.start : 0u),
		     (unsigned)(read_ok ? summary.size : 0u),
		     (unsigned)(read_ok ? summary.capacity : 0u),
		     read_ok ? summary.tail_len : (size_t)0, id);
	if (n > 0 && n <= (int)EXPORT_SLOT_LEN)
		mask |= exp_emit(fdt, 1u, slot,
				 1u << TB323FU_RAMOOPS_EXPORT_BIT_S0);

	/* s1 + s2..s8: only a decoded raw log has a stage and a tail. */
	if (read_ok && raw) {
		char hex[TB323FU_RAMOOPS_STAGE_MAX * 2u + 1u];
		size_t hl = 0;

		if (summary.have_stage)
			hl = exp_hex(hex, (const uint8_t *)summary.stage,
				     exp_stage_len(summary.stage));
		hex[hl] = '\0';
		n = snprintf(slot, sizeof(slot), "pc1:stage=%s", hex);
		if (n > 0 && n <= (int)EXPORT_SLOT_LEN)
			mask |= exp_emit(fdt, 2u, slot,
					 1u << TB323FU_RAMOOPS_EXPORT_BIT_S1);

		for (unsigned k = 0; k < EXPORT_SEG_COUNT; k++) {
			char thex[EXPORT_SEG_BYTES * 2u + 1u];
			size_t off = (size_t)k * EXPORT_SEG_BYTES;
			size_t avail = 0, thl = 0;

			if (off < summary.tail_len) {
				avail = summary.tail_len - off;
				if (avail > EXPORT_SEG_BYTES)
					avail = EXPORT_SEG_BYTES;
				thl = exp_hex(thex, summary.tail + off, avail);
			}
			thex[thl] = '\0';
			n = snprintf(slot, sizeof(slot), "pc1:t%u=%s", k, thex);
			if (n > 0 && n <= (int)EXPORT_SLOT_LEN)
				mask |= exp_emit(fdt, 3u + k, slot,
						 1u << (TB323FU_RAMOOPS_EXPORT_BIT_S2 + k));
		}
	}

	/* probe last: carries the mask of the nine data slots. */
	n = snprintf(slot, sizeof(slot),
		     "pc1:cap=%d:rc=%d:m=%x:raw=1:ecc=0",
		     capture_status, rc,
		     mask >> TB323FU_RAMOOPS_EXPORT_BIT_S0);
	if (n > 0 && n <= (int)EXPORT_SLOT_LEN)
		mask |= exp_emit(fdt, 0u, slot,
				 1u << TB323FU_RAMOOPS_EXPORT_BIT_PROBE);

	return (int)mask;
}
