/*
 * Freestanding ARM64 /init for the external-initramfs QEMU acceptance fixture.
 *
 * Built twice (once per archive) with -DEXTERNAL_INITRD_MARKER="<unique>".
 * Prints the unique marker on fd 1, then asks the kernel to power the machine
 * off via the raw reboot syscall. If the reboot syscall returns, stay alive so
 * the guest visibly fails to power down instead of silently exiting.
 *
 * No libc, no headers: raw svc only.
 */

#ifndef EXTERNAL_INITRD_MARKER
#define EXTERNAL_INITRD_MARKER "KSHIM_EXTERNAL_INITRD_MARKER_UNSET"
#endif

/* write(2) = 64, reboot(2) = 142 on arm64. */
#define SYS_WRITE 64
#define SYS_REBOOT 142

/* LINUX_REBOOT_* ABI values. */
#define REBOOT_MAGIC1 0xfee1deadL
#define REBOOT_MAGIC2 0x28121969L
#define REBOOT_POWER_OFF 0x4321fedcL

static long sys4(long nr, long a0, long a1, long a2, long a3)
{
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x8 __asm__("x8") = nr;

    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
                     : "memory");
    return x0;
}

void _start(void)
{
    static const char marker[] = EXTERNAL_INITRD_MARKER "\n";

    sys4(SYS_WRITE, 1, (long)marker, (long)(sizeof(marker) - 1), 0);
    sys4(SYS_REBOOT, REBOOT_MAGIC1, REBOOT_MAGIC2, REBOOT_POWER_OFF, 0);

    for (;;)
        __asm__ volatile("wfe");
}
