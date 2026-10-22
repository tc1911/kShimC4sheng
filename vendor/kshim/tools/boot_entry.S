/* Position-independent Android kernel entry. Linux boot ABI: MMU off,
 * boot CPU only, x0=FDT. Copy the shim to its board Kernel reservation. */
.section .text, "ax"
.global _start
_start:
    b entry
    nop
    .quad 0                       // text_offset
    .quad SHIM_MEMORY_SIZE + 0x1000
    .quad 0                       // little endian, 4K unspecified page size
    .quad 0
    .quad 0
    .quad 0
    .ascii "ARM\x64"
    .long 0
entry:
    msr daifset, #0xf
    adr x9, payload
    ldr x15, =SHIM_LOAD_ADDRESS
    ldr x11, =SHIM_FILE_SIZE
    mov x16, x11
    /* ABL may load us directly at SHIM_LOAD_ADDRESS. Run the overlapping
     * memmove from a separate reserved page so it cannot overwrite itself. */
    adr x4, relocator
    ldr x5, =SHIM_SCRATCH_ADDRESS
    /* The reserved scratch page must not intersect the incoming payload.
     * An out-of-reservation loader placement can otherwise overwrite source
     * bytes before memmove begins. Refuse such a placement before any write. */
    add x6, x9, x11
    cmp x5, x6
    b.hs scratch_safe
    add x6, x5, #0x1000
    cmp x6, x9
    b.ls scratch_safe
unsafe_placement:
    wfe
    b unsafe_placement
scratch_safe:
    mov x8, x5
    mov x6, #(relocator_end - relocator)
copy_relocator:
    ldp x12, x13, [x4], #16
    stp x12, x13, [x5], #16
    subs x6, x6, #16
    b.ne copy_relocator
    mov x4, x8
    mrs x6, ctr_el0
    ubfx x6, x6, #16, #4
    mov x7, #4
    lsl x7, x7, x6
clean_relocator:
    dc cvau, x4
    add x4, x4, x7
    cmp x4, x5
    b.lo clean_relocator
    dsb sy
    ic iallu
    dsb sy
    isb
    br x8
    .ltorg
    .balign 16
relocator:
    mov x10, x15
    cmp x9, x10
    b.eq copied
    b.hi forward
    add x9, x9, x11
    add x10, x10, x11
backward:
    /* Image slots and the payload are 16-byte aligned. Keep a byte path
     * for a future caller with an unaligned placement or image length. */
    orr x12, x9, x10
    tst x12, #15
    b.ne backward_bytes
backward_blocks:
    cmp x11, #16
    b.lo backward_bytes
    ldp x12, x13, [x9, #-16]!
    stp x12, x13, [x10, #-16]!
    subs x11, x11, #16
    b.ne backward_blocks
    b copied
backward_bytes:
    cbz x11, copied
backward_byte:
    ldrb w12, [x9, #-1]!
    strb w12, [x10, #-1]!
    subs x11, x11, #1
    b.ne backward_byte
    b copied
forward:
    orr x12, x9, x10
    tst x12, #15
    b.ne forward_bytes
forward_blocks:
    cmp x11, #16
    b.lo forward_bytes
    ldp x12, x13, [x9], #16
    stp x12, x13, [x10], #16
    subs x11, x11, #16
    b.ne forward_blocks
    b copied
forward_bytes:
    cbz x11, copied
forward_byte:
    ldrb w12, [x9], #1
    strb w12, [x10], #1
    subs x11, x11, #1
    b.ne forward_byte
copied:
    mrs x12, ctr_el0
    ubfx x12, x12, #16, #4
    mov x13, #4
    lsl x13, x13, x12
    sub x12, x13, #1
    bic x10, x15, x12
    add x11, x15, x16
clean:
    dc cvau, x10
    add x10, x10, x13
    cmp x10, x11
    b.lo clean
    dsb sy
    ic iallu
    dsb sy
    isb
    br x15
    .balign 16
relocator_end:
payload:
.section .note.GNU-stack, "", %progbits
