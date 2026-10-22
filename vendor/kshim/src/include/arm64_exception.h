#pragma once
#include <stdint.h>
extern void arm64_install_exception_vector(void);
extern void arm64_enable_interrupts(void);
extern void arm64_disable_interrupts(void);

// Define ARM64 Exception context structure
typedef struct arm64_exception_context
{
    uint64_t x[32]; // General purpose registers x0-x30
    uint64_t q[32][2]; // Vector registers q0-q31
    uint64_t elr; // Stack pointer for EL0
    uint64_t spsr; // Exception Link Register for EL1
    uint64_t fpsr; // Saved Program Status Register for EL1
    uint64_t esr; // Exception Syndrome Register
    uint64_t far; // Fault Address Register
    uint64_t padding; // Padding for alignment
} __attribute__((packed)) ARM64_EXCEPTION_CONTEXT, *PARM64_EXCEPTION_CONTEXT;