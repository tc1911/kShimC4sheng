/* SPDX-License-Identifier: MIT */
#pragma once
#include <dt.h>
#include <Library/spmi.h>
#include <Library/cr_resources.h>
#include <framebuffer.h>
#include <kshim_mmu.h>
#define KSHIM_SPMI_MAX 4
#define KSHIM_RESOURCE_MAX 96
struct kshim_spmi_resource { int node; SpmiDeviceContext controller; };
struct kshim_touch_resource {
    int node;
    uint16_t address;
    uint8_t generation;
    uint32_t width, height;
    bool flip_x, flip_y;
    char rail_names[2][9];
    struct CrQupResources qup;
};
struct kshim_resources {
    const void *fdt;
    struct kshim_spmi_resource spmi[KSHIM_SPMI_MAX];
    unsigned spmi_count;
    struct kshim_touch_resource touch;
    kshim_framebuffer_config_t framebuffer;
    /* Free display-reservation memory past the scanout frame (or the
     * Kconfig test window) for the UI's font and background textures. */
    struct dt_range ui_scratch;
    kshim_mmu_region_t mappings[KSHIM_RESOURCE_MAX];
    size_t mapping_count;
    int spmi_status, touch_status, splash_status;
};
int kshim_resources_init(const void *fdt, size_t available);
struct kshim_resources *kshim_resources_get(void);
int kshim_dt_spmi(const void *fdt, struct kshim_resources *resources);
int kshim_dt_spmi_identity(const void *fdt, int node, uint8_t *controller,
                           uint8_t *bus, uint8_t *sid);
int kshim_dt_touch(const void *fdt, struct kshim_touch_resource *touch);
int kshim_dt_gpio(const void *fdt, int provider, uint32_t pin,
                  uintptr_t *address, const char *function, uint32_t *mux);
int kshim_dt_splash_reservation(const void *fdt, struct dt_range *memory, bool *no_map);
int kshim_dt_splash(const void *fdt, const struct CrIo *io,
                    kshim_framebuffer_config_t *framebuffer,
                    kshim_mmu_region_t *mmio);
int kshim_resource_map(struct kshim_resources *r, uint64_t base, uint64_t size,
                       kshim_mmu_memory_type_t type, uint32_t permissions);

int kshim_dt_spmi_reg(const void *fdt, int node, uint16_t *offset);

unsigned kshim_dt_soc(const void *fdt, int provider);

struct kshim_splash_trace {
	uint32_t count;
	uint32_t last_offset;
	uint32_t values[5];
	uint32_t first[15];
	/* DT failure stage of the last kshim_dt_splash attempt (0 = none/legacy),
	 * the root "compatible" string-list count (negative = malformed list) and
	 * whether an exact "qcom,sunp" list entry was present. Diagnostic only. */
	unsigned dt_stage;
	int root_compatible_count;
	bool root_has_sunp;
};

const struct kshim_splash_trace *kshim_splash_trace_get(void);

int kshim_splash_trace_report(const struct kshim_splash_trace *t, int status, char *out, size_t capacity);

struct CrSplashResources;

struct kshim_splash_snapshot {
	uint32_t words[68];
	bool valid;
	bool stable;
};

const struct kshim_splash_snapshot *kshim_splash_snapshot_get(void);

void kshim_splash_snapshot_capture(const struct CrSplashResources *r, const struct CrIo *io);

int kshim_splash_snapshot_report(const struct kshim_splash_snapshot *s, unsigned slot, char *out, size_t capacity);
