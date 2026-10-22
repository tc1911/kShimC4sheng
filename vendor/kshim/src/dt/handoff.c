#include "dt_handoff.h"
#include "dt.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
int dt_handoff_reservations(const void *runtime, void *output); /* internal helper */

/* Diagnostic detail of last legacy normalization call; not functional state. */
static char detail[80];
const char *dt_handoff_failure_detail(void)
{
	return detail;
}

#define HANDOFF_MAX_BANKS 64
#define HANDOFF_MAX_CAP   (2u * 1024u * 1024u)

static int dt_ovl(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	return (a < d) && (c < b);
}

int dt_handoff_memory(const void *runtime, size_t runtime_size,
		      const void *payload, size_t payload_size,
		      void *output, size_t capacity)
{
	int rc, root, node, child;
	uint32_t sac, ssc, tac, tsc;
	size_t per_bank, nbank = 0, i, j;
	struct dt_range banks[HANDOFF_MAX_BANKS];
	fdt32_t reg[HANDOFF_MAX_BANKS * 4];
	uint64_t ob, oe, rb, re, pb, pe;

	strcpy(detail, "validate");
	rc = dt_validate(runtime, runtime_size);
	if (rc < 0)
		return rc;
	rc = dt_validate(payload, payload_size);
	if (rc < 0)
		return rc;

	if (output == NULL)
		return -FDT_ERR_BADVALUE;
	if (((uintptr_t)output & 7u) != 0)
		return -FDT_ERR_BADVALUE;
	if (capacity < sizeof(struct fdt_header))
		return -FDT_ERR_NOSPACE;
	if (capacity > HANDOFF_MAX_CAP || capacity > (size_t)INT_MAX)
		return -FDT_ERR_BADVALUE;

	ob = (uint64_t)(uintptr_t)output;
	oe = ob + (uint64_t)capacity;
	if (oe < ob)
		return -FDT_ERR_BADVALUE;
	rb = (uint64_t)(uintptr_t)runtime;
	re = rb + (uint64_t)runtime_size;
	if (re < rb || dt_ovl(ob, oe, rb, re))
		return -FDT_ERR_BADVALUE;
	pb = (uint64_t)(uintptr_t)payload;
	pe = pb + (uint64_t)payload_size;
	if (pe < pb || dt_ovl(ob, oe, pb, pe))
		return -FDT_ERR_BADVALUE;

	/* source root address/size cells */
	strcpy(detail, "cells");
	root = fdt_path_offset(runtime, "/");
	if (root < 0)
		return root;
	rc = dt_u32(runtime, root, "#address-cells", &sac);
	if (rc < 0)
		return rc;
	rc = dt_u32(runtime, root, "#size-cells", &ssc);
	if (rc < 0)
		return rc;
	if ((sac != 1 && sac != 2) || (ssc != 1 && ssc != 2))
		return -FDT_ERR_BADVALUE;

	/* target root must be 2/2 */
	{
		int proot = fdt_path_offset(payload, "/");
		if (proot < 0)
			return proot;
		rc = dt_u32(payload, proot, "#address-cells", &tac);
		if (rc < 0)
			return rc;
		rc = dt_u32(payload, proot, "#size-cells", &tsc);
		if (rc < 0)
			return rc;
		if (tac != 2 || tsc != 2)
			return -FDT_ERR_BADVALUE;
	}

	per_bank = 4u * ((size_t)sac + (size_t)ssc);

	/* collect enabled memory banks in traversal order */
	strcpy(detail, "ram");
	for (node = fdt_first_subnode(runtime, root); node >= 0;
	     node = fdt_next_subnode(runtime, node)) {
		const char *dtype = dt_string(runtime, node, "device_type");
		const void *prop;
		int plen;
		if (dtype == NULL || strcmp(dtype, "memory") != 0)
			continue;
		if (!dt_available(runtime, node))
			continue;
		prop = fdt_getprop(runtime, node, "reg", &plen);
		if (prop == NULL || plen <= 0)
			return -FDT_ERR_BADVALUE;
		if ((size_t)plen % per_bank != 0)
			return -FDT_ERR_BADVALUE;
		for (i = 0; i < (size_t)plen / per_bank; i++) {
			uint64_t base, sz, bend;
			rc = dt_raw_reg(runtime, node, (int)i, &base, &sz);
			if (rc < 0)
				return rc;
			/* zero-size bank: Linux skips, not malformed */
			if (sz == 0)
				continue;
			if (base > UINT64_MAX - sz) {
				snprintf(detail, sizeof(detail), "v:%llx,%llx",
					 (unsigned long long)base,
					 (unsigned long long)sz);
				return -FDT_ERR_BADVALUE;
			}
			bend = base + sz;
			for (j = 0; j < nbank; j++) {
				uint64_t bj = banks[j].base;
				uint64_t ej = banks[j].base + banks[j].size;
				if (dt_ovl(base, bend, bj, ej)) {
					snprintf(detail, sizeof(detail),
						 "o:%llx,%llx:%llx,%llx",
						 (unsigned long long)base,
						 (unsigned long long)sz,
						 (unsigned long long)bj,
						 (unsigned long long)banks[j].size);
					return -FDT_ERR_BADVALUE;
				}
			}
			if (nbank >= HANDOFF_MAX_BANKS)
				return -FDT_ERR_NOSPACE;
			banks[nbank].base = base;
			banks[nbank].size = sz;
			nbank++;
		}
	}
	if (nbank == 0)
		return -FDT_ERR_NOTFOUND;

	for (i = 0; i < nbank; i++) {
		reg[i * 4 + 0] = cpu_to_fdt32((uint32_t)(banks[i].base >> 32));
		reg[i * 4 + 1] = cpu_to_fdt32((uint32_t)(banks[i].base & 0xffffffffu));
		reg[i * 4 + 2] = cpu_to_fdt32((uint32_t)(banks[i].size >> 32));
		reg[i * 4 + 3] = cpu_to_fdt32((uint32_t)(banks[i].size & 0xffffffffu));
	}

	strcpy(detail, "output");
	rc = fdt_open_into(payload, output, (int)capacity);
	if (rc < 0)
		return rc;

	/* remove every root child with device_type == memory */
	for (;;) {
		int found = 0;
		root = fdt_path_offset(output, "/");
		if (root < 0)
			return root;
		for (child = fdt_first_subnode(output, root); child >= 0;
		     child = fdt_next_subnode(output, child)) {
			const char *dtype = dt_string(output, child, "device_type");
			if (dtype != NULL && strcmp(dtype, "memory") == 0) {
				rc = fdt_del_node(output, child);
				if (rc < 0)
					return rc;
				found = 1;
				break;
			}
		}
		if (!found)
			break;
	}

	root = fdt_path_offset(output, "/");
	if (root < 0)
		return root;
	rc = fdt_add_subnode(output, root, "memory");
	if (rc < 0)
		return rc;
	node = rc;
	rc = fdt_setprop_string(output, node, "device_type", "memory");
	if (rc < 0)
		return rc;
	rc = fdt_setprop(output, node, "reg", reg,
			 (int)(nbank * 4 * sizeof(fdt32_t)));
	if (rc < 0)
		return rc;

	/* drop stale chosen initrd bounds (ignore missing props only) */
	{
		int chosen = fdt_path_offset(output, "/chosen");
		if (chosen >= 0) {
			rc = fdt_delprop(output, chosen, "linux,initrd-start");
			if (rc < 0 && rc != -FDT_ERR_NOTFOUND)
				return rc;
			chosen = fdt_path_offset(output, "/chosen");
			if (chosen < 0)
				return chosen;
			rc = fdt_delprop(output, chosen, "linux,initrd-end");
			if (rc < 0 && rc != -FDT_ERR_NOTFOUND)
				return rc;
		}
	}

	strcpy(detail, "reservations");
	rc = dt_handoff_reservations(runtime, output);
	if (rc < 0)
		return rc;
	strcpy(detail, "pack");
	rc = fdt_pack(output);
	if (rc < 0)
		return rc;

	rc = dt_validate(output, capacity);
	if (rc < 0)
		return rc;

	strcpy(detail, "done");
	return 0;
}
