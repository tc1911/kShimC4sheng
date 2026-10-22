#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <dt.h>
#include <dt_handoff.h>

static uint64_t g_out[2048];

static void expect_neg(void *rt, size_t rtsz, void *pl, size_t plsz, size_t cap)
{
	static uint64_t srt[2048], spl[2048];
	memcpy(srt, rt, rtsz);
	memcpy(spl, pl, plsz);
	int r = dt_handoff_memory(rt, rtsz, pl, plsz, g_out, cap);
	assert(r < 0);
	assert(memcmp(srt, rt, rtsz) == 0);
	assert(memcmp(spl, pl, plsz) == 0);
}

int main(void)
{
	static uint64_t rt[2048], pl[2048], out[2048], snaprt[2048], snappl[2048];
	fdt32_t reg8[8] = {
		cpu_to_fdt32(0), cpu_to_fdt32(0x80000000u),
		cpu_to_fdt32(0), cpu_to_fdt32(0x40000000u),
		cpu_to_fdt32(1), cpu_to_fdt32(0),
		cpu_to_fdt32(0), cpu_to_fdt32(0x40000000u)
	};
	fdt32_t reg16[4] = {0, 0, 0, 0};
	int off, ch, n, len;

	/* runtime tree: one /memory node, two banks */
	assert(fdt_create_empty_tree(rt, sizeof rt) == 0);
	assert(fdt_setprop_u32(rt, 0, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(rt, 0, "#size-cells", 2) == 0);
	off = fdt_add_subnode(rt, 0, "memory");
	assert(off >= 0);
	assert(fdt_setprop_string(rt, off, "device_type", "memory") == 0);
	assert(fdt_setprop(rt, off, "reg", reg8, sizeof reg8) == 0);
	off = fdt_add_subnode(rt, 0, "chosen");
	assert(off >= 0);
	assert(fdt_setprop_string(rt, off, "bootargs", "android-only") == 0);

	/* payload tree */
	assert(fdt_create_empty_tree(pl, sizeof pl) == 0);
	assert(fdt_setprop_u32(pl, 0, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(pl, 0, "#size-cells", 2) == 0);
	assert(fdt_setprop_string(pl, 0, "model", "payload-model") == 0);
	off = fdt_add_subnode(pl, 0, "memory");
	assert(off >= 0);
	assert(fdt_setprop_string(pl, off, "device_type", "memory") == 0);
	assert(fdt_setprop(pl, off, "reg", reg16, sizeof reg16) == 0);
	off = fdt_add_subnode(pl, 0, "memory@a0000000");
	assert(off >= 0);
	assert(fdt_setprop_string(pl, off, "device_type", "memory") == 0);
	assert(fdt_setprop(pl, off, "reg", reg16, sizeof reg16) == 0);
	ch = fdt_add_subnode(pl, 0, "chosen");
	assert(ch >= 0);
	assert(fdt_setprop_string(pl, ch, "bootargs", "linux-only") == 0);
	assert(fdt_setprop_string(pl, ch, "stdout-path", "serial0") == 0);
	assert(fdt_setprop_u32(pl, ch, "linux,initrd-start", 0) == 0);
	assert(fdt_setprop_u32(pl, ch, "linux,initrd-end", 0) == 0);

	/* positive case */
	memcpy(snaprt, rt, sizeof rt);
	memcpy(snappl, pl, sizeof pl);
	assert(dt_handoff_memory(rt, sizeof rt, pl, sizeof pl, out, sizeof out) == 0);
	assert(!dt_validate(out, sizeof out));
	assert(memcmp(snaprt, rt, sizeof rt) == 0);
	assert(memcmp(snappl, pl, sizeof pl) == 0);

	n = 0;
	fdt_for_each_subnode(off, out, 0) {
		const fdt32_t *rg;
		const char *dtv = fdt_getprop(out, off, "device_type", NULL);
		if (dtv && !strcmp(dtv, "memory")) {
			n++;
			rg = fdt_getprop(out, off, "reg", &len);
			assert(rg && len == (int)sizeof reg8);
			assert(memcmp(rg, reg8, sizeof reg8) == 0);
		}
	}
	assert(n == 1);

	assert(!strcmp(fdt_getprop(out, 0, "model", NULL), "payload-model"));
	ch = fdt_path_offset(out, "/chosen");
	assert(ch >= 0);
	assert(!strcmp(fdt_getprop(out, ch, "bootargs", NULL), "linux-only"));
	assert(!strcmp(fdt_getprop(out, ch, "stdout-path", NULL), "serial0"));
	assert(fdt_getprop(out, ch, "linux,initrd-start", &len) == NULL && len == -FDT_ERR_NOTFOUND);
	assert(fdt_getprop(out, ch, "linux,initrd-end", &len) == NULL && len == -FDT_ERR_NOTFOUND);

	/* zero-size placeholder banks at begin/middle/end are skipped */
	{
		static uint64_t srt[2048], spl[2048];
		fdt32_t zw[20] = {
			cpu_to_fdt32(0), cpu_to_fdt32(0xd8800000u), cpu_to_fdt32(0), cpu_to_fdt32(0),
			cpu_to_fdt32(0), cpu_to_fdt32(0x80000000u), cpu_to_fdt32(0), cpu_to_fdt32(0x40000000u),
			cpu_to_fdt32(0), cpu_to_fdt32(0xc0000000u), cpu_to_fdt32(0), cpu_to_fdt32(0),
			cpu_to_fdt32(1), cpu_to_fdt32(0), cpu_to_fdt32(0), cpu_to_fdt32(0x40000000u),
			cpu_to_fdt32(0), cpu_to_fdt32(0xe0000000u), cpu_to_fdt32(0), cpu_to_fdt32(0)
		};
		off = fdt_path_offset(rt, "/memory");
		assert(off >= 0);
		assert(fdt_setprop(rt, off, "reg", zw, sizeof zw) == 0);
		memcpy(srt, rt, sizeof rt);
		memcpy(spl, pl, sizeof pl);
		assert(dt_handoff_memory(rt, sizeof rt, pl, sizeof pl, out, sizeof out) == 0);
		assert(!dt_validate(out, sizeof out));
		assert(memcmp(srt, rt, sizeof rt) == 0);
		assert(memcmp(spl, pl, sizeof pl) == 0);
		n = 0;
		fdt_for_each_subnode(off, out, 0) {
			const fdt32_t *rg;
			const char *dtv = fdt_getprop(out, off, "device_type", NULL);
			if (dtv && !strcmp(dtv, "memory")) {
				n++;
				rg = fdt_getprop(out, off, "reg", &len);
				assert(rg && len == (int)sizeof reg8);
				assert(memcmp(rg, reg8, sizeof reg8) == 0);
			}
		}
		assert(n == 1);
		memcpy(rt, snaprt, sizeof rt);
	}

	/* negative: unsupported destination capacity */
	expect_neg(rt, sizeof rt, pl, sizeof pl, 64);

	/* negative: all-zero RAM reg */
	{
		fdt32_t zc[4] = {0, 0, 0, 0};
		off = fdt_path_offset(rt, "/memory");
		assert(off >= 0);
		assert(fdt_setprop(rt, off, "reg", zc, sizeof zc) == 0);
	}
	expect_neg(rt, sizeof rt, pl, sizeof pl, sizeof g_out);
	memcpy(rt, snaprt, sizeof rt);

	/* negative: malformed (12-byte) RAM reg */
	off = fdt_path_offset(rt, "/memory");
	assert(off >= 0);
	assert(fdt_setprop(rt, off, "reg", reg8, 12) == 0);
	expect_neg(rt, sizeof rt, pl, sizeof pl, sizeof g_out);
	memcpy(rt, snaprt, sizeof rt);

	/* negative: RAM range overflow */
	{
		fdt32_t ovf[4] = {cpu_to_fdt32(0xffffffffu), cpu_to_fdt32(0xfffff000u),
			cpu_to_fdt32(0), cpu_to_fdt32(0x2000u)};
		off = fdt_path_offset(rt, "/memory");
		assert(off >= 0);
		assert(fdt_setprop(rt, off, "reg", ovf, sizeof ovf) == 0);
	}
	expect_neg(rt, sizeof rt, pl, sizeof pl, sizeof g_out);
	memcpy(rt, snaprt, sizeof rt);

	/* negative: overlapping banks */
	{
		fdt32_t ob[8];
		memcpy(ob, reg8, sizeof ob);
		ob[4] = cpu_to_fdt32(0);
		ob[5] = cpu_to_fdt32(0x90000000u);
		ob[6] = cpu_to_fdt32(0);
		ob[7] = cpu_to_fdt32(0x40000000u);
		off = fdt_path_offset(rt, "/memory");
		assert(off >= 0);
		assert(fdt_setprop(rt, off, "reg", ob, sizeof ob) == 0);
	}
	expect_neg(rt, sizeof rt, pl, sizeof pl, sizeof g_out);
	memcpy(rt, snaprt, sizeof rt);

	/* negative: unsupported root 1/1 */
	assert(fdt_setprop_u32(pl, 0, "#address-cells", 1) == 0);
	assert(fdt_setprop_u32(pl, 0, "#size-cells", 1) == 0);
	expect_neg(rt, sizeof rt, pl, sizeof pl, sizeof g_out);
	memcpy(rt, snaprt, sizeof rt);

	printf("dt_handoff_test PASS\n");
	return 0;
}
