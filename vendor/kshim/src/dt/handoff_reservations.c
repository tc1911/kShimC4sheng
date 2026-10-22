#include "dt.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define SRC_CAP 128
#define DST_CAP 256

struct item {
    uint64_t base, size;
    bool nomap, reusable;
};

static int collect(const void *f, struct item *arr, size_t cap, size_t *count)
{
    size_t n = 0;
    int nrsv = fdt_num_mem_rsv(f);
    if (nrsv < 0)
        return nrsv;
    for (int i = 0; i < nrsv; i++) {
        uint64_t a, s;
        int r = fdt_get_mem_rsv(f, i, &a, &s);
        if (r)
            return r;
        if (s == 0 || a + s < a)
            return -FDT_ERR_BADVALUE;
        if (n >= cap)
            return -FDT_ERR_NOSPACE;
        arr[n].base = a; arr[n].size = s;
        arr[n].nomap = false; arr[n].reusable = false;
        n++;
    }

    int root = fdt_path_offset(f, "/");
    if (root < 0)
        return root;
    int rm = fdt_subnode_offset(f, root, "reserved-memory");
    if (rm == -FDT_ERR_NOTFOUND) {
        *count = n;
        return 0;
    }
    if (rm < 0)
        return rm;
    if (!dt_available(f, rm)) {
        *count = n;
        return 0;
    }

    uint32_t ac, sc, rac, rsc;
    int r = dt_u32(f, root, "#address-cells", &ac);
    if (r)
        return r;
    r = dt_u32(f, root, "#size-cells", &sc);
    if (r)
        return r;
    r = dt_u32(f, rm, "#address-cells", &rac);
    if (r)
        return r;
    r = dt_u32(f, rm, "#size-cells", &rsc);
    if (r)
        return r;
    if ((rac != 1 && rac != 2) || (rsc != 1 && rsc != 2))
        return -FDT_ERR_BADVALUE;
    if (rac != ac || rsc != sc)
        return -FDT_ERR_BADVALUE;

    int rl;
    const void *rg = fdt_getprop(f, rm, "ranges", &rl);
    if (!rg)
        return rl;
    if (rl != 0)
        return -FDT_ERR_BADVALUE;

    int cells = (int)(4 * (rac + rsc));
    for (int ch = fdt_first_subnode(f, rm); ch >= 0;
         ch = fdt_next_subnode(f, ch)) {
        if (!dt_available(f, ch))
            continue;
        int len;
        const void *reg = fdt_getprop(f, ch, "reg", &len);
        if (!reg) {
            if (len == -FDT_ERR_NOTFOUND)
                continue;
            return len;
        }
        if (len <= 0 || len % cells)
            return -FDT_ERR_BADVALUE;
        bool nomap = false, reusable = false;
        r = dt_bool(f, ch, "no-map", &nomap);
        if (r)
            return r;
        r = dt_bool(f, ch, "reusable", &reusable);
        if (r)
            return r;
        if (nomap && reusable)
            return -FDT_ERR_BADVALUE;
        int nent = len / cells;
        for (int k = 0; k < nent; k++) {
            uint64_t b, s;
            r = dt_raw_reg(f, ch, (unsigned)k, &b, &s);
            if (r)
                return r;
            if (s == 0)
                continue;
            if (b + s < b)
                return -FDT_ERR_BADVALUE;
            if (n >= cap)
                return -FDT_ERR_NOSPACE;
            arr[n].base = b; arr[n].size = s;
            arr[n].nomap = nomap; arr[n].reusable = reusable;
            n++;
        }
    }
    *count = n;
    return 0;
}

static int append_gap(void *out, void *overlay, struct item *arr,
                      size_t *count, uint64_t base, uint64_t size,
                      bool nomap)
{
    if (*count >= DST_CAP)
        return -FDT_ERR_NOSPACE;
    if (size == 0 || base + size < base)
        return -FDT_ERR_BADVALUE;

    if (!nomap) {
        int r = fdt_add_mem_rsv(out, base, size);
        if (r)
            return r;
    } else {
        char name[64];
        snprintf(name, sizeof name, "kshim-firmware@%llx",
                 (unsigned long long)base);

        if (overlay != NULL) {
            int trm = fdt_subnode_offset(out, 0, "reserved-memory");
            if (trm == -FDT_ERR_NOTFOUND) {
                /* Target has no reserved-memory node to guard. */
            } else if (trm < 0) {
                return trm;
            } else {
                if (!dt_available(out, trm))
                    return -FDT_ERR_BADSTATE;
                int tc = fdt_subnode_offset(out, trm, name);
                if (tc == -FDT_ERR_NOTFOUND) {
                    /* No collision with the target tree. */
                } else if (tc < 0) {
                    return tc;
                } else {
                    return -FDT_ERR_EXISTS;
                }
            }
        }

        void *tree = overlay ? overlay : out;
        int parentroot = overlay
            ? fdt_path_offset(overlay, "/fragment@0/__overlay__") : 0;
        if (parentroot < 0)
            return parentroot;
        int rm = fdt_subnode_offset(tree, parentroot, "reserved-memory");
        if (rm == -FDT_ERR_NOTFOUND) {
            rm = fdt_add_subnode(tree, parentroot, "reserved-memory");
            if (rm < 0)
                return rm;
            int r = fdt_setprop_u32(tree, rm, "#address-cells", 2);
            if (r)
                return r;
            r = fdt_setprop_u32(tree, rm, "#size-cells", 2);
            if (r)
                return r;
            r = fdt_setprop_empty(tree, rm, "ranges");
            if (r)
                return r;
        } else {
            if (rm < 0)
                return rm;
            if (!dt_available(tree, rm))
                return -FDT_ERR_BADSTATE;
            uint32_t c;
            int r = dt_u32(tree, rm, "#address-cells", &c);
            if (r || c != 2)
                return r ? r : -FDT_ERR_BADVALUE;
            r = dt_u32(tree, rm, "#size-cells", &c);
            if (r || c != 2)
                return r ? r : -FDT_ERR_BADVALUE;
            int rl;
            const void *rg = fdt_getprop(tree, rm, "ranges", &rl);
            if (!rg)
                return rl;
            if (rl != 0)
                return -FDT_ERR_BADVALUE;
        }
        if (fdt_subnode_offset(tree, rm, name) >= 0)
            return -FDT_ERR_EXISTS;
        int ch = fdt_add_subnode(tree, rm, name);
        if (ch < 0)
            return ch;
        fdt64_t reg[2];
        reg[0] = cpu_to_fdt64(base);
        reg[1] = cpu_to_fdt64(size);
        int r = fdt_setprop(tree, ch, "reg", reg, sizeof reg);
        if (r)
            return r;
        r = fdt_setprop_empty(tree, ch, "no-map");
        if (r)
            return r;
    }

    arr[*count].base = base;
    arr[*count].size = size;
    arr[*count].nomap = nomap;
    arr[*count].reusable = false;
    (*count)++;
    return 0;
}

static int merge_reservations(const void *runtime, void *output, void *overlay)
{
    struct item src[SRC_CAP], dst[DST_CAP];
    size_t nsrc = 0, ndst = 0;
    int r = collect(runtime, src, SRC_CAP, &nsrc);
    if (r)
        return r;
    r = collect(output, dst, DST_CAP, &ndst);
    if (r)
        return r;

    for (int pass = 0; pass < 2; pass++) {
        bool want = (pass == 0);
        for (size_t i = 0; i < nsrc; i++) {
            if (src[i].nomap != want)
                continue;
            uint64_t cursor = src[i].base;
            uint64_t end = src[i].base + src[i].size;
            while (cursor < end) {
                uint64_t cover_end = cursor;
                uint64_t next_start = end;
                for (size_t j = 0; j < ndst; j++) {
                    uint64_t db = dst[j].base;
                    uint64_t de = dst[j].base + dst[j].size;
                    if (de <= cursor || db >= end)
                        continue;
                    if (dst[j].reusable ||
                        (src[i].nomap && !dst[j].nomap))
                        return -FDT_ERR_BADVALUE;
                    if (db <= cursor) {
                        if (de > cover_end)
                            cover_end = de;
                    } else if (db < next_start) {
                        next_start = db;
                    }
                }
                if (cover_end > cursor) {
                    cursor = cover_end < end ? cover_end : end;
                    continue;
                }
                r = append_gap(output, overlay, dst, &ndst, cursor,
                               next_start - cursor, src[i].nomap);
                if (r)
                    return r;
                cursor = next_start;
            }
        }
    }
    return 0;
}

int dt_handoff_reservations(const void *runtime, void *output)
{
    return merge_reservations(runtime, output, NULL);
}

int dt_handoff_reservations_overlay(const void *runtime, void *output,
                                    void *overlay)
{
    return merge_reservations(runtime, output, overlay);
}
