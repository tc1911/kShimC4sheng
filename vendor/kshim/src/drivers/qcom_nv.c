/* SPDX-License-Identifier: MIT
 * Last-boot selection in a PMIC SDAM scratch slot.
 *
 * On sheng, SDAM 0x8400/0x8500 keep their contents across a reboot while
 * 0x7100 is cleared by ABL. The record lives at 0x50 with a backup at 0x60;
 * both are battery-backed and are not written by ABL or either OS.
 */
#if !defined(KSHIM_HOST_TEST)
#include <config.h>
#endif
#include <qcom_nv.h>
#include <qcom_reboot.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#if defined(CONFIG_KSHIM_BOOT_LAST) && CONFIG_KSHIM_BOOT_LAST

#define NV_OFFSET       0x50U
#define NV_BACKUP       0x80U
#define NV_MAGIC        0x5445534BU  /* "KSET" */
#define NV_VERSION      2U
#define NV_FLAG_SETTINGS 1U

struct nv_record {
    uint32_t magic;
    uint8_t  version;
    uint8_t  index;
    uint8_t  language;
    uint8_t  timeout_s;
    uint16_t flags;
    uint32_t fingerprint;
    uint32_t crc;
};

static int NvLastStatus = -1;
static int NvSlot = -1;
static uint8_t NvLastIndex;

static uint32_t nv_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static int nv_read_at(const void *fdt, uint16_t base, uint16_t off,
                      struct nv_record *rec)
{
    struct kshim_qcom_reboot_config cfg;
    if (kshim_qcom_nv_config(fdt, base, &cfg)) return -1;
    if (kshim_qcom_nv_read_cfg(&cfg, off, rec, sizeof(*rec))) return -1;
    if (rec->magic != NV_MAGIC || rec->version != NV_VERSION) return -1;
    if (rec->crc != nv_crc32((const uint8_t *)rec, offsetof(struct nv_record, crc)))
        return -1;
    return 0;
}

static int nv_read_any(const void *fdt, struct nv_record *rec)
{
    static const uint16_t bases[] = {0x8400U, 0x8500U};
    static const uint16_t slots[] = {NV_OFFSET, NV_BACKUP};
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i)
        for (size_t j = 0; j < sizeof(slots) / sizeof(slots[0]); ++j)
            if (nv_read_at(fdt, bases[i], slots[j], rec) == 0) {
                NvSlot = (int)bases[i] + slots[j];
                return 0;
            }
    return -1;
}

static int nv_write_at(const void *fdt, uint16_t base, uint16_t off,
                       struct nv_record *rec)
{
    struct kshim_qcom_reboot_config cfg;
    struct nv_record check;
    rec->magic = NV_MAGIC;
    rec->version = NV_VERSION;
    rec->crc = nv_crc32((const uint8_t *)rec, offsetof(struct nv_record, crc));
    if (kshim_qcom_nv_config(fdt, base, &cfg)) return -1;
    if (kshim_qcom_nv_write_cfg(&cfg, off, rec, sizeof(*rec))) return -1;
    if (kshim_qcom_nv_read_cfg(&cfg, off, &check, sizeof(check))) return -1;
    return memcmp(rec, &check, sizeof(check)) == 0 ? 0 : -1;
}

static int nv_write_any(const void *fdt, const struct nv_record *rec)
{
    static const uint16_t bases[] = {0x8400U, 0x8500U};
    int wrote = 0;
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        struct nv_record copy = *rec;
        if (nv_write_at(fdt, bases[i], NV_OFFSET, &copy) == 0) {
            NvSlot = (int)bases[i] + NV_OFFSET;
            wrote = 1;
        }
        copy = *rec;
        (void)nv_write_at(fdt, bases[i], NV_BACKUP, &copy);
    }
    return wrote ? 0 : -1;
}

int kshim_nv_last_status(uint8_t *index, int *slot)
{
    if (index) *index = NvLastIndex;
    if (slot) *slot = NvSlot;
    return NvLastStatus;
}

int kshim_nv_load(const void *fdt, struct kshim_nv_selection *out)
{
    struct nv_record rec;

    NvLastStatus = -1;
    NvLastIndex = 0;
    NvSlot = -1;
    if (!out) return -1;
    if (nv_read_any(fdt, &rec) == 0) {
        out->index = rec.index;
        out->fingerprint = rec.fingerprint;
        out->language = rec.language;
        out->timeout_s = rec.timeout_s;
        out->has_settings = (rec.flags & NV_FLAG_SETTINGS) != 0;
        NvLastStatus = 0;
        NvLastIndex = rec.index;
        return 0;
    }
    NvLastStatus = -2;
    return -1;
}

int kshim_nv_store(const void *fdt, uint8_t index, uint32_t fingerprint)
{
    struct nv_record rec = {0};

    (void)nv_read_any(fdt, &rec);
    rec.index = index;
    rec.fingerprint = fingerprint;
    if (nv_write_any(fdt, &rec) == 0) {
        NvLastStatus = 0;
        NvLastIndex = index;
        return 0;
    }
    return -1;
}

int kshim_nv_store_settings(const void *fdt, uint8_t language, uint8_t timeout_s)
{
    struct nv_record rec = {0};

    (void)nv_read_any(fdt, &rec);
    rec.language = language;
    rec.timeout_s = timeout_s;
    rec.flags |= NV_FLAG_SETTINGS;
    return nv_write_any(fdt, &rec);
}

#else

int kshim_nv_last_status(uint8_t *index, int *slot)
{
    if (index) *index = 0;
    if (slot) *slot = -1;
    return -1;
}

int kshim_nv_load(const void *fdt, struct kshim_nv_selection *out)
{
    (void)fdt; (void)out;
    return -1;
}

int kshim_nv_store(const void *fdt, uint8_t index, uint32_t fingerprint)
{
    (void)fdt; (void)index; (void)fingerprint;
    return -1;
}

int kshim_nv_store_settings(const void *fdt, uint8_t language, uint8_t timeout_s)
{
    (void)fdt; (void)language; (void)timeout_s;
    return -1;
}

#endif
