/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <kshim_mmu.h>
#include <dt.h>
__asm__(".pushsection .test_image,\"aw\",@progbits\n.balign 4096\n"
        ".global __image_start_address__\n__image_start_address__:\n.space 8192\n"
        ".global __image_end_address__\n__image_end_address__:\n.popsection\n");
static uint64_t entry(uint64_t a)
{
    uint64_t *table=(void*)(uintptr_t)kshim_mmu_root_table(); assert(table);
    for(unsigned level=0;level<4;++level){
        uint64_t d=table[(a>>(12+9*(3-level)))&511];
        if(!(d&1) || (d&3)==1 || level==3) return d;
        table=(void*)(uintptr_t)(d&UINT64_C(0x0000fffffffff000));
    }
    return 0;
}
int main(void)
{
    kshim_mmu_region_t maps[]={
        {0xa90000,4,KSHIM_MMU_DEVICE,KSHIM_MMU_READ|KSHIM_MMU_WRITE},
        {0xa90010,4,KSHIM_MMU_DEVICE,KSHIM_MMU_READ|KSHIM_MMU_WRITE},
        {0x80860000,0x20000,KSHIM_MMU_NORMAL_NC,KSHIM_MMU_READ},
    };
    kshim_mmu_config_t config={.extra_regions=maps,.extra_region_count=3};
    assert(!kshim_mmu_configure(&config)); assert(entry(0xa90000)&1);
    assert(((entry(0xa90000)>>2)&7)==KSHIM_MMU_DEVICE);
    assert(entry(0x80860000)&(1U<<7));
    assert(!entry(0x80000000)); assert(!entry(0x100000));
    maps[1].type=KSHIM_MMU_NORMAL_WB;
    assert(kshim_mmu_configure(&config)); maps[1].type=KSHIM_MMU_DEVICE;
    uint64_t tree[512]; assert(!fdt_create_empty_tree(tree,sizeof(tree)));
    assert(!fdt_add_mem_rsv(tree,0x80860000,0x20000));
    config.fdt=tree; maps[2].type=KSHIM_MMU_NORMAL_WB;
    assert(kshim_mmu_configure(&config));
    maps[2].type=KSHIM_MMU_NORMAL_NC;
    assert(!kshim_mmu_configure(&config)); /* Explicit consumer mapping only. */
    assert(!kshim_mmu_prepare_handoff());
    puts("MMU page reuse, resource types, reservations and unmapped RAM passed");
}
