/* SPDX-License-Identifier: MIT
 *
 * tests/sspp_v13_test.c
 *
 * Positive regression test for the SPP/SSPP v13 splash reader.
 * Uses only the real public header (Library/cr_splash.h, which pulls in
 * Library/cr_geni.h) and the real register layout. No invented symbols.
 */
#include <Library/cr_splash.h>
#include <oskal/cr_stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_BASE   ((uintptr_t)0x9800000u)
#define TEST_SIZE   ((size_t)0x1c8800u)
#define TEST_WORDS  (0x1c8800u / 4u)

static uint32_t g_regs[TEST_WORDS];
static unsigned g_reads;
static unsigned g_unstable_reads;
static bool g_unstable_enabled;

static void fail(const char *what, uintptr_t address)
{
    fprintf(stderr, "sspp_v13_test: %s (0x%lx)\n", what, (unsigned long)address);
    abort();
}

static unsigned reg_index(uintptr_t offset)
{
    if ((offset & 0x3u) != 0u)
        fail("unaligned register offset", offset);
    if (offset >= (uintptr_t)TEST_SIZE)
        fail("register offset out of bounds", offset);
    return (unsigned)(offset / 4u);
}

static void set_reg(uintptr_t offset, uint32_t value)
{
    g_regs[reg_index(offset)] = value;
}

static bool addr_allowed(uintptr_t off)
{
    static const uintptr_t lm[2] = { 0x104000u, 0x10c000u };
    static const uintptr_t stage[11] = {
        0x40u, 0x70u, 0xa0u, 0xd0u, 0x100u, 0x130u,
        0x160u, 0x190u, 0x1c0u, 0x1f0u, 0x220u
    };
    static const uintptr_t rect[2] = { 0x2d000u, 0x36000u };
    static const uintptr_t roff[16] = {
        0x0u, 0x4u, 0x8u, 0x10u, 0x14u, 0x18u, 0x1cu, 0x20u,
        0x30u, 0x34u, 0x38u, 0x3cu, 0x4cu, 0x5cu, 0x64u, 0x74u
    };
    int m, i, r;

    /* CTL0 */
    if (off == 0x200f4u || off == 0x2012cu || off == 0x20130u)
        return true;

    /* COMMON */
    if (off == 0x2c010u || off == 0x35010u)
        return true;

    /* Layer mixers: select, control, background, stage values. */
    for (m = 0; m < 2; m++) {
        if (off == lm[m] + 0x0u)
            return true;
        if (off == lm[m] + 0x4u)
            return true;
        if (off == lm[m] + 0x14u)
            return true;
        for (i = 0; i < 11; i++) {
            if (off == lm[m] + stage[i] + 4u)
                return true;   /* stage + 4, never the stage base */
        }
    }

    /* RECT0 tables, left and right. */
    for (r = 0; r < 2; r++) {
        for (i = 0; i < 16; i++) {
            if (off == rect[r] + roff[i])
                return true;
        }
    }
    return false;
}

static uint32_t test_read32(void *context, uintptr_t address)
{
    uintptr_t off;
    uint32_t value;

    (void)context;

    if (address < TEST_BASE)
        fail("read32 below base", address);
    off = address - TEST_BASE;
    if ((off & 0x3u) != 0u)
        fail("read32 unaligned", off);
    if (off >= (uintptr_t)TEST_SIZE)
        fail("read32 out of bounds", off);
    if (!addr_allowed(off))
        fail("read32 disallowed offset", off);

    value = g_regs[reg_index(off)];
    g_reads++;

    /* The left RECT0 address register goes unstable on its second read,
     * but only while a test explicitly enables that behavior. */
    if (off == 0x2d04cu && g_unstable_enabled) {
        g_unstable_reads++;
        if (g_unstable_reads >= 2u)
            value ^= 0x4u;
    }
    return value;
}

static void test_write32(void *context, uintptr_t address, uint32_t value)
{
    (void)context;
    (void)address;
    (void)value;
    fail("unexpected write32", address);
}

static void fixture_reset(void)
{
    memset(g_regs, 0, sizeof(g_regs));
    g_reads = 0;
    g_unstable_reads = 0;
    g_unstable_enabled = false;
}

static void fixture_fill(struct CrSplashResources *r)
{
    static const uint32_t pipe[10] = {
        0x2c000u, 0x35000u, 0x3e000u, 0x47000u, 0x98000u,
        0xa1000u, 0xaa000u, 0xb3000u, 0xbc000u, 0xc5000u
    };
    static const uint32_t ctl[6] = {
        0x20000u, 0x21000u, 0x22000u, 0x23000u, 0x24000u, 0x25000u
    };
    static const uint32_t mixer[8] = {
        0x104000u, 0x10c000u, 0x114000u, 0x11c000u,
        0x124000u, 0x12c000u, 0x134000u, 0x13c000u
    };
    static const uint32_t stage[11] = {
        0x40u, 0x70u, 0xa0u, 0xd0u, 0x100u, 0x130u,
        0x160u, 0x190u, 0x1c0u, 0x1f0u, 0x220u
    };
    int i;

    memset(r, 0, sizeof(*r));
    r->base = TEST_BASE;
    r->size = TEST_SIZE;
    r->reserved_base = 0xfc800000u;
    r->reserved_size = 0x2b00000u;

    for (i = 0; i < 10; i++) {
        r->pipe[i] = pipe[i];
        r->pipe_id[i] = (uint8_t)i;
    }
    r->pipe_count = 10;

    for (i = 0; i < 6; i++)
        r->ctl[i] = ctl[i];
    r->ctl_count = 6;

    for (i = 0; i < 8; i++) {
        r->mixer[i] = mixer[i];
        r->mixer_id[i] = (uint8_t)i;
    }
    r->mixer_count = 8;
    r->lm_source_select = true;
    r->sspp_rec_separated = true;

    for (i = 0; i < 11; i++)
        r->stage[i] = stage[i];
    r->stage_count = 11;
}

static void fixture_program(void)
{
    static const uintptr_t lm[2] = { 0x104000u, 0x10c000u };
    static const uintptr_t stage[11] = {
        0x40u, 0x70u, 0xa0u, 0xd0u, 0x100u, 0x130u,
        0x160u, 0x190u, 0x1c0u, 0x1f0u, 0x220u
    };
    int m, i;

    /* CTL0 */
    set_reg(0x200f4u, 6u);
    set_reg(0x2012cu, 0x30000u);
    set_reg(0x20130u, 3u);

    /* Layer mixers. */
    for (m = 0; m < 2; m++) {
        set_reg(lm[m] + 0x0u, (m == 0) ? 0x0u : 0x80000000u);
        set_reg(lm[m] + 0x4u, 0x0be003b8u);
        set_reg(lm[m] + 0x14u, 0xc0c0u);
        for (i = 0; i < 11; i++)
            set_reg(lm[m] + stage[i] + 4u, (i == 5) ? 0x4140u : 0xc0c0u);
    }

    /* RECT0, left (0x2d000) and right (0x36000). */
    set_reg(0x2d000u + 0x0u,  0x237ffu);
    set_reg(0x36000u + 0x0u,  0x237ffu);
    set_reg(0x2d000u + 0x4u,  0x03020001u);
    set_reg(0x36000u + 0x4u,  0x03020001u);
    set_reg(0x2d000u + 0x8u,  0u);
    set_reg(0x36000u + 0x8u,  0u);
    set_reg(0x2d000u + 0x10u, 0u);
    set_reg(0x36000u + 0x10u, 0u);
    set_reg(0x2d000u + 0x14u, 0x0be003b8u);
    set_reg(0x36000u + 0x14u, 0x0be003b8u);
    set_reg(0x2d000u + 0x18u, 0u);
    set_reg(0x36000u + 0x18u, 952u);
    set_reg(0x2d000u + 0x1cu, 0x0be003b8u);
    set_reg(0x36000u + 0x1cu, 0x0be003b8u);
    set_reg(0x2d000u + 0x20u, 0u);
    set_reg(0x36000u + 0x20u, 952u);
    set_reg(0x2d000u + 0x30u, 0u);
    set_reg(0x36000u + 0x30u, 0u);
    set_reg(0x2d000u + 0x34u, 0u);
    set_reg(0x36000u + 0x34u, 0u);
    set_reg(0x2d000u + 0x38u, 0u);
    set_reg(0x36000u + 0x38u, 0u);
    set_reg(0x2d000u + 0x3cu, 0u);
    set_reg(0x36000u + 0x3cu, 0u);
    set_reg(0x2d000u + 0x4cu, 0xfc800000u);
    set_reg(0x36000u + 0x4cu, 0xfc800000u);
    set_reg(0x2d000u + 0x5cu, 7616u);
    set_reg(0x36000u + 0x5cu, 7616u);
    set_reg(0x2d000u + 0x64u, 0xfc800000u);
    set_reg(0x36000u + 0x64u, 0xfc800000u);
    set_reg(0x2d000u + 0x74u, 0u);
    set_reg(0x36000u + 0x74u, 0u);

    /* COMMON + 0x10 is left zero by the reset. */
}

static void io_bind(struct CrIo *io)
{
    memset(io, 0, sizeof(*io));
    io->context = NULL;
    io->read32 = test_read32;
    io->write32 = test_write32;
}

int main(void)
{
    struct CrSplashResources resources;
    struct CrSplash splash, snapshot;
    struct CrIo io;
    int rc;
    unsigned reads_before;

    io_bind(&io);

    /* --- 1. positive fixture ----------------------------------------- */
    fixture_reset();
    fixture_fill(&resources);
    fixture_program();

    if (g_reads != 0u)
        fail("reads performed before CrSplashRead", (uintptr_t)0u);

    memset(&splash, 0, sizeof(splash));
    rc = CrSplashRead(&resources, &io, &splash);
    if (rc != 0) {
        fprintf(stderr, "positive: unexpected rc %d\n", rc);
        return 1;
    }
    if (splash.address != (uintptr_t)0xfc800000u ||
        splash.width != 1904u || splash.height != 3040u ||
        splash.stride != 7616u || splash.size != (size_t)23152640u ||
        splash.format != CR_SPLASH_ARGB8888) {
        fprintf(stderr,
                "positive: bad splash addr=0x%lx %ux%u stride=%u size=%lu fmt=%d\n",
                (unsigned long)splash.address, splash.width, splash.height,
                splash.stride, (unsigned long)splash.size, (int)splash.format);
        return 1;
    }
    /* Every MMIO stayed inside [base, base + size): any disallowed or
     * out-of-range access would already have aborted in read32. */
    if (g_reads == 0u)
        fail("positive: no MMIO observed", (uintptr_t)0u);

    /* --- 2. right OUT_XY overlaps the left panel by one pixel --------- */
    fixture_reset();
    fixture_fill(&resources);
    fixture_program();
    set_reg(0x36000u + 0x20u, 951u);   /* right x = 951 -> 952 - 951 = 1 pixel overlap */

    memset(&splash, 0x5a, sizeof(splash));
    memcpy(&snapshot, &splash, sizeof(splash));

    rc = CrSplashRead(&resources, &io, &splash);
    if (rc == 0) {
        fprintf(stderr, "overlap: expected rejection, got success\n");
        return 1;
    }
    if (memcmp(&splash, &snapshot, sizeof(splash)) != 0) {
        fprintf(stderr, "overlap: output modified on rejection\n");
        return 1;
    }

    /* A rejection must not poison the reader: the positive fixture
     * still resolves after the failed call. */
    fixture_reset();
    fixture_fill(&resources);
    fixture_program();
    memset(&splash, 0, sizeof(splash));
    rc = CrSplashRead(&resources, &io, &splash);
    if (rc != 0) {
        fprintf(stderr, "restore: positive after negative rc=%d\n", rc);
        return 1;
    }

    /* --- 3. unstable left address register ---------------------------- */
    fixture_reset();
    fixture_fill(&resources);
    fixture_program();
    /* 0x2d04c (left RECT0 address) flips on the second read. */
    g_unstable_enabled = true;

    memset(&splash, 0, sizeof(splash));
    rc = CrSplashRead(&resources, &io, &splash);
    if (rc != CR_BUS_AGAIN) {
        fprintf(stderr, "unstable: expected CR_BUS_AGAIN, got %d\n", rc);
        return 1;
    }

    /* --- 4. out-of-range pipe base, no MMIO --------------------------- */
    fixture_reset();
    fixture_fill(&resources);
    resources.pipe[0] = 0xfffff000u;
    fixture_program();

    reads_before = g_reads;
    memset(&splash, 0, sizeof(splash));
    rc = CrSplashRead(&resources, &io, &splash);
    if (rc == 0) {
        fprintf(stderr, "pipe bounds: expected rejection, got success\n");
        return 1;
    }
    if (g_reads != reads_before)
        fail("pipe bounds: MMIO performed for invalid pipe", (uintptr_t)0u);

    /* --- 5. compact negative table ------------------------------------ *
     * Each row mutates exactly one register of an otherwise valid
     * fixture. The reader must reject and leave the caller's output
     * untouched; the strict MMIO allowlist keeps the reader honest. */
    {
        static const struct {
            uintptr_t off;
            uint32_t  value;
        } neg[] = {
            { 0x36020u,  0x3b9u      },  /* gap in right crop */
            { 0x36018u,  0x3b7u      },  /* non-contiguous crop */
            { 0x2d074u,  0x1u        },  /* secure overlay */
            { 0x2d008u,  0x8000u     },  /* rotation */
            { 0x2d008u,  0x0008u     },  /* op bit3 set: must reject */
            { 0x36004u,  0x03010002u },  /* right unpack different color order: must reject */
            { 0x2d000u,  0x400237ffu },  /* format bit 30 */
            { 0x2d030u,  0x1u        },  /* ODX */
            { 0x2c010u,  0x1u        },  /* multirect */
            { 0x2012cu,  0x10000u    },  /* missing pipe */
            { 0x104104u, 0xc040u     },  /* extra stage */
            { 0x10c134u, 0xc040u     },  /* missing paired source */
            { 0x104134u, 0x4150u     },  /* rect1 */
            { 0x200f4u,  0x0u        },  /* primary inactive */
        };
        size_t i;

        for (i = 0; i < sizeof(neg) / sizeof(neg[0]); i++) {
            fixture_reset();
            fixture_fill(&resources);
            fixture_program();
            set_reg(neg[i].off, neg[i].value);

            memset(&splash, 0x5a, sizeof(splash));
            memcpy(&snapshot, &splash, sizeof(splash));

            rc = CrSplashRead(&resources, &io, &splash);
            if (rc == 0) {
                fprintf(stderr, "neg[%lu]: expected rejection, got success\n",
                        (unsigned long)i);
                return 1;
            }
            if (memcmp(&splash, &snapshot, sizeof(splash)) != 0) {
                fprintf(stderr, "neg[%lu]: output modified on rejection\n",
                        (unsigned long)i);
                return 1;
            }
        }
    }

    /* --- 6. reserved region one byte too small ------------------------ */
    fixture_reset();
    fixture_fill(&resources);
    resources.reserved_size = 23152639u;
    fixture_program();

    memset(&splash, 0x5a, sizeof(splash));
    memcpy(&snapshot, &splash, sizeof(splash));
    rc = CrSplashRead(&resources, &io, &splash);
    if (rc == 0) {
        fprintf(stderr, "reserved bounds: expected rejection, got success\n");
        return 1;
    }
    if (memcmp(&splash, &snapshot, sizeof(splash)) != 0) {
        fprintf(stderr, "reserved bounds: output modified on rejection\n");
        return 1;
    }

    /* --- 7. final positive, exact framebuffer ------------------------- */
    fixture_reset();
    fixture_fill(&resources);
    fixture_program();

    memset(&splash, 0, sizeof(splash));
    rc = CrSplashRead(&resources, &io, &splash);
    if (rc != 0) {
        fprintf(stderr, "final positive: unexpected rc %d\n", rc);
        return 1;
    }
    if (splash.address != (uintptr_t)0xfc800000u ||
        splash.width != 1904u || splash.height != 3040u ||
        splash.stride != 7616u || splash.size != (size_t)23152640u ||
        splash.format != CR_SPLASH_ARGB8888) {
        fprintf(stderr,
                "final positive: bad splash addr=0x%lx %ux%u stride=%u size=%lu fmt=%d\n",
                (unsigned long)splash.address, splash.width, splash.height,
                splash.stride, (unsigned long)splash.size, (int)splash.format);
        return 1;
    }

    printf("sspp_v13_test: all positive/negative checks passed\n");
    return 0;
}
