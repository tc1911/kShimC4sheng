/* SPDX-License-Identifier: MIT */
#include "dt.h"
#include <string.h>

static int report_slot(void *fdt, const char *key, const char *value)
{
	const size_t keylen = strlen(key);
	const size_t slotlen = 80;
	const char marker[] = "pending";
	const size_t mlen = sizeof(marker) - 1;
	int chosen, plen;
	const char *bootargs, *p, *base;
	size_t vlen;
	int found = 0, offset = 0;
	char slot[80];
	size_t i;
	const char *v, *vend;
	size_t vlen2;

	if (!fdt || !key || !value)
		return -1;
	vlen = strlen(value);
	if (vlen == 0 || vlen > slotlen)
		return -1;
	for (i = 0; i < vlen; i++) {
		unsigned char c = (unsigned char)value[i];
		if (c < 33 || c > 126 || c == '"' || c == '\\')
			return -1;
	}

	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return -1;
	bootargs = fdt_getprop(fdt, chosen, "bootargs", &plen);
	if (!bootargs || plen <= 0 || bootargs[plen - 1] != '\0')
		return -1;
	for (i = 0; i < (size_t)(plen - 1); i++)
		if (bootargs[i] == '\0')
			return -1;

	base = bootargs;
	p = bootargs;
	while (*p) {
		const char *end = strchr(p, ' ');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len >= keylen && memcmp(p, key, keylen) == 0) {
			if (found)
				return -1;
			found = 1;
			offset = (int)(p - base) + (int)keylen;
		}
		if (!end)
			break;
		p = end + 1;
	}
	if (!found)
		return -1;

	v = base + offset;
	vend = strchr(v, ' ');
	vlen2 = vend ? (size_t)(vend - v) : strlen(v);
	if (vlen2 != slotlen || memcmp(v, marker, mlen) != 0)
		return -1;
	for (i = mlen; i < slotlen; i++)
		if (v[i] != '_')
			return -1;

	memcpy(slot, value, vlen);
	memset(slot + vlen, '_', slotlen - vlen);
	return fdt_setprop_inplace_namelen_partial(fdt, chosen, "bootargs", 8,
						  (uint32_t)offset, slot, (int)slotlen);
}

int dt_probe_report(void *fdt, const char *value)
{
	return report_slot(fdt, "androidboot.kshim_probe=", value);
}

int dt_probe_report_slot(void *fdt, unsigned slot, const char *value)
{
	char key[] = "androidboot.kshim_s0=";

	if (slot > 8)
		return -1;
	key[sizeof(key) - 3] = (char)('0' + (int)slot);
	return report_slot(fdt, key, value);
}

/* ---- Two-slot DT-only diagnostic report --------------------------------
 *
 * Separate, narrow helper (see dt.h for the contract).  Both slots are
 * preflighted in full before the first byte is written, and the actual edit
 * uses only bounded in-place copies into the caller's writable property
 * storage.  No libfdt write API is used, so there is no partial first-slot
 * write if a later slot would have failed.  The two source prefixes are both
 * 12 bytes, but the two source names are not ("kshim_probe=" is 12 and
 * "kshim_s0=" is 9); each span is derived from its own string. */

#define REPORT_SLOT_BYTES   80
#define REPORT_PREFIX_BYTES 12
#define REPORT_MARKER       "pending"

static int report_value_ok(const char *value)
{
	size_t vlen, i;

	if (!value)
		return -1;
	vlen = strlen(value);
	if (vlen == 0 || vlen > REPORT_SLOT_BYTES)
		return -1;
	for (i = 0; i < vlen; i++) {
		unsigned char c = (unsigned char)value[i];
		if (c < 33 || c > 126 || c == '"' || c == '\\')
			return -1;
	}
	return 0;
}

/* Count token-boundary occurrences of key in the bootargs body ba[0..len),
 * which excludes the terminal NUL.  A match counts only when it begins a
 * token (offset 0 or just after a space) and key is an exact prefix of that
 * token, so a key-looking substring inside another token's name or value is
 * not a key.  The first match's value offset/length (up to the next space or
 * the terminal NUL) is stored when requested.  Never reads past ba[len). */
static size_t report_token_count(const char *ba, size_t len, const char *key,
				 size_t keylen, size_t *value_off,
				 size_t *value_len)
{
	size_t i, count = 0;

	if (keylen == 0 || keylen > len)
		return 0;
	for (i = 0; i + keylen <= len; i++) {
		size_t v, end;

		if (i != 0 && ba[i - 1] != ' ')
			continue;
		if (memcmp(ba + i, key, keylen) != 0)
			continue;
		v = i + keylen;
		end = v;
		while (end < len && ba[end] != ' ' && ba[end] != '\0')
			end++;
		if (count == 0) {
			if (value_off)
				*value_off = v;
			if (value_len)
				*value_len = end - v;
		}
		count++;
	}
	return count;
}

/* A source slot value is valid only as exactly "pending" + 73 underscores. */
static int report_slot_pending(const char *ba, size_t value_off, size_t vlen)
{
	const size_t mlen = sizeof(REPORT_MARKER) - 1;
	size_t i;

	if (vlen != REPORT_SLOT_BYTES)
		return -1;
	if (memcmp(ba + value_off, REPORT_MARKER, mlen) != 0)
		return -1;
	for (i = mlen; i < REPORT_SLOT_BYTES; i++)
		if (ba[value_off + i] != '_')
			return -1;
	return 0;
}

int dt_diagnostic_report_slots(void *fdt, const char *probe, const char *s0)
{
	static const char src_probe[] = "kshimreport.kshim_probe=";
	static const char src_s0[] = "kshimreport.kshim_s0=";
	static const char tgt_probe[] = "androidboot.kshim_probe=";
	static const char tgt_s0[] = "androidboot.kshim_s0=";
	static const char tgt_prefix[] = "androidboot.";
	const size_t src_probe_len = sizeof(src_probe) - 1;
	const size_t src_s0_len = sizeof(src_s0) - 1;
	int chosen, plen;
	const char *ba;
	size_t len, i, poff = 0, plen2 = 0, soff = 0, slen2 = 0;
	size_t pval, sval;
	char pv[REPORT_SLOT_BYTES], sv[REPORT_SLOT_BYTES];
	char *w;

	if (!fdt || !probe || !s0)
		return -1;
	if (report_value_ok(probe) != 0 || report_value_ok(s0) != 0)
		return -1;

	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return -1;
	ba = fdt_getprop(fdt, chosen, "bootargs", &plen);
	if (!ba || plen <= 0 || ba[plen - 1] != '\0')
		return -1;
	len = (size_t)plen - 1;
	for (i = 0; i < len; i++)
		if (ba[i] == '\0')
			return -1;

	/* Preflight both slots before writing anything. */
	if (report_token_count(ba, len, src_probe, src_probe_len,
			       &poff, &plen2) != 1)
		return -1;
	if (report_token_count(ba, len, src_s0, src_s0_len,
			       &soff, &slen2) != 1)
		return -1;
	if (report_token_count(ba, len, tgt_probe, sizeof(tgt_probe) - 1,
			       NULL, NULL) != 0)
		return -1;
	if (report_token_count(ba, len, tgt_s0, sizeof(tgt_s0) - 1,
			       NULL, NULL) != 0)
		return -1;
	if (report_slot_pending(ba, poff, plen2) != 0)
		return -1;
	if (report_slot_pending(ba, soff, slen2) != 0)
		return -1;

	/* Build the padded 80-byte report values. */
	pval = strlen(probe);
	memcpy(pv, probe, pval);
	if (pval < REPORT_SLOT_BYTES)
		memset(pv + pval, '_', REPORT_SLOT_BYTES - pval);
	sval = strlen(s0);
	memcpy(sv, s0, sval);
	if (sval < REPORT_SLOT_BYTES)
		memset(sv + sval, '_', REPORT_SLOT_BYTES - sval);

	/* All checks passed; the only writes are bounded in-place copies. */
	w = (char *)(uintptr_t)ba;
	memcpy(w + (poff - src_probe_len), tgt_prefix, REPORT_PREFIX_BYTES);
	memcpy(w + poff, pv, REPORT_SLOT_BYTES);
	memcpy(w + (soff - src_s0_len), tgt_prefix, REPORT_PREFIX_BYTES);
	memcpy(w + soff, sv, REPORT_SLOT_BYTES);
	return 0;
}
