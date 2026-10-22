/* SPDX-License-Identifier: MIT */
/*
 * TB323FU M2 persistent-console offline: capture / bind / read_console.
 *
 * Read-only FDT inspection (capture), in-place handoff annotation (bind) and a
 * raw, ECC-free console reader (read_console).  All three follow the frozen
 * contracts in tb323fu_ramoops.h and the kernel references (ram.c probe order,
 * ram_core.c persistent_ram_buffer layout / persistent_ram_save_old ordering).
 *
 * No allocation and no large buffers: the console scan walks the reconstructed
 * circular log byte by byte and keeps only constant-size state.
 */
#include "tb323fu_ramoops.h"

#include <libfdt.h>
#include <limits.h>
#include <string.h>

/* ---- endian helpers ----------------------------------------------------- */
static uint32_t rd_le32(const volatile uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rd_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* DT "status": absent means active; only okay/ok are candidates. */
static bool node_active(const void *fdt, int node)
{
	int len;
	const void *s = fdt_getprop(fdt, node, "status", &len);

	if (s == NULL)
		return true;
	if (len == 5 && memcmp(s, "okay", 5) == 0)
		return true;
	if (len == 3 && memcmp(s, "ok", 3) == 0)
		return true;
	return false;
}

/* DT u32 property (big-endian): 0 with *out set, -1 if absent or malformed. */
static int get_u32_prop(const void *fdt, int node, const char *name,
			uint32_t *out)
{
	int len;
	const uint8_t *p = (const uint8_t *)fdt_getprop(fdt, node, name, &len);

	if (p == NULL || len != 4)
		return -1;
	*out = rd_be32(p);
	return 0;
}

/* ======================================================================== */
/* capture()                                                                */
/* ======================================================================== */
int tb323fu_ramoops_capture(const void *runtime, size_t runtime_size,
			    tb323fu_ramoops_layout *out)
{
	const void *fdt = runtime;
	const uint8_t *p;
	int total, plen;
	int off, cand = -1, nqcom = 0;
	int rm, rnode, parent;
	uint32_t ph, ac, sc, rec, con, pms, ecc, mty;
	uint64_t base, size;

	if (out != NULL)
		memset(out, 0, sizeof(*out));
	if (runtime == NULL || out == NULL)
		return TB323FU_RAMOOPS_ERR_INVALID;
	if (runtime_size < sizeof(struct fdt_header))
		return TB323FU_RAMOOPS_ERR_INVALID;
	/*
	 * Validate the whole blob, not just the header: a tree whose header is
	 * well-formed but whose structure block is truncated or whose final
	 * FDT_END token is corrupted must be rejected before any node is walked.
	 */
	if (fdt_check_full(fdt, runtime_size) != 0)
		return TB323FU_RAMOOPS_ERR_INVALID;
	total = fdt_totalsize(fdt);
	if (total <= 0 || (size_t)total > runtime_size)
		return TB323FU_RAMOOPS_ERR_INVALID;

	/* Exactly one *available* qcom,ramoops. */
	off = -1;
	while ((off = fdt_node_offset_by_compatible(fdt, off,
						    "qcom,ramoops")) >= 0) {
		if (!node_active(fdt, off))
			continue;
		nqcom++;
		cand = off;
	}
	if (nqcom == 0)
		return TB323FU_RAMOOPS_ERR_NOT_FOUND;
	if (nqcom > 1)
		return TB323FU_RAMOOPS_ERR_AMBIGUOUS;

	/* memory-region: exactly one phandle cell (4 bytes, big-endian). */
	p = (const uint8_t *)fdt_getprop(fdt, cand, "memory-region", &plen);
	if (p == NULL)
		return TB323FU_RAMOOPS_ERR_NOT_FOUND;
	if (plen != 4)
		return TB323FU_RAMOOPS_ERR_INVALID;
	ph = rd_be32(p);

	rnode = fdt_node_offset_by_phandle(fdt, ph);
	if (rnode < 0)
		return TB323FU_RAMOOPS_ERR_NOT_FOUND;

	rm = fdt_path_offset(fdt, "/reserved-memory");
	if (rm < 0)
		return TB323FU_RAMOOPS_ERR_NOT_FOUND;

	/* A disabled parent deactivates the whole reservation subtree. */
	if (!node_active(fdt, rm))
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	/* Reservation must be a direct, available child of /reserved-memory. */
	parent = fdt_parent_offset(fdt, rnode);
	if (parent != rm)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (!node_active(fdt, rnode))
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	/* Parent geometry: #address-cells == #size-cells == 2, empty ranges. */
	p = (const uint8_t *)fdt_getprop(fdt, rm, "#address-cells", &plen);
	if (p == NULL || plen != 4)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	ac = rd_be32(p);
	p = (const uint8_t *)fdt_getprop(fdt, rm, "#size-cells", &plen);
	if (p == NULL || plen != 4)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	sc = rd_be32(p);
	if (ac != 2 || sc != 2)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (fdt_getprop(fdt, rm, "ranges", &plen) == NULL || plen != 0)
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	/* reg: exactly 16 bytes, exact fixed base/size. */
	p = (const uint8_t *)fdt_getprop(fdt, rnode, "reg", &plen);
	if (p == NULL || plen != 16)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	base = ((uint64_t)rd_be32(p) << 32) | rd_be32(p + 4);
	size = ((uint64_t)rd_be32(p + 8) << 32) | rd_be32(p + 12);
	if (base != TB323FU_RAMOOPS_REGION_BASE ||
	    size != TB323FU_RAMOOPS_REGION_SIZE)
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	/* no-map must be present and empty (boolean); reusable must be absent. */
	if (fdt_getprop(fdt, rnode, "no-map", &plen) == NULL || plen != 0)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (fdt_getprop(fdt, rnode, "reusable", &plen) != NULL)
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	/* qcom profile: each exactly 4 bytes with the fixed value. */
	if (get_u32_prop(fdt, cand, "record-size", &rec) != 0 ||
	    rec != TB323FU_RAMOOPS_RECORD_SIZE)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (get_u32_prop(fdt, cand, "console-size", &con) != 0 ||
	    con != TB323FU_RAMOOPS_CONSOLE_SIZE)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (get_u32_prop(fdt, cand, "pmsg-size", &pms) != 0 ||
	    pms != TB323FU_RAMOOPS_PMSG_SIZE)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (get_u32_prop(fdt, cand, "ecc-size", &ecc) != 0 ||
	    ecc != TB323FU_RAMOOPS_ECC_SIZE)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (get_u32_prop(fdt, cand, "mem-type", &mty) != 0 ||
	    mty != TB323FU_RAMOOPS_MEM_TYPE)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	/* ftrace-size: absent, or exactly 4 bytes and zero. */
	p = (const uint8_t *)fdt_getprop(fdt, cand, "ftrace-size", &plen);
	if (p != NULL) {
		if (plen != 4 || rd_be32(p) != TB323FU_RAMOOPS_FTRACE_SIZE)
			return TB323FU_RAMOOPS_ERR_LAYOUT;
	}

	out->base = base;
	out->size = size;
	out->record_size = rec;
	out->console_size = con;
	out->pmsg_size = pms;
	out->ftrace_size = TB323FU_RAMOOPS_FTRACE_SIZE;
	out->ecc_size = ecc;
	out->mem_type = mty;
	out->region_phandle = ph;
	out->valid = true;
	return TB323FU_RAMOOPS_OK;
}

/* ======================================================================== */
/* bind()                                                                   */
/* ======================================================================== */
static bool ranges_overlap(uint64_t b1, uint64_t s1, uint64_t b2, uint64_t s2)
{
	/* A zero-length range occupies nothing and never overlaps. */
	if (s1 == 0 || s2 == 0)
		return false;
	return b1 < b2 + s2 && b2 < b1 + s1;
}

static int fdt_err_status(int err)
{
	if (err == -FDT_ERR_NOSPACE)
		return TB323FU_RAMOOPS_ERR_CAPACITY;
	return TB323FU_RAMOOPS_ERR_LAYOUT;
}

int tb323fu_ramoops_bind(void *output, size_t capacity,
			 const tb323fu_ramoops_layout *layout)
{
	int total, rc, rm, exact = -1, nexact = 0, c, len, off;
	bool partial = false;

	if (output == NULL || layout == NULL)
		return TB323FU_RAMOOPS_ERR_INVALID;

	/*
	 * Only a fully valid captured profile may drive physical edits; a stale
	 * or corrupted layout (valid == false, or any field off the fixed M2
	 * profile) must never be bound to a real reservation.
	 */
	if (!layout->valid ||
	    layout->base != TB323FU_RAMOOPS_REGION_BASE ||
	    layout->size != TB323FU_RAMOOPS_REGION_SIZE ||
	    layout->record_size != TB323FU_RAMOOPS_RECORD_SIZE ||
	    layout->console_size != TB323FU_RAMOOPS_CONSOLE_SIZE ||
	    layout->pmsg_size != TB323FU_RAMOOPS_PMSG_SIZE ||
	    layout->ftrace_size != TB323FU_RAMOOPS_FTRACE_SIZE ||
	    layout->ecc_size != TB323FU_RAMOOPS_ECC_SIZE ||
	    layout->mem_type != TB323FU_RAMOOPS_MEM_TYPE)
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	if (capacity > (size_t)INT_MAX)
		return TB323FU_RAMOOPS_ERR_OVERFLOW;
	/* Never dereference the FDT header in a buffer shorter than one. */
	if (capacity < sizeof(struct fdt_header))
		return TB323FU_RAMOOPS_ERR_INVALID;
	if (fdt_check_header(output) != 0)
		return TB323FU_RAMOOPS_ERR_INVALID;
	total = fdt_totalsize(output);
	if (total <= 0)
		return TB323FU_RAMOOPS_ERR_INVALID;
	if (capacity < (size_t)total)
		return TB323FU_RAMOOPS_ERR_CAPACITY;
	/* Reject a header-valid but structurally corrupt/truncated blob. */
	if (fdt_check_full(output, capacity) != 0)
		return TB323FU_RAMOOPS_ERR_INVALID;

	/*
	 * Expand the blob to the full output capacity so later edits have room;
	 * a packed tree handed in with capacity == totalsize cannot accept any
	 * new bytes and must fail with ERR_CAPACITY.
	 */
	rc = fdt_open_into(output, output, (int)capacity);
	if (rc != 0)
		return TB323FU_RAMOOPS_ERR_CAPACITY;

	rm = fdt_path_offset(output, "/reserved-memory");
	if (rm < 0)
		return TB323FU_RAMOOPS_ERR_NOT_FOUND;

	/*
	 * The parent gates and describes the reservation set: it must be
	 * active, use 2/2 cell addressing and carry an empty ranges (identity
	 * map).  Otherwise the reg decoding below is not trustworthy and a
	 * disabled parent deactivates the whole subtree.
	 */
	if (!node_active(output, rm))
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	{
		const uint8_t *cp = (const uint8_t *)fdt_getprop(
			output, rm, "#address-cells", &len);

		if (cp == NULL || len != 4 || rd_be32(cp) != 2)
			return TB323FU_RAMOOPS_ERR_LAYOUT;
		cp = (const uint8_t *)fdt_getprop(output, rm, "#size-cells",
						  &len);
		if (cp == NULL || len != 4 || rd_be32(cp) != 2)
			return TB323FU_RAMOOPS_ERR_LAYOUT;
		cp = (const uint8_t *)fdt_getprop(output, rm, "ranges", &len);
		if (cp == NULL || len != 0)
			return TB323FU_RAMOOPS_ERR_LAYOUT;
	}

	/*
	 * Locate the unique exact (base/size) reservation, walking every
	 * 16-byte reg tuple of each child rather than ignoring multi-tuple
	 * regs.  Any tuple that only partially overlaps the target range
	 * forbids binding, even when an exact match also exists: the backend
	 * would otherwise alias another reservation's physical memory.
	 */
	for (c = fdt_first_subnode(output, rm); c >= 0;
	     c = fdt_next_subnode(output, c)) {
		const uint8_t *rp =
			(const uint8_t *)fdt_getprop(output, c, "reg", &len);

		if (rp == NULL || len < 16)
			continue;
		for (off = 0; off + 16 <= len; off += 16) {
			uint64_t b, s;

			b = ((uint64_t)rd_be32(rp + off) << 32) |
			    rd_be32(rp + off + 4);
			s = ((uint64_t)rd_be32(rp + off + 8) << 32) |
			    rd_be32(rp + off + 12);
			/*
			 * A zero-length tuple occupies nothing: skip it, so an
			 * unrelated size-0 reservation whose base happens to
			 * fall inside the target range is never misread as an
			 * overlap.
			 */
			if (s == 0)
				continue;
			if (b + s < b) /* base + size overflow */
				return TB323FU_RAMOOPS_ERR_OVERFLOW;
			if (b == layout->base && s == layout->size) {
				/*
				 * The backend is platform_get_resource(dev,
				 * IORESOURCE_MEM, 0) (ram.c ramoops_probe): it
				 * binds reg tuple 0 only.  An exact range held in
				 * a later tuple of a multi-tuple reg would make
				 * the backend pick the first tuple instead, so
				 * the exact reservation must carry exactly one
				 * 16-byte reg tuple.
				 */
				if (len != 16)
					return TB323FU_RAMOOPS_ERR_LAYOUT;
				nexact++;
				exact = c;
			} else if (ranges_overlap(b, s, layout->base,
						  layout->size)) {
				partial = true;
			}
		}
	}
	if (partial)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (nexact > 1)
		return TB323FU_RAMOOPS_ERR_AMBIGUOUS;
	if (nexact == 0)
		return TB323FU_RAMOOPS_ERR_NOT_FOUND;

	/* The exact reservation: boolean no-map, no reusable, fully active. */
	{
		const uint8_t *np = (const uint8_t *)fdt_getprop(
			output, exact, "no-map", &len);

		if (np == NULL || len != 0)
			return TB323FU_RAMOOPS_ERR_LAYOUT;
	}
	if (fdt_getprop(output, exact, "reusable", &len) != NULL)
		return TB323FU_RAMOOPS_ERR_LAYOUT;
	if (!node_active(output, exact))
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	/* An already bound exact node passed the layout checks above; refresh it.
	 * Any other ramoops node conflicts. */
	for (c = fdt_node_offset_by_compatible(output, -1, "ramoops"); c >= 0;
	     c = fdt_node_offset_by_compatible(output, c, "ramoops")) {
		if (c != exact)
			return TB323FU_RAMOOPS_ERR_LAYOUT;
	}

	/* ... nor already carry the target node name. */
	for (c = fdt_next_node(output, -1, NULL); c >= 0;
	     c = fdt_next_node(output, c, NULL)) {
		const char *nm;

		if (c == exact)
			continue;
		nm = fdt_get_name(output, c, NULL);
		if (nm != NULL && strcmp(nm, "ramoops@c2000000") == 0)
			return TB323FU_RAMOOPS_ERR_LAYOUT;
	}

	/* Rename in place: reg and any existing phandle are preserved. */
	rc = fdt_set_name(output, exact, "ramoops@c2000000");
	if (rc != 0)
		return fdt_err_status(rc);

	exact = fdt_path_offset(output, "/reserved-memory/ramoops@c2000000");
	if (exact < 0)
		return TB323FU_RAMOOPS_ERR_LAYOUT;

	rc = fdt_setprop_string(output, exact, "compatible", "ramoops");
	if (rc == 0)
		rc = fdt_setprop_u32(output, exact, "record-size",
				     TB323FU_RAMOOPS_RECORD_SIZE);
	if (rc == 0)
		rc = fdt_setprop_u32(output, exact, "console-size",
				     TB323FU_RAMOOPS_CONSOLE_SIZE);
	if (rc == 0)
		rc = fdt_setprop_u32(output, exact, "pmsg-size",
				     TB323FU_RAMOOPS_PMSG_SIZE);
	if (rc == 0)
		rc = fdt_setprop_u32(output, exact, "ftrace-size",
				     layout->ftrace_size);
	if (rc == 0)
		rc = fdt_setprop_u32(output, exact, "ecc-size",
				     TB323FU_RAMOOPS_ECC_SIZE);
	if (rc == 0)
		rc = fdt_setprop_u32(output, exact, "mem-type",
				     TB323FU_RAMOOPS_MEM_TYPE);
	if (rc == 0)
		rc = fdt_setprop_u32(output, exact, "max-reason",
				     TB323FU_RAMOOPS_MAX_REASON);
	if (rc != 0)
		return fdt_err_status(rc);

	return TB323FU_RAMOOPS_OK;
}

/* ======================================================================== */
/* read_console()                                                           */
/* ======================================================================== */
static const char kStageMark[] = "KSHIM_LINUX_STAGE: ";
#define STAGE_MARK_LEN (sizeof(kStageMark) - 1u)

/*
 * Streaming stage scan over the reconstructed circular log.  The marker may
 * appear anywhere inside a newline-terminated line (real kernel console lines
 * carry a "[    0.000000] " timestamp prefix), so it is matched with a small
 * in-line matcher rather than strstr over the raw blob (which would break on
 * binary bytes and on ring wrap).  Only the last complete line whose value X
 * is 1..32 bytes of [0x21,0x7e] wins; a partial final line without '\n' never
 * counts.  buf is never filled past 32 bytes and out->stage receives at most
 * 32 bytes plus NUL, so no truncation is ever reported as a full stage.
 */
static void scan_stage(const volatile uint8_t *data, uint32_t start,
		       uint32_t size, tb323fu_console_summary *out)
{
	uint32_t match = 0, caplen = 0;
	bool in_stage = false, overflow = false;
	uint8_t extra = 0; /* first byte beyond the fixed capture window */
	char buf[TB323FU_RAMOOPS_STAGE_MAX];

	for (uint32_t i = 0; i < size; i++) {
		uint8_t b = (uint8_t)data[(start + i) % size];

		if (in_stage) {
			if (b == (uint8_t)'\n') {
				uint32_t n = caplen;

				/*
				 * A single overflow byte is tolerated only when
				 * it is the '\r' of a CRLF: a full 32-char
				 * stage followed by "\r\n" is complete, the
				 * '\r' terminating the line rather than
				 * extending the value.  Any other overflow
				 * (33+ real stage bytes) stays rejected.
				 */
				if (overflow) {
					if (caplen ==
						    TB323FU_RAMOOPS_STAGE_MAX + 1 &&
					    extra == (uint8_t)'\r')
						n = TB323FU_RAMOOPS_STAGE_MAX;
					else
						n = 0;
				} else if (n > 0 &&
					   buf[n - 1] == '\r') {
					n--; /* strip a single trailing CR */
				}
				if (n >= 1 && n <= TB323FU_RAMOOPS_STAGE_MAX) {
					bool safe = true;

					for (uint32_t k = 0; k < n; k++) {
						unsigned char ch =
							(unsigned char)buf[k];

						if (ch < 0x21 || ch > 0x7e) {
							safe = false;
							break;
						}
					}
					if (safe) {
						memcpy(out->stage, buf, n);
						out->stage[n] = '\0';
						out->have_stage = true;
					}
				}
				in_stage = false;
				match = 0;
				caplen = 0;
				overflow = false;
				extra = 0;
			} else {
				if (caplen < TB323FU_RAMOOPS_STAGE_MAX)
					buf[caplen] = (char)b;
				else if (caplen == TB323FU_RAMOOPS_STAGE_MAX)
					extra = b;
				caplen++;
				if (caplen > TB323FU_RAMOOPS_STAGE_MAX)
					overflow = true;
			}
			continue;
		}

		if (b == (uint8_t)'\n') {
			match = 0;
			continue;
		}
		if (b == (uint8_t)kStageMark[match]) {
			match++;
			if (match == STAGE_MARK_LEN) {
				in_stage = true;
				match = 0;
				caplen = 0;
				overflow = false;
				extra = 0;
			}
		} else {
			match = (b == (uint8_t)kStageMark[0]) ? 1u : 0u;
		}
	}
}

int tb323fu_ramoops_read_console(const volatile uint8_t *region, size_t len,
				 const tb323fu_ramoops_layout *layout,
				 tb323fu_console_summary *out)
{
	const volatile uint8_t *data;
	uint32_t sig, start, size, cap;
	size_t tail_len;

	if (out != NULL)
		memset(out, 0, sizeof(*out));
	if (region == NULL || layout == NULL || out == NULL)
		return TB323FU_RAMOOPS_ERR_INVALID;
	if (len < TB323FU_RAMOOPS_REGION_SIZE)
		return TB323FU_RAMOOPS_ERR_INVALID;

	/* The layout must be a captured layout carrying the fixed profile. */
	if (!layout->valid ||
	    layout->base != TB323FU_RAMOOPS_REGION_BASE ||
	    layout->size != TB323FU_RAMOOPS_REGION_SIZE ||
	    layout->record_size != TB323FU_RAMOOPS_RECORD_SIZE ||
	    layout->console_size != TB323FU_RAMOOPS_CONSOLE_SIZE ||
	    layout->pmsg_size != TB323FU_RAMOOPS_PMSG_SIZE ||
	    layout->ftrace_size != TB323FU_RAMOOPS_FTRACE_SIZE ||
	    layout->ecc_size != TB323FU_RAMOOPS_ECC_SIZE ||
	    layout->mem_type != TB323FU_RAMOOPS_MEM_TYPE)
		return TB323FU_RAMOOPS_ERR_INVALID;

	cap = TB323FU_RAMOOPS_CONSOLE_USABLE;
	out->capacity = cap;
	out->ecc_corrected = false; /* raw log: never claim ECC correction */

	sig = rd_le32(region + TB323FU_RAMOOPS_CONSOLE_OFF + 0);
	start = rd_le32(region + TB323FU_RAMOOPS_CONSOLE_OFF + 4);
	size = rd_le32(region + TB323FU_RAMOOPS_CONSOLE_OFF + 8);

	/*
	 * Echo the raw header start/size before classification so BAD_HEADER
	 * reports the values actually stored, not zeros.  They lie inside the
	 * region validated above, so this read is always in bounds; no tail,
	 * stage or RAM write follows for NO_SIGNATURE / BAD_HEADER / EMPTY.
	 */
	out->start = start;
	out->size = size;

	if (sig != TB323FU_PRZ_SIG) {
		out->state = TB323FU_CONSOLE_NO_SIGNATURE;
		return TB323FU_RAMOOPS_OK;
	}
	if (size > cap || start > size) {
		out->state = TB323FU_CONSOLE_BAD_HEADER;
		return TB323FU_RAMOOPS_OK;
	}
	if (size == 0) {
		out->state = TB323FU_CONSOLE_EMPTY;
		return TB323FU_RAMOOPS_OK;
	}

	out->state = TB323FU_CONSOLE_RAW_LOG;

	data = region + TB323FU_RAMOOPS_CONSOLE_OFF + TB323FU_PRZ_HEADER_SIZE;

	/* tail: last min(size, TAIL_MAX) logical bytes, ring-order exact. */
	tail_len = (size < TB323FU_RAMOOPS_TAIL_MAX) ? size
						     : TB323FU_RAMOOPS_TAIL_MAX;
	{
		uint32_t first = size - (uint32_t)tail_len;

		for (size_t j = 0; j < tail_len; j++) {
			uint32_t idx = (first + (uint32_t)j) % size;

			out->tail[j] = (uint8_t)data[(start + idx) % size];
		}
		out->tail_len = tail_len;
	}

	scan_stage(data, start, size, out);

	return TB323FU_RAMOOPS_OK;
}
