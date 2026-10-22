#pragma once
#include <lib/stdport.h>
#include <config.h>

#ifdef CONFIG_ASSERT_HALT
#define ASSERT(x)                                                   \
    do                                                              \
    {                                                               \
        if (!(x))                                                   \
        {                                                           \
            printf("ASSERT: %s, %s, %d\n", #x, __FILE__, __LINE__); \
            while (1)                                               \
                ;                                                   \
        }                                                           \
    } while (0)
#else
#define ASSERT(x)                                                   \
    do                                                              \
    {                                                               \
        if (!(x))                                                   \
        {                                                           \
            printf("ASSERT: %s, %s, %d\n", #x, __FILE__, __LINE__); \
        }                                                           \
    } while (0)
#endif


#define COMPILER_ASSERT(x)                                          \
    _Static_assert(x, "Compile-time assertion failed: " #x)
