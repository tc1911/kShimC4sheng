/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qcom_dt.h>
#include <qcom_keys_fdt.h>
#include <kshim_console.h>
static void *fixture(const char *name, size_t *bytes)
{
    char path[1024]; snprintf(path, sizeof(path), "%s/%s", strcmp(name,"kona-resources.dtb") ? FIXTURES : GENERATED_FIXTURES, name);
    FILE *fp = fopen(path, "rb"); assert(fp);
    assert(!fseek(fp, 0, SEEK_END)); long size = ftell(fp); assert(size > 0);
    rewind(fp); void *data = calloc(1, (size_t)size + 16384); assert(data);
    assert(fread(data, 1, size, fp) == (size_t)size); fclose(fp); *bytes = size;
    assert(!dt_validate(data, size));
    assert(dt_validate(data, size - 1));
    assert(dt_validate(data, 8));
    assert(!fdt_open_into(data, data, size + 16384));
    return data;
}
static void test_real(const char *name, bool touch)
{
    size_t bytes; void *f = fixture(name, &bytes);
    struct kshim_resources *r = calloc(1, sizeof(*r)); assert(r);
    struct kshim_console_resource c;
    assert(!kshim_console_probe(f, &c)); assert(c.base == 0xa90000 && c.geni);
    /* The UI scratch comes from this reservation's tail past the frame. */
    struct dt_range splash; bool no_map = true;
    assert(!kshim_dt_splash_reservation(f, &splash, &no_map));
    assert(splash.base == 0x9c000000 && splash.size == 0x2400000 && !no_map);
    assert(!kshim_dt_spmi(f, r)); assert(r->spmi_count == 1);
    assert(r->spmi[0].controller.Regions[0].BaseAddress == 0xc440000);
    QcomKeyDescriptor keys[QCOM_KEYS_MAX]; size_t count;
    assert(!QcomKeysBuildFromDeviceTree(f, keys, QCOM_KEYS_MAX, &count));
    fprintf(stderr, "%s key count=%zu\n", name, count);
    for(size_t i=0;i<count;++i) fprintf(stderr, "key: %u base=%llx\n",keys[i].Action,(unsigned long long)keys[i].Base);
    assert(count == 3);
    int status = kshim_dt_touch(f, &r->touch);
    fprintf(stderr, "%s touch=%d keys=%zu\n", name, status, count);
    if (touch) {
        assert(!status); assert(r->touch.generation == 5 && r->touch.address == 0x49);
        assert(r->touch.qup.geni == 0xc80000 && r->touch.qup.irq_pin == 0x3d7a000);
        assert(r->touch.qup.cmd_db_dictionary == 0xc3f000c);
        assert(r->touch.qup.tcs_offset == 0xd00 && r->touch.qup.active_count == 2);
        int bus = fdt_parent_offset(f, r->touch.node);
        assert(!fdt_setprop_string(f, bus, "status", "disabled"));
        assert(kshim_dt_touch(f, &r->touch));
    }
    struct dt_range ranges[128]; bool nomap[128]; size_t nr;
    assert(!dt_reservations(f, ranges, nomap, 128, &nr)); assert(nr > 10);
    free(r); free(f);
}
static void test_common(void)
{
    uint64_t blob[2048]; void *f = blob;
    assert(!fdt_create_empty_tree(f, sizeof(blob)));
    assert(!fdt_setprop_u32(f, 0, "#address-cells", 1));
    assert(!fdt_setprop_u32(f, 0, "#size-cells", 1));
    int b = fdt_add_subnode(f, 0, "bus"); assert(b > 0);
    assert(!fdt_setprop_u32(f, b, "#address-cells", 1));
    assert(!fdt_setprop_u32(f, b, "#size-cells", 1));
    fdt32_t ranges[] = {cpu_to_fdt32(0x1000), cpu_to_fdt32(0x200000), cpu_to_fdt32(0x10000)};
    assert(!fdt_setprop(f,b,"ranges",ranges,sizeof(ranges)));
    int n=fdt_add_subnode(f,b,"uart@1000"); assert(n>0);
    fdt32_t reg[]={cpu_to_fdt32(0x1000),cpu_to_fdt32(0x1000)};
    assert(!fdt_setprop(f,n,"reg",reg,sizeof(reg)));
    assert(!fdt_setprop_string(f,n,"compatible","qcom,msm-geni-console"));
    assert(!fdt_setprop_u32(f,n,"phandle",1));
    assert(!fdt_setprop_string(f,n,"reg-names","uart"));
    struct dt_range r; assert(!dt_reg_named(f,n,"uart",&r)); assert(r.base==0x200000);
    struct kshim_console_resource c; assert(!kshim_console_probe(f,&c)); assert(c.base==r.base);
    int chosen=fdt_add_subnode(f,0,"chosen"); assert(chosen>0);
    assert(!fdt_setprop_string(f,chosen,"bootargs","earlycon=msm_geni_serial,0x200000 console=x"));
    assert(!kshim_console_probe(f,&c)); assert(c.base==0x200000);
    assert(!fdt_setprop_string(f,chosen,"bootargs","earlycon=msm_geni_serial,200000"));
    assert(!kshim_console_probe(f,&c));
    assert(!fdt_setprop_string(f,chosen,"bootargs","earlycon=msm_geni_serial,0x20000z"));
    assert(kshim_console_probe(f,&c));
    assert(!fdt_delprop(f,chosen,"bootargs"));
    assert(!fdt_setprop_string(f,chosen,"stdout-path","serial0:115200n8"));
    int aliases=fdt_add_subnode(f,0,"aliases"); assert(aliases>0);
    assert(!fdt_setprop_string(f,aliases,"serial0","/bus/uart@1000"));
    assert(!kshim_console_probe(f,&c));
    b=fdt_path_offset(f,"/bus"); assert(!fdt_setprop_string(f,b,"status","disabled"));
    assert(kshim_console_probe(f,&c)); assert(dt_phandle(f,1)<0);
    assert(!fdt_delprop(f,b,"status")); n=fdt_path_offset(f,"/bus/uart@1000");
    assert(dt_phandle(f,1)==n);
    assert(!fdt_setprop(f,n,"phandle",reg,8)); assert(dt_phandle(f,1)<0);
    assert(!fdt_setprop_u32(f,n,"phandle",1));
    assert(!fdt_setprop(f,n,"reg",reg,7)); assert(dt_reg(f,n,0,&r));
    assert(!fdt_setprop(f,n,"reg",reg,8));
    assert(!fdt_setprop(f,n,"reg-names","uart\0uart",10)); assert(dt_reg_named(f,n,"uart",&r));
    b=fdt_path_offset(f,"/bus"); assert(!fdt_delprop(f,b,"ranges"));
    n=fdt_path_offset(f,"/bus/uart@1000"); assert(dt_reg(f,n,0,&r));
    kshim_framebuffer_config_t fb; kshim_mmu_region_t mmio;
    assert(kshim_dt_splash(f,NULL,&fb,&mmio)); assert(!fb.render_address);
    struct dt_range none; assert(kshim_dt_splash_reservation(f,&none,NULL));
}
static void test_kona(void)
{
    size_t size; void *f=fixture("kona-resources.dtb",&size);
    struct kshim_touch_resource t;
    assert(kshim_dt_touch(f,&t)); /* Stock HDK selection intentionally empty. */
    int bus=fdt_path_offset(f,"/i2c@a94000");
    assert(!fdt_setprop_string(f,bus,"qcom,i2c-touch-active","st,fts"));
    /* The kona fixture is an ST FTM4 node; the FTM5-only prober rejects it. */
    assert(kshim_dt_touch(f,&t));
    struct kshim_resources *r=calloc(1,sizeof(*r)); assert(r);
    assert(!kshim_dt_spmi(f,r) && r->spmi_count==2);
    uint8_t controller,bus_id,sid;
    int n=fdt_path_offset(f,"/spmi@c450000/pmic@0/pon@800");
    assert(!kshim_dt_spmi_identity(f,n,&controller,&bus_id,&sid));
    assert(controller==1 && bus_id==1 && sid==0);
    int display=fdt_path_offset(f,"/display");
    assert(!fdt_setprop_string(f,display,"status","disabled"));
    assert(kshim_dt_touch(f,&t));
    assert(!fdt_delprop(f,display,"status"));
    bus=fdt_path_offset(f,"/i2c@a94000");
    assert(!fdt_delprop(f,bus,"clocks")); assert(kshim_dt_touch(f,&t));
    struct dt_range reservations[8]; bool no_map[8]; size_t nr;
    assert(!dt_reservations(f,reservations,no_map,8,&nr) && nr==2);
    assert(no_map[1] && reservations[1].base==0x80860000);
    int db=fdt_path_offset(f,"/reserved-memory/cmd-db@80860000");
    assert(!fdt_setprop_u32(f,db,"no-map",1));
    assert(dt_reservations(f,reservations,no_map,8,&nr));
    free(r); free(f);
}
int main(void)
{
    test_common(); test_kona(); test_real("andromeda-fdt.dtb",true); test_real("hdk8150-fdt.dtb",false);
    puts("DT fixtures and resource boundaries passed");
}
