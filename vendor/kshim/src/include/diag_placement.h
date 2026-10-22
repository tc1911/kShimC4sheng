/* SPDX-License-Identifier: MIT */
/*
 * diag_placement.h - read-only diagnostic helper.
 *
 * Verifies a physical span against an already-validated normalized HandoffDtb.
 * Diagnostic only; included only under KSHIM_DIAGNOSTIC_MEMORY_ONLY. No tree
 * writes, no device access, no heap, no mutable static state.
 */
#ifndef DIAG_PLACEMENT_H
#define DIAG_PLACEMENT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "dt.h"

#define DIAG_MAX_BANKS 64

struct diag_placement {
    bool covered;
    unsigned nomap_overlaps;
    unsigned header_overlaps;
};

/*
 * Check whether [base, base+size) is fully covered by the union of the
 * /memory banks, and count overlaps with FDT header reservations and with
 * no-map regions declared under /reserved-memory.
 *
 * Returns 0 when the blob parses as expected (coverage and collisions do not
 * affect the return value), or a negative FDT error code when the tree is
 * malformed. The caller must not use *out when a negative value is returned.
 *
 * Precondition: the caller has already validated the blob (dt_validate).
 * This helper never dereferences the physical span [base, base+size).
 */
static inline int diag_check_span(const void *fdt, uint64_t base, uint64_t size,
                                  struct diag_placement *out)
{
    struct dt_range banks[DIAG_MAX_BANKS];
    unsigned nbank = 0;
    uint64_t qend;
    int root;
    int mem;
    int rmem;
    uint32_t ac;
    uint32_t sc;

    if (fdt == 0 || out == 0)
        return -FDT_ERR_BADVALUE;
    memset(out, 0, sizeof(*out));

    if (size == 0)
        return -FDT_ERR_BADVALUE;
    qend = base + size;
    if (qend < base)
        return -FDT_ERR_BADVALUE;

    root = fdt_path_offset(fdt, "/");
    if (root < 0)
        return root;

    if (dt_u32(fdt, root, "#address-cells", &ac) != 0)
        return -FDT_ERR_BADSTRUCTURE;
    if (dt_u32(fdt, root, "#size-cells", &sc) != 0)
        return -FDT_ERR_BADSTRUCTURE;
    if (ac != 2 || sc != 2)
        return -FDT_ERR_BADSTRUCTURE;

    mem = fdt_path_offset(fdt, "/memory");
    if (mem < 0)
        return mem;
    {
        const char *dev = dt_string(fdt, mem, "device_type");
        if (dev == 0 || strcmp(dev, "memory") != 0)
            return -FDT_ERR_BADSTRUCTURE;
    }
    if (!dt_available(fdt, mem))
        return -FDT_ERR_BADSTRUCTURE;

    {
        int len = 0;
        const void *p = fdt_getprop(fdt, mem, "reg", &len);
        unsigned total;
        unsigned i;

        if (p == 0)
            return len < 0 ? len : -FDT_ERR_BADSTRUCTURE;
        if (len <= 0 || (len % 16) != 0)
            return -FDT_ERR_BADSTRUCTURE;
        total = (unsigned)len / 16;
        if (total > DIAG_MAX_BANKS)
            return -FDT_ERR_BADSTRUCTURE;

        for (i = 0; i < total; i++) {
            uint64_t b = 0;
            uint64_t s = 0;

            if (dt_raw_reg(fdt, mem, (int)i, &b, &s) != 0)
                return -FDT_ERR_BADSTRUCTURE;
            if (s == 0)
                continue;
            if (b + s < b)
                return -FDT_ERR_BADSTRUCTURE;
            banks[nbank].base = b;
            banks[nbank].size = s;
            nbank++;
        }
    }
    if (nbank == 0)
        return -FDT_ERR_BADSTRUCTURE;

    /* Coverage of [base, qend) by the union of banks. No sorting assumption:
     * each pass takes the farthest end among banks that cover the cursor. */
    {
        uint64_t cursor = base;
        unsigned pass;

        out->covered = false;
        for (pass = 0; pass < nbank; pass++) {
            uint64_t best = 0;
            bool found = false;
            unsigned i;

            for (i = 0; i < nbank; i++) {
                uint64_t bs = banks[i].base;
                uint64_t be = banks[i].base + banks[i].size;

                if (bs <= cursor && cursor < be) {
                    if (!found || be > best) {
                        best = be;
                        found = true;
                    }
                }
            }
            if (!found)
                break;
            if (best >= qend) {
                out->covered = true;
                break;
            }
            cursor = best;
        }
    }

    /* Standard FDT header reservations count only as header_overlaps. */
    {
        int n = fdt_num_mem_rsv(fdt);
        int i;

        if (n < 0)
            return n;
        for (i = 0; i < n; i++) {
            uint64_t ra = 0;
            uint64_t rs = 0;

            if (fdt_get_mem_rsv(fdt, i, &ra, &rs) != 0)
                return -FDT_ERR_BADSTRUCTURE;
            if (rs == 0)
                continue;
            if (ra + rs < ra)
                return -FDT_ERR_BADSTRUCTURE;
            if (ra < qend && base < ra + rs)
                out->header_overlaps++;
        }
    }

    /* /reserved-memory: count only true no-map children that overlap. */
    rmem = fdt_path_offset(fdt, "/reserved-memory");
    if (rmem < 0) {
        if (rmem != -FDT_ERR_NOTFOUND)
            return rmem;
    } else if (dt_available(fdt, rmem)) {
        int c;

        if (dt_u32(fdt, rmem, "#address-cells", &ac) != 0 || ac != 2)
            return -FDT_ERR_BADSTRUCTURE;
        if (dt_u32(fdt, rmem, "#size-cells", &sc) != 0 || sc != 2)
            return -FDT_ERR_BADSTRUCTURE;
        {
            int rlen = 0;
            const void *rp = fdt_getprop(fdt, rmem, "ranges", &rlen);

            if (rp == 0 || rlen != 0)
                return -FDT_ERR_BADSTRUCTURE;
        }

        for (c = fdt_first_subnode(fdt, rmem); c >= 0;
             c = fdt_next_subnode(fdt, c)) {
            int len = 0;
            const void *p;
            bool nomap = false;
            unsigned total;
            unsigned i;

            if (!dt_available(fdt, c))
                continue;

            p = fdt_getprop(fdt, c, "reg", &len);
            if (p == 0) {
                if (len == -FDT_ERR_NOTFOUND)
                    continue; /* dynamic child without reg: skip */
                return len < 0 ? len : -FDT_ERR_BADSTRUCTURE;
            }
            if (len <= 0 || (len % 16) != 0)
                return -FDT_ERR_BADSTRUCTURE;

            {
                int br = dt_bool(fdt, c, "no-map", &nomap);
                if (br != 0)
                    return br < 0 ? br : -FDT_ERR_BADSTRUCTURE;
            }

            total = (unsigned)len / 16;
            for (i = 0; i < total; i++) {
                uint64_t b = 0;
                uint64_t s = 0;

                if (dt_raw_reg(fdt, c, (int)i, &b, &s) != 0)
                    return -FDT_ERR_BADSTRUCTURE;
                if (s == 0)
                    continue;
                if (b + s < b)
                    return -FDT_ERR_BADSTRUCTURE;
                if (nomap && b < qend && base < b + s)
                    out->nomap_overlaps++;
            }
        }
    }

    return 0;
}

#endif /* DIAG_PLACEMENT_H */
