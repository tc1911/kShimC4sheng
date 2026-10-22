#include "qcom_dt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE      0x9800000u
#define REG_SIZE  0x1c8800u

static uint32_t regs[REG_SIZE / 4];

static uintptr_t unstable_off;
static unsigned unstable_reads;

static unsigned observed_count;
static uint32_t observed_values[4096], observed_last_offset;

static uint32_t read32(void *ctx, uintptr_t addr)
{
	(void)ctx;
	assert(addr >= BASE);
	assert((addr & 3u) == 0);
	uintptr_t off = addr - BASE;
	assert(off + 4 <= REG_SIZE);

	if (off >= 0x20000 && off < 0x26000) {
		uintptr_t blk = off & ~0xfffu;
		uintptr_t r = off - blk;
		assert(blk == 0x20000);
		assert(r == 0xf4 || r == 0x12c || r == 0x130);
	} else {
		int ok = 0;
		if (off == 0x2c010 || off == 0x35010 || off == 0xbc010 || off == 0xc5010) ok = 1;
		if (off >= 0x2d000 && off <= 0x2d077) ok = 1;
		if (off >= 0x36000 && off <= 0x36077) ok = 1;
		if (off >= 0xbd000 && off <= 0xbd077) ok = 1;
		if (off >= 0xc6000 && off <= 0xc6077) ok = 1;
		if (off >= 0x104000 && off <= 0x13c3ff) ok = 1;
		assert(ok);
	}
	uint32_t val;
	if (off == unstable_off && ++unstable_reads == 2)
		val = regs[off / 4] ^ 4u;
	else
		val = regs[off / 4];
	assert(observed_count < 4096);
	observed_values[observed_count] = val;
	observed_last_offset = off;
	observed_count++;
	return val;
}

static void write32(void *ctx, uintptr_t addr, uint32_t val)
{
	(void)ctx; (void)addr; (void)val;
	assert(0);
}

static int checked_splash(const void *f, const struct CrIo *io,
			  kshim_framebuffer_config_t *fb,
			  kshim_mmu_region_t *mmio)
{
	observed_count = 0;
	observed_last_offset = 0;
	int rc = kshim_dt_splash(f, io, fb, mmio);
	const struct kshim_splash_trace *t = kshim_splash_trace_get();
	assert(t);
	assert(t->count == observed_count);
	assert(t->last_offset == observed_last_offset);
	for (int i = 0; i < 5; i++) {
		int idx = (int)observed_count - 5 + i;
		uint32_t expected = idx < 0 ? 0 : observed_values[idx];
		assert(t->values[(observed_count + i) % 5] == expected);
	}
	for (int i = 0; i < 15; i++) {
		uint32_t expected = (i < (int)observed_count && i < 15)
			? observed_values[i] : 0;
		assert(t->first[i] == expected);
	}
	return rc;
}

int main(void)
{
	FILE *f = fopen(FIXTURE_PATH, "rb");
	assert(f);
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	assert(sz > 0);
	void *dtb = malloc((size_t)sz);
	assert(dtb);
	assert(fread(dtb, 1, (size_t)sz, f) == (size_t)sz);
	fclose(f);

	struct CrIo io = { .read32 = read32, .write32 = write32 };

	const uintptr_t lm_off[8] = {
		0x104000, 0x10c000, 0x114000, 0x11c000,
		0x124000, 0x12c000, 0x134000, 0x13c000
	};
	const uintptr_t stage_off[11] = {
		0x40, 0x70, 0xa0, 0xd0, 0x100, 0x130,
		0x160, 0x190, 0x1c0, 0x1f0, 0x220
	};
	const uintptr_t dma4 = 0xbd000;
	const uintptr_t dma5 = 0xc6000;

	for (int i = 0; i < 8; i++)
		regs[(lm_off[i] + 0x14) / 4] = 0xc0c0;

	for (int i = 0; i < 8; i++)
		for (int s = 0; s < 11; s++)
			regs[(lm_off[i] + stage_off[s] + 4) / 4] = 0xc0c0;

	regs[(0x20000 + 0xf4) / 4]  = 6;
	regs[(0x20000 + 0x12c) / 4] = 0x30;
	regs[(0x20000 + 0x130) / 4] = 3;

	regs[(lm_off[0] + stage_off[0] + 4) / 4] = 0xc004;
	regs[(lm_off[1] + stage_off[0] + 4) / 4] = 0xc005;

	for (int i = 0; i < 8; i++)
		regs[(lm_off[i] + 0x04) / 4] = (3040u << 16) | 952u;

	regs[(lm_off[1] + 0x00) / 4] |= 0x80000000u;

	regs[(dma4 + 0x14) / 4] = (3040u << 16) | 952u;
	regs[(dma4 + 0x1c) / 4] = (3040u << 16) | 952u;
	regs[(dma4 + 0x18) / 4] = 0;
	regs[(dma4 + 0x4c) / 4] = 0xfc800000u;
	regs[(dma4 + 0x5c) / 4] = 7616;
	regs[(dma4 + 0x00) / 4] = 0x237ff;
	regs[(dma4 + 0x04) / 4] = 0x03020001;
	regs[(dma4 + 0x64) / 4] = 0xfc800000u;

	regs[(dma5 + 0x14) / 4] = (3040u << 16) | 952u;
	regs[(dma5 + 0x1c) / 4] = (3040u << 16) | 952u;
	regs[(dma5 + 0x18) / 4] = 952;
	regs[(dma5 + 0x4c) / 4] = 0xfc800000u;
	regs[(dma5 + 0x5c) / 4] = 7616;
	regs[(dma5 + 0x00) / 4] = 0x237ff;
	regs[(dma5 + 0x04) / 4] = 0x03020001;
	regs[(dma5 + 0x64) / 4] = 0xfc800000u;

	kshim_mmu_region_t mmio = {0};
	kshim_framebuffer_config_t fb;
	memset(&fb, 0, sizeof(fb));

	int rc = checked_splash(dtb, &io, &fb, &mmio);
	assert(rc == 0);
	assert(fb.width == 1904);
	assert(fb.height == 3040);
	assert(fb.stride == 7616);
	assert(fb.bpp == 32);
	assert(fb.render_address == 0xfc800000u);
	assert(fb.buffer_size == 23152640u);
	assert(fb.format == KSHIM_FB_FORMAT_ARGB8888);

	regs[(dma4 + 0x08) / 4] = 1;
	rc = checked_splash(dtb, &io, &fb, &mmio);
	assert(rc != 0);
	regs[(dma4 + 0x08) / 4] = 0;

	regs[(lm_off[0] + stage_off[1] + 4) / 4] = 0xc005;
	rc = checked_splash(dtb, &io, &fb, &mmio);
	assert(rc != 0);
	regs[(lm_off[0] + stage_off[1] + 4) / 4] = 0xc0c0;

	/* Succeeds again once the negative cases have been reset. */
	rc = checked_splash(dtb, &io, &fb, &mmio);
	assert(rc == 0);

	/* CTL0 pipe-active without the DMA5 bit must fail. */
	regs[(0x20000 + 0x12c) / 4] = 0x10;
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	regs[(0x20000 + 0x12c) / 4] = 0x30;

	/* Multirect rect1 is unsupported. */
	regs[(lm_off[0] + stage_off[0] + 4) / 4] = 0xc014;
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	regs[(lm_off[0] + stage_off[0] + 4) / 4] = 0xc004;

	/* DMA4 address outside the reserved buffer must fail. */
	regs[(dma4 + 0x4c) / 4] = 0xfc7ff000u;
	regs[(dma4 + 0x64) / 4] = 0xfc7ff000u;
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	regs[(dma4 + 0x4c) / 4] = 0xfc800000u;
	regs[(dma4 + 0x64) / 4] = 0xfc800000u;

	/* DMA4 format with bit 30 set must fail. */
	regs[(dma4 + 0x00) / 4] |= 0x40000000u;
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	regs[(dma4 + 0x00) / 4] = 0x237ff;

	/* VIG routing test */
	memcpy(&regs[0x2d000 / 4], &regs[dma4 / 4], 0x78);
	memcpy(&regs[0x36000 / 4], &regs[dma5 / 4], 0x78);
	regs[(0x20000 + 0x12c) / 4] = 0x30000;
	regs[(lm_off[0] + stage_off[0] + 4) / 4] = 0xc040;
	regs[(lm_off[1] + stage_off[0] + 4) / 4] = 0xc041;
	assert(checked_splash(dtb, &io, &fb, &mmio) == 0);
	assert(fb.width == 1904);

	/* R18 source-split: both LMs distribute one scanout across the two VIG pipes. */
	regs[(lm_off[0] + stage_off[0] + 4) / 4] = 0xc0c0;
	regs[(lm_off[1] + stage_off[0] + 4) / 4] = 0xc0c0;
	regs[(lm_off[0] + stage_off[5] + 4) / 4] = 0x4140;
	regs[(lm_off[1] + stage_off[5] + 4) / 4] = 0x4140;
	regs[(0x36000 + 0x20) / 4] = 952;
	assert(checked_splash(dtb, &io, &fb, &mmio) == 0);
	assert(fb.render_address == 0xfc800000u);
	assert(fb.width == 1904);
	assert(fb.height == 3040);
	assert(fb.stride == 7616);
	assert(fb.buffer_size == 23152640u);
	assert(fb.format == KSHIM_FB_FORMAT_ARGB8888);
	regs[(lm_off[0] + stage_off[5] + 4) / 4] = 0xc0c0;
	regs[(lm_off[1] + stage_off[5] + 4) / 4] = 0xc0c0;
	regs[(0x36000 + 0x20) / 4] = 0;

	regs[(0x20000 + 0x12c) / 4] = 0x30;
	regs[(lm_off[0] + stage_off[0] + 4) / 4] = 0xc004;
	regs[(lm_off[1] + stage_off[0] + 4) / 4] = 0xc005;

	/* Move the active routes to hardware LM6/LM7. */
	memcpy(&regs[lm_off[6] / 4], &regs[lm_off[0] / 4], 0x400);
	memcpy(&regs[lm_off[7] / 4], &regs[lm_off[1] / 4], 0x400);
	regs[(0x20000 + 0x130) / 4] = 0xc0;

	assert(checked_splash(dtb, &io, &fb, &mmio) == 0);
	int sde = fdt_node_offset_by_compatible(dtb, -1, "qcom,sde-kms");
	assert(sde >= 0);

	int prop_len = 0;
	const fdt32_t *mixer_prop =
		fdt_getprop(dtb, sde, "qcom,sde-mixer-off", &prop_len);
	assert(mixer_prop && prop_len == 12 * (int)sizeof(fdt32_t));
	fdt32_t mixer_copy[12];
	memcpy(mixer_copy, mixer_prop, sizeof(mixer_copy));

	const fdt32_t *stage_prop =
		fdt_getprop(dtb, sde, "qcom,sde-mixer-blend-op-off", &prop_len);
	assert(stage_prop && prop_len == 11 * (int)sizeof(fdt32_t));
	fdt32_t stage_copy[11];
	memcpy(stage_copy, stage_prop, sizeof(stage_copy));

	/* Zeroing the first mixer slot must still succeed (IDs preserved). */
	fdt32_t mixer_tmp[12];
	memcpy(mixer_tmp, mixer_copy, sizeof(mixer_tmp));
	mixer_tmp[0] = cpu_to_fdt32(0);
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-off",
				   mixer_tmp, sizeof(mixer_tmp)) == 0);
	assert(checked_splash(dtb, &io, &fb, &mmio) == 0);
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-off",
				   mixer_copy, sizeof(mixer_copy)) == 0);
	regs[(0x20000 + 0x130) / 4] = 3;

	/* A bogus mixer slot while LM0 is active must fail. */
	memcpy(mixer_tmp, mixer_copy, sizeof(mixer_tmp));
	mixer_tmp[0] = cpu_to_fdt32(0xf0f);
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-off",
				   mixer_tmp, sizeof(mixer_tmp)) == 0);
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-off",
				   mixer_copy, sizeof(mixer_copy)) == 0);

	/* Duplicate stage offset must fail. */
	fdt32_t stage_tmp[11];
	memcpy(stage_tmp, stage_copy, sizeof(stage_tmp));
	stage_tmp[1] = stage_tmp[0];
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-blend-op-off",
				   stage_tmp, sizeof(stage_tmp)) == 0);
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-blend-op-off",
				   stage_copy, sizeof(stage_copy)) == 0);

	/* Invalid stage offset must fail. */
	memcpy(stage_tmp, stage_copy, sizeof(stage_tmp));
	stage_tmp[0] = cpu_to_fdt32(0xfffffffc);
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-blend-op-off",
				   stage_tmp, sizeof(stage_tmp)) == 0);
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	assert(fdt_setprop_inplace(dtb, sde, "qcom,sde-mixer-blend-op-off",
				   stage_copy, sizeof(stage_copy)) == 0);

	/* An unstable pipe-address read must fail, then recover. */
	unstable_off = dma4 + 0x4c;
	unstable_reads = 0;
	assert(checked_splash(dtb, &io, &fb, &mmio) != 0);
	unstable_off = 0;
	assert(checked_splash(dtb, &io, &fb, &mmio) == 0);

	puts("Canoe dual scanout, sparse LM IDs, bounds and unstable-state checks passed");
	free(dtb);
	return 0;
}
