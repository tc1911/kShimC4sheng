#include "dt.h"
#include "dt_handoff.h"
#include <string.h>
#include <stdint.h>

static bool is_ddr_region(const char *name)
{
	const char *p;

	if (!name)
		return false;
	if (strncmp(name, "region", 6) != 0)
		return false;
	p = name + 6;
	if (*p < '0' || *p > '9')
		return false;
	while (*p >= '0' && *p <= '9')
		p++;
	return *p == '\0';
}

static int copy_ddr_regions(const void *source, void *dest, int dest_parent)
{
	int srcoff, ddr_dest = -1, err, propoff;

	srcoff = fdt_path_offset(source, "/ddr-regions");
	if (srcoff < 0)
		return (srcoff == -FDT_ERR_NOTFOUND) ? 0 : srcoff;

	fdt_for_each_property_offset(propoff, source, srcoff) {
		const char *pname;
		const void *pval;
		int plen;

		pval = fdt_getprop_by_offset(source, propoff, &pname, &plen);
		if (!pval)
			return plen;
		if (!is_ddr_region(pname))
			continue;
		if (plen <= 0 || (plen % 4) != 0)
			return -FDT_ERR_BADVALUE;
		if (ddr_dest < 0) {
			err = fdt_add_subnode(dest, dest_parent, "ddr-regions");
			if (err < 0)
				return err;
			ddr_dest = err;
		}
		err = fdt_setprop(dest, ddr_dest, pname, pval, plen);
		if (err < 0)
			return err;
	}
	return 0;
}

int dt_memory_capture(const void *runtime, size_t runtime_size,
		      void *saved, size_t capacity)
{
	uint64_t empty[64];
	fdt32_t ddr_type;
	const char *name;
	const fdt32_t *prop;
	int plen, err, root, node, memoff;
	bool have_ddr = false;

	err = fdt_create_empty_tree(empty, sizeof(empty));
	if (err < 0)
		return err;
	err = fdt_setprop_cell(empty, 0, "#address-cells", 2);
	if (err < 0)
		return err;
	err = fdt_setprop_cell(empty, 0, "#size-cells", 2);
	if (err < 0)
		return err;

	err = dt_handoff_memory(runtime, runtime_size, empty, sizeof(empty),
				saved, capacity);
	if (err < 0)
		return err;

	err = fdt_open_into(saved, saved, capacity);
	if (err < 0)
		return err;

	root = fdt_path_offset(runtime, "/");
	if (root < 0)
		return root;

	fdt_for_each_subnode(node, runtime, root) {
		name = dt_string(runtime, node, "device_type");
		if (!name || strcmp(name, "memory") != 0)
			continue;
		if (!dt_available(runtime, node))
			continue;
		prop = fdt_getprop(runtime, node, "ddr_device_type", &plen);
		if (!prop)
			continue;
		if (plen != 4)
			return -FDT_ERR_BADVALUE;
		{
			fdt32_t val;

			memcpy(&val, prop, sizeof(val));
			if (have_ddr &&
			    memcmp(&val, &ddr_type, sizeof(val)) != 0)
				return -FDT_ERR_BADVALUE;
			ddr_type = val;
			have_ddr = true;
		}
	}

	if (have_ddr) {
		memoff = fdt_path_offset(saved, "/memory");
		if (memoff < 0)
			return memoff;
		err = fdt_setprop(saved, memoff, "ddr_device_type",
				  &ddr_type, 4);
		if (err < 0)
			return err;
	}

	err = copy_ddr_regions(runtime, saved, 0);
	if (err < 0)
		return err;

	err = fdt_pack(saved);
	if (err < 0)
		return err;

	return dt_validate(saved, capacity);
}

int dt_handoff_reservations_overlay(const void *runtime, void *output, void *overlay);

#define DT_MEMORY_APPLY_MAX (2u * 1024u * 1024u)

static bool valid_range(const void *p, size_t len)
{
	uintptr_t base = (uintptr_t)p;

	if (!p)
		return false;
	return base <= (uintptr_t)UINTPTR_MAX - len;
}

static bool overlap(const void *a, size_t alen, const void *b, size_t blen)
{
	uintptr_t ap = (uintptr_t)a;
	uintptr_t bp = (uintptr_t)b;

	return ap < bp + blen && bp < ap + alen;
}

static int check_image(const void *img, size_t len)
{
	if (!valid_range(img, len))
		return -FDT_ERR_BADVALUE;
	return dt_validate(img, len);
}

static int check_cells(const void *fdt)
{
	uint32_t ac, sc;
	int root, err;

	root = fdt_path_offset(fdt, "/");
	if (root < 0)
		return root;
	err = dt_u32(fdt, root, "#address-cells", &ac);
	if (err < 0)
		return err;
	err = dt_u32(fdt, root, "#size-cells", &sc);
	if (err < 0)
		return err;
	if (ac != 2 || sc != 2)
		return -FDT_ERR_BADVALUE;
	return 0;
}

static int check_memory(const void *saved)
{
	const fdt32_t *reg;
	int memoff, len;

	memoff = fdt_path_offset(saved, "/memory");
	if (memoff < 0)
		return memoff;
	reg = fdt_getprop(saved, memoff, "reg", &len);
	if (!reg || len <= 0 || (len % 16) != 0)
		return -FDT_ERR_BADVALUE;
	if (fdt_getprop(saved, memoff, "ddr_device_type", &len) && len != 4)
		return -FDT_ERR_BADVALUE;
	return 0;
}

static int strip_memory(void *fdt)
{
	int root, node;

	root = fdt_path_offset(fdt, "/");
	if (root < 0)
		return root;
restart:
	fdt_for_each_subnode(node, fdt, root) {
		const char *type = dt_string(fdt, node, "device_type");

		if (type && strcmp(type, "memory") == 0) {
			int err = fdt_del_node(fdt, node);

			if (err < 0)
				return err;
			goto restart;
		}
	}
	return 0;
}

static int strip_ddr_regions(void *fdt)
{
	int off, propoff;

	off = fdt_path_offset(fdt, "/ddr-regions");
	if (off < 0)
		return (off == -FDT_ERR_NOTFOUND) ? 0 : off;
restart:
	fdt_for_each_property_offset(propoff, fdt, off) {
		const char *pname;

		if (!fdt_getprop_by_offset(fdt, propoff, &pname, NULL))
			return -FDT_ERR_BADVALUE;
		if (is_ddr_region(pname)) {
			int err = fdt_delprop(fdt, off, pname);

			if (err < 0)
				return err;
			goto restart;
		}
	}
	return 0;
}

static int build_overlay(const void *saved, void *overlay,
			 size_t overlay_capacity)
{
	const void *prop;
	int err, root, frag, ovroot, mem, src, len;

	err = fdt_create_empty_tree(overlay, overlay_capacity);
	if (err < 0)
		return err;
	root = fdt_path_offset(overlay, "/");
	if (root < 0)
		return root;
	frag = fdt_add_subnode(overlay, root, "fragment@0");
	if (frag < 0)
		return frag;
	err = fdt_setprop_string(overlay, frag, "target-path", "/");
	if (err < 0)
		return err;
	ovroot = fdt_add_subnode(overlay, frag, "__overlay__");
	if (ovroot < 0)
		return ovroot;
	mem = fdt_add_subnode(overlay, ovroot, "memory");
	if (mem < 0)
		return mem;
	err = fdt_setprop_string(overlay, mem, "device_type", "memory");
	if (err < 0)
		return err;
	src = fdt_path_offset(saved, "/memory");
	if (src < 0)
		return src;
	prop = fdt_getprop(saved, src, "reg", &len);
	if (!prop || len <= 0 || (len % 16) != 0)
		return -FDT_ERR_BADVALUE;
	err = fdt_setprop(overlay, mem, "reg", prop, len);
	if (err < 0)
		return err;
	prop = fdt_getprop(saved, src, "ddr_device_type", &len);
	if (prop) {
		if (len != 4)
			return -FDT_ERR_BADVALUE;
		err = fdt_setprop(overlay, mem, "ddr_device_type", prop, len);
		if (err < 0)
			return err;
	}
	err = copy_ddr_regions(saved, overlay, ovroot);
	if (err < 0)
		return err;
	return 0;
}

int dt_memory_apply(const void *saved, size_t saved_size,
		    const void *payload, size_t payload_size,
		    void *output, size_t capacity,
		    void *overlay, size_t overlay_capacity)
{
	int err;

	if (!valid_range(output, capacity) ||
	    !valid_range(overlay, overlay_capacity))
		return -FDT_ERR_BADVALUE;
	if (((uintptr_t)output & 7u) || ((uintptr_t)overlay & 7u))
		return -FDT_ERR_BADVALUE;
	if (capacity < sizeof(struct fdt_header) || capacity > DT_MEMORY_APPLY_MAX)
		return -FDT_ERR_BADVALUE;
	if (overlay_capacity < sizeof(struct fdt_header) ||
	    overlay_capacity > DT_MEMORY_APPLY_MAX)
		return -FDT_ERR_BADVALUE;
	if (!valid_range(saved, saved_size) || !valid_range(payload, payload_size))
		return -FDT_ERR_BADVALUE;

	if (overlap(output, capacity, overlay, overlay_capacity) ||
	    overlap(output, capacity, saved, saved_size) ||
	    overlap(output, capacity, payload, payload_size) ||
	    overlap(overlay, overlay_capacity, saved, saved_size) ||
	    overlap(overlay, overlay_capacity, payload, payload_size))
		return -FDT_ERR_BADVALUE;

	err = check_image(saved, saved_size);
	if (err < 0)
		return err;
	err = check_image(payload, payload_size);
	if (err < 0)
		return err;

	err = check_cells(saved);
	if (err < 0)
		return err;
	err = check_cells(payload);
	if (err < 0)
		return err;
	err = check_memory(saved);
	if (err < 0)
		return err;

	err = fdt_open_into(payload, output, capacity);
	if (err < 0)
		return err;
	err = strip_memory(output);
	if (err < 0)
		return err;
	err = strip_ddr_regions(output);
	if (err < 0)
		return err;

	err = build_overlay(saved, overlay, overlay_capacity);
	if (err < 0)
		return err;

	err = dt_handoff_reservations_overlay(saved, output, overlay);
	if (err < 0)
		return err;
	err = fdt_overlay_apply(output, overlay);
	if (err < 0)
		return err;
	err = fdt_pack(output);
	if (err < 0)
		return err;
	return dt_validate(output, capacity);
}
