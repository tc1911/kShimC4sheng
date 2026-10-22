#ifndef DIAG_MEMBLOCK_GATE_H
#define DIAG_MEMBLOCK_GATE_H

#include "dt.h"
#include <string.h>

struct diag_memblock_bounds {
	unsigned banks;
	unsigned memory_bound;
	unsigned reserved_bound;
};

/*
 * diag_memblock_gate - private M34 diagnostic read-only memory-block gate.
 *
 * Preconditions:
 *   - "fdt" points at a dt_validate()'d, finalized, normalized HandoffDtb blob.
 *     The caller owns normalization; this gate performs no writes to the blob,
 *     to physical RAM, MMIO, heap, or any global state (read-only).
 *   - Frozen Image profile: the caller's static 128-entry reservation/segment
 *     capacity is fixed and must not be resized before paging is enabled.
 *   - Dynamic allocation and runtime callbacks are not accepted; only the
 *     compiled, pinned Linux registered callbacks are considered.
 *
 * Scope: private M34 diagnostic gate only; does not define general boot
 * policy and makes no claim regarding physical Linux boot, CPU behavior,
 * or test execution.
 *
 * Returns 0 when both derived bounds fit within the frozen 128 capacity,
 * otherwise a negative FDT error.
 */
static inline int diag_memblock_gate(const void *fdt, struct diag_memblock_bounds *out)
{
	int root, memory, reserved;
	uint32_t cells;
	size_t nbank = 0;
	struct dt_range banks[64];
	struct dt_range reservations[128];
	bool no_map[128];
	size_t count = 0;
	size_t i, j;
	int node, rc;
	int len;
	const void *prop;
	const char *str;

	if (fdt == NULL || out == NULL)
		return -FDT_ERR_BADVALUE;

	memset(out, 0, sizeof(*out));

	root = fdt_path_offset(fdt, "/");
	if (root < 0)
		return -FDT_ERR_BADVALUE;
	if (dt_u32(fdt, root, "#address-cells", &cells) < 0 || cells != 2)
		return -FDT_ERR_BADVALUE;
	if (dt_u32(fdt, root, "#size-cells", &cells) < 0 || cells != 2)
		return -FDT_ERR_BADVALUE;

	memory = fdt_path_offset(fdt, "/memory");
	if (memory < 0)
		return -FDT_ERR_BADVALUE;
	if (!dt_available(fdt, memory))
		return -FDT_ERR_BADVALUE;
	str = dt_string(fdt, memory, "device_type");
	if (str == NULL || strcmp(str, "memory") != 0)
		return -FDT_ERR_BADVALUE;

	prop = fdt_getprop(fdt, memory, "reg", &len);
	if (prop == NULL || len <= 0 || (len % 16) != 0)
		return -FDT_ERR_BADVALUE;
	nbank = (size_t)len / 16;
	if (nbank == 0 || nbank > 64)
		return -FDT_ERR_BADVALUE;

	for (i = 0; i < nbank; i++) {
		uint64_t address, size;

		rc = dt_raw_reg(fdt, memory, (unsigned)i, &address, &size);
		if (rc < 0)
			return rc;
		if (size == 0)
			return -FDT_ERR_BADVALUE;
		if (address + size < address)
			return -FDT_ERR_BADVALUE;
		banks[i].base = address;
		banks[i].size = size;
	}

	for (i = 0; i < nbank; i++) {
		for (j = i + 1; j < nbank; j++) {
			uint64_t a_start = banks[i].base;
			uint64_t a_end = banks[i].base + banks[i].size;
			uint64_t b_start = banks[j].base;
			uint64_t b_end = banks[j].base + banks[j].size;

			if (a_start < b_end && b_start < a_end)
				return -FDT_ERR_BADVALUE;
		}
	}

	reserved = fdt_path_offset(fdt, "/reserved-memory");
	if (reserved >= 0) {
		if (!dt_available(fdt, reserved))
			return -FDT_ERR_BADVALUE;
		if (dt_u32(fdt, reserved, "#address-cells", &cells) < 0 || cells != 2)
			return -FDT_ERR_BADVALUE;
		if (dt_u32(fdt, reserved, "#size-cells", &cells) < 0 || cells != 2)
			return -FDT_ERR_BADVALUE;
		prop = fdt_getprop(fdt, reserved, "ranges", &len);
		if (prop == NULL || len != 0)
			return -FDT_ERR_BADVALUE;

		for (node = fdt_first_subnode(fdt, reserved); node >= 0;
		     node = fdt_next_subnode(fdt, node)) {
			const void *compat, *regprop, *sizeprop;
			int clen, reglen, sizelen;

			if (!dt_available(fdt, node))
				continue;

			compat = fdt_getprop(fdt, node, "compatible", &clen);
			(void)clen;
			if (compat != NULL) {
				if (fdt_stringlist_count(fdt, node, "compatible") < 0)
					return -FDT_ERR_BADVALUE;
				if (fdt_node_check_compatible(fdt, node, "shared-dma-pool") == 0)
					return -FDT_ERR_BADVALUE;
				if (fdt_node_check_compatible(fdt, node, "restricted-dma-pool") == 0)
					return -FDT_ERR_BADVALUE;
			}

			regprop = fdt_getprop(fdt, node, "reg", &reglen);
			sizeprop = fdt_getprop(fdt, node, "size", &sizelen);
			(void)reglen;
			(void)sizelen;
			if (regprop == NULL && sizeprop != NULL)
				return -FDT_ERR_BADVALUE;
		}
	} else if (reserved != -FDT_ERR_NOTFOUND) {
		return reserved;
	}

	rc = dt_reservations(fdt, reservations, no_map, 128, &count);
	if (rc < 0)
		return rc;

	out->banks = (unsigned)nbank;
	out->memory_bound = (unsigned)nbank + 4;
	out->reserved_bound = (unsigned)count + 4;

	for (i = 0; i < count; i++) {
		if (!no_map[i])
			continue;
		for (j = 0; j < nbank; j++) {
			uint64_t b_start = banks[j].base;
			uint64_t b_end = banks[j].base + banks[j].size;
			uint64_t r_base = reservations[i].base;
			uint64_t r_end = reservations[i].base + reservations[i].size;

			if (r_base > b_start && r_base < b_end)
				out->memory_bound++;
			if (r_end > b_start && r_end < b_end)
				out->memory_bound++;
		}
	}

	if (out->memory_bound > 128 || out->reserved_bound > 128)
		return -FDT_ERR_BADVALUE;

	return 0;
}

#endif /* DIAG_MEMBLOCK_GATE_H */
