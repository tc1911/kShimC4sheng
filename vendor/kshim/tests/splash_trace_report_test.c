/* SPDX-License-Identifier: MIT */
/*
 * Standalone regression for kshim_splash_trace_report().
 *
 * Deliberately independent of the implementation: the compact string is an
 * exact literal, and it is decoded back into the original 15 values using
 * local slice + strtoul helpers. The function under test is never used as
 * an oracle.
 */
#include "qcom_dt.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Decode exactly n hex characters at p; independent of the implementation. */
static uint32_t hex_field(const char *p, int n)
{
	char tmp[9];

	assert(n > 0 && n < (int)sizeof(tmp));
	memcpy(tmp, p, (size_t)n);
	tmp[n] = '\0';
	return (uint32_t)strtoul(tmp, NULL, 16);
}

/* Build the compact-eligible trace and mirror its 15 values. */
static void fill_compact(struct kshim_splash_trace *t, uint32_t mirror[15])
{
	static const uint32_t ctl[3] = {
		0x12345678u, 0x89abcdefu, 0xfeed0003u
	};
	static const uint32_t sel[12] = {
		0xc0c0u, 0xc004u, 0x0040u, 0x1234u, 0x00ffu, 0xc0c0u,
		0x0001u, 0xabcdu, 0x0040u, 0x0100u, 0x00c0u, 0x0c0cu
	};
	int i;

	memset(t, 0, sizeof(*t));
	t->count = 15;
	t->last_offset = 0x104224u;
	for (i = 0; i < 3; i++) {
		t->first[i] = ctl[i];
		mirror[i] = ctl[i];
	}
	for (i = 0; i < 12; i++) {
		t->first[3 + i] = sel[i];
		mirror[3 + i] = sel[i];
	}
}

#define COMPACT_STR \
	"r14:1234567889abcdeffeed0003" \
	"c0c0c0040040123400ffc0c00001abcd0040010000c00c0c"

static void test_compact(void)
{
	struct kshim_splash_trace t;
	uint32_t mirror[15];
	char buf[128];
	int n, i;

	fill_compact(&t, mirror);

	memset(buf, 0x5a, sizeof(buf));
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));

	assert(n == 76);
	assert(strlen(buf) == 76);
	assert(buf[76] == '\0');
	assert(memcmp(buf, "r14:", 4) == 0);
	assert(strcmp(buf, COMPACT_STR) == 0);

	/* Reverse decode: 3 x 08x CTL words, then 12 x 04x selectors. */
	for (i = 0; i < 3; i++)
		assert(hex_field(buf + 4 + i * 8, 8) == mirror[i]);
	for (i = 0; i < 12; i++)
		assert(hex_field(buf + 4 + 24 + i * 4, 4) == mirror[3 + i]);

	/* Sun variant (last_offset 0x45224) reuses the exact same compact body. */
	fill_compact(&t, mirror);
	t.last_offset = 0x45224u;
	memset(buf, 0x5a, sizeof(buf));
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));
	assert(n == 76);
	assert(strlen(buf) == 76);
	assert(strcmp(buf, COMPACT_STR) == 0);
}

static void test_wide(void)
{
	struct kshim_splash_trace t;
	uint32_t mirror[15];
	char buf[128];
	int n;

	fill_compact(&t, mirror);
	/* First selector slot (array index 9) carries non-zero high bits. */
	t.first[9] = 0x0001c0c0u;

	memset(buf, 0x5a, sizeof(buf));
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));

	assert(strcmp(buf, "r14:wide:i=9:v=1c0c0") == 0);
	assert(n == (int)strlen("r14:wide:i=9:v=1c0c0"));
	assert(buf[n] == '\0');

	/* Sun variant (last_offset 0x45224) yields the identical wide form. */
	fill_compact(&t, mirror);
	t.first[9] = 0x0001c0c0u;
	t.last_offset = 0x45224u;
	memset(buf, 0x5a, sizeof(buf));
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));
	assert(strcmp(buf, "r14:wide:i=9:v=1c0c0") == 0);
	assert(n == (int)strlen("r14:wide:i=9:v=1c0c0"));
	assert(buf[n] == '\0');
}

static void test_gate(void)
{
	struct kshim_splash_trace t;
	uint32_t mirror[15];
	char buf[128];
	int n;

	/* Correct trace reaches the compact form (gate sanity). */
	fill_compact(&t, mirror);
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));
	assert(n == 76);

	/* Wrong status falls back; untouched ring is all zero. */
	fill_compact(&t, mirror);
	n = kshim_splash_trace_report(&t, 0, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=0:n=15:o=104224:v=0,0,0,0,0") == 0);
	assert(n == (int)strlen("r14:s=0:n=15:o=104224:v=0,0,0,0,0"));

	/* Wrong count falls back. */
	fill_compact(&t, mirror);
	t.count = 14;
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=-95:n=14:o=104224:v=0,0,0,0,0") == 0);

	/* Wrong last_offset falls back. */
	fill_compact(&t, mirror);
	t.last_offset = 0x104225u;
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=-95:n=15:o=104225:v=0,0,0,0,0") == 0);

	/* Sun other-offset (0x45225) also stays on the ordinary ring fallback. */
	fill_compact(&t, mirror);
	t.last_offset = 0x45225u;
	n = kshim_splash_trace_report(&t, -95, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=-95:n=15:o=45225:v=0,0,0,0,0") == 0);
}

static void test_fallback(void)
{
	struct kshim_splash_trace t;
	char buf[128];
	int n;

	/* Stable fallback: normal status, full count, no compact gate. */
	memset(&t, 0, sizeof(t));
	t.count = 116;
	t.last_offset = 0x250f4u;
	n = kshim_splash_trace_report(&t, 0, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=0:n=116:o=250f4:v=0,0,0,0,0") == 0);
	assert(n == (int)strlen("r14:s=0:n=116:o=250f4:v=0,0,0,0,0"));

	/* Ring chronological reconstruction, count wraps past the 5 slots. */
	memset(&t, 0, sizeof(t));
	t.count = 7;
	t.last_offset = 0x250f4u;
	t.values[0] = 1u;
	t.values[1] = 2u;
	t.values[2] = 3u;
	t.values[3] = 4u;
	t.values[4] = 5u;
	n = kshim_splash_trace_report(&t, 0, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=0:n=7:o=250f4:v=3,4,5,1,2") == 0);
	assert(n == (int)strlen("r14:s=0:n=7:o=250f4:v=3,4,5,1,2"));

	/* Missing reads: zero trace stays on the fallback path. */
	memset(&t, 0, sizeof(t));
	n = kshim_splash_trace_report(&t, 0, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=0:n=0:o=0:v=0,0,0,0,0") == 0);
	assert(n == (int)strlen("r14:s=0:n=0:o=0:v=0,0,0,0,0"));
}

static void test_bounds(void)
{
	struct kshim_splash_trace t;
	uint32_t mirror[15];
	char buf[160];
	int n;
	size_t i;

	fill_compact(&t, mirror);

	/* capacity < 81 is rejected with no writes. */
	memset(buf, 0x5a, sizeof(buf));
	assert(kshim_splash_trace_report(&t, -95, buf, 80) == -1);
	for (i = 0; i < sizeof(buf); i++)
		assert((unsigned char)buf[i] == 0x5a);

	/* NULL trace is rejected with no writes. */
	memset(buf, 0x5a, sizeof(buf));
	assert(kshim_splash_trace_report(NULL, -95, buf, sizeof(buf)) == -1);
	for (i = 0; i < sizeof(buf); i++)
		assert((unsigned char)buf[i] == 0x5a);

	/* NULL output is rejected. */
	assert(kshim_splash_trace_report(&t, -95, NULL, sizeof(buf)) == -1);

	/* capacity 81: exactly 76 chars + NUL, sentinels beyond untouched. */
	memset(buf, 0x5a, sizeof(buf));
	n = kshim_splash_trace_report(&t, -95, buf, 81);
	assert(n == 76);
	assert(buf[76] == '\0');
	for (i = 81; i < sizeof(buf); i++)
		assert((unsigned char)buf[i] == 0x5a);
}

/* New DT-stage failure format and its gates. */
static void test_dt_failure(void)
{
	struct kshim_splash_trace t;
	char buf[128];
	int n;
	size_t i;

	/* Exact new failure format: stage 2, root count 1, sunp present. */
	memset(&t, 0, sizeof(t));
	t.dt_stage = 2;
	t.root_compatible_count = 1;
	t.root_has_sunp = true;
	memset(buf, 0x5a, sizeof(buf));
	n = kshim_splash_trace_report(&t, -1, buf, sizeof(buf));
	assert(strcmp(buf, "r14:dt=2:cc=1:sun=1") == 0);
	assert(n == (int)strlen("r14:dt=2:cc=1:sun=1"));
	assert(buf[n] == '\0');

	/* A negative compatible count (malformed list) keeps its sign. */
	memset(&t, 0, sizeof(t));
	t.dt_stage = 1;
	t.root_compatible_count = -11;
	t.root_has_sunp = false;
	n = kshim_splash_trace_report(&t, -1, buf, sizeof(buf));
	assert(strcmp(buf, "r14:dt=1:cc=-11:sun=0") == 0);
	assert(n == (int)strlen("r14:dt=1:cc=-11:sun=0"));

	/* A two-digit stage still stays within 80 characters. */
	memset(&t, 0, sizeof(t));
	t.dt_stage = 11;
	t.root_compatible_count = 5;
	t.root_has_sunp = true;
	n = kshim_splash_trace_report(&t, -1, buf, sizeof(buf));
	assert(strcmp(buf, "r14:dt=11:cc=5:sun=1") == 0);
	assert(n == (int)strlen("r14:dt=11:cc=5:sun=1"));

	/* capacity 80 is rejected with no writes. */
	memset(&t, 0, sizeof(t));
	t.dt_stage = 2;
	t.root_compatible_count = 1;
	t.root_has_sunp = true;
	memset(buf, 0x5a, sizeof(buf));
	assert(kshim_splash_trace_report(&t, -1, buf, 80) == -1);
	for (i = 0; i < sizeof(buf); i++)
		assert((unsigned char)buf[i] == 0x5a);

	/* capacity 81 succeeds; only the string + NUL are written. */
	memset(buf, 0x5a, sizeof(buf));
	n = kshim_splash_trace_report(&t, -1, buf, 81);
	assert(n == (int)strlen("r14:dt=2:cc=1:sun=1"));
	assert(strcmp(buf, "r14:dt=2:cc=1:sun=1") == 0);
	assert(buf[n] == '\0');
	for (i = (size_t)n + 1u; i < sizeof(buf); i++)
		assert((unsigned char)buf[i] == 0x5a);

	/* Gate: a non-empty ring keeps the legacy form even at status -1. */
	memset(&t, 0, sizeof(t));
	t.dt_stage = 2;
	t.root_compatible_count = 1;
	t.root_has_sunp = true;
	t.count = 15;
	n = kshim_splash_trace_report(&t, -1, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=-1:n=15:o=0:v=0,0,0,0,0") == 0);
	assert(n == (int)strlen("r14:s=-1:n=15:o=0:v=0,0,0,0,0"));

	/* Gate: status other than -1 keeps the legacy form. */
	memset(&t, 0, sizeof(t));
	t.dt_stage = 2;
	t.root_compatible_count = 1;
	t.root_has_sunp = true;
	n = kshim_splash_trace_report(&t, 0, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=0:n=0:o=0:v=0,0,0,0,0") == 0);

	/* status -1 with stage 0 keeps the exact legacy fallback string. */
	memset(&t, 0, sizeof(t));
	n = kshim_splash_trace_report(&t, -1, buf, sizeof(buf));
	assert(strcmp(buf, "r14:s=-1:n=0:o=0:v=0,0,0,0,0") == 0);
	assert(n == (int)strlen("r14:s=-1:n=0:o=0:v=0,0,0,0,0"));
}

int main(void)
{
	test_compact();
	test_wide();
	test_gate();
	test_fallback();
	test_bounds();
	test_dt_failure();
	puts("splash_trace_report_test: ok");
	return 0;
}
