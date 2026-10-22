/*
 * simplefb.c - populate a /chosen/simple-framebuffer node.
 *
 * Phase 1 (prevalidation) mutates nothing: a negative return leaves the
 * input blob byte-for-byte unchanged.  Phase 2 (mutation) can fail with
 * -FDT_ERR_NOSPACE after partial writes, so the caller must treat any
 * negative return as "do not boot this tree, use the Android fallback".
 * We intentionally do NOT fdt_pack() the partial result.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libfdt.h>

#include "dt.h"
#include "dt_simplefb.h"

static int fmt_bpp(kshim_fb_format_t format, uint32_t *bpp)
{
    switch (format) {
    case KSHIM_FB_FORMAT_RGB565:    *bpp = 16; return 0;
    case KSHIM_FB_FORMAT_XRGB8888:  *bpp = 32; return 0;
    case KSHIM_FB_FORMAT_ARGB8888:  *bpp = 32; return 0;
    case KSHIM_FB_FORMAT_XBGR8888:  *bpp = 32; return 0;
    case KSHIM_FB_FORMAT_ABGR8888:  *bpp = 32; return 0;
    default: return -1;
    }
}

static const char *fmt_name(kshim_fb_format_t format)
{
    switch (format) {
    case KSHIM_FB_FORMAT_RGB565:    return "r5g6b5";
    case KSHIM_FB_FORMAT_XRGB8888:  return "x8r8g8b8";
    case KSHIM_FB_FORMAT_ARGB8888:  return "a8r8g8b8";
    case KSHIM_FB_FORMAT_XBGR8888:  return "x8b8g8r8";
    case KSHIM_FB_FORMAT_ABGR8888:  return "a8b8g8r8";
    default: return NULL;
    }
}

/*
 * Create /reserved-memory/kshim-framebuffer@<physical> with reg = <64 physical
 * 64 buffer_size> and an empty no-map property.  This mirrors the stock
 * cont_splash_region (reserved, NOT no-map) so the full backing buffer_size is
 * carved out of the linear map instead of being aliased by the last partial
 * page.  Creates the /reserved-memory parent if absent; all fdt_* returns are
 * checked and any failure propagates negative to the caller.
 */
static int add_scanout_nomap(void *blob, uint64_t physical,
                             uint64_t buffer_size)
{
    char rname[64];
    fdt64_t reg[2];
    int rsv, child, n, root;

    n = snprintf(rname, sizeof(rname), "kshim-framebuffer@%llx",
                 (unsigned long long)physical);
    if (n < 0 || (size_t)n >= sizeof(rname))
        return -1;

    rsv = fdt_path_offset(blob, "/reserved-memory");
    if (rsv < 0) {
        root = fdt_path_offset(blob, "/");
        if (root < 0)
            return -1;
        rsv = fdt_add_subnode(blob, root, "reserved-memory");
        if (rsv < 0)
            return -1;
        if (fdt_setprop_u32(blob, rsv, "#address-cells", 2) != 0 ||
            fdt_setprop_u32(blob, rsv, "#size-cells", 2) != 0 ||
            fdt_setprop(blob, rsv, "ranges", NULL, 0) != 0)
            return -1;
    }

    child = fdt_add_subnode(blob, rsv, rname);
    if (child < 0)
        return -1;

    reg[0] = cpu_to_fdt64(physical);
    reg[1] = cpu_to_fdt64(buffer_size);
    if (fdt_setprop(blob, child, "reg", reg, sizeof(reg)) != 0)
        return -1;
    if (fdt_setprop(blob, child, "no-map", NULL, 0) != 0)
        return -1;

    return 0;
}

int dt_simplefb_apply(void *blob, size_t capacity,
                      const kshim_framebuffer_config_t *fb)
{
    int chosen, template = -1, count = 0, node, nr, i, need_rsv = 1;
    int rsv;
    uint32_t ac, sc, bpp;
    uint64_t end;
    const char *status;
    char name[64];
    char rname[64];

    /* --- Phase 1: prevalidation (blob untouched on any failure) --- */
    if (dt_validate(blob, capacity) != 0)
        return -1;
    if (capacity > (size_t)INT_MAX)
        return -1;

    chosen = fdt_path_offset(blob, "/chosen");
    if (chosen < 0)
        return 0; /* no /chosen -> no-op, bytes unchanged even if fb==NULL */

    for (node = fdt_first_subnode(blob, chosen); node >= 0;
         node = fdt_next_subnode(blob, node)) {
        if (fdt_node_check_compatible(blob, node, "simple-framebuffer") == 0) {
            if (count)
                return -1; /* duplicate */
            template = node;
            count = 1;
        }
    }
    if (count == 0)
        return 0; /* no compatible child -> no-op, bytes unchanged */

    if (fdt_get_property(blob, template, "memory-region", NULL))
        return -1; /* present, even zero-length -> reject */

    status = dt_string(blob, template, "status");
    if (!status || strcmp(status, "disabled") != 0)
        return -1;

    if (dt_u32(blob, chosen, "#address-cells", &ac) != 0 || ac != 2)
        return -1;
    if (dt_u32(blob, chosen, "#size-cells", &sc) != 0 || sc != 2)
        return -1;
    if (!fdt_getprop(blob, chosen, "ranges", &nr) || nr != 0)
        return -1;

    if (!fb || !fb->render_address || !fb->width || !fb->height ||
        !fb->stride || !fb->buffer_size)
        return -1;
    if (fmt_bpp(fb->format, &bpp) != 0 || fb->bpp != bpp)
        return -1; /* bpp must match the declared format exactly */
    if ((uint64_t)fb->width * (bpp / 8) > (uint64_t)fb->stride)
        return -1;
    {
        uint64_t span = (uint64_t)fb->stride * (uint64_t)fb->height;
        if (span > (uint64_t)SIZE_MAX || span > fb->buffer_size)
            return -1;
        end = fb->render_address + span;
        if (end < fb->render_address || end > (uint64_t)UINTPTR_MAX)
            return -1;
    }

    {
        int n = snprintf(name, sizeof(name), "framebuffer@%llx",
                         (unsigned long long)fb->render_address);
        if (n < 0 || (size_t)n >= sizeof(name))
            return -1;
    }
    node = fdt_subnode_offset(blob, chosen, name);
    if (node >= 0 && node != template)
        return -1; /* name collision with a node that is not ourselves */

    nr = fdt_num_mem_rsv(blob);
    for (i = 0; i < nr; i++) {
        uint64_t addr, size;
        if (fdt_get_mem_rsv(blob, i, &addr, &size) != 0)
            return -1; /* propagate negative */
        if (addr + size < addr)
            return -1; /* overflow rejected BEFORE any mutation */
        if (addr <= fb->render_address &&
            addr + size >= end)
            need_rsv = 0; /* wholly covering -> no own reservation needed */
    }

    /* Full backing size (not the visible span) must fit before we mutate. */
    {
        uint64_t rsv_end = (uint64_t)fb->render_address +
                           (uint64_t)fb->buffer_size;
        if (rsv_end < (uint64_t)fb->render_address ||
            rsv_end > (uint64_t)UINTPTR_MAX ||
            rsv_end > (uint64_t)UINT64_MAX)
            return -1;
    }

    {
        int n = snprintf(rname, sizeof(rname), "kshim-framebuffer@%llx",
                         (unsigned long long)fb->render_address);
        if (n < 0 || (size_t)n >= sizeof(rname))
            return -1;
    }

    rsv = fdt_path_offset(blob, "/reserved-memory");
    if (rsv >= 0) {
        uint32_t rac, rsc;
        int rnr;
        if (!dt_available(blob, rsv))
            return -1;
        if (dt_u32(blob, rsv, "#address-cells", &rac) != 0 || rac != 2)
            return -1;
        if (dt_u32(blob, rsv, "#size-cells", &rsc) != 0 || rsc != 2)
            return -1;
        if (!fdt_getprop(blob, rsv, "ranges", &rnr) || rnr != 0)
            return -1;
        if (fdt_subnode_offset(blob, rsv, rname) >= 0)
            return -1; /* name collision with pre-existing child */
    }

    /*
     * --- Phase 2: mutation.  From here on a failure may leave the blob
     * partially modified (typically -FDT_ERR_NOSPACE); the caller must
     * not boot it and should fall back.  No pack, no rollback pretending.
     */
    if (fdt_open_into(blob, blob, capacity) != 0)
        return -1;

    /* open_into on the same blob keeps the template offset relative-valid. */
    if (fdt_set_name(blob, template, name) != 0)
        return -1;
    {
        uint8_t reg[16];
        uint64_t base = fb->render_address;
        uint64_t size = end - fb->render_address;
        for (i = 0; i < 8; i++) {
            reg[i]     = (uint8_t)(base >> (56 - 8 * i));
            reg[8 + i] = (uint8_t)(size >> (56 - 8 * i)); /* big-endian */
        }
        if (fdt_setprop(blob, template, "reg", reg, sizeof(reg)) != 0)
            return -1;
    }
    if (fdt_setprop_u32(blob, template, "width", fb->width) != 0 ||
        fdt_setprop_u32(blob, template, "height", fb->height) != 0 ||
        fdt_setprop_u32(blob, template, "stride", fb->stride) != 0 ||
        fdt_setprop_string(blob, template, "format",
                           fmt_name(fb->format)) != 0 ||
        fdt_setprop_string(blob, template, "status", "okay") != 0)
        return -1;

    if (need_rsv &&
        fdt_add_mem_rsv(blob, fb->render_address,
                        end - fb->render_address) != 0)
        return -1;

    /*
     * Last mutation: only now create the reserved-memory subtree, so the
     * earlier template/chosen offsets are never reused after a root-node
     * insertion shifts the tree.
     */
    if (add_scanout_nomap(blob, (uint64_t)fb->render_address,
                          (uint64_t)fb->buffer_size) != 0)
        return -1;

    return 0;
}
