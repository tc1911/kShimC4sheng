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

#if defined(CONFIG_KSHIM_BOOT_LAST) && CONFIG_KSHIM_BOOT_LAST

#define NV_OFFSET       0x50U
#define NV_BACKUP       0x60U
#define NV_SLOT_SIZE    16U
#define NV_MAGIC        0x5445534BU  /* "KSET" */
#define NV_VERSION      1U

struct nv_record {
    uint32_t magic;
    uint8_t  version;
    uint8_t  index;
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
    if (rec->crc != nv_crc32((const uint8_t *)rec, 12)) return -1;
    return 0;
}

static int nv_write_at(const void *fdt, uint16_t base, uint16_t off,
                       struct nv_record *rec)
{
    struct kshim_qcom_reboot_config cfg;
    struct nv_record check;
    rec->magic = NV_MAGIC;
    rec->version = NV_VERSION;
    rec->flags = 0;
    rec->crc = nv_crc32((const uint8_t *)rec, 12);
    if (kshim_qcom_nv_config(fdt, base, &cfg)) return -1;
    if (kshim_qcom_nv_write_cfg(&cfg, off, rec, sizeof(*rec))) return -1;
    if (kshim_qcom_nv_read_cfg(&cfg, off, &check, sizeof(check))) return -1;
    return memcmp(rec, &check, sizeof(check)) == 0 ? 0 : -1;
}

int kshim_nv_last_status(uint8_t *index, int *slot)
{
    if (index) *index = NvLastIndex;
    if (slot) *slot = NvSlot;
    return NvLastStatus;
}

int kshim_nv_load(const void *fdt, struct kshim_nv_selection *out)
{
    static const uint16_t bases[] = {0x8400U, 0x8500U};
    struct nv_record rec;

    NvLastStatus = -1;
    NvLastIndex = 0;
    NvSlot = -1;
    if (!out) return -1;
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        if (nv_read_at(fdt, bases[i], NV_OFFSET, &rec) == 0 ||
            nv_read_at(fdt, bases[i], NV_BACKUP, &rec) == 0) {
            out->index = rec.index;
            out->fingerprint = rec.fingerprint;
            NvLastStatus = 0;
            NvLastIndex = rec.index;
            NvSlot = (int)bases[i] + NV_OFFSET;
            return 0;
        }
    }
    NvLastStatus = -2;
    return -1;
}

int kshim_nv_store(const void *fdt, uint8_t index, uint32_t fingerprint)
{
    static const uint16_t bases[] = {0x8400U, 0x8500U};
    struct nv_record rec = {0};

    rec.index = index;
    rec.fingerprint = fingerprint;
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        if (nv_write_at(fdt, bases[i], NV_OFFSET, &rec) == 0 ||
            nv_write_at(fdt, bases[i], NV_BACKUP, &rec) == 0) {
            if (NvSlot < 0) NvSlot = (int)bases[i] + NV_OFFSET;
            NvLastStatus = 0;
            NvLastIndex = index;
        }
    }
    return NvLastStatus == 0 ? 0 : -1;
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

#endif
