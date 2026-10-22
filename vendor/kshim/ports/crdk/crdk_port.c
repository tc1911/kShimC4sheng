#include <oskal/cr_interrupt.h>
#include <crdk_os.h>
#include <stdarg.h>
#include <stdio.h>
#if !defined(__aarch64__)
#include <time.h>
#endif

void CrOsBarrier(void)
{
#if defined(__aarch64__)
  __asm__ volatile("dsb sy" ::: "memory");
#else
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}
uint32_t CrMmioRead32(uintptr_t address)
{ CrOsBarrier(); uint32_t v = *(volatile uint32_t *)address; CrOsBarrier(); return v; }
uint32_t CrMmioWrite32(uintptr_t address, uint32_t value)
{ CrOsBarrier(); *(volatile uint32_t *)address = value; CrOsBarrier(); return value; }
uint64_t CrOsTime(void)
{
#if defined(__aarch64__)
  uint64_t ticks, frequency;
  __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(ticks));
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
  if (!frequency) CrOsPanic("Missing timer frequency");
  return ticks / frequency * 1000000 + (ticks % frequency) * 1000000 / frequency;
#else
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
#endif
}
void CrOsDelay(uint64_t us)
{
  uint64_t start = CrOsTime();
  while (CrOsTime() - start < us) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#endif
  }
}
void CrOsLog(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}
_Noreturn void CrOsPanic(const char *condition)
{
  CrOsLog("CrDK assertion: %s\n", condition);
  for (;;) {
#if defined(__aarch64__)
    __asm__ volatile("wfe");
#else
    abort();
#endif
  }
}

/* This port is polled. It never changes the PMIC/GIC interrupt ownership. */
CR_STATUS CrRegisterInterrupt(CR_INTERRUPT_CONFIG *Config)
{
  (void)Config;
  return CR_UNSUPPORTED;
}

CR_STATUS CrUnregisterInterrupt(CR_INTERRUPT_CONFIG *Config)
{
  (void)Config;
  return CR_UNSUPPORTED;
}
