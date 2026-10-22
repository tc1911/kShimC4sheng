/* SPDX-License-Identifier: MIT */
#pragma once
/* Implementation of CrDK's CR_FREESTANDING OSKAL contract. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define IN
#define OUT
#define OPTIONAL
#define STATIC static
#define CONST const
#define TRUE true
#define FALSE false
#define EFIAPI
typedef void VOID;
typedef uint8_t BOOLEAN;
typedef char CHAR8;
typedef uint16_t CHAR16;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uintptr_t UINTN;
typedef intptr_t INTN;
#define MAX_UINT16 UINT16_MAX
#define MAX_UINT32 UINT32_MAX
#define MAX_UINT64 UINT64_MAX
#define MAX_UINTN UINTPTR_MAX
typedef int CR_STATUS;
#define CR_SUCCESS 0
#define CR_ABORT (-125)
#define CR_INVALID_PARAMETER (-22)
#define CR_NOT_FOUND (-2)
#define CR_OUT_OF_RESOURCES (-12)
#define CR_UNSUPPORTED (-95)
#define CR_DEVICE_ERROR (-5)
#define CR_TIMEOUT (-110)
#define CR_BUSY (-16)
#define CR_BUFFER_TOO_SMALL (-28)
#define CR_ERROR(s) ((s) < 0)
#define CR_STATIC_ASSERT(c, m) _Static_assert(c, m)
#define CR_ASSERT(c) do { if (!(c)) CrOsPanic(#c); } while (0)
#define TO_BOOL(x) (!!(x))
#define CR_LOG_CHAR8_STR_FMT "%s"
#define LOG_COLOR_RESET ""
#define LOG_COLOR_INFO ""
#define LOG_COLOR_WARN ""
#define LOG_COLOR_ERROR ""
void CrOsLog(const char *format, ...);
_Noreturn void CrOsPanic(const char *condition);
#define log_raw CrOsLog
#define log_info CrOsLog
#define log_warn CrOsLog
#define log_err CrOsLog
#define log_debug CrOsLog
#define cr_memset memset
#define cr_memcpy memcpy
#define cr_memcmp memcmp
#define cr_strcmp strcmp
#define cr_strncmp strncmp
#define cr_malloc malloc
#define cr_free free
void CrOsDelay(uint64_t us);
uint64_t CrOsTime(void);
uint32_t CrMmioRead32(uintptr_t address);
uint32_t CrMmioWrite32(uintptr_t address, uint32_t value);
void CrOsBarrier(void);
#define cr_sleep CrOsDelay
#define CR_MEM_BARRIER_DATA_SYN_BARRIAR() CrOsBarrier()
static inline uint32_t CrMmioUpdateBits32(uintptr_t a, uint32_t m, uint32_t v)
{ return CrMmioWrite32(a, (CrMmioRead32(a) & ~m) | (v & m)); }
static inline uint32_t CrMmioOr32(uintptr_t a, uint32_t v)
{ return CrMmioWrite32(a, CrMmioRead32(a) | v); }
static inline uint32_t CrMmioAnd32(uintptr_t a, uint32_t v)
{ return CrMmioWrite32(a, CrMmioRead32(a) & v); }
typedef struct { uint32_t value; } CR_LOCK;
static inline void CrLockInit(CR_LOCK *l) { __atomic_store_n(&l->value, 0, __ATOMIC_RELAXED); }
static inline void CrLockAcquire(CR_LOCK *l)
{ while (__atomic_exchange_n(&l->value, 1, __ATOMIC_ACQUIRE)) __asm__ volatile("" ::: "memory"); }
static inline void CrLockRelease(CR_LOCK *l) { __atomic_store_n(&l->value, 0, __ATOMIC_RELEASE); }
static inline uint32_t CrAtomicLoad32(volatile uint32_t *p)
{ return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline uint32_t CrAtomicOr32(volatile uint32_t *p, uint32_t v)
{ return __atomic_fetch_or(p, v, __ATOMIC_ACQ_REL); }
static inline uint32_t CrAtomicAnd32(volatile uint32_t *p, uint32_t v)
{ return __atomic_fetch_and(p, v, __ATOMIC_ACQ_REL); }
