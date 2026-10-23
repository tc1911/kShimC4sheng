/* SPDX-License-Identifier: MIT
 * Last-boot record in the PMIC SDAM scratch area. The reboot driver already
 * owns the SCM IO accessors; this layer only defines a small checksummed,
 * double-buffered record.
 */
#if !defined(KSHIM_HOST_TEST)
#include <config.h>
#endif
#include <qcom_nv.h>
#include <qcom_reboot.h>
#include <string.h>

#if defined(CONFIG_KSHIM_BOOT_LAST) && CONFIG_KSHIM_BOOT_LAST

#define KSHIM_NV_OFFSET 0xE0U
#define KSHIM_NV_SLOT_SIZE 16U
#define KSHIM_NV_MAGIC UINT32_C(0x5445534B) /* "KSET" in little-endian bytes */
#define KSHIM_NV_VERSION 1U

struct nv_slot {
    uint32_t magic;
    uint8_t version;
    uint8_t index;
    uint16_t flags;
    uint32_t fingerprint;
    uint32_t crc;
} __attribute__((packed));

static uint32_t nv_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int slot_valid(const struct nv_slot *slot)
{
    if (slot->magic != KSHIM_NV_MAGIC || slot->version != KSHIM_NV_VERSION)
        return 0;
    return nv_crc32((const uint8_t *)slot, 12) == slot->crc;
}

int kshim_nv_load(const void *fdt, struct kshim_nv_selection *out)
{
    uint8_t raw[KSHIM_NV_SLOT_SIZE * 2];
    if (!out || kshim_qcom_nv_read(fdt, KSHIM_NV_OFFSET, raw, sizeof(raw)))
        return -1;
    /* Prefer the second slot: it is written last, so it is the newest. */
    for (int i = 1; i >= 0; --i) {
        struct nv_slot slot;
        memcpy(&slot, raw + (uint32_t)i * KSHIM_NV_SLOT_SIZE, sizeof(slot));
        if (slot_valid(&slot)) {
            out->fingerprint = slot.fingerprint;
            out->index = slot.index;
            return 0;
        }
    }
    return -1;
}

int kshim_nv_store(const void *fdt, uint8_t index, uint32_t fingerprint)
{
    struct nv_slot slot = {
        .magic = KSHIM_NV_MAGIC,
        .version = KSHIM_NV_VERSION,
        .index = index,
        .flags = 0,
        .fingerprint = fingerprint,
        .crc = 0,
    };
    slot.crc = nv_crc32((const uint8_t *)&slot, 12);
    uint8_t probe[KSHIM_NV_SLOT_SIZE * 2];
    /* Probe with a read first: if SCM cannot reach this SDAM range, do not
     * attempt a write at all. */
    if (kshim_qcom_nv_read(fdt, KSHIM_NV_OFFSET, probe, sizeof(probe)))
        return -1;
    uint8_t raw[KSHIM_NV_SLOT_SIZE * 2];
    memcpy(raw, &slot, sizeof(slot));
    memcpy(raw + KSHIM_NV_SLOT_SIZE, &slot, sizeof(slot));
    if (kshim_qcom_nv_write(fdt, KSHIM_NV_OFFSET, raw, sizeof(raw)))
        return -1;
    /* Verify the newest slot survived the SCM transaction. */
    struct nv_slot check;
    if (kshim_qcom_nv_read(fdt, KSHIM_NV_OFFSET + KSHIM_NV_SLOT_SIZE,
                           &check, sizeof(check)) ||
        memcmp(&check, &slot, sizeof(slot)))
        return -1;
    return 0;
}

#else

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
