/* SPDX-License-Identifier: MIT */
/*
 * tests/tb322fc_splash_test.c
 *
 * Host regression tests for TB322FC / SM8750 "Sun" (DPU12) continuous splash
 * detection.
 *
 * Capability evidence:
 *   Sun  DPU12 = { lm_source_select = true, sspp_rec_separated = false }
 *   Canoe DPU13 = { lm_source_select = true, sspp_rec_separated = true }
 * Sun keeps the two capabilities independent: it is selected by an explicit
 * 0xc0000000 version or by an exact qcom,sunp root with an absent version
 * (fallback, restricted to the measured resource shape), while an explicit
 * 0xd0000000 on a non-Sun root keeps the Canoe capability.
 *
 * The device tree is synthesized in-process with libfdt (no fixture file) and
 * the MDP MMIO is modelled through CrIo read32/write32. Only the exact legacy
 * reads are permitted: Sun must use LM source-select routing and the old SSPP
 * snapshots. Any SSPP SOURCE_0 (pipe + 0x1000) or REC-only access fails, every
 * write fails, and out-of-range/unaligned reads fail.
 */
#include "qcom_dt.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MDP register window of the captured .096 TB322FC Sun resource. */
#define MDP_BASE   0x0ae00000u
#define MDP_SIZE   0x00093800u
#define MDP_WORDS  (MDP_SIZE / 4u)

/* cont_splash_region reservation. */
#define FB_BASE    0xfc800000u
#define FB_SIZE    0x02b00000u

#define BLOB_SIZE  65536

/* ---- captured Sun resource shape (all offsets hex) --------------------- */

static const uint32_t CTL_OFF[6] = {
	0x16000u, 0x17000u, 0x18000u, 0x19000u, 0x1a000u, 0x1b000u
};
static const uint32_t SSPP_OFF[10] = {
	0x5000u, 0x7000u, 0x9000u, 0xb000u, 0x25000u,
	0x27000u, 0x29000u, 0x2b000u, 0x2d000u, 0x2f000u
};
/* qcom,sde-mixer-off: eight real LMs then four dummy (0xf0f) slots. */
static const uint32_t MIXER_RAW[12] = {
	0x45000u, 0x46000u, 0x47000u, 0x48000u, 0x49000u, 0x4a000u,
	0x4b000u, 0x4c000u, 0xf0fu, 0xf0fu, 0xf0fu, 0xf0fu
};
static const uint32_t STAGE_OFF[11] = {
	0x40u, 0x70u, 0xa0u, 0xd0u, 0x100u, 0x130u,
	0x160u, 0x190u, 0x1c0u, 0x1f0u, 0x220u
};

/* DMA4/DMA5 are the 9th/10th SSPP entries -> pipe_id 8/9. */
#define PIPE_DMA4  0x2d000u
#define PIPE_DMA5  0x2f000u

/* VIG0/VIG1 are the 1st/2nd SSPP entries -> pipe_id 0/1. */
#define PIPE_VIG0  0x5000u
#define PIPE_VIG1  0x7000u

/* ---- MMIO model -------------------------------------------------------- */

struct mock {
	uint32_t regs[MDP_WORDS];
	unsigned reads, writes;
	/* R* S* R* ordering state: 0 = first routing pass, 1 = snapshot run,
	 * 2 = second routing pass. */
	unsigned state;
	bool order_bad;
	unsigned route_count, snap_count;
	uintptr_t route_seq[512];
	uintptr_t snap_seq[512];
	bool saw_lm_select;
	uintptr_t unstable_off;
	unsigned unstable_reads;
	/* Exactly the two SSPP bases the active fixture routes from; the only
	 * bases whose legacy SNAP_REL words may be read. Set by the fixture's
	 * mock_program* before every run. */
	uintptr_t snap_pipe[2];
	/* First/last register offset (relative to MDP_BASE) of the run; recorded
	 * only, never used to alter read/write acceptance. */
	uintptr_t first_read_off, last_read_off;
};

static struct mock g_mock;

static void fail(const char *what, uintptr_t off)
{
	fprintf(stderr, "tb322fc_splash_test: %s (0x%lx)\n", what,
		(unsigned long)off);
	abort();
}

/* Legacy SSPP snapshot (non-REC) reads, relative to the active pipe base. */
static const uintptr_t SNAP_REL[11] = {
	0x00u, 0x08u, 0x0cu, 0x10u, 0x14u, 0x24u,
	0x30u, 0x34u, 0x38u, 0x70u, 0x170u
};

static bool snapshot_off_ok(uintptr_t off)
{
	for (unsigned p = 0; p < 2u; ++p)
		for (unsigned i = 0; i < 11u; ++i)
			if (off == g_mock.snap_pipe[p] + SNAP_REL[i])
				return true;
	return false;
}

static bool routing_off_ok(uintptr_t off)
{
	/* Every CTL control dword is probed; only CTL0 is active. */
	for (unsigned c = 0; c < 6u; ++c)
		if (off == CTL_OFF[c] + 0xf4u)
			return true;
	if (off == CTL_OFF[0] + 0x12cu || off == CTL_OFF[0] + 0x130u)
		return true;

	/* Active LM0/LM1: select, out, BG and each blend stage + 4. */
	for (unsigned m = 0; m < 2u; ++m) {
		uintptr_t lm = MIXER_RAW[m];

		if (off == lm + 0x00u || off == lm + 0x04u ||
		    off == lm + 0x14u)
			return true;
		for (unsigned s = 0; s < 11u; ++s)
			if (off == lm + STAGE_OFF[s] + 4u)
				return true;
	}
	return false;
}

static uint32_t read32(void *context, uintptr_t address)
{
	struct mock *m = context;
	uintptr_t off;

	if (address < MDP_BASE)
		fail("read below MDP base", address);
	off = address - MDP_BASE;
	if ((off & 3u) != 0u)
		fail("unaligned register read", off);
	if (off + 4u > MDP_SIZE)
		fail("register read past MDP size", off);

	/* Sun must never touch the DPU13 REC/SOURCE_0 SSPP view; only the two
	 * active fixture bases are modelled at all. */
	for (unsigned p = 0; p < 2u; ++p) {
		uintptr_t b = g_mock.snap_pipe[p];

		if (off >= b + 0x1000u && off < b + 0x1080u)
			fail("SSPP SOURCE_0 / REC read", off);
		if (off == b + 0x64u || off == b + 0x74u)
			fail("REC-only SSPP read", off);
	}

	bool route = routing_off_ok(off);
	bool snap = snapshot_off_ok(off);
	if (!route && !snap)
		fail("unexpected read offset", off);
	if (route && snap)
		fail("ambiguous read offset", off);

	if (route) {
		if (m->state == 1u)
			m->state = 2u;	/* snapshots done -> second active pass */
		else if (m->state != 0u && m->state != 2u)
			m->order_bad = true;
		if (m->route_count < 512u)
			m->route_seq[m->route_count] = off;
		m->route_count++;
		if (off == MIXER_RAW[0] + 0x14u || off == MIXER_RAW[1] + 0x14u)
			m->saw_lm_select = true;
	} else {
		if (m->state == 0u)
			m->state = 1u;	/* first active pass done -> snapshots */
		else if (m->state != 1u)
			m->order_bad = true;
		if (m->snap_count < 512u)
			m->snap_seq[m->snap_count] = off;
		m->snap_count++;
	}
	if (m->reads == 0u)
		m->first_read_off = off;
	m->last_read_off = off;
	m->reads++;

	uint32_t value = m->regs[off / 4u];
	if (m->unstable_off == off && ++m->unstable_reads == 2u)
		value ^= 4u;
	return value;
}

static void write32(void *context, uintptr_t address, uint32_t value)
{
	struct mock *m = context;

	(void)value;
	m->writes++;
	fail("unexpected MMIO write", address);
}

static void mock_reset(void)
{
	memset(&g_mock, 0, sizeof(g_mock));
}

/* Program an initial valid Sun scanout: two DMA SSPPs split left/right. */
static void mock_program(void)
{
	const uint32_t src = (3040u << 16) | 952u;
	uint32_t pipe[2] = { PIPE_DMA4, PIPE_DMA5 };

	g_mock.snap_pipe[0] = PIPE_DMA4;
	g_mock.snap_pipe[1] = PIPE_DMA5;

	g_mock.regs[(CTL_OFF[0] + 0xf4u) / 4u] = 6u;
	g_mock.regs[(CTL_OFF[0] + 0x12cu) / 4u] = 0x30u;
	g_mock.regs[(CTL_OFF[0] + 0x130u) / 4u] = 3u;

	for (unsigned m = 0; m < 2u; ++m) {
		uintptr_t lm = MIXER_RAW[m];

		g_mock.regs[(lm + 0x00u) / 4u] = (m == 0u) ? 0u : 0x80000000u;
		g_mock.regs[(lm + 0x04u) / 4u] = src;
		g_mock.regs[(lm + 0x14u) / 4u] = 0xc0c0u;
		for (unsigned s = 0; s < 11u; ++s)
			g_mock.regs[(lm + STAGE_OFF[s] + 4u) / 4u] = 0xc0c0u;
		/* Stage0 selects the LM's own DMA source (0xc004 / 0xc005). */
		g_mock.regs[(lm + STAGE_OFF[0] + 4u) / 4u] =
			(m == 0u) ? 0xc004u : 0xc005u;
	}

	for (unsigned i = 0; i < 2u; ++i) {
		uintptr_t p = pipe[i];

		g_mock.regs[(p + 0x00u) / 4u] = src;			/* src */
		g_mock.regs[(p + 0x08u) / 4u] = (i == 0u) ? 0u : 952u;	/* xy */
		g_mock.regs[(p + 0x0cu) / 4u] = src;			/* out */
		g_mock.regs[(p + 0x10u) / 4u] = 0u;			/* out_xy */
		g_mock.regs[(p + 0x14u) / 4u] = FB_BASE;		/* addr */
		g_mock.regs[(p + 0x24u) / 4u] = 7616u;			/* stride */
		g_mock.regs[(p + 0x30u) / 4u] = 0x237ffu;		/* format */
		g_mock.regs[(p + 0x34u) / 4u] = 0x03020001u;		/* unpack */
		g_mock.regs[(p + 0x38u) / 4u] = 0u;			/* op */
		g_mock.regs[(p + 0x70u) / 4u] = 0u;			/* secure */
		g_mock.regs[(p + 0x170u) / 4u] = 0u;			/* rect */
	}
}

/*
 * Program the shared LM0/LM1 scanout: both active LMs place the same two
 * VIG0/VIG1 sources at the same blend stage (index 5), so a single VIG
 * source pair feeds both mixers.
 *
 * EVIDENCE BOUNDARY: the R7 capture only covers the CTL control words
 * (CTL0 +0xf4 = 6, +0x12c = 0x30000, +0x130 = 3) and the LM0 blend
 * selectors (BG 0xc0c0; every blend stage 0xc0c0 except stage index 5 =
 * 0x4140 = VIG0+VIG1). Everything else here -- the LM1 routing, both LMs'
 * geometry/op words and every SSPP geometry/data word -- is synthetic
 * scaffolding chosen to exercise the shared-source layout. This is not a
 * hardware snapshot and asserts no acceptance of real silicon.
 */
static void mock_program_shared(void)
{
	const uint32_t src = (3040u << 16) | 952u;

	g_mock.snap_pipe[0] = PIPE_VIG0;
	g_mock.snap_pipe[1] = PIPE_VIG1;

	g_mock.regs[(CTL_OFF[0] + 0xf4u) / 4u] = 6u;
	g_mock.regs[(CTL_OFF[0] + 0x12cu) / 4u] = 0x30000u;
	g_mock.regs[(CTL_OFF[0] + 0x130u) / 4u] = 3u;

	for (unsigned m = 0; m < 2u; ++m) {
		uintptr_t lm = MIXER_RAW[m];

		g_mock.regs[(lm + 0x00u) / 4u] = (m == 0u) ? 0u : 0x80000000u;
		g_mock.regs[(lm + 0x04u) / 4u] = src;
		g_mock.regs[(lm + 0x14u) / 4u] = 0xc0c0u;
		for (unsigned s = 0; s < 11u; ++s)
			g_mock.regs[(lm + STAGE_OFF[s] + 4u) / 4u] = 0xc0c0u;
		/* Stage index 5 carries the VIG0+VIG1 pair (0x4140). */
		g_mock.regs[(lm + STAGE_OFF[5] + 4u) / 4u] = 0x4140u;
	}

	for (unsigned i = 0; i < 2u; ++i) {
		uintptr_t p = (i == 0u) ? PIPE_VIG0 : PIPE_VIG1;
		uint32_t x = (i == 0u) ? 0u : 952u;

		g_mock.regs[(p + 0x00u) / 4u] = src;		/* src */
		g_mock.regs[(p + 0x08u) / 4u] = x;		/* src xy */
		g_mock.regs[(p + 0x0cu) / 4u] = src;		/* out */
		g_mock.regs[(p + 0x10u) / 4u] = x;		/* out_xy */
		g_mock.regs[(p + 0x14u) / 4u] = FB_BASE;	/* addr */
		g_mock.regs[(p + 0x24u) / 4u] = 7616u;		/* stride */
		g_mock.regs[(p + 0x30u) / 4u] = 0x237ffu;	/* format */
		g_mock.regs[(p + 0x34u) / 4u] = 0x03020001u;	/* unpack */
		g_mock.regs[(p + 0x38u) / 4u] = 0u;		/* op */
		g_mock.regs[(p + 0x70u) / 4u] = 0u;		/* secure */
		g_mock.regs[(p + 0x170u) / 4u] = 0u;		/* rect */
	}
}

static void io_bind(struct CrIo *io)
{
	memset(io, 0, sizeof(*io));
	io->context = &g_mock;
	io->read32 = read32;
	io->write32 = write32;
}

/* ---- DT synthesis ------------------------------------------------------ */

static unsigned char g_blob[BLOB_SIZE] __attribute__((aligned(8)));

static void set_cells(void *b, int node, const char *name,
		      const uint32_t *values, unsigned count)
{
	fdt32_t tmp[16];

	assert(count <= 16u);
	for (unsigned i = 0; i < count; ++i)
		tmp[i] = cpu_to_fdt32(values[i]);
	assert(fdt_setprop(b, node, name, tmp,
			   (int)(count * sizeof(fdt32_t))) == 0);
}

static int sde_node(const void *b)
{
	int n = fdt_node_offset_by_compatible(b, -1, "qcom,sde-kms");

	assert(n >= 0);
	return n;
}

/*
 * Build the captured Sun resource tree. compat is the exact root
 * compatible; ver/ver_len, when non-NULL, add qcom,sde-hw-version.
 */
static void build_sun(const char *compat, const void *ver, int ver_len)
{
	void *b = g_blob;

	memset(b, 0, BLOB_SIZE);
	assert(fdt_create_empty_tree(b, BLOB_SIZE) == 0);
	assert(fdt_setprop_string(b, 0, "compatible", compat) == 0);
	assert(fdt_setprop_u32(b, 0, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(b, 0, "#size-cells", 2) == 0);

	int rm = fdt_add_subnode(b, 0, "reserved-memory");
	assert(rm >= 0);
	assert(fdt_setprop_u32(b, rm, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(b, rm, "#size-cells", 2) == 0);
	assert(fdt_setprop(b, rm, "ranges", NULL, 0) == 0);
	int cs = fdt_add_subnode(b, rm, "cont_splash_region@fc800000");
	assert(cs >= 0);
	assert(fdt_setprop_string(b, cs, "label", "cont_splash_region") == 0);
	fdt64_t cs_reg[2] = { cpu_to_fdt64(FB_BASE), cpu_to_fdt64(FB_SIZE) };
	assert(fdt_setprop(b, cs, "reg", cs_reg, sizeof(cs_reg)) == 0);

	int sde = fdt_add_subnode(b, 0, "display-subsystem");
	assert(sde >= 0);
	assert(fdt_setprop_string(b, sde, "compatible", "qcom,sde-kms") == 0);
	assert(fdt_setprop_string(b, sde, "reg-names", "mdp_phys") == 0);
	fdt64_t mdp_reg[2] = { cpu_to_fdt64(MDP_BASE), cpu_to_fdt64(MDP_SIZE) };
	assert(fdt_setprop(b, sde, "reg", mdp_reg, sizeof(mdp_reg)) == 0);

	set_cells(b, sde, "qcom,sde-ctl-off", CTL_OFF, 6u);
	set_cells(b, sde, "qcom,sde-sspp-off", SSPP_OFF, 10u);

	char types[40];
	for (unsigned i = 0; i < 4u; ++i)
		memcpy(types + i * 4u, "vig", 4u);
	for (unsigned i = 0; i < 6u; ++i)
		memcpy(types + 16u + i * 4u, "dma", 4u);
	assert(fdt_setprop(b, sde, "qcom,sde-sspp-type", types,
			   (int)sizeof(types)) == 0);

	set_cells(b, sde, "qcom,sde-mixer-off", MIXER_RAW, 12u);
	set_cells(b, sde, "qcom,sde-mixer-blend-op-off", STAGE_OFF, 11u);
	assert(fdt_setprop_u32(b, sde, "qcom,sde-mixer-blendstages", 11u) == 0);
	assert(fdt_setprop_u32(b, sde, "qcom,sde-sspp-src-size", 0x344u) == 0);
	if (ver)
		assert(fdt_setprop(b, sde, "qcom,sde-hw-version", ver,
				   ver_len) == 0);
}

/* ---- expectation helpers ---------------------------------------------- */

static void expect_zero_outputs(const kshim_framebuffer_config_t *fb,
				const kshim_mmu_region_t *mmio)
{
	assert(fb->render_address == 0u && fb->width == 0u && fb->height == 0u &&
	       fb->stride == 0u && fb->bpp == 0u && fb->buffer_size == 0u &&
	       fb->format == 0);
	assert(mmio->base == 0u && mmio->size == 0u && mmio->type == 0 &&
	       mmio->permissions == 0u);
}

/* The framebuffer/MMIO result and the routing ordering contract shared by
 * every accepted Sun scanout (single DMA half, or the shared-LM VIG pair). */
static void assert_success_contract(const kshim_framebuffer_config_t *fb,
				    const kshim_mmu_region_t *mmio)
{
	assert(fb->width == 1904u);
	assert(fb->height == 3040u);
	assert(fb->stride == 7616u);
	assert(fb->bpp == 32u);
	assert(fb->format == KSHIM_FB_FORMAT_ARGB8888);
	assert(fb->render_address == FB_BASE);
	assert(fb->buffer_size == 23152640u);

	assert(mmio->base == MDP_BASE);
	assert(mmio->size == MDP_SIZE);
	assert(mmio->type == KSHIM_MMU_DEVICE);
	assert(mmio->permissions == KSHIM_MMU_READ);

	/* No writes, and Sun uses LM source-select routing, never the REC view. */
	assert(g_mock.writes == 0u);
	assert(!g_mock.order_bad);
	assert(g_mock.state == 2u);
	assert(g_mock.saw_lm_select);

	/* Ordering: active routing, old snapshots, old snapshots, active
	 * routing. Each pass repeats the same offsets, so both sequences are
	 * two identical halves. */
	assert(g_mock.route_count >= 2u && (g_mock.route_count % 2u) == 0u);
	/* 2 pipes * 11 registers * 2 passes = 44 old snapshot reads. */
	assert(g_mock.snap_count == 44u);
	unsigned rh = g_mock.route_count / 2u;
	unsigned sh = g_mock.snap_count / 2u;
	for (unsigned i = 0; i < rh; ++i)
		assert(g_mock.route_seq[i] == g_mock.route_seq[i + rh]);
	for (unsigned i = 0; i < sh; ++i)
		assert(g_mock.snap_seq[i] == g_mock.snap_seq[i + sh]);
}

static void expect_sun_success(const void *blob, const struct CrIo *io)
{
	unsigned char before[BLOB_SIZE];
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;

	memcpy(before, blob, BLOB_SIZE);
	memset(&fb, 0x5a, sizeof(fb));
	memset(&mmio, 0x5a, sizeof(mmio));

	mock_reset();
	mock_program();
	int rc = kshim_dt_splash(blob, io, &fb, &mmio);
	if (rc != 0)
		fprintf(stderr, "Sun splash expected success, got %d\n", rc);
	assert(rc == 0);

	assert_success_contract(&fb, &mmio);

	/* The input DT is byte-for-byte untouched. */
	assert(memcmp(before, blob, BLOB_SIZE) == 0);
}

/* A rejection that must happen before any MMIO is issued. */
static void expect_rejected_before_mmio(const void *blob, const struct CrIo *io)
{
	unsigned char before[BLOB_SIZE];
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;

	memcpy(before, blob, BLOB_SIZE);
	memset(&fb, 0x5a, sizeof(fb));
	memset(&mmio, 0x5a, sizeof(mmio));
	mock_reset();
	mock_program();
	int rc = kshim_dt_splash(blob, io, &fb, &mmio);
	assert(rc != 0);
	assert(g_mock.writes == 0u);
	assert(g_mock.reads == 0u);
	expect_zero_outputs(&fb, &mmio);
	assert(!g_mock.order_bad);
	assert(memcmp(before, blob, BLOB_SIZE) == 0);
}

/* A rejection that may legitimately perform reads while probing. */
static void expect_rejected(const void *blob, const struct CrIo *io)
{
	unsigned char before[BLOB_SIZE];
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;

	memcpy(before, blob, BLOB_SIZE);
	memset(&fb, 0x5a, sizeof(fb));
	memset(&mmio, 0x5a, sizeof(mmio));
	mock_reset();
	mock_program();
	int rc = kshim_dt_splash(blob, io, &fb, &mmio);
	assert(rc != 0);
	assert(g_mock.writes == 0u);
	expect_zero_outputs(&fb, &mmio);
	assert(!g_mock.order_bad);
	assert(memcmp(before, blob, BLOB_SIZE) == 0);
}

/* A recheck failure: one register changes value on its second read. */
static void expect_failed_unstable(const void *blob, const struct CrIo *io,
				   uintptr_t unstable_off)
{
	unsigned char before[BLOB_SIZE];
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;

	memcpy(before, blob, BLOB_SIZE);
	memset(&fb, 0x5a, sizeof(fb));
	memset(&mmio, 0x5a, sizeof(mmio));
	mock_reset();
	mock_program();
	g_mock.unstable_off = unstable_off;
	int rc = kshim_dt_splash(blob, io, &fb, &mmio);
	assert(rc != 0);
	assert(g_mock.writes == 0u);
	expect_zero_outputs(&fb, &mmio);
	assert(!g_mock.order_bad);
	assert(memcmp(before, blob, BLOB_SIZE) == 0);
}

/* ---- shared LM0/LM1 fixture (synthetic) -------------------------------- */

/* One line before any assertion, so a RED failure names the case and the
 * exact rc/read/snapshot/route counts it reached. */
static void print_shared_case(const char *name, int rc)
{
	printf("shared-lm %s: rc=%d reads=%u snapshots=%u routes=%u\n",
	       name, rc, g_mock.reads, g_mock.snap_count, g_mock.route_count);
	fflush(stdout);
}

/* The shared-LM positive: expected to succeed and to match the existing
 * combined framebuffer contract, with the unchanged routing/snapshot order
 * and 44 legacy SSPP snapshot reads. It FAILS against today's production,
 * whose active_lm rejects the second source, and must pass unchanged once
 * the shared layout is supported. */
static void expect_shared_lm_success(const void *blob, const struct CrIo *io)
{
	unsigned char before[BLOB_SIZE];
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;

	memcpy(before, blob, BLOB_SIZE);
	memset(&fb, 0x5a, sizeof(fb));
	memset(&mmio, 0x5a, sizeof(mmio));

	mock_reset();
	mock_program_shared();
	int rc = kshim_dt_splash(blob, io, &fb, &mmio);
	print_shared_case("positive-shared-lm", rc);
	assert(rc == 0);

	assert_success_contract(&fb, &mmio);
	assert(memcmp(before, blob, BLOB_SIZE) == 0);
}

/* One-factor negative: reset to the valid shared fixture, apply a single
 * register mutation, then require a clean rejection (nonzero rc, zeroed
 * outputs, no writes, untouched input DT, no ordering violation). */
static void expect_shared_rejected(const char *name, const void *blob,
				   const struct CrIo *io, void (*mutate)(void))
{
	unsigned char before[BLOB_SIZE];
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;

	memcpy(before, blob, BLOB_SIZE);
	memset(&fb, 0x5a, sizeof(fb));
	memset(&mmio, 0x5a, sizeof(mmio));

	mock_reset();
	mock_program_shared();
	if (mutate)
		mutate();
	int rc = kshim_dt_splash(blob, io, &fb, &mmio);
	print_shared_case(name, rc);
	assert(rc != 0);
	assert(g_mock.writes == 0u);
	expect_zero_outputs(&fb, &mmio);
	assert(!g_mock.order_bad);
	assert(memcmp(before, blob, BLOB_SIZE) == 0);
}

/* Instability negative: one selected word changes on its second read, so the
 * recheck must reject. The second read is required to prove the mutation
 * actually took effect, rather than the run being refused early. */
static void expect_shared_unstable(const char *name, const void *blob,
				   const struct CrIo *io, uintptr_t unstable_off)
{
	unsigned char before[BLOB_SIZE];
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;

	memcpy(before, blob, BLOB_SIZE);
	memset(&fb, 0x5a, sizeof(fb));
	memset(&mmio, 0x5a, sizeof(mmio));

	mock_reset();
	mock_program_shared();
	g_mock.unstable_off = unstable_off;
	int rc = kshim_dt_splash(blob, io, &fb, &mmio);
	print_shared_case(name, rc);
	assert(rc != 0);
	assert(g_mock.unstable_reads >= 2u);
	assert(g_mock.writes == 0u);
	expect_zero_outputs(&fb, &mmio);
	assert(!g_mock.order_bad);
	assert(memcmp(before, blob, BLOB_SIZE) == 0);
}

static void mutate_shared_noncontiguous(void)
{
	/* VIG1 still reads x=952 but from a base 4 bytes late. */
	g_mock.regs[(PIPE_VIG1 + 0x14u) / 4u] = FB_BASE + 4u;
}

static void mutate_shared_overlap(void)
{
	/* VIG1 output starts one pixel early (x=951). */
	g_mock.regs[(PIPE_VIG1 + 0x10u) / 4u] = 951u;
}

static void mutate_shared_gap(void)
{
	/* VIG1 output starts one pixel late (x=953). */
	g_mock.regs[(PIPE_VIG1 + 0x10u) / 4u] = 953u;
}

static void mutate_shared_lm1_mask(void)
{
	/* LM1 stage5 selects only VIG0; LM0 still selects the VIG0+VIG1 pair. */
	g_mock.regs[(MIXER_RAW[1] + STAGE_OFF[5] + 4u) / 4u] = 0xc040u;
}

static void mutate_shared_lm1_stage(void)
{
	/* Same pair, different blend stage on LM1 only. */
	g_mock.regs[(MIXER_RAW[1] + STAGE_OFF[5] + 4u) / 4u] = 0xc0c0u;
	g_mock.regs[(MIXER_RAW[1] + STAGE_OFF[6] + 4u) / 4u] = 0x4140u;
}

static void mutate_shared_duplicate(void)
{
	/* LM0 stage5 selects VIG0 twice. */
	g_mock.regs[(MIXER_RAW[0] + STAGE_OFF[5] + 4u) / 4u] = 0x4040u;
}

/* ---- real-DTB offline mode (argv[1]) ---------------------------------- */

/*
 * Optional real-input mode. The caller supplies a composed .259 DTB path; this
 * runs the Sun splash detector against that exact tree offline using only the
 * existing mock CrIo callbacks. No BLOB_SIZE stack snapshot is used for real
 * input: the whole loaded byte length is snapshotted dynamically.
 *
 * Loads a real DTB exactly like stock_dt_check.c load_blob (fopen/fseek/ftell/
 * malloc/fread), so the length is taken from the file, never a constant.
 */
static size_t load_real_dtb(const char *path, unsigned char **out)
{
	FILE *fp = fopen(path, "rb");
	long n;
	unsigned char *buf;

	if (!fp)
		fail("cannot open real DTB", 0u);
	if (fseek(fp, 0, SEEK_END) != 0)
		fail("cannot seek real DTB", 0u);
	n = ftell(fp);
	if (n <= 0)
		fail("empty or unseekable real DTB", 0u);
	if (fseek(fp, 0, SEEK_SET) != 0)
		fail("cannot rewind real DTB", 0u);
	buf = malloc((size_t)n);
	if (!buf)
		fail("out of memory loading real DTB", 0u);
	if (fread(buf, 1, (size_t)n, fp) != (size_t)n)
		fail("short read of real DTB", 0u);
	fclose(fp);
	*out = buf;
	return (size_t)n;
}

/*
 * Exact multi-compatible root list for case 2: five strings, each NUL-
 * separated, with exactly one final terminator (the implicit one). There is
 * no trailing empty entry; sizeof() includes that single terminator.
 */
static const char COMPAT_LIST[] =
	"qcom,sun-qrd\0"
	"qcom,sun\0"
	"qcom,sunp-qrd\0"
	"qcom,sunp\0"
	"qcom,qrd";

#define REAL_EXTRA_CAPACITY 4096u

struct real_case_result {
	int rc;
	unsigned reads, writes;
	uintptr_t first_off, last_off;
	kshim_framebuffer_config_t fb;
	kshim_mmu_region_t mmio;
	bool tree_unchanged;
	unsigned dt_stage;
	int root_compatible_count;
	bool root_has_sunp;
};

/*
 * Shared real-input case runner. Validates the buffer, takes a full-length
 * pre-parse snapshot, runs kshim_dt_splash inside a fresh parent-index scope
 * with the existing mock callbacks, closes the index, then proves every byte
 * of the whole buffer is unchanged and reports one line before any assertion.
 */
static struct real_case_result run_real_case(const char *name, const void *blob,
					     size_t available)
{
	struct real_case_result r;
	unsigned char *snapshot;
	struct CrIo io;
	char first[24], last[24];

	assert(dt_validate(blob, available) == 0);

	snapshot = malloc(available);
	if (!snapshot)
		fail("out of memory snapshotting real DTB", 0u);
	memcpy(snapshot, blob, available);

	memset(&r.fb, 0x5a, sizeof(r.fb));
	memset(&r.mmio, 0x5a, sizeof(r.mmio));
	io_bind(&io);
	mock_reset();
	mock_program();

	dt_parent_index_begin(blob);
	r.rc = kshim_dt_splash(blob, &io, &r.fb, &r.mmio);
	dt_parent_index_end();

	{
		/* This test TU does not define KSHIM_DIAGNOSTIC_RESOURCES_ONLY, but
		 * the kshim_dt library is compiled with it, so the diagnostic getter
		 * reports the real stage/count/sunp fields of that exact run. */
		const struct kshim_splash_trace *trace = kshim_splash_trace_get();

		r.dt_stage = trace->dt_stage;
		r.root_compatible_count = trace->root_compatible_count;
		r.root_has_sunp = trace->root_has_sunp;
	}

	r.reads = g_mock.reads;
	r.writes = g_mock.writes;
	r.first_off = g_mock.first_read_off;
	r.last_off = g_mock.last_read_off;
	r.tree_unchanged = memcmp(blob, snapshot, available) == 0;

	if (r.reads != 0u) {
		snprintf(first, sizeof(first), "0x%lx",
			 (unsigned long)r.first_off);
		snprintf(last, sizeof(last), "0x%lx",
			 (unsigned long)r.last_off);
	} else {
		snprintf(first, sizeof(first), "no-reads");
		snprintf(last, sizeof(last), "no-reads");
	}
	printf("%s: rc=%d reads=%u writes=%u first=%s last=%s tree_unchanged=%d "
	       "stage=%u cc=%d sun=%d "
	       "fb=%ux%u/%u/%ubpp/fmt%d/addr0x%lx/buf%u "
	       "mmio=0x%lx+0x%lx/type%d/perm0x%x\n",
	       name, r.rc, r.reads, r.writes, first, last,
	       (int)r.tree_unchanged, r.dt_stage, r.root_compatible_count,
	       r.root_has_sunp ? 1 : 0,
	       r.fb.width, r.fb.height, r.fb.stride,
	       (unsigned)r.fb.bpp, (int)r.fb.format,
	       (unsigned long)r.fb.render_address, (unsigned)r.fb.buffer_size,
	       (unsigned long)r.mmio.base, (unsigned long)r.mmio.size,
	       (int)r.mmio.type, (unsigned)r.mmio.permissions);
	fflush(stdout);

	free(snapshot);
	return r;
}

/* Success contract shared by the original single-string Sun tree and the fixed
 * valid multi-compatible tree: identical rc, read/write counts, read bounds,
 * framebuffer/MMIO fields, and untouched whole tree. */
static void assert_real_sun_success(const struct real_case_result *c)
{
	assert(c->rc == 0);
	assert(c->reads == 106u);
	assert(c->writes == 0u);
	assert(c->first_off == 0x160f4u);
	assert(c->last_off == 0x46000u);
	assert(c->tree_unchanged);
	assert(c->fb.width == 1904u);
	assert(c->fb.height == 3040u);
	assert(c->fb.stride == 7616u);
	assert(c->fb.bpp == 32u);
	assert(c->fb.format == KSHIM_FB_FORMAT_ARGB8888);
	assert(c->fb.render_address == FB_BASE);
	assert(c->fb.buffer_size == 23152640u);
	assert(c->mmio.base == MDP_BASE);
	assert(c->mmio.size == MDP_SIZE);
	assert(c->mmio.type == KSHIM_MMU_DEVICE);
	assert(c->mmio.permissions == KSHIM_MMU_READ);
}

static int offline_real_dt(const char *path)
{
	unsigned char *blob = NULL;
	unsigned char *orig_snapshot, *copy;
	size_t size, cap;
	struct real_case_result c;

	size = load_real_dtb(path, &blob);
	printf("loaded real DTB: %zu bytes\n", size);
	fflush(stdout);

	orig_snapshot = malloc(size);
	if (!orig_snapshot)
		fail("out of memory snapshotting real DTB", 0u);
	memcpy(orig_snapshot, blob, size);

	/* Case 1: the unmodified real tree must succeed exactly as the
	 * synthesized regression tree: no tree edit, reads>0, no writes, the
	 * exact framebuffer/MMIO contract, and the known traversal. */
	c = run_real_case("case1-original-sunp", blob, size);
	assert_real_sun_success(&c);
	/* Diagnostic stage/count/sunp for the original single-string root. */
	assert(c.dt_stage == 11u);
	assert(c.root_compatible_count == 1);
	assert(c.root_has_sunp);
	/* Same ordering contract as the synthesized success helper. */
	assert(!g_mock.order_bad);
	assert(g_mock.state == 2u);
	assert(g_mock.saw_lm_select);
	assert(g_mock.route_count >= 2u && (g_mock.route_count % 2u) == 0u);
	assert(g_mock.snap_count == 44u);
	{
		unsigned rh = g_mock.route_count / 2u;
		unsigned sh = g_mock.snap_count / 2u;

		for (unsigned i = 0; i < rh; ++i)
			assert(g_mock.route_seq[i] == g_mock.route_seq[i + rh]);
		for (unsigned i = 0; i < sh; ++i)
			assert(g_mock.snap_seq[i] == g_mock.snap_seq[i + sh]);
	}
	/* The original loaded bytes must be untouched after case 1. */
	assert(memcmp(blob, orig_snapshot, size) == 0);

	/* Case 2: a NEW independent writable copy with modest extra capacity,
	 * zero-filled so full-buffer snapshots are deterministic. Only the root
	 * compatible is replaced by the exact valid five-string list.
	 *
	 * FIXED VALID-MULTI-COMPATIBLE REGRESSION: the root is now matched as a
	 * legal exact string-list entry, so this tree succeeds exactly like
	 * case 1 (same rc, reads/writes, read bounds, framebuffer/MMIO and
	 * untouched whole tree). */
	cap = size + REAL_EXTRA_CAPACITY;
	copy = malloc(cap);
	if (!copy)
		fail("out of memory copying real DTB", 0u);
	memset(copy, 0, cap);
	assert(fdt_open_into(blob, copy, (int)cap) == 0);
	assert(fdt_setprop(copy, 0, "compatible", COMPAT_LIST,
			   (int)sizeof(COMPAT_LIST)) == 0);
	c = run_real_case("case2-multi-compatible", copy, cap);
	assert_real_sun_success(&c);
	/* The five-string list still carries exactly one exact "qcom,sunp"
	 * entry, and the run reaches CrSplashRead. */
	assert(c.dt_stage == 11u);
	assert(c.root_compatible_count == 5);
	assert(c.root_has_sunp);
	free(copy);
	assert(memcmp(blob, orig_snapshot, size) == 0);

	/* Case 3: another NEW independent writable copy (never the case 2 tree);
	 * the original root compatible is left untouched and ONLY `label` is
	 * removed from /reserved-memory/splash_region. */
	cap = size + REAL_EXTRA_CAPACITY;
	copy = malloc(cap);
	if (!copy)
		fail("out of memory copying real DTB", 0u);
	memset(copy, 0, cap);
	assert(fdt_open_into(blob, copy, (int)cap) == 0);
	{
		int splash = fdt_path_offset(copy, "/reserved-memory/splash_region");

		assert(splash >= 0);
		assert(fdt_delprop(copy, splash, "label") == 0);
	}
	c = run_real_case("case3-missing-splash-label", copy, cap);
	assert(c.rc == -1);
	assert(c.reads == 0u);
	assert(c.writes == 0u);
	assert(c.tree_unchanged);
	expect_zero_outputs(&c.fb, &c.mmio);
	/* The label loss is a stage-2 reservation lookup failure with the root
	 * compatible still a single exact "qcom,sunp". */
	assert(c.dt_stage == 2u);
	assert(c.root_compatible_count == 1);
	assert(c.root_has_sunp);
	free(copy);
	assert(memcmp(blob, orig_snapshot, size) == 0);

	free(orig_snapshot);
	free(blob);
	return 0;
}

int main(int argc, char **argv)
{
	struct CrIo io;
	fdt32_t ver_c000 = cpu_to_fdt32(0xc0000000u);
	fdt32_t ver_d000 = cpu_to_fdt32(0xd0000000u);
	fdt32_t ver_bad = cpu_to_fdt32(0x12345678u);

	/* argc 1: the existing in-process regression body, unchanged.
	 * argc 2: the offline real-DT cases, then exit.
	 * other:  usage and failure. */
	if (argc == 2)
		return offline_real_dt(argv[1]);
	if (argc != 1) {
		fprintf(stderr, "usage: %s [real-dtb]\n", argv[0]);
		return 1;
	}

	io_bind(&io);

	/* 1. Explicit valid 0xc0000000 version on a Sun root succeeds. */
	build_sun("qcom,sunp", &ver_c000, 4);
	expect_sun_success(g_blob, &io);

	/* 2. Absent version, exact qcom,sunp root and the known shape. */
	build_sun("qcom,sunp", NULL, 0);
	expect_sun_success(g_blob, &io);

	/* 3. Conflicting 0xd0000000 on qcom,sunp must be rejected, never
	 *    silently reclassified as Canoe; an unsupported explicit version
	 *    on a Sun root is rejected too. Both precede any MMIO. */
	build_sun("qcom,sunp", &ver_d000, 4);
	expect_rejected_before_mmio(g_blob, &io);
	build_sun("qcom,sunp", &ver_bad, 4);
	expect_rejected_before_mmio(g_blob, &io);

	/* 4. A present but malformed version (zero length or a wide value) is
	 *    rejected, never treated as absent. */
	build_sun("qcom,sunp", NULL, 0);
	assert(fdt_setprop(g_blob, sde_node(g_blob), "qcom,sde-hw-version",
			   NULL, 0) == 0);
	expect_rejected_before_mmio(g_blob, &io);
	build_sun("qcom,sunp", NULL, 0);
	{
		fdt32_t wide[2] = { cpu_to_fdt32(0xc0000000u), cpu_to_fdt32(0u) };

		assert(fdt_setprop(g_blob, sde_node(g_blob),
				   "qcom,sde-hw-version", wide,
				   (int)sizeof(wide)) == 0);
	}
	expect_rejected_before_mmio(g_blob, &io);

	/* 5. Absent version, unknown root, and a non-exact qcom,sunp-extra. */
	build_sun("qcom,unknown", NULL, 0);
	expect_rejected_before_mmio(g_blob, &io);
	build_sun("qcom,sunp-extra", NULL, 0);
	expect_rejected_before_mmio(g_blob, &io);

	/* 5b. The root "compatible" is a legal string list matched by exact
	 *     entry, with no substring/prefix/casefold shortcuts. */
	/* A five-string list carrying an exact "qcom,sunp" entry is accepted
	 * exactly like the single-string root (absent version -> fallback). */
	build_sun("qcom,sunp", NULL, 0);
	assert(fdt_setprop(g_blob, 0, "compatible", COMPAT_LIST,
			   (int)sizeof(COMPAT_LIST)) == 0);
	expect_sun_success(g_blob, &io);

	/* A complete list of prefix/near misses with no exact "qcom,sunp"
	 * entry is rejected before any read. */
	build_sun("qcom,sunp", NULL, 0);
	{
		static const char near[] =
			"qcom,sunp-extra\0"
			"qcom,sun-qrd\0"
			"qcom,sunz\0"
			"qcom,unknown";

		assert(fdt_setprop(g_blob, 0, "compatible", near,
				   (int)sizeof(near)) == 0);
	}
	expect_rejected_before_mmio(g_blob, &io);

	/* A malformed list missing its terminal NUL, whose FIRST entry is the
	 * exact "qcom,sunp", is rejected before any read: the whole list count
	 * must validate before the entry is matched, so a bad tail cannot hide
	 * behind an early exact entry. */
	build_sun("qcom,sunp", NULL, 0);
	{
		static const char truncated[] = {
			'q', 'c', 'o', 'm', ',', 's', 'u', 'n', 'p', '\0',
			'q', 'c', 'o', 'm', ',', 'x'
		};

		assert(fdt_setprop(g_blob, 0, "compatible", truncated,
				   (int)sizeof(truncated)) == 0);
	}
	expect_rejected_before_mmio(g_blob, &io);

	/* A valid multi-string Sun list with a conflicting 0xd0000000 version
	 * is rejected before any read, exactly like the single-string root. */
	build_sun("qcom,sunp", &ver_d000, 4);
	assert(fdt_setprop(g_blob, 0, "compatible", COMPAT_LIST,
			   (int)sizeof(COMPAT_LIST)) == 0);
	expect_rejected_before_mmio(g_blob, &io);

	/* 6. Malformed resource shapes are rejected. */
	/* 6a. Too few SSPP pipes. */
	build_sun("qcom,sunp", NULL, 0);
	{
		static const uint32_t fewer[8] = {
			0x5000u, 0x7000u, 0x9000u, 0xb000u,
			0x25000u, 0x27000u, 0x29000u, 0x2b000u
		};
		char t8[32];

		for (unsigned i = 0; i < 4u; ++i)
			memcpy(t8 + i * 4u, "vig", 4u);
		for (unsigned i = 0; i < 4u; ++i)
			memcpy(t8 + 16u + i * 4u, "dma", 4u);
		set_cells(g_blob, sde_node(g_blob), "qcom,sde-sspp-off",
			  fewer, 8u);
		assert(fdt_setprop(g_blob, sde_node(g_blob),
				   "qcom,sde-sspp-type", t8,
				   (int)sizeof(t8)) == 0);
	}
	expect_rejected(g_blob, &io);

	/* 6b. A real (non-dummy) LM beyond hardware slot 7. */
	build_sun("qcom,sunp", NULL, 0);
	{
		uint32_t mix[12];

		memcpy(mix, MIXER_RAW, sizeof(mix));
		mix[8] = 0x12345u;
		set_cells(g_blob, sde_node(g_blob), "qcom,sde-mixer-off",
			  mix, 12u);
	}
	expect_rejected(g_blob, &io);

	/* 6c. Blend-stage count that disagrees with the blend-op table. */
	build_sun("qcom,sunp", NULL, 0);
	assert(fdt_setprop_u32(g_blob, sde_node(g_blob),
			       "qcom,sde-mixer-blendstages", 10u) == 0);
	expect_rejected(g_blob, &io);

	/* 7. Resource bounds failures are rejected without an out-of-range
	 *    read (the existing CrDK bounds are reused). */
	/* 7a. SSPP pipe base outside the MDP window. */
	build_sun("qcom,sunp", NULL, 0);
	{
		uint32_t pipes[10];

		memcpy(pipes, SSPP_OFF, sizeof(pipes));
		pipes[0] = 0xfffff000u;
		set_cells(g_blob, sde_node(g_blob), "qcom,sde-sspp-off",
			  pipes, 10u);
	}
	expect_rejected_before_mmio(g_blob, &io);

	/* 7b. Blend-stage offset that overflows the window. */
	build_sun("qcom,sunp", NULL, 0);
	{
		uint32_t stage[11];

		memcpy(stage, STAGE_OFF, sizeof(stage));
		stage[0] = 0xfffffffcu;
		set_cells(g_blob, sde_node(g_blob),
			  "qcom,sde-mixer-blend-op-off", stage, 11u);
	}
	expect_rejected_before_mmio(g_blob, &io);

	/* 7c. MDP MMIO size smaller than the populated registers. */
	build_sun("qcom,sunp", NULL, 0);
	{
		fdt64_t small[2] = { cpu_to_fdt64(MDP_BASE), cpu_to_fdt64(0x1000u) };

		assert(fdt_setprop(g_blob, sde_node(g_blob), "reg", small,
				   (int)sizeof(small)) == 0);
	}
	expect_rejected_before_mmio(g_blob, &io);

	/* 8. An unstable SSPP snapshot or active-routing register fails the
	 *    recheck and leaves the outputs zeroed. */
	build_sun("qcom,sunp", NULL, 0);
	expect_failed_unstable(g_blob, &io, PIPE_DMA4 + 0x14u);
	build_sun("qcom,sunp", NULL, 0);
	expect_failed_unstable(g_blob, &io, MIXER_RAW[0] + 0x04u);

	/* 9. Reset to the valid tree still succeeds after the negative cases. */
	build_sun("qcom,sunp", &ver_c000, 4);
	expect_sun_success(g_blob, &io);

	/* 10. Shared LM0/LM1 routing over one VIG0/VIG1 source pair.
	 *     RED target: this positive is expected to FAIL against today's
	 *     production (active_lm rejects the second source) and is not
	 *     written to expect failure. It goes first so RED stops here. */
	build_sun("qcom,sunp", NULL, 0);
	expect_shared_lm_success(g_blob, &io);

	/* 10a-10f. One-factor negatives, each reset from the valid shared
	 * fixture. All share the same synthesized tree. */
	expect_shared_rejected("neg-noncontiguous-source", g_blob, &io,
			       mutate_shared_noncontiguous);
	expect_shared_rejected("neg-output-overlap", g_blob, &io,
			       mutate_shared_overlap);
	expect_shared_rejected("neg-output-gap", g_blob, &io,
			       mutate_shared_gap);
	expect_shared_rejected("neg-lm1-mask", g_blob, &io,
			       mutate_shared_lm1_mask);
	expect_shared_rejected("neg-lm1-stage", g_blob, &io,
			       mutate_shared_lm1_stage);
	expect_shared_rejected("neg-duplicate-selector", g_blob, &io,
			       mutate_shared_duplicate);

	/* 10g-10h. Instability negatives; the mutated word must be re-read. */
	expect_shared_unstable("neg-unstable-snapshot", g_blob, &io,
			       PIPE_VIG0 + 0x14u);
	expect_shared_unstable("neg-unstable-routing", g_blob, &io,
			       MIXER_RAW[0] + 0x04u);

	puts("tb322fc_splash_test: Sun DPU12 regression checks executed");
	return 0;
}
