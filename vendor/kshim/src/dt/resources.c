/* SPDX-License-Identifier: MIT */
#include <qcom_dt.h>
#include <config.h>
#include <qcom_keys_fdt.h>
#include <qcom_key_platform.h>
#include <string.h>
static struct kshim_resources resources;
static bool initialized;
struct kshim_resources *kshim_resources_get(void) { return &resources; }
int kshim_resource_map(struct kshim_resources *r, uint64_t base, uint64_t size,
                       kshim_mmu_memory_type_t type, uint32_t permissions)
{
    if (!base || !size || base > UINT64_MAX - size) return -1;
    /* Coalesce device pages before page-table construction. A GPIO input and
     * a pinmux register on the same page must have one memory type/permission. */
    if (type == KSHIM_MMU_DEVICE) {
        if (base + size > UINT64_MAX - 4095) return -1;
        uint64_t end = (base + size + 4095) & ~UINT64_C(4095);
        base &= ~UINT64_C(4095); size = end - base;
    }
    for (size_t i = 0; i < r->mapping_count;) {
        kshim_mmu_region_t *m = &r->mappings[i];
        if (m->type == type && type == KSHIM_MMU_DEVICE &&
            base < m->base + m->size && m->base < base + size) {
            uint64_t end = base + size > m->base + m->size ? base + size : m->base + m->size;
            if (m->base < base) base = m->base;
            size = end - base; permissions |= m->permissions;
            r->mappings[i] = r->mappings[--r->mapping_count];
            i = 0;
            continue;
        }
        if (m->type == type && m->permissions == permissions &&
            dt_contains((struct dt_range){m->base, m->size}, base, size)) return 0;
        ++i;
    }
    if (r->mapping_count == KSHIM_RESOURCE_MAX) return -1;
    r->mappings[r->mapping_count++] = (kshim_mmu_region_t){base, size, type, permissions};
    return 0;
}
#if CONFIG_KSHIM_TOUCH
static bool ram_contains(const void *f, uint64_t base, uint64_t size)
{
    for (int n = fdt_next_node(f, -1, NULL); n >= 0; n = fdt_next_node(f, n, NULL)) {
        const char *type = dt_string(f, n, "device_type");
        if (!type || strcmp(type, "memory") || !dt_available(f, n)) continue;
        struct dt_range r;
        for (unsigned i = 0; dt_reg(f, n, i, &r) == 0; ++i)
            if (dt_contains(r, base, size)) return true;
    }
    return false;
}
static int touch_maps(struct kshim_resources *r, const struct CrIo *io)
{
    struct CrQupResources *q = &r->touch.qup;
    const struct dt_range mmio[] = {{q->geni, q->geni_size}, {q->vote, 4}, {q->rcg, 0x3c},
        {q->ahb_halt[0], 4}, {q->ahb_halt[1], 4}, {q->serial_halt, 4},
        {q->bus_pins[0], 16}, {q->bus_pins[1], 16}, {q->irq_pin, 16},
        {q->reset_pin, 16}, {q->rsc_base, q->rsc_size}};
    for (unsigned i = 0; i < sizeof(mmio) / sizeof(mmio[0]); ++i)
        if (kshim_resource_map(r, mmio[i].base, mmio[i].size, KSHIM_MMU_DEVICE, KSHIM_MMU_READ | KSHIM_MMU_WRITE)) return -1;
    if (q->gate_pin && kshim_resource_map(r, q->gate_pin, 16, KSHIM_MMU_DEVICE, KSHIM_MMU_READ | KSHIM_MMU_WRITE)) return -1;
    if (q->cmd_db_dictionary) {
        if (!io || !io->read32 || q->cmd_db_dictionary > UINTPTR_MAX - 8) return -1;
        uint32_t base = io->read32(io->context, q->cmd_db_dictionary);
        uint32_t size = io->read32(io->context, q->cmd_db_dictionary + 4);
        if ((base & 3) || size < 144 || size > 0x20000 || !ram_contains(r->fdt, base, size)) return -1;
        q->cmd_db_base = base; q->cmd_db_size = size;
        /* Snapshot the descriptor once before the MMU; the shared library
         * now queries this bounded read-only region directly. */
        q->cmd_db_dictionary = 0;
    }
    if (!ram_contains(r->fdt, q->cmd_db_base, q->cmd_db_size)) return -1;
    return kshim_resource_map(r, q->cmd_db_base, q->cmd_db_size, KSHIM_MMU_NORMAL_NC, KSHIM_MMU_READ);
}
#endif
#if CONFIG_KSHIM_FRAMEBUFFER
/* The display reservation past the scanout frame is owned by the display and
 * unused while kShimC runs; the UI keeps its inflated font and background
 * textures there instead of in the runtime slot. */
static void ui_scratch_from_splash(const void *f, struct kshim_resources *r)
{
    struct dt_range reservation;
    uint64_t start = r->framebuffer.render_address + r->framebuffer.buffer_size;
    if (kshim_dt_splash_reservation(f, &reservation, NULL) ||
        start < r->framebuffer.render_address || start > UINT64_MAX - 4095) return;
    start = (start + 4095) & ~UINT64_C(4095);
    uint64_t end = reservation.base + reservation.size;
    if (start >= reservation.base && start < end)
        r->ui_scratch = (struct dt_range){start, end - start};
}
/* Write-back unless a no-map reservation covers it: those may only be
 * mapped non-cacheable. */
static kshim_mmu_memory_type_t ui_scratch_type(const void *f, struct dt_range scratch)
{
    struct dt_range ranges[128]; bool no_map[128]; size_t count;
    if (dt_reservations(f, ranges, no_map, 128, &count)) return KSHIM_MMU_NORMAL_NC;
    for (size_t i = 0; i < count; ++i)
        if (no_map[i] && scratch.base < ranges[i].base + ranges[i].size &&
            ranges[i].base < scratch.base + scratch.size) return KSHIM_MMU_NORMAL_NC;
    return KSHIM_MMU_NORMAL_WB;
}
#endif
static int resources_init_validated(const void *f)
{
    initialized = false;
    memset(&resources, 0, sizeof(resources));
    resources.fdt = f;
    resources.spmi_status = resources.touch_status = resources.splash_status = -1;
#if CONFIG_QCOM_SPMI
    resources.spmi_status = kshim_dt_spmi(f, &resources);
    SpmiDeviceContext *contexts[KSHIM_SPMI_MAX];
    unsigned count = resources.spmi_status ? 0 : resources.spmi_count;
    for (unsigned i = 0; i < count; ++i) {
        contexts[i] = &resources.spmi[i].controller;
        for (unsigned j = 0; j < SPMI_MEMORY_REGION_TYPE_MAX; ++j) {
            SpmiMemoryRegion *m = &contexts[i]->Regions[j];
            if (m->Size && kshim_resource_map(&resources, m->BaseAddress, m->Size,
                    KSHIM_MMU_DEVICE, KSHIM_MMU_READ | KSHIM_MMU_WRITE)) return -1;
        }
    }
    kshim_qcom_spmi_bind(contexts, count);
#endif
#if CONFIG_QCOM_KEYS
    QcomKeyDescriptor keys[QCOM_KEYS_MAX]; size_t key_count;
    if (!QcomKeysBuildFromDeviceTree(f, keys, QCOM_KEYS_MAX, &key_count)) {
        for (size_t i = 0; i < key_count; ++i)
            if (!QCOM_KEY_IS_SPMI_TOKEN(keys[i].Base) &&
                kshim_resource_map(&resources, keys[i].Base + keys[i].RegisterOffset,
                    4, KSHIM_MMU_DEVICE, KSHIM_MMU_READ)) return -1;
    }
#endif
#if CONFIG_KSHIM_TOUCH
    resources.touch_status = kshim_dt_touch(f, &resources.touch);
    if (!resources.touch_status) {
        /* Failed optional probes must not leave partial mappings or upgraded
         * permissions behind after device-page coalescing. */
        kshim_mmu_region_t saved[KSHIM_RESOURCE_MAX];
        size_t count = resources.mapping_count;
        memcpy(saved, resources.mappings, count * sizeof(*saved));
        resources.touch_status = touch_maps(&resources, CrIoDefault());
        if (resources.touch_status) {
            memcpy(resources.mappings, saved, count * sizeof(*saved));
            resources.mapping_count = count;
        }
    }
#endif
#if CONFIG_KSHIM_FRAMEBUFFER
    kshim_mmu_region_t display;
    resources.splash_status = kshim_dt_splash(f, CrIoDefault(), &resources.framebuffer, &display);
    if (!resources.splash_status && kshim_resource_map(&resources, display.base, display.size,
            display.type, display.permissions)) return -1;
    if (!resources.splash_status) ui_scratch_from_splash(f, &resources);
#if CONFIG_KSHIM_UI_SCRATCH_SIZE
    if (!resources.ui_scratch.size)
        resources.ui_scratch = (struct dt_range){CONFIG_KSHIM_UI_SCRATCH_BASE,
                                                 CONFIG_KSHIM_UI_SCRATCH_SIZE};
#endif
    if (resources.ui_scratch.size && kshim_resource_map(&resources,
            resources.ui_scratch.base, resources.ui_scratch.size,
            ui_scratch_type(f, resources.ui_scratch), KSHIM_MMU_READ | KSHIM_MMU_WRITE))
        resources.ui_scratch = (struct dt_range){0};
#endif
    initialized = true;
    return 0;
}

int kshim_resources_init(const void *f, size_t available)
{
    if (dt_validate(f, available)) return -1;
    if (initialized && resources.fdt == f) return 0;
    dt_parent_index_begin(f);
    int rc = resources_init_validated(f);
    dt_parent_index_end();
    return rc;
}
