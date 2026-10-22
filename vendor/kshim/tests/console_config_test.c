/* SPDX-License-Identifier: MIT */
/* Regression coverage for the compile-time debug-console switch
 * (CONFIG_KSHIM_DEBUG_CONSOLE). The real src/drivers/console.c is compiled
 * into both executables; only its dependency boundary (DT validate/probe,
 * GENI polled UART, PL011) is stubbed here, so no real MMIO or FDT is touched.
 *
 *   console_default_test  TEST_CONSOLE_DISABLED=0: unchanged default console.
 *   console_disabled_test TEST_CONSOLE_DISABLED=1: the switch makes every
 *                         entry point inert and crosses no dependency edge. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libfdt.h>
#include <kshim_console.h>
#include <dt.h>
#include <pl011_uart.h>
#include <Library/cr_debug_uart_poll.h>
#include <Library/cr_geni.h>

/* A tiny aligned FDT blob: only magic + totalsize are read by the inline
 * fdt_totalsize used by kshim_console_init, so the buffer must be aligned and
 * at least a full header. Validation/probing is stubbed, so no tree is parsed. */
static _Alignas(8) unsigned char TinyFdt[64];
static void BuildTinyFdt(void)
{
    memset(TinyFdt, 0, sizeof TinyFdt);
    struct fdt_header *header = (struct fdt_header *)TinyFdt;
    header->magic = cpu_to_fdt32(0xd00dfeedU); /* FDT_MAGIC */
    header->totalsize = cpu_to_fdt32(sizeof TinyFdt);
}

#if TEST_CONSOLE_DISABLED

/* Dependency edges a disabled console must never cross. */
static unsigned DtCalls, ProbeCalls, IoCalls, GeniInitCalls, GeniWriteCalls,
    GeniReadCalls, GeniQuiesceCalls, Pl011InitCalls, Pl011WriteCalls,
    Pl011ReadCalls;
static unsigned DependencyCalls(void)
{
    return DtCalls + ProbeCalls + IoCalls + GeniInitCalls + GeniWriteCalls +
           GeniReadCalls + GeniQuiesceCalls + Pl011InitCalls + Pl011WriteCalls +
           Pl011ReadCalls;
}

int dt_validate(const void *fdt, size_t available)
{ (void)fdt; (void)available; ++DtCalls; return 0; }
int kshim_console_probe(const void *fdt, struct kshim_console_resource *resource)
{
    (void)fdt; ++ProbeCalls;
    resource->base = 0x400000; resource->size = 0x1000; resource->geni = true;
    return 0;
}
const struct CrIo *CrIoDefault(void)
{ static const struct CrIo io = {0}; ++IoCalls; return &io; }
int CrDebugUartPollInit(struct CrDebugUartPoll *uart)
{ (void)uart; ++GeniInitCalls; return 0; }
int CrDebugUartPollWrite(struct CrDebugUartPoll *uart, const void *data, size_t length)
{ (void)uart; (void)data; (void)length; ++GeniWriteCalls; return 0; }
int CrDebugUartPollRead(struct CrDebugUartPoll *uart, uint8_t *byte)
{ (void)uart; (void)byte; ++GeniReadCalls; return 0; }
int CrDebugUartPollQuiesce(struct CrDebugUartPoll *uart)
{ (void)uart; ++GeniQuiesceCalls; return 0; }
void pl011_init(uint64_t mmio_base)
{ (void)mmio_base; ++Pl011InitCalls; }
void pl011_write(uint64_t mmio_base, char *str, int len)
{ (void)mmio_base; (void)str; (void)len; ++Pl011WriteCalls; }
int pl011_try_read(uint64_t mmio_base, char *character)
{ (void)mmio_base; (void)character; ++Pl011ReadCalls; return 0; }

int main(void)
{
    BuildTinyFdt();
    char byte = 0x5a;

    /* Every entry point is already inert before the first init. */
    assert(kshim_console_quiesce() == 0);
    kshim_console_write("pre", 3);
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x5a);
    assert(kshim_console_base() == 0 && kshim_console_size() == 0);
    assert(DependencyCalls() == 0);

    /* The switch disables the console at compile time: even a well-formed blob
     * must not run DT validation/probe or any GENI/PL011/MMIO code. */
    int status = kshim_console_init(TinyFdt);
    assert(DependencyCalls() == 0);
    assert(status == -1);

    /* All entry points stay inert regardless of arguments. */
    kshim_console_write(NULL, 0);
    kshim_console_write("ignored", 7);
    byte = 0x33;
    assert(kshim_console_try_read(NULL) == 0);
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x33);
    assert(kshim_console_base() == 0 && kshim_console_size() == 0);
    assert(kshim_console_quiesce() == 0);
    assert(kshim_console_quiesce() == 0);

    /* A non-null invalid blob must be ignored too, never dereferenced. */
    assert(kshim_console_init((const void *)(uintptr_t)0x10) == -1);
    assert(kshim_console_init(NULL) == -1);
    assert(kshim_console_init(TinyFdt) == -1);
    kshim_console_write("ignored", 7);
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x33);
    assert(kshim_console_quiesce() == 0);

    assert(DependencyCalls() == 0);
    puts("PASS: debug console disabled is fully inert");
    return 0;
}

#else /* TEST_CONSOLE_DISABLED == 0: preserve the existing default console. */

static unsigned DtCalls, ProbeCalls, IoCalls, GeniInitCalls, GeniWriteCalls,
    GeniReadCalls, GeniQuiesceCalls, Pl011InitCalls, Pl011WriteCalls,
    Pl011ReadCalls;
static int DtResult, ProbeResult, GeniInitResult, GeniQuiesceResult;
static int GeniReadResult, Pl011ReadResult;
static uint8_t GeniReadByte;
static char Pl011ReadByte;
static uintptr_t ProbeBase;
static size_t ProbeSize;
static bool ProbeGeni;
static uint64_t Pl011InitBase;
static char GeniWritten[8], Pl011Written[8];
static int GeniWrittenLen, Pl011WrittenLen;

static void ResetCalls(void)
{
    DtCalls = ProbeCalls = IoCalls = GeniInitCalls = GeniWriteCalls =
        GeniReadCalls = GeniQuiesceCalls = Pl011InitCalls = Pl011WriteCalls =
        Pl011ReadCalls = 0;
}

int dt_validate(const void *fdt, size_t available)
{ (void)fdt; (void)available; ++DtCalls; return DtResult; }
int kshim_console_probe(const void *fdt, struct kshim_console_resource *resource)
{
    (void)fdt; ++ProbeCalls;
    resource->base = ProbeBase; resource->size = ProbeSize;
    resource->geni = ProbeGeni; return ProbeResult;
}
const struct CrIo *CrIoDefault(void)
{ static const struct CrIo io = {0}; ++IoCalls; return &io; }
int CrDebugUartPollInit(struct CrDebugUartPoll *uart)
{ (void)uart; ++GeniInitCalls; return GeniInitResult; }
int CrDebugUartPollWrite(struct CrDebugUartPoll *uart, const void *data, size_t length)
{
    (void)uart; ++GeniWriteCalls; GeniWrittenLen = (int)length;
    memcpy(GeniWritten, data, length < sizeof GeniWritten ? length : sizeof GeniWritten);
    return 0;
}
int CrDebugUartPollRead(struct CrDebugUartPoll *uart, uint8_t *byte)
{ (void)uart; ++GeniReadCalls; if (GeniReadResult > 0) *byte = GeniReadByte; return GeniReadResult; }
int CrDebugUartPollQuiesce(struct CrDebugUartPoll *uart)
{ (void)uart; ++GeniQuiesceCalls; return GeniQuiesceResult; }
void pl011_init(uint64_t mmio_base)
{ ++Pl011InitCalls; Pl011InitBase = mmio_base; }
void pl011_write(uint64_t mmio_base, char *str, int len)
{
    (void)mmio_base; ++Pl011WriteCalls; Pl011WrittenLen = len;
    memcpy(Pl011Written, str, (size_t)len < sizeof Pl011Written ? (size_t)len : sizeof Pl011Written);
}
int pl011_try_read(uint64_t mmio_base, char *character)
{ (void)mmio_base; ++Pl011ReadCalls; if (Pl011ReadResult > 0) *character = Pl011ReadByte; return Pl011ReadResult; }

int main(void)
{
    BuildTinyFdt();
    char byte = 0x5a;

    /* Uninitialized console: writes and reads are inert, no dependency runs. */
    assert(kshim_console_quiesce() == 0);
    kshim_console_write("pre", 3);
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x5a);
    assert(kshim_console_base() == 0 && kshim_console_size() == 0);

    /* Rejected inputs return -1 before any probe. */
    ResetCalls(); DtResult = 1; /* dt_validate rejects the blob */
    assert(kshim_console_init(TinyFdt) == -1);
    assert(DtCalls == 1 && ProbeCalls == 0);
    assert(kshim_console_init(NULL) == -1);
    assert(DtCalls == 1 && ProbeCalls == 0);

    /* GENI profile: a successful init crosses each edge exactly once. */
    ResetCalls();
    DtResult = 0; ProbeResult = 0; ProbeGeni = true; ProbeBase = 0x400000;
    ProbeSize = 0x1000; GeniInitResult = 0;
    assert(kshim_console_init(TinyFdt) == 0);
    assert(DtCalls == 1 && ProbeCalls == 1 && IoCalls == 1 && GeniInitCalls == 1);
    assert(kshim_console_base() == 0x400000 && kshim_console_size() == 0x1000);

    /* A repeated init is idempotent. */
    assert(kshim_console_init(TinyFdt) == 0);
    assert(DtCalls == 1 && ProbeCalls == 1 && IoCalls == 1 && GeniInitCalls == 1);

    /* Writes and reads dispatch to the GENI poller. */
    kshim_console_write("hi", 2);
    assert(GeniWriteCalls == 1 && GeniWrittenLen == 2 && memcmp(GeniWritten, "hi", 2) == 0);
    GeniReadResult = 1; GeniReadByte = 0x41;
    assert(kshim_console_try_read(&byte) == 1 && byte == 0x41 && GeniReadCalls == 1);
    GeniReadResult = 0; byte = 0x22;
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x22); /* empty -> 0 */
    GeniReadResult = -5;
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x22); /* error maps to 0 */

    /* A failed quiesce keeps the console initialized and visible. */
    GeniQuiesceResult = 1;
    assert(kshim_console_quiesce() == 1);
    assert(GeniQuiesceCalls == 1);
    assert(kshim_console_base() == 0x400000 && kshim_console_size() == 0x1000);

    /* A later successful quiesce clears the visible state. */
    GeniQuiesceResult = 0;
    assert(kshim_console_quiesce() == 0);
    assert(GeniQuiesceCalls == 2);
    assert(kshim_console_base() == 0 && kshim_console_size() == 0);
    byte = 0x33;
    kshim_console_write("ignored", 7);
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x33);
    assert(GeniWriteCalls == 1 && GeniReadCalls == 3);

    /* PL011 profile dispatches through the PL011 driver only. */
    ResetCalls();
    ProbeGeni = false; ProbeBase = 0x500000; ProbeSize = 0x800;
    assert(kshim_console_init(TinyFdt) == 0);
    assert(DtCalls == 1 && ProbeCalls == 1 && Pl011InitCalls == 1);
    assert(Pl011InitBase == 0x500000 && IoCalls == 0 && GeniInitCalls == 0);
    assert(kshim_console_base() == 0x500000 && kshim_console_size() == 0x800);
    kshim_console_write("A", 1);
    assert(Pl011WriteCalls == 1 && Pl011WrittenLen == 1 && Pl011Written[0] == 'A');
    Pl011ReadResult = 1; Pl011ReadByte = 0x37;
    assert(kshim_console_try_read(&byte) == 1 && byte == 0x37 && Pl011ReadCalls == 1);
    assert(kshim_console_quiesce() == 0);
    assert(GeniQuiesceCalls == 0); /* PL011 never polls the GENI UART */
    assert(kshim_console_base() == 0 && kshim_console_size() == 0);
    byte = 0x44;
    kshim_console_write("ignored", 7);
    assert(kshim_console_try_read(&byte) == 0 && byte == 0x44);
    assert(Pl011WriteCalls == 1 && Pl011ReadCalls == 1);

    puts("PASS: default debug console behaviour preserved");
    return 0;
}

#endif
