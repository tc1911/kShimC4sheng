#pragma once

// 8-bit MMIO operations
#define REG8_OUT(addr) (*(volatile uint8_t *)(addr))
#define REG8_IN(addr, val) (REG8_OUT(addr) = (uint8_t)(val))
#define REG8_IN_MEM_BARRIER(addr, val) \
    {                                  \
        REG8_OUT(addr) = (uint8_t)(val);  \
        asm volatile("" ::: "memory");  \
    }
#define REG8_MSK_SET(addr, msk) (REG8_OUT(addr) |= (uint8_t)(msk))
#define REG8_MSK_CLR(addr, msk) (REG8_OUT(addr) &= ~(uint8_t)(msk))
#define REG8_MSK_OUT(addr, msk) (REG8_OUT(addr) & (uint8_t)(msk))
#define REG8_MSK_SFT_OUT(addr, msk, sft) ((REG8_OUT(addr) & (uint8_t)(msk)) >> (sft))
#define REG8_SFT_SET(addr, sft, val) (REG8_OUT(addr) |= ((uint8_t)(val) << (sft)))

// 16-bit MMIO operations
#define REG16_OUT(addr) (*(volatile uint16_t *)(addr))
#define REG16_IN(addr, val) (REG16_OUT(addr) = (uint16_t)(val))
#define REG16_IN_MEM_BARRIER(addr, val) \
    {                                   \
        REG16_OUT(addr) = (uint16_t)(val); \
        asm volatile("" ::: "memory");   \
    }
#define REG16_MSK_SET(addr, msk) (REG16_OUT(addr) |= (uint16_t)(msk))
#define REG16_MSK_CLR(addr, msk) (REG16_OUT(addr) &= ~(uint16_t)(msk))
#define REG16_MSK_OUT(addr, msk) (REG16_OUT(addr) & (uint16_t)(msk))
#define REG16_MSK_SFT_OUT(addr, msk, sft) ((REG16_OUT(addr) & (uint16_t)(msk)) >> (sft))
#define REG16_SFT_SET(addr, sft, val) (REG16_OUT(addr) |= ((uint16_t)(val) << (sft)))

// 32-bit MMIO operations
#define REG32_OUT(addr) (*(volatile uint32_t *)(addr))
#define REG32_IN(addr, val) (REG32_OUT(addr) = (uint32_t)(val))
#define REG32_IN_MEM_BARRIER(addr, val) \
    {                                   \
        REG32_OUT(addr) = (uint32_t)(val); \
        asm volatile("" ::: "memory");   \
    }
#define REG32_MSK_SET(addr, msk) (REG32_OUT(addr) |= (uint32_t)(msk))
#define REG32_MSK_CLR(addr, msk) (REG32_OUT(addr) &= ~(uint32_t)(msk))
#define REG32_MSK_CLR_SET(addr, msk, val) (REG32_OUT(addr) = (REG32_OUT(addr) & ~(uint32_t)(msk)) | ((uint32_t)(val)))
#define REG32_MSK_OUT(addr, msk) (REG32_OUT(addr) & (uint32_t)(msk))
#define REG32_MSK_SFT_OUT(addr, msk, sft) ((REG32_OUT(addr) & (uint32_t)(msk)) >> (sft))
#define REG32_SFT_SET(addr, sft, val) (REG32_OUT(addr) |= ((uint32_t)(val) << (sft)))

// 64-bit MMIO operations
#define REG64_OUT(addr) (*(volatile uint64_t *)(addr))
#define REG64_IN(addr, val) (REG64_OUT(addr) = (uint64_t)(val))
#define REG64_IN_MEM_BARRIER(addr, val) \
    {                                   \
        REG64_OUT(addr) = (uint64_t)(val); \
        asm volatile("" ::: "memory");   \
    }
#define REG64_MSK_SET(addr, msk) (REG64_OUT(addr) |= (uint64_t)(msk))
#define REG64_MSK_CLR(addr, msk) (REG64_OUT(addr) &= ~(uint64_t)(msk))
#define REG64_MSK_OUT(addr, msk) (REG64_OUT(addr) & (uint64_t)(msk))
#define REG64_MSK_SFT_OUT(addr, msk, sft) ((REG64_OUT(addr) & (uint64_t)(msk)) >> (sft))
#define REG64_SFT_SET(addr, sft, val) (REG64_OUT(addr) |= ((uint64_t)(val) << (sft)))
