#pragma once

// provides some helper functions to make
// ASM code more readable
#define ASM_FUNC(name)          \
    .global name;               \
    .type name, % function;     \
    .section  .text.##name, "ax"; \
    .p2align 2;                 \
    name:
