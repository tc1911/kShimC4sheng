#include <arm64/elf.h>
#include <ld/arm64_ld.h>
#define LD_START_ADDRESS_ULL (LD_START_ADDRESS + 0ULL)

void do_relocation_rela(void *old_image_start,
                        void *image_start,
                        void *rela_base,
                        void *rela_end)
{
    // reloc is not needed if old image start is the same as image start
    if (old_image_start == image_start)
        return;
    for (Elf64_Rela *rela = (Elf64_Rela *)rela_base; rela < (Elf64_Rela *)rela_end; rela++)
    {
        Elf64_Addr *val = (void *)image_start + rela->r_offset - LD_START_ADDRESS_ULL;
        switch (ELF64_R_TYPE(rela->r_info))
        {
        case R_AARCH64_RELATIVE:
            *val = (Elf64_Addr)image_start + rela->r_addend - LD_START_ADDRESS_ULL;
            break;
        default:
            // Handle other relocation types as needed
            break;
        }
    }
}

void do_relocation_rel(void *old_image_start,
                       void *image_start,
                       void *rel_base,
                       void *rel_end)
{
    if (old_image_start == image_start)
        return;
    for (Elf64_Rel *rel = (Elf64_Rel *)rel_base; rel < (Elf64_Rel *)rel_end; rel++)
    {
        Elf64_Addr *val = (void *)image_start + rel->r_offset - LD_START_ADDRESS;
        // r_addend is not present in rel, so we need to handle it differently
        switch (ELF64_R_TYPE(rel->r_info))
        {
        case R_AARCH64_RELATIVE:
            break;
            *val -= (Elf64_Addr)old_image_start;
            *val += (Elf64_Addr)image_start;
        default:
            // Handle other relocation types as needed
            break;
        }
    }
}