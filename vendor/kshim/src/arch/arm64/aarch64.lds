#include <ld/arm64_ld.h>

ENTRY(_start)
SECTIONS
{
    /*. = 0x40200000;*/
    . = LD_START_ADDRESS;
    __image_start_address__ = .;
    . = ALIGN(8);
    .text.boot : {*(.text.boot)}
    .text : {*(.text .text.*)}

    . = ALIGN(8);
    .rodata : {*(.rodata .rodata.* .ro_data .ro_data.*)}
    .uefi_payload ALIGN(16) : { KEEP(*(.uefi_payload)); }
    .data : { 
        *(.data .data.*);
    }

    .bss ALIGN(0x40) (NOLOAD) : {
        __bss_start__ = .;
        *(.bss*);
        *(COMMON);
        . = ALIGN(0x40);
        __bss_end__ = .;
    }

    .mmu_tables ALIGN(0x1000) (NOLOAD) : {
        __mmu_tables_start__ = .;
        KEEP(*(.mmu_tables));
        . = ALIGN(0x1000);
        __mmu_tables_end__ = .;
    }

    .smp_stacks ALIGN(0x40) (NOLOAD) : {
        __smp_stacks_start__ = .;
        KEEP(*(.smp_stacks));
        . = ALIGN(0x40);
        __smp_stacks_end__ = .;
    }

    . = ALIGN(8);

    .gots ALIGN(4) : {
        __gots_start__ = .;
        *(.got);
        *(.got.*);
        __gots_end__ = .;
        . = ALIGN(8);
    }

    .rels ALIGN(8) : {
        __reldyn_start__ = .;
        *(.rel);
        *(.rel.*);
        __reldyn_end__ = .;
        . = ALIGN(8);
    }

    .relas ALIGN(8) : {
        __reladyn_start__ = .;
        *(.rela);
        *(.rela.*);
        __reladyn_end__ = .;
        . = ALIGN(8);
    }

    .heap ALIGN(0x40) (NOLOAD) : {
        . = ALIGN(16);
        __heap_start = .;
        . = . + 0x10000;
        __heap_end = .;
    }

    .stack ALIGN(0x40) (NOLOAD) : {
        . = ALIGN(0x40);
        __stack_base__ = .;
        /* primary stack */
        . = . + LD_STACK_SIZE;
        . = ALIGN(0x40);
        __stack_top__ = .;
    }

    . = ALIGN(8);
    /DISCARD/ : { *(.note.GNU-stack) }
    __runtime_end_address__ = .;

    ASSERT(__runtime_end_address__ - __image_start_address__ <=
            LD_IMAGE_SIZE,
            "kShim runtime exceeds image slot")
    . = __image_start_address__ + LD_IMAGE_SIZE - 1;
    .image_end : { BYTE(0); }
    __image_end_address__ = .;
}
