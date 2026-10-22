#ifndef DIAG_M1_SUMMARY_H
#define DIAG_M1_SUMMARY_H
/*
 * diag_m1_summary.h - PRIVATE M1 controlled-return summary helper.
 *
 * Header-only. Reads final_dt and snapshot strictly read-only, and emits
 * five fixed reports (slots 2..6) by rewriting ONLY the bounded 80-byte
 * value bytes of the matching androidboot.kshim_sN= tokens inside
 * runtime's /chosen/bootargs. No other property is touched; the blob is
 * never grown.
 *
 * Slots: 0 menu, 1 geometry, 2 mlog, 3 ram, 4 rsv, 5 fb, 6 proof,
 *        7 keypoll, 8 SPMI.  This file owns 2..6 only, placed after
 *        memory/simplefb/pack and before kernelprobe.
 *
 * FNV1a32 is a byte FINGERPRINT (summary), NOT a security checksum.
 *
 * "missing" fingerprints report 0 meaning UNKNOWN, never "hardware pass".
 * Genuine negative libfdt errors are never suppressed.
 *
 * This is a private M1 controlled return summary, not a permanent
 * Linux gate. A missing/incomplete actual DT is still reported (error
 * return or 0 fingerprints), never masked as success.
 *
 * Build and validation records are maintained outside the source tree.
 * No local compile; CPU compile tests happen later only on
 * a remote build-server (Docker). Not compiled/tested in-tree.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libfdt.h>

#include "dt.h"          /* dt_validate, dt_reservations, dt_raw_reg, dt_u32 ... */
#include "framebuffer.h" /* kshim_framebuffer_config_t */

#define M1_SLOT_FIRST 2u
#define M1_SLOT_LAST  6u
#define M1_VAL_LEN    80

#define M1_OK           0
#define M1_E_ARG       -1
#define M1_E_VALIDATE  -2
#define M1_E_CAPACITY  -3
#define M1_E_NO_MEMORY -4
#define M1_E_CELLS     -5
#define M1_E_REG       -6
#define M1_E_OVF       -7
#define M1_E_TOOMANY   -8
#define M1_E_RSV       -9
#define M1_E_NOFB     -10
#define M1_E_CHOSEN   -11
#define M1_E_BOOTARGS -12
#define M1_E_SLOT     -13
#define M1_E_WRITE    -14

/* Fingerprint (summary), not a security checksum. */
static inline uint32_t m1_fnv1a32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0x811c9dc5u;
    size_t i;
    if (!p && len) return 0u;
    for (i = 0; i < len; i++) {
        h ^= (uint32_t)p[i];
        h *= 0x01000193u;
    }
    return h;
}

static inline void m1_le64(uint8_t *d, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8; i++) d[i] = (uint8_t)(v >> (8u * i));
}

/* pad a prepared summary string into an exact 80-char value field */
static inline int m1_pad80(char dst[M1_VAL_LEN], const char *src)
{
    size_t n, k;
    if (!src) return -1;
    n = strnlen(src, (size_t)M1_VAL_LEN + 1u);
    if (n == 0u || n > (size_t)M1_VAL_LEN) return -1; /* reject 0 or >80 */
    for (k = 0; k < n; k++) {
        unsigned char c = (unsigned char)src[k];
        if (c < 33u || c > 126u) return -1;           /* printable, no space */
    }
    memcpy(dst, src, n);
    if (n < (size_t)M1_VAL_LEN)
        memset(dst + n, '_', (size_t)M1_VAL_LEN - n);
    return 0;
}

/* first node whose compatible list contains compat; a negative libfdt
 * status (e.g. -FDT_ERR_NOTFOUND) is returned verbatim to the caller */
static inline int m1_node_by_compat(const void *fdt, const char *compat)
{
    return fdt_node_offset_by_compatible(fdt, -1, compat);
}

/* bounded fingerprint of a property value; *out is set to 0 (UNKNOWN)
 * for an absent node/prop, other libfdt errors are returned verbatim */
static inline int m1_prop_hash(const void *fdt, int off, const char *name,
                               uint32_t *out)
{
    int len = 0;
    const void *v;
    *out = 0u;
    if (off < 0) {
        if (off == -FDT_ERR_NOTFOUND) return 0;  /* optional node absent */
        return off;                              /* other libfdt error */
    }
    v = fdt_getprop(fdt, off, name, &len);
    if (!v) {
        if (len == -FDT_ERR_NOTFOUND) return 0;  /* optional prop absent */
        return len;
    }
    if (len > 0) *out = m1_fnv1a32(v, (size_t)len);
    return 0;
}

/* PSCI method: 1 smc, 2 hvc, 0 missing/unknown; negative libfdt errors
 * are propagated, an absent node is not fatal. */
static inline int m1_psci(const void *fdt)
{
    int len = 0;
    int off;
    const char *m;
    off = m1_node_by_compat(fdt, "arm,psci-1.0");
    if (off < 0) {
        if (off == -FDT_ERR_NOTFOUND) return 0;  /* optional node absent */
        return off;
    }
    m = (const char *)fdt_getprop(fdt, off, "method", &len);
    if (!m) {
        if (len == -FDT_ERR_NOTFOUND) return 0;
        return len;
    }
    if (len == 4 && memcmp(m, "smc", 3) == 0 && m[3] == '\0') return 1;
    if (len == 4 && memcmp(m, "hvc", 3) == 0 && m[3] == '\0') return 2;
    return 0;
}

/* locate androidboot.kshim_sN= at a token boundary across the ENTIRE
 * validated bootargs; returns 0 only on exactly one match so a duplicate
 * key can never be written. Value runs to space or the NUL. */
static inline int m1_find_slot(const char *ba, size_t ba_len, unsigned slot,
                               size_t *voff, size_t *vlen)
{
    char tok[32];
    int tlen;
    size_t i, found = 0u, fvoff = 0u, fvlen = 0u;
    if (slot < M1_SLOT_FIRST || slot > M1_SLOT_LAST) return -1;
    tlen = snprintf(tok, sizeof(tok), "androidboot.kshim_s%u=", slot);
    if (tlen <= 0 || (size_t)tlen >= sizeof(tok)) return -1;
    for (i = 0; i + (size_t)tlen <= ba_len; i++) {
        size_t v, end;
        if (i != 0 && ba[i - 1] != ' ') continue;
        if (memcmp(ba + i, tok, (size_t)tlen) != 0) continue;
        v = i + (size_t)tlen;
        end = v;
        while (end < ba_len && ba[end] != ' ' && ba[end] != '\0')
            end++;
        found++;
        if (found > 1u) return -1;   /* duplicate key: refuse */
        fvoff = v;
        fvlen = end - v;
    }
    if (found != 1u) return -1;
    *voff = fvoff;
    *vlen = fvlen;
    return 0;
}

/* old value gate: exactly 80 chars in ASCII 33..126 (no space/NUL); the
 * state prefix must match the slot so an unrelated value is never
 * overwritten. Slot 2..5: pending or r25:. Slot 6: pending, r32: or r26:. */
static inline int m1_slot_ok(unsigned slot, const char *v, size_t vlen)
{
    size_t k;
    if (vlen != (size_t)M1_VAL_LEN) return -1;
    for (k = 0; k < vlen; k++) {
        unsigned char c = (unsigned char)v[k];
        if (c < 33u || c > 126u) return -1;      /* printable, no space */
    }
    if (memcmp(v, "pending", 7) == 0) {          /* 7 chars + 73 '_' */
        for (k = 7; k < vlen; k++) if (v[k] != '_') return -1;
        return 0;
    }
    if (slot >= 2u && slot <= 5u) {
        if (memcmp(v, "r25:", 4) == 0) {
            return 0;
        }
    } else if (slot == 6u) {
        if (memcmp(v, "r32:", 4) == 0 || memcmp(v, "r26:", 4) == 0) {
            return 0;
        }
    }
    return -1;
}

/* prepared per-slot edit */
struct m1_edit {
    size_t voff;
    size_t vlen;
    char   val[M1_VAL_LEN];
};

/*
 * diag_m1_summary - prepare + validate all five reports, then write.
 * runtime: FDT whose /chosen/bootargs is rewritten in place.
 * final_dt / snapshot: read-only. capacity: final_dt buffer bound.
 * fb: required live framebuffer candidate (QEMU has a FB).
 * Returns 0 or negative M1_E_*. Every validation error is reported
 * before any write, so a validated run writes all five slots or none;
 * a partial write is possible only if the in-place API fails
 * unexpectedly after an earlier slot was already written.
 */
static inline int diag_m1_summary(void *runtime, const void *final_dt,
                                  size_t capacity, const void *snapshot,
                                  size_t snapshot_capacity,
                                  const kshim_framebuffer_config_t *fb)
{
    char tmp[128];
    char rep[5][M1_VAL_LEN];
    struct m1_edit ed[5];
    const char *ba;
    int blen = 0, coff, s;
    uint32_t fsz, dh, mh;

    if (!runtime || !final_dt || !snapshot) return M1_E_ARG;

    /* --- structural validation of the two read-only inputs --- */
    if (dt_validate(final_dt, capacity) != 0)                       return M1_E_VALIDATE;
    if ((size_t)fdt_totalsize(final_dt) > capacity)       return M1_E_CAPACITY;
    if (dt_validate(snapshot, snapshot_capacity) != 0)                       return M1_E_VALIDATE;
    if ((size_t)fdt_totalsize(snapshot) > snapshot_capacity) return M1_E_CAPACITY;

    /* --- slot2: mlog identity + packed fingerprints --- */
    fsz = (uint32_t)fdt_totalsize(final_dt);
    dh  = m1_fnv1a32(final_dt, (size_t)fdt_totalsize(final_dt));
    mh  = m1_fnv1a32(snapshot, (size_t)fdt_totalsize(snapshot));
    snprintf(tmp, sizeof(tmp), "m1l:id=mlog1:sz=%u:dh=%08x:mh=%08x", fsz, dh, mh);
    if (m1_pad80(rep[0], tmp) != 0) return M1_E_SLOT;

    /* --- slot3: /memory ram banks (2/2 cells, 16-byte tuples) --- */
    {
        int noff, rlen = 0, n, t;
        const uint8_t *reg;
        uint8_t ser[64 * 16];
        size_t sl = 0;
        unsigned ram = 0;
        uint64_t bytes = 0;
        uint32_t rh;

        noff = fdt_path_offset(final_dt, "/memory");
        if (noff < 0) return M1_E_NO_MEMORY;
        if (fdt_address_cells(final_dt, 0) != 2) return M1_E_CELLS;
        if (fdt_size_cells(final_dt, 0) != 2)    return M1_E_CELLS;
        reg = (const uint8_t *)fdt_getprop(final_dt, noff, "reg", &rlen);
        if (!reg || rlen <= 0 || (rlen % 16) != 0) return M1_E_REG;

        n = rlen / 16;
        for (t = 0; t < n; t++) {
            const uint8_t *p = reg + (size_t)t * 16u;
            uint64_t base = fdt64_ld((const fdt64_t *)p);
            uint64_t size = fdt64_ld((const fdt64_t *)(p + 8));
            if (size == 0u) continue;                  /* skip zero-size only */
            if (base + size < base) return M1_E_OVF;   /* bank end overflow */
            if (bytes + size < bytes) return M1_E_OVF; /* total overflow */
            bytes += size;
            ram++;
            if (ram > 64u) return M1_E_TOOMANY;        /* capacity limit */
            m1_le64(ser + sl, base); sl += 8;
            m1_le64(ser + sl, size); sl += 8;
        }
        if (ram == 0u) return M1_E_NO_MEMORY;          /* need a real bank */
        rh = m1_fnv1a32(ser, sl);
        snprintf(tmp, sizeof(tmp), "m1l:ram=%u:bytes=%llx:rh=%08x",
                 ram, (unsigned long long)bytes, rh);
        if (m1_pad80(rep[1], tmp) != 0) return M1_E_SLOT;
    }

    /* --- slot4: reservations + headers count --- */
    {
        struct dt_range ranges[128];
        bool nomap[128];
        uint8_t ser[128 * 17];
        size_t count = 0;
        unsigned nm = 0;
        size_t i;
        size_t sl = 0;
        int rc, hdr;
        uint32_t hh;

        rc = dt_reservations(final_dt, ranges, nomap, 128u, &count);
        if (rc != 0) return M1_E_RSV;   /* rc0 required; includes capacity exceed */
        for (i = 0; i < count; i++) if (nomap[i]) nm++;
        hdr = fdt_num_mem_rsv(final_dt);
        if (hdr < 0) return M1_E_RSV;
        for (i = 0; i < count; i++) {   /* LE base+size+nomap per tuple */
            m1_le64(ser + sl, (uint64_t)ranges[i].base); sl += 8;
            m1_le64(ser + sl, (uint64_t)ranges[i].size); sl += 8;
            ser[sl++] = nomap[i] ? 1u : 0u;
        }
        hh = m1_fnv1a32(ser, sl);
        snprintf(tmp, sizeof(tmp), "m1l:rsv=%u:nm=%u:hr=%u:hh=%08x",
                 (unsigned)count, nm, (unsigned)hdr, hh);
        if (m1_pad80(rep[2], tmp) != 0) return M1_E_SLOT;
    }

    /* --- slot5: live framebuffer (no MMIO, 64-bit multiply) --- */
    {
        uint64_t fbb, vscr, bk;
        if (!fb) return M1_E_NOFB;
        fbb  = (uint64_t)fb->render_address;
        vscr = (uint64_t)fb->stride * (uint64_t)fb->height;
        bk   = (uint64_t)fb->buffer_size;
        snprintf(tmp, sizeof(tmp),
                 "m1l:fb=%llx:vs=%llx:bk=%llx:w=%u:h=%u:st=%u",
                 (unsigned long long)fbb, (unsigned long long)vscr,
                 (unsigned long long)bk, (unsigned)fb->width,
                 (unsigned)fb->height, (unsigned)fb->stride);
        if (m1_pad80(rep[3], tmp) != 0) return M1_E_SLOT;
    }

    /* --- slot6: structural fingerprint proof (missing => 0 unknown) --- */
    {
        int p, rc;
        uint32_t g, t, gp, ur, pr;
        p = m1_psci(final_dt);
        if (p < 0) return p;   /* non-NOTFOUND libfdt error: propagate */
        rc = m1_prop_hash(final_dt, m1_node_by_compat(final_dt, "arm,gic-v3"), "reg", &g);
        if (rc != 0) return rc;
        rc = m1_prop_hash(final_dt, m1_node_by_compat(final_dt, "arm,armv8-timer"), "interrupts", &t);
        if (rc != 0) return rc;
        rc = m1_prop_hash(final_dt, m1_node_by_compat(final_dt, "qcom,kaanapali-tlmm"), "gpio-reserved-ranges", &gp);
        if (rc != 0) return rc;
        rc = m1_prop_hash(final_dt, m1_node_by_compat(final_dt, "qcom,kaanapali-dwc3"), "reg", &ur);
        if (rc != 0) return rc;
        rc = m1_prop_hash(final_dt, m1_node_by_compat(final_dt, "nxp,ptn3222"), "reg", &pr);
        if (rc != 0) return rc;
        snprintf(tmp, sizeof(tmp),
                 "m1l:p=%d:g=%08x:t=%08x:gp=%08x:ur=%08x:pr=%08x",
                 p, g, t, gp, ur, pr);
        if (m1_pad80(rep[4], tmp) != 0) return M1_E_SLOT;
    }

    /* --- locate + validate ALL five writable slots before any write --- */
    coff = fdt_path_offset(runtime, "/chosen");
    if (coff < 0) return M1_E_CHOSEN;
    ba = (const char *)fdt_getprop(runtime, coff, "bootargs", &blen);
    if (!ba || blen <= 0) return M1_E_BOOTARGS;   /* no unbounded strlen */
    {
        int i, nul = 0;
        for (i = 0; i < blen; i++) {
            unsigned char c = (unsigned char)ba[i];
            if (c == 0u) nul++;
            else if (c < 0x20u || c > 0x7eu) return M1_E_BOOTARGS;
        }
        if (nul != 1 || ba[blen - 1] != '\0') return M1_E_BOOTARGS;
    }
    for (s = 0; s < 5; s++) {
        memcpy(ed[s].val, rep[s], M1_VAL_LEN);
        if (m1_find_slot(ba, (size_t)blen, (unsigned)(2 + s),
                         &ed[s].voff, &ed[s].vlen) != 0)
            return M1_E_SLOT;   /* unique token not found */
        if (m1_slot_ok((unsigned)(2 + s), ba + ed[s].voff, ed[s].vlen) != 0)
            return M1_E_SLOT;   /* unrelated / bad old value */
    }

    /* --- inplace writes: offsets already validated; only an unexpected
     * --- in-place API failure can leave a partial write here --- */
    for (s = 0; s < 5; s++) {
        int rc = fdt_setprop_inplace_namelen_partial(
            runtime, coff, "bootargs",
            (int)(sizeof("bootargs") - 1u), (int)ed[s].voff,
            ed[s].val, (int)M1_VAL_LEN);
        if (rc != 0) return M1_E_WRITE;
    }
    return M1_OK;
}

#endif /* DIAG_M1_SUMMARY_H */
