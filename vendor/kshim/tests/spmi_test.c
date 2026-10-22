#include "spmi_config.h"
/* PMIC Arbiter v1/v2/v3/v5/v7/v8/v8.5 host tests. Compile both translation units with
 * -include tests/spmi_config.h; no physical MMIO mappings are created. */
#include "spmi_config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <qcom_key_platform.h>
#include <qcom_keys.h>

uint32_t kshim_test_spmi_core[TEST_SPMI_CORE_BYTES / 4U];
uint32_t kshim_test_spmi_observer[TEST_SPMI_OBSERVER_BYTES / 4U];
uint32_t kshim_test_spmi_config[TEST_SPMI_CONFIG_BYTES / 4U];
uint32_t kshim_test_spmi_channels[1];
uint32_t kshim_test_spmi_map[TEST_SPMI_MAP_BYTES / 4U];
uint32_t kshim_test_spmi_owner[TEST_SPMI_MAP_BYTES / 4U];
uint64_t kshim_test_spmi_core_size = TEST_SPMI_CORE_BYTES;
uint64_t kshim_test_spmi_observer_size = TEST_SPMI_OBSERVER_BYTES;
uint64_t kshim_test_spmi_config_size = TEST_SPMI_CONFIG_BYTES;
uint64_t kshim_test_spmi_map_size = TEST_SPMI_MAP_BYTES;
uint64_t kshim_test_spmi_owner_size = TEST_SPMI_MAP_BYTES;
unsigned kshim_test_spmi_ee = 2U;
unsigned kshim_test_spmi_bus;
unsigned kshim_test_spmi_channel;

static uint32_t *StatusRegister;
static unsigned ElapsedUs;
static unsigned CompleteAfterUs;
static uint32_t CompletionStatus;
static uint32_t *ReadBase = kshim_test_spmi_observer;
static uint32_t ExtraCommandBits;
static unsigned ErrorLimit = 8U;
static int HasApidMap = 1;

static int test_spmi_read32(void *ctx, uint8_t sid, uint32_t address, uint32_t *value)
{
    static SpmiDeviceContext controller;
    static int bound;
    if (!bound) {
#define REGION(Type, Base, Size) controller.Regions[SPMI_MEMORY_REGION_TYPE_##Type] = (SpmiMemoryRegion){(uintptr_t)(Base), (size_t)(Size)}
        REGION(CORE, kshim_test_spmi_core, kshim_test_spmi_core_size);
        REGION(CH_SLAVES, kshim_test_spmi_channels, sizeof(kshim_test_spmi_channels));
        REGION(OBSERVER, kshim_test_spmi_observer, kshim_test_spmi_observer_size);
        REGION(INTERRUPT, kshim_test_spmi_config, kshim_test_spmi_config_size);
        REGION(CONFIG, kshim_test_spmi_config, kshim_test_spmi_config_size);
        REGION(CH_MAP, kshim_test_spmi_map, kshim_test_spmi_map_size);
        REGION(CH_OWNER, kshim_test_spmi_owner, kshim_test_spmi_owner_size);
#undef REGION
        controller.ActiveEE = kshim_test_spmi_ee;
        controller.BusId = kshim_test_spmi_bus;
        controller.Channel = kshim_test_spmi_channel;
        SpmiDeviceContext *contexts[] = {&controller};
        kshim_qcom_spmi_bind(contexts, 1);
        bound = 1;
    }
    return kshim_qcom_spmi_read32(ctx, sid, address, value);
}

void kshim_qcom_spmi_delay_us(uint32_t Microseconds)
{
    assert(Microseconds == 1U);
    ElapsedUs += Microseconds;
    if (CompleteAfterUs != 0U && ElapsedUs == CompleteAfterUs)
        *StatusRegister = CompletionStatus;
}

static uint32_t *Response(unsigned Offset, unsigned CompleteAfter,
                          uint32_t Status, uint32_t Data)
{
    uint32_t *Channel = &ReadBase[Offset / 4U];
    assert(Offset + 0x1cU <= TEST_SPMI_OBSERVER_BYTES);
    Channel[0] = 0U;
    StatusRegister = &Channel[0x08U / 4U];
    *StatusRegister = CompleteAfter == 0U ? Status : 0U;
    Channel[0x18U / 4U] = Data;
    ElapsedUs = 0U;
    CompleteAfterUs = CompleteAfter;
    CompletionStatus = Status;
    return Channel;
}

static void ExerciseChannel(unsigned Offset)
{
    uint32_t Value = 0x12345678U;
    uint32_t *Channel = Response(Offset, 200U, 1U, 0xdead005aU);
    assert(test_spmi_read32(NULL, 3U, 0x220U, &Value) == 0);
    assert(Value == 0x5aU && ElapsedUs == 200U);
    assert(Channel[0] == ((1U << 27) | (0x20U << 4) | ExtraCommandBits));

    (void)Response(Offset, 999U, 1U, 0xa5U);
    assert(kshim_qcom_key_read32_status(NULL, QCOM_KEY_SPMI_RESOURCE(0, kshim_test_spmi_bus, 3U, 0x220U),
                                       &Value) == 0);
    assert(Value == 0xa5U && ElapsedUs == 999U);

    (void)Response(Offset, 1001U, 1U, 0xa5U);
    Value = 0x12345678U;
    assert(test_spmi_read32(NULL, 3U, 0x220U, &Value) != 0);
    assert(ElapsedUs == 1000U && Value == 0x12345678U);

    /* Transport errors must not leak a stale data byte into a key sample. */
    for (unsigned Error = 2U; Error <= ErrorLimit; Error <<= 1) {
        (void)Response(Offset, 3U, 1U | Error, 0xffU);
        Value = 0x12345678U;
        assert(test_spmi_read32(NULL, 3U, 0x220U, &Value) != 0);
        assert(ElapsedUs == 3U && Value == 0x12345678U);
    }
    (void)Response(Offset, 0U, 1U, 0xffU);
    assert(test_spmi_read32(NULL, 16U, 0x220U, &Value) != 0);
    assert(test_spmi_read32(NULL, 3U, 0x10000U, &Value) != 0);
    if (HasApidMap)
        assert(test_spmi_read32(NULL, 3U, 0x330U, &Value) != 0);
    assert(Channel[0] == 0U && ElapsedUs == 0U);
}

static void TestV1(void)
{
    kshim_test_spmi_core[0] = 0x10000000U;
    kshim_test_spmi_channel = 5U;
    ReadBase = kshim_test_spmi_core;
    ExtraCommandBits = (3U << 20) | (2U << 12);
    HasApidMap = 0;
    ExerciseChannel(0x800U + 0x80U * 5U);
}

static void TestV2(void)
{
    kshim_test_spmi_core[0] = 0x20010000U;
    kshim_test_spmi_core[(0x800U + 4U * 511U) / 4U] = 0x302U << 8;
    kshim_test_spmi_config[(0x700U + 4U * 511U) / 4U] = 2U;
    ExerciseChannel(0x2000U + 0x8000U * 511U);
}

static void TestV3(void)
{
    kshim_test_spmi_core[0] = 0x30000000U;
    kshim_test_spmi_core[(0x800U + 4U * 511U) / 4U] = 0x302U << 8;
    kshim_test_spmi_config[(0x700U + 4U * 511U) / 4U] = 2U;
    ExerciseChannel(0x2000U + 0x8000U * 511U);
}

static void TestV8(void)
{
    kshim_test_spmi_core[0] = 0x80000000U;
    kshim_test_spmi_core[1] = 8191U;
    kshim_test_spmi_map[8190U] = 0x302U | (1U << 31);
    kshim_test_spmi_owner[8190U] = 2U;
    ExerciseChannel(0x80000U + 0x20U * 8190U);
}

static void TestV8P5(void)
{
    kshim_test_spmi_bus = 3U;
    kshim_test_spmi_core[0] = 0x80050000U;
    for (unsigned Index = 1; Index <= 4; Index++)
        kshim_test_spmi_core[Index] = 2048U;
    kshim_test_spmi_map[8191U] = 0x302U | (1U << 31);
    kshim_test_spmi_owner[2047U] = 2U;
    kshim_test_spmi_owner_size = 2048U * 4U;
    ErrorLimit = 64U;
    ExerciseChannel(0x80000U + 0x20U * 8191U);
}

static void TestV5(void)
{
    kshim_test_spmi_core[0] = 0x50000000U;
    kshim_test_spmi_core[1] = 512U;
    /* Highest v5 APID, including its owner-table and EE offsets. */
    kshim_test_spmi_core[(0x900U + 4U * 511U) / 4U] = 0x302U << 8;
    kshim_test_spmi_config[(0x700U + 4U * 511U) / 4U] = 2U;
    ExerciseChannel(0x20000U + 0x80U * 511U);
}

static void TestV7(void)
{
    kshim_test_spmi_core[0] = 0x70000000U;
    kshim_test_spmi_core[1] = 1024U;
    /* The count needs bit 10: a ten-bit FEATURES mask breaks this case. */
    kshim_test_spmi_core[(0x2000U + 4U * 1023U) / 4U] = 0x302U << 8;
    kshim_test_spmi_config[1023U] = 2U;
    ExerciseChannel(0x10000U + 0x20U * 1023U);
}

static void TestV7SecondaryBus(void)
{
    kshim_test_spmi_bus = 1U;
    kshim_test_spmi_core[0] = 0x70000000U;
    kshim_test_spmi_core[1] = 600U;
    kshim_test_spmi_core[2] = 424U;
    /* APID mappings stay global; the secondary owner table starts at zero. */
    kshim_test_spmi_core[(0x2000U + 4U * 1023U) / 4U] = 0x302U << 8;
    kshim_test_spmi_config[423U] = 2U;
    kshim_test_spmi_config_size = 424U * 4U;
    ExerciseChannel(0x10000U + 0x20U * 1023U);
}

static void TestDuplicatePpid(void)
{
    uint32_t Value = 0U;
    kshim_test_spmi_core[0] = 0x70000000U;
    kshim_test_spmi_core[1] = 3U;
    kshim_test_spmi_core[0x2000U / 4U] = 0x302U << 8;
    kshim_test_spmi_core[0x2004U / 4U] = 0x302U << 8;
    kshim_test_spmi_core[0x2008U / 4U] = (0x302U << 8) | (1U << 24);
    kshim_test_spmi_config[0] = 0U;
    kshim_test_spmi_config[1] = 2U;
    kshim_test_spmi_config[2] = 1U;
    uint32_t *Channel = Response(0x10020U, 0U, 1U, 0x5aU);
    assert(test_spmi_read32(NULL, 3U, 0x220U, &Value) == 0);
    assert(Value == 0x5aU && Channel[0] != 0U);
    assert(kshim_test_spmi_observer[0x10000U / 4U] == 0U);
    assert(kshim_test_spmi_observer[0x10040U / 4U] == 0U);
}

static void TestInvalidCount(void)
{
    const uint32_t Cases[][4] = {
        {0x50000000U, 513U, 0U, 0U},
        {0x70000000U, 1025U, 0U, 0U},
        {0x70000000U, 600U, 425U, 1U},
        {0x70000000U, 0U, 0U, 0U},
        {0x50000000U, 1U, 0U, 1U},
        {0x70000000U, 1U, 0U, 2U},
        {0x80000000U, 1U, 0U, 4U}
    };
    for (size_t Index = 0; Index < sizeof(Cases) / sizeof(Cases[0]); Index++) {
        uint32_t Value = 0x12345678U;
        kshim_test_spmi_core[0] = Cases[Index][0];
        kshim_test_spmi_core[1] = Cases[Index][1];
        kshim_test_spmi_core[2] = Cases[Index][2];
        kshim_test_spmi_bus = Cases[Index][3];
        assert(test_spmi_read32(NULL, 3U, 0x220U, &Value) != 0);
        assert(Value == 0x12345678U && ElapsedUs == 0U);
    }
}

static void TestShortWindow(void)
{
    uint32_t Value = 0x12345678U;
    kshim_test_spmi_core[0] = 0x70000000U;
    kshim_test_spmi_core[1] = 1U;
    kshim_test_spmi_core[0x2000U / 4U] = 0x302U << 8;
    kshim_test_spmi_config[0] = 2U;
    /* The status register fits, but RDATA0 is outside the declared window. */
    kshim_test_spmi_observer_size = 0x10018U;
    (void)Response(0x10000U, 0U, 1U, 0xffU);
    assert(test_spmi_read32(NULL, 3U, 0x220U, &Value) != 0);
    assert(Value == 0x12345678U && ElapsedUs == 0U);
    assert(kshim_test_spmi_observer[0x10000U / 4U] == 0U);
}

static void TestMmioDispatch(void)
{
    uint32_t Register = 0x1234abcdU;
    uint32_t Value = 0U;
    assert(kshim_qcom_key_read32_status(NULL, (uintptr_t)&Register, &Value) == 0);
    assert(Value == (Register & 1U));
    Register &= ~1U;
    assert(kshim_qcom_key_read32_status(NULL, (uintptr_t)&Register, &Value) == 0);
    assert(Value == 0U); /* Output/configuration bits are not the GPIO input. */
    assert(kshim_qcom_key_read32_status(NULL, 0U, &Value) != 0);
    assert(kshim_qcom_key_read32_status(NULL, (uintptr_t)&Register, NULL) != 0);
}

static void RunIsolated(const char *Name, void (*Test)(void))
{
    /* Each child starts with the production driver's static context uninitialized. */
    fflush(NULL);
    pid_t Child = fork();
    assert(Child >= 0);
    if (Child == 0) {
        Test();
        exit(0);
    }
    int Status;
    assert(waitpid(Child, &Status, 0) == Child);
    if (!WIFEXITED(Status) || WEXITSTATUS(Status) != 0) {
        fprintf(stderr, "SPMI case failed: %s\n", Name);
        exit(1);
    }
    printf("SPMI case passed: %s\n", Name);
}

int main(void)
{
    RunIsolated("v1 channel 5 and full SID/address command", TestV1);
    RunIsolated("v2 APID 511 and observer layout", TestV2);
    RunIsolated("v3 APID 511 and observer layout", TestV3);
    RunIsolated("v5 APID 511, delayed completion, timeout and errors", TestV5);
    RunIsolated("v7 1024 APIDs, delayed completion, timeout and errors", TestV7);
    RunIsolated("v7 secondary bus owner indexing", TestV7SecondaryBus);
    RunIsolated("v8 separate map and owner windows, APID 8190", TestV8);
    RunIsolated("v8.5 bus 3, APID 8191, CRC/parity/NACK/denied/dropped", TestV8P5);
    RunIsolated("duplicate PPID prefers the current EE", TestDuplicatePpid);
    RunIsolated("invalid APID count", TestInvalidCount);
    RunIsolated("short observer window", TestShortWindow);
    RunIsolated("CrDK TLMM input dispatch", TestMmioDispatch);
    puts("Qualcomm SPMI regressions passed");
    return 0;
}
