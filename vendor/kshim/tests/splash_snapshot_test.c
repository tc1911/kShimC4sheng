/* SPDX-License-Identifier: MIT */
#include <qcom_dt.h>
#include <Library/cr_splash.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define EXP_COUNT 63u

struct snapshot_fixture {
    uintptr_t base;
    uint32_t offsets[EXP_COUNT];
    unsigned reads;
    long mutation;
};

static uint32_t fixture_read32(void *context, uintptr_t address)
{
    struct snapshot_fixture *fx = context;
    unsigned idx = fx->reads % EXP_COUNT;
    assert(address == (uintptr_t)(fx->base + fx->offsets[idx]));
    uint32_t value;
    if (idx == 0u) value = 6u;
    else if (idx == 1u) value = 0x30000u;
    else if (idx == 2u) value = 3u;
    else value = 0x10000000u + fx->offsets[idx];
    if (fx->mutation >= 0 && (long)fx->reads == fx->mutation) value ^= 0x1u;
    ++fx->reads;
    return value;
}

static void fixture_write32(void *context, uintptr_t address, uint32_t value)
{
    (void)context;
    (void)address;
    (void)value;
    assert(0);
}

static void init_resources(struct CrSplashResources *r)
{
    static const uint32_t ctl[6] = {
        0x20000u, 0x21000u, 0x22000u, 0x23000u, 0x24000u, 0x25000u
    };
    static const uint32_t lm[8] = {
        0x104000u, 0x10c000u, 0x114000u, 0x11c000u,
        0x124000u, 0x12c000u, 0x134000u, 0x13c000u
    };
    static const uint32_t pipes[10] = {
        0x2c000u, 0x35000u, 0x3e000u, 0x47000u, 0x98000u,
        0xa1000u, 0xaa000u, 0xb3000u, 0xbc000u, 0xc5000u
    };
    static const uint32_t stage[11] = {
        0x40u, 0x70u, 0xa0u, 0xd0u, 0x100u, 0x130u,
        0x160u, 0x190u, 0x1c0u, 0x1f0u, 0x220u
    };
    memset(r, 0, sizeof(*r));
    r->base = 0x9800000u;
    r->size = 0x1c8800u;
    r->reserved_base = 0x9800000u;
    r->reserved_size = 0x1c8800u;
    r->lm_source_select = true;
    r->ctl_count = 6;
    for (unsigned i = 0; i < 6; ++i) r->ctl[i] = ctl[i];
    r->mixer_count = 8;
    for (unsigned i = 0; i < 8; ++i) {
        r->mixer[i] = lm[i];
        r->mixer_id[i] = (uint8_t)i;
    }
    r->pipe_count = 10;
    for (unsigned i = 0; i < 10; ++i) {
        r->pipe[i] = pipes[i];
        r->pipe_id[i] = (uint8_t)i;
    }
    r->stage_count = 11;
    for (unsigned i = 0; i < 11; ++i) r->stage[i] = stage[i];
}

static void load_expected(uint32_t out[EXP_COUNT])
{
    static const uint32_t literal[EXP_COUNT] = {
        0x200f4u, 0x2012cu, 0x20130u,
        0x104000u, 0x104004u, 0x104014u, 0x104044u, 0x104074u,
        0x1040a4u, 0x1040d4u, 0x104104u, 0x104134u, 0x104164u,
        0x104194u, 0x1041c4u, 0x1041f4u, 0x104224u,
        0x10c000u, 0x10c004u, 0x10c014u, 0x10c044u, 0x10c074u,
        0x10c0a4u, 0x10c0d4u, 0x10c104u, 0x10c134u, 0x10c164u,
        0x10c194u, 0x10c1c4u, 0x10c1f4u, 0x10c224u,
        0x2d000u, 0x2d004u, 0x2d008u, 0x2d010u, 0x2d014u, 0x2d018u,
        0x2d01cu, 0x2d020u, 0x2d030u, 0x2d034u, 0x2d038u, 0x2d03cu,
        0x2d04cu, 0x2d05cu, 0x2d064u, 0x2d074u,
        0x36000u, 0x36004u, 0x36008u, 0x36010u, 0x36014u, 0x36018u,
        0x3601cu, 0x36020u, 0x36030u, 0x36034u, 0x36038u, 0x3603cu,
        0x3604cu, 0x3605cu, 0x36064u, 0x36074u
    };
    memcpy(out, literal, sizeof(literal));
}

static uint32_t expected_word(const uint32_t *exp, unsigned idx)
{
    if (idx == 0u) return 6u;
    if (idx == 1u) return 0x30000u;
    if (idx == 2u) return 3u;
    return 0x10000000u + exp[idx];
}

static void fixture_reset(struct snapshot_fixture *fx, const uint32_t *exp,
                          long mutation)
{
    memcpy(fx->offsets, exp, EXP_COUNT * sizeof(uint32_t));
    fx->reads = 0;
    fx->mutation = mutation;
}

static void expect_invalid(const struct CrSplashResources *r,
                           struct snapshot_fixture *fx, struct CrIo *io,
                           const uint32_t *exp)
{
    fixture_reset(fx, exp, -1);
    kshim_splash_snapshot_capture(r, io);
    const struct kshim_splash_snapshot *s = kshim_splash_snapshot_get();
    assert(s != NULL);
    assert(!s->valid);
    assert(fx->reads == 0u);
    for (unsigned i = 0; i < 68; ++i) assert(s->words[i] == 0u);
}

static void check_report(const uint32_t *exp, unsigned want_stable)
{
    const struct kshim_splash_snapshot *s = kshim_splash_snapshot_get();
    assert(s != NULL);
    assert(s->valid);
    assert((unsigned)s->stable == want_stable);

    char buf[160];
    for (unsigned slot = 0; slot <= 7; ++slot) {
        memset(buf, 0xAA, sizeof(buf));
        int n = kshim_splash_snapshot_report(s, slot, buf, sizeof(buf));
        assert(n == 72);
        assert(strlen(buf) == 72);
        char pre[16];
        int pn = snprintf(pre, sizeof(pre), "r18:%u:%u:", slot, want_stable);
        assert(pn == 8);
        assert(memcmp(buf, pre, 8) == 0);
        for (unsigned i = 0; i < 8; ++i) {
            unsigned idx = slot * 8u + i;
            uint32_t w = idx < EXP_COUNT ? expected_word(exp, idx) : 0u;
            char hex[9];
            int hn = snprintf(hex, sizeof(hex), "%08x", (unsigned)w);
            assert(hn == 8);
            assert(memcmp(buf + 8 + i * 8, hex, 8) == 0);
        }
    }
}

int main(void)
{
    struct CrSplashResources r;
    init_resources(&r);
    uint32_t exp[EXP_COUNT];
    load_expected(exp);

    struct snapshot_fixture fx;
    memset(&fx, 0, sizeof(fx));
    fx.base = (uintptr_t)r.base;
    fx.mutation = -1;

    struct CrIo io;
    memset(&io, 0, sizeof(io));
    io.context = &fx;
    io.read32 = fixture_read32;
    io.write32 = fixture_write32;

    const struct kshim_splash_snapshot *s;

    init_resources(&r);
    fixture_reset(&fx, exp, -1);
    kshim_splash_snapshot_capture(&r, &io);
    s = kshim_splash_snapshot_get();
    assert(s != NULL);
    assert(s->valid);
    assert(s->stable);
    assert(fx.reads == 126u);
    for (unsigned i = 0; i < EXP_COUNT; ++i)
        assert(s->words[i] == expected_word(exp, i));
    for (unsigned i = EXP_COUNT; i < 68; ++i) assert(s->words[i] == 0u);
    check_report(exp, 1u);

    fixture_reset(&fx, exp, 125);
    kshim_splash_snapshot_capture(&r, &io);
    s = kshim_splash_snapshot_get();
    assert(s->valid);
    assert(!s->stable);
    assert(fx.reads == 126u);
    for (unsigned i = 0; i < EXP_COUNT; ++i)
        assert(s->words[i] == expected_word(exp, i));
    check_report(exp, 0u);

    for (unsigned gate = 0; gate < 3; ++gate) {
        init_resources(&r);
        fixture_reset(&fx, exp, (long)gate);
        kshim_splash_snapshot_capture(&r, &io);
        s = kshim_splash_snapshot_get();
        assert(!s->valid);
        assert(fx.reads == gate + 1u);

        init_resources(&r);
        fixture_reset(&fx, exp, (long)(63u + gate));
        kshim_splash_snapshot_capture(&r, &io);
        s = kshim_splash_snapshot_get();
        assert(s->valid);
        assert(!s->stable);
        assert(fx.reads == 64u + gate);
        for (unsigned i = 0; i < EXP_COUNT; ++i)
            assert(s->words[i] == expected_word(exp, i));
    }

    init_resources(&r);
    r.ctl_count = 5;
    expect_invalid(&r, &fx, &io, exp);

    init_resources(&r);
    r.mixer_id[0] = 1;
    expect_invalid(&r, &fx, &io, exp);

    init_resources(&r);
    r.pipe_id[0] = 1;
    expect_invalid(&r, &fx, &io, exp);

    init_resources(&r);
    r.base = (uint64_t)UINTPTR_MAX;
    expect_invalid(&r, &fx, &io, exp);

    init_resources(&r);
    r.base = 0x9800001u;
    expect_invalid(&r, &fx, &io, exp);

    init_resources(&r);
    r.stage[10] = 0xFFFFFFF0u;
    expect_invalid(&r, &fx, &io, exp);

    init_resources(&r);
    r.pipe[0] = 0xFFFFF000u;
    expect_invalid(&r, &fx, &io, exp);

    init_resources(&r);
    fixture_reset(&fx, exp, -1);
    kshim_splash_snapshot_capture(&r, &io);
    s = kshim_splash_snapshot_get();
    assert(s->valid);
    assert(s->stable);

    unsigned char guard[128];
    memset(guard, 0xAA, sizeof(guard));
    assert(kshim_splash_snapshot_report(s, 0, (char *)guard, 80) == -1);
    for (unsigned i = 0; i < sizeof(guard); ++i) assert(guard[i] == 0xAAu);

    memset(guard, 0xAA, sizeof(guard));
    assert(kshim_splash_snapshot_report(s, 8, (char *)guard, sizeof(guard)) == -1);
    for (unsigned i = 0; i < sizeof(guard); ++i) assert(guard[i] == 0xAAu);

    memset(guard, 0xAA, sizeof(guard));
    assert(kshim_splash_snapshot_report(s, 0, NULL, sizeof(guard)) == -1);

    struct kshim_splash_snapshot unready;
    memset(&unready, 0, sizeof(unready));
    unready.valid = false;
    memset(guard, 0xAA, sizeof(guard));
    assert(kshim_splash_snapshot_report(&unready, 0, (char *)guard, sizeof(guard)) == -1);
    for (unsigned i = 0; i < sizeof(guard); ++i) assert(guard[i] == 0xAAu);

    return 0;
}
