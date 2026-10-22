#include <uefi_boot.h>
#include <lvgl_port.h>
#include <config.h>
#include <stddef.h>
#include <platform.h>
#include <kshim_console.h>

#if defined(CONFIG_KSHIM_UEFI_TEST_MENU) && CONFIG_KSHIM_UEFI_TEST_MENU
#ifndef KSHIM_HAS_UEFI_PAYLOAD
#error "The UEFI test menu requires -DKSHIM_UEFI_PAYLOAD=/path/to/firmware.fd"
#endif
extern const unsigned char kshim_uefi_payload_start[];
extern const unsigned char kshim_uefi_payload_end[];
static uint64_t mBootArgs[4];

void kshim_boot_set_args(uint64_t Fdt, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3)
{
  mBootArgs[0] = Fdt;
  mBootArgs[1] = Arg1;
  mBootArgs[2] = Arg2;
  mBootArgs[3] = Arg3;
}

int kshim_menu_activate(kshim_menu_action_t Action)
{
  if (Action != KSHIM_MENU_BOOT && Action != KSHIM_MENU_RECOVERY)
    return -1;
  size_t Size = (size_t)(kshim_uefi_payload_end - kshim_uefi_payload_start);
  uintptr_t Destination = CONFIG_KSHIM_UEFI_LOAD_ADDRESS;
  if (Size == 0 || Size > CONFIG_KSHIM_UEFI_MAX_SIZE ||
      Destination == 0 || Destination > UINTPTR_MAX - Size)
    return -1;
  if (kshim_platform_prepare_handoff() != 0)
    return -2;
  /* The payload is in the shim's Kernel reservation; UEFI has a separate
   * FD reservation. Boot A and B deliberately use the same test payload. */
  for (size_t Index = 0; Index < Size; Index++)
    ((volatile unsigned char *)Destination)[Index] = kshim_uefi_payload_start[Index];

  uint64_t Ctr;
  __asm__ volatile("mrs %0, ctr_el0" : "=r"(Ctr));
  size_t Line = (size_t)4U << ((Ctr >> 16) & 15U);
  for (uintptr_t Address = Destination & ~(Line - 1U);
       Address < Destination + Size; Address += Line)
    __asm__ volatile("dc cvau, %0" : : "r"(Address) : "memory");
  __asm__ volatile("dsb sy\n\tic iallu\n\tdsb sy\n\tisb" ::: "memory");
  if (kshim_console_quiesce())
    return -2;
  void (*Entry)(uint64_t, uint64_t, uint64_t, uint64_t) = (void *)Destination;
  Entry(mBootArgs[0], mBootArgs[1], mBootArgs[2], mBootArgs[3]);
  return -1;
}
#else
void kshim_boot_set_args(uint64_t Fdt, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3)
{
  (void)Fdt; (void)Arg1; (void)Arg2; (void)Arg3;
}
#endif
