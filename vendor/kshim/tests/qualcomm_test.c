/* Host regressions for Qualcomm key events and the libfdt adapter. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfdt.h>
#include <qcom_keys.h>
#include <qcom_keys_fdt.h>

#define ARRAY_SIZE(Array) (sizeof(Array) / sizeof((Array)[0]))
#define TEST_FDT_BYTES 16384U

typedef struct {
    uint32_t Value[3];
    unsigned FailMask;
    unsigned Reads;
    uint64_t LastAddress;
} FakeKeys;

static int ReadKeys(void *Context, uint64_t Address, uint32_t *Value)
{
    FakeKeys *Bus = Context;
    unsigned Index;

    Bus->Reads++;
    Bus->LastAddress = Address;
    if (QCOM_KEY_IS_SPMI_TOKEN(Address))
        Index = 0U;
    else if (Address == 0x200U)
        Index = 1U;
    else {
        assert(Address == 0x300U);
        Index = 2U;
    }
    *Value = UINT32_MAX;
    if ((Bus->FailMask & (1U << Index)) != 0U)
        return -1;
    *Value = Bus->Value[Index];
    return 0;
}

static void TestKeyEvents(void)
{
    QcomKeyDescriptor Descriptors[3];
    QcomKeys Keys;
    QcomKeyEvent Event;
    FakeKeys Bus = {.Value = {0U, 1U, 1U}};

    assert(QcomKeyActionFromCode(115U) == QCOM_KEY_ACTION_UP);
    assert(QcomKeyActionFromCode(114U) == QCOM_KEY_ACTION_DOWN);
    assert(QcomKeyActionFromCode(116U) == QCOM_KEY_ACTION_SELECT);
    assert(QcomKeyActionFromCode(0U) == QCOM_KEY_ACTION_NONE);
    assert(QcomKeyMakePonSource(&Descriptors[0], QCOM_KEY_SPMI_TOKEN(3U, 0x1300U),
                               116U, 0x80U, 0U) == QCOM_KEYS_OK);
    assert(QcomKeyMakeGpioAt(&Descriptors[1], 0x200U, 1U, 115U, 1U) == QCOM_KEYS_OK);
    assert(QcomKeyMakeGpioAt(&Descriptors[2], 0x300U, 1U, 114U, 1U) == QCOM_KEYS_OK);
    assert(QcomKeysInitEx(&Keys, Descriptors, 3U, NULL, ReadKeys, &Bus) == QCOM_KEYS_OK);
    QcomKeysSetDebounce(&Keys, 2U);

    /* Released inputs and a one-sample bounce must not navigate the menu. */
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    Bus.Value[0] = 0x80U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    Bus.Value[0] = 0U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    Bus.Value[0] = 0x80U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_EVENT);
    assert(Event.Action == QCOM_KEY_ACTION_SELECT && Event.Type == QCOM_KEY_EVENT_PRESS);
    assert(Event.Code == 116U);
    assert(Bus.LastAddress == 0x300U);
    for (unsigned Index = 0; Index < 8U; Index++)
        assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);

    /* Simultaneous volume transitions are queued separately, once each. */
    Bus.Value[1] = 0U;
    Bus.Value[2] = 0U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_EVENT);
    assert(Event.Action == QCOM_KEY_ACTION_UP && Event.Code == 115U);
    unsigned Reads = Bus.Reads;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_EVENT);
    assert(Event.Action == QCOM_KEY_ACTION_DOWN && Event.Code == 114U);
    assert(Bus.Reads == Reads); /* Draining a queue must not sample again. */
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);

    Bus.Value[1] = 1U;
    Bus.Value[2] = 1U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    QcomKeysSetEmitRelease(&Keys, 1U);
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    Bus.Value[0] = 0U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_EVENT);
    assert(Event.Action == QCOM_KEY_ACTION_SELECT && Event.Type == QCOM_KEY_EVENT_RELEASE);
}

static void TestFailedReads(void)
{
    QcomKeyDescriptor Descriptors[2];
    QcomKeys Keys;
    QcomKeyEvent Event;
    FakeKeys Bus = {.Value = {0U, 1U, 1U}, .FailMask = 1U};

    assert(QcomKeyMakePon(&Descriptors[0], QCOM_KEY_SPMI_TOKEN(0U, 0x800U),
                          116U) == QCOM_KEYS_OK);
    assert(QcomKeyMakeGpioAt(&Descriptors[1], 0x200U, 1U, 115U, 1U) == QCOM_KEYS_OK);
    assert(QcomKeysInitEx(&Keys, Descriptors, 2U, NULL, ReadKeys, &Bus) == QCOM_KEYS_OK);
    QcomKeysSetDebounce(&Keys, 1U);
    /* Error data is all ones: it must never create an active-high Power edge. */
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_NO_EVENT);
    Bus.Value[1] = 0U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_EVENT);
    assert(Event.Action == QCOM_KEY_ACTION_UP);
    Bus.FailMask = 3U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_EIO);
    Bus.FailMask = 0U;
    Bus.Value[0] = 1U;
    assert(QcomKeysPoll(&Keys, &Event) == QCOM_KEYS_EVENT);
    assert(Event.Action == QCOM_KEY_ACTION_SELECT);
}

static void TestTokenBounds(void)
{
    QcomKeyDescriptor Descriptor;
    QcomKeys Keys;
    FakeKeys Bus = {.Value = {0x80U, 1U, 1U}};
    uint8_t Pressed = 0U;

    assert(QcomKeyMakePonSource(&Descriptor, QCOM_KEY_SPMI_TOKEN(15U, 0xffefU),
                               116U, 0x80U, 0U) == QCOM_KEYS_OK);
    assert(QcomKeysInitEx(&Keys, &Descriptor, 1U, NULL, ReadKeys, &Bus) == QCOM_KEYS_OK);
    assert(QcomKeyReadPressed(&Keys, &Descriptor, &Pressed) == QCOM_KEYS_OK);
    assert(Pressed == 1U && Bus.LastAddress == QCOM_KEY_SPMI_TOKEN(15U, 0xffffU));
    assert(QcomKeyMakePon(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, 0xfff0U),
                          116U) == QCOM_KEYS_EINVAL);
    assert(QcomKeyMakePon(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, UINT32_MAX),
                          116U) == QCOM_KEYS_EINVAL);
    assert(QcomKeyMakePon(&Descriptor, QCOM_KEY_SPMI_TOKEN(16U, 0x1300U),
                          116U) == QCOM_KEYS_EINVAL);
    assert(QcomKeyMakeGpioAt(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, 0x10000U),
                             1U, 115U, 1U) == QCOM_KEYS_EINVAL);
    assert(QcomKeyMakeGpioAt(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, 0xffffU),
                             1U, 115U, 1U) == QCOM_KEYS_OK);
    Descriptor.RegisterOffset = 1U;
    unsigned Reads = Bus.Reads;
    assert(QcomKeyReadPressed(&Keys, &Descriptor, &Pressed) == QCOM_KEYS_EINVAL);
    Descriptor.Base = QCOM_KEY_SPMI_TOKEN(3U, UINT32_MAX);
    Descriptor.RegisterOffset = 0x10U;
    assert(QcomKeyReadPressed(&Keys, &Descriptor, &Pressed) == QCOM_KEYS_EINVAL);
    assert(Bus.Reads == Reads); /* An overflow must not reach the next SID. */

    assert(QcomKeyMakeSpmiGpio(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, 0xfeefU),
                              2U, 115U, 1U) == QCOM_KEYS_OK);
    assert(Descriptor.Base == QCOM_KEY_SPMI_TOKEN(3U, 0xffefU));
    assert(QcomKeyMakeSpmiGpio(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, 0xfeefU),
                              3U, 115U, 1U) == QCOM_KEYS_EINVAL);
    assert(QcomKeyMakeSpmiGpio(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, 0xb000U),
                              0U, 115U, 1U) == QCOM_KEYS_EINVAL);
    assert(QcomKeyMakeSpmiGpio(&Descriptor, QCOM_KEY_SPMI_TOKEN(3U, 0xb000U),
                              UINT32_MAX, 115U, 1U) == QCOM_KEYS_EINVAL);
    assert(QcomKeyMakeSpmiGpio(&Descriptor, 0xb000U, 1U, 115U, 1U) == QCOM_KEYS_EINVAL);
}

static void PropertyCells(void *Blob, const char *Name, const uint32_t *Values,
                          size_t Count)
{
    fdt32_t Cells[4];
    assert(Count <= ARRAY_SIZE(Cells));
    for (size_t Index = 0; Index < Count; Index++)
        Cells[Index] = cpu_to_fdt32(Values[Index]);
    assert(fdt_property(Blob, Name, Cells, (int)(Count * sizeof(*Cells))) == 0);
}

static void GpioKey(void *Blob, const char *Name, uint32_t Code,
                    uint32_t Phandle, uint32_t Gpio, const char *Status)
{
    uint32_t Cells[] = {Phandle, Gpio, 1U};
    assert(fdt_begin_node(Blob, Name) == 0);
    assert(fdt_property_u32(Blob, "linux,code", Code) == 0);
    PropertyCells(Blob, "gpios", Cells, ARRAY_SIZE(Cells));
    if (Status != NULL)
        assert(fdt_property_string(Blob, "status", Status) == 0);
    assert(fdt_end_node(Blob) == 0);
}

/* The sequential writer preserves the Hall-before-volume container ordering. */
static void MakeTree(void *Blob)
{
    uint32_t TlmmReg[] = {0U, 0x0f100000U, 0U, 0x300000U};
    uint32_t PmicReg[] = {3U, 0U};
    const char PmicCompatible[] = "qcom,spmi-pmic";
    const char PowerCompatible[] = "qcom,pmk8550-pwrkey\0qcom,pmk8350-pwrkey";
    const char GpioCompatible[] = "qcom,spmi-gpio";

    assert(fdt_create(Blob, TEST_FDT_BYTES) == 0);
    assert(fdt_finish_reservemap(Blob) == 0);
    assert(fdt_begin_node(Blob, "") == 0);
    assert(fdt_property_u32(Blob, "#address-cells", 2U) == 0);
    assert(fdt_property_u32(Blob, "#size-cells", 2U) == 0);

    assert(fdt_begin_node(Blob, "tlmm@f100000") == 0);
    assert(fdt_property_string(Blob, "compatible", "qcom,sm8450-tlmm") == 0);
    PropertyCells(Blob, "reg", TlmmReg, ARRAY_SIZE(TlmmReg));
    assert(fdt_property_u32(Blob, "phandle", 2U) == 0);
    assert(fdt_property_u32(Blob, "#gpio-cells", 2U) == 0);
    assert(fdt_property(Blob, "gpio-controller", "", 0) == 0);
    assert(fdt_end_node(Blob) == 0);

    assert(fdt_begin_node(Blob, "spmi") == 0);
    assert(fdt_property_string(Blob, "compatible", "qcom,spmi-pmic-arb") == 0);
    assert(fdt_property_u32(Blob, "#address-cells", 2U) == 0);
    assert(fdt_property_u32(Blob, "#size-cells", 0U) == 0);
    assert(fdt_begin_node(Blob, "pmic@3") == 0);
    assert(fdt_property(Blob, "compatible", PmicCompatible, sizeof(PmicCompatible)) == 0);
    PropertyCells(Blob, "reg", PmicReg, ARRAY_SIZE(PmicReg));
    assert(fdt_property_u32(Blob, "#address-cells", 1U) == 0);
    assert(fdt_property_u32(Blob, "#size-cells", 0U) == 0);
    assert(fdt_begin_node(Blob, "pon@1300") == 0);
    assert(fdt_property_u32(Blob, "reg", 0x1300U) == 0);
    assert(fdt_begin_node(Blob, "pwrkey") == 0);
    assert(fdt_property(Blob, "compatible", PowerCompatible, sizeof(PowerCompatible)) == 0);
    assert(fdt_property_u32(Blob, "linux,code", 116U) == 0);
    assert(fdt_end_node(Blob) == 0);
    assert(fdt_end_node(Blob) == 0);
    assert(fdt_begin_node(Blob, "gpio@b000") == 0);
    assert(fdt_property(Blob, "compatible", GpioCompatible, sizeof(GpioCompatible)) == 0);
    assert(fdt_property_u32(Blob, "reg", 0xb000U) == 0);
    assert(fdt_property_u32(Blob, "phandle", 1U) == 0);
    assert(fdt_property_u32(Blob, "#gpio-cells", 2U) == 0);
    assert(fdt_property(Blob, "gpio-controller", "", 0) == 0);
    assert(fdt_end_node(Blob) == 0);
    assert(fdt_end_node(Blob) == 0);
    assert(fdt_end_node(Blob) == 0);

    assert(fdt_begin_node(Blob, "hall-keys") == 0);
    assert(fdt_property_string(Blob, "compatible", "gpio-keys") == 0);
    GpioKey(Blob, "hall", 0U, 2U, 124U, NULL);
    assert(fdt_end_node(Blob) == 0);

    assert(fdt_begin_node(Blob, "disabled-keys") == 0);
    assert(fdt_property_string(Blob, "compatible", "gpio-keys") == 0);
    assert(fdt_property_string(Blob, "status", "disabled") == 0);
    GpioKey(Blob, "up", 115U, 2U, 1U, NULL);
    assert(fdt_end_node(Blob) == 0);

    assert(fdt_begin_node(Blob, "volume-keys") == 0);
    assert(fdt_property_string(Blob, "compatible", "gpio-keys") == 0);
    for (unsigned Index = 0; Index < 12U; Index++) {
        char Name[24];
        (void)snprintf(Name, sizeof(Name), "irrelevant-%u", Index);
        GpioKey(Blob, Name, 999U, 2U, Index, NULL);
    }
    GpioKey(Blob, "disabled-up", 115U, 2U, 1U, "disabled");
    GpioKey(Blob, "up", 115U, 1U, 6U, NULL);
    GpioKey(Blob, "down", 114U, 2U, 21U, NULL);
    assert(fdt_end_node(Blob) == 0);
    assert(fdt_begin_node(Blob, "duplicate-keys") == 0);
    assert(fdt_property_string(Blob, "compatible", "gpio-keys") == 0);
    GpioKey(Blob, "up", 115U, 1U, 7U, NULL);
    GpioKey(Blob, "down", 114U, 2U, 22U, NULL);
    assert(fdt_end_node(Blob) == 0);
    assert(fdt_end_node(Blob) == 0);
    assert(fdt_finish(Blob) == 0);
    assert(fdt_open_into(Blob, Blob, TEST_FDT_BYTES) == 0);
}

static int Node(void *Blob, const char *Path)
{
    int Offset = fdt_path_offset(Blob, Path);
    assert(Offset >= 0);
    return Offset;
}

static void Disable(void *Blob, const char *Path)
{
    assert(fdt_setprop_string(Blob, Node(Blob, Path), "status", "disabled") == 0);
}

static void SetCells(void *Blob, const char *Path, const char *Name,
                     const uint32_t *Values, size_t Count)
{
    fdt32_t Cells[4];
    assert(Count <= ARRAY_SIZE(Cells));
    for (size_t Index = 0; Index < Count; Index++)
        Cells[Index] = cpu_to_fdt32(Values[Index]);
    assert(fdt_setprop(Blob, Node(Blob, Path), Name, Cells,
                       (int)(Count * sizeof(*Cells))) == 0);
}

static void ExpectActions(void *Blob, unsigned ExpectedMask)
{
    QcomKeyDescriptor Descriptors[QCOM_KEYS_MAX];
    size_t Count = 0;
    unsigned Mask = 0;
    int Result = QcomKeysBuildFromDeviceTree(Blob, Descriptors, ARRAY_SIZE(Descriptors), &Count);
    assert(Result == (ExpectedMask != 0U ? QCOM_KEYS_OK : QCOM_KEYS_EIO));
    for (size_t Index = 0; Index < Count; Index++) {
        unsigned Bit = 1U << Descriptors[Index].Action;
        assert((Mask & Bit) == 0U);
        Mask |= Bit;
    }
    if (Mask != ExpectedMask)
        fprintf(stderr, "FDT action mask: expected %#x, got %#x\n", ExpectedMask, Mask);
    assert(Mask == ExpectedMask);
}

static void TestFdt(void)
{
    union { uint64_t Alignment; unsigned char Bytes[TEST_FDT_BYTES]; } Storage;
    void *Blob = Storage.Bytes;
    QcomKeyDescriptor Descriptors[3];
    size_t Count = 0;
    unsigned Up = 1U << QCOM_KEY_ACTION_UP;
    unsigned Down = 1U << QCOM_KEY_ACTION_DOWN;
    unsigned Power = 1U << QCOM_KEY_ACTION_SELECT;

    MakeTree(Blob);
    ExpectActions(Blob, Up | Down | Power);
    assert(QcomKeysBuildFromDeviceTree(Blob, Descriptors, 3U, &Count) == QCOM_KEYS_OK);
    assert(Count == 3U); /* Duplicate compatibles/actions must not fill capacity. */
    assert(Descriptors[0].Base == QCOM_KEY_SPMI_TOKEN(3U, 0x1300U));
    assert(Descriptors[0].RegisterOffset == 0x10U && Descriptors[0].Mask == 0x80U);
    assert(Descriptors[0].ActiveLow == 0U);
    assert(Descriptors[1].Base == (QCOM_KEY_SPMI_TOKEN(3U, 0xb510U) | QCOM_KEY_PMIC_GPIO_FLAG));
    assert(Descriptors[1].ActiveLow == 1U);
    assert(Descriptors[2].Base == UINT64_C(0x0f115004));
    assert(Descriptors[2].ActiveLow == 1U);

    /* Legacy PON uses bit 0; Gen3 uses bit 7, both active high. */
    assert(fdt_setprop_string(Blob, Node(Blob, "/spmi/pmic@3/pon@1300/pwrkey"),
                              "compatible", "qcom,pm8941-pwrkey") == 0);
    assert(QcomKeysBuildFromDeviceTree(Blob, Descriptors, 3U, &Count) == QCOM_KEYS_OK);
    assert(Descriptors[0].Mask == 1U && Descriptors[0].ActiveLow == 0U);

    const char *DisabledPaths[] = {
        "/spmi/pmic@3/pon@1300/pwrkey", "/spmi/pmic@3/pon@1300",
        "/spmi/pmic@3", "/spmi", "/spmi/pmic@3/gpio@b000",
        "/tlmm@f100000", "/volume-keys", "/volume-keys/up", "/"
    };
    const unsigned DisabledMasks[] = {
        Up | Down, Up | Down, Down, Down, Down | Power,
        Up | Power, Up | Down | Power, Up | Down | Power, 0U
    };
    for (size_t Index = 0; Index < ARRAY_SIZE(DisabledPaths); Index++) {
        MakeTree(Blob);
        Disable(Blob, DisabledPaths[Index]);
        fprintf(stderr, "FDT disabled node: %s\n", DisabledPaths[Index]);
        ExpectActions(Blob, DisabledMasks[Index]);
    }

    /* Missing, truncated, or non-four-bit slave IDs cannot become SID zero. */
    MakeTree(Blob);
    assert(fdt_delprop(Blob, Node(Blob, "/spmi/pmic@3"), "reg") == 0);
    ExpectActions(Blob, Down);
    const uint32_t InvalidSids[] = {16U, 256U, UINT32_MAX};
    for (size_t Index = 0; Index < ARRAY_SIZE(InvalidSids); Index++) {
        uint32_t Reg[] = {InvalidSids[Index], 0U};
        MakeTree(Blob);
        SetCells(Blob, "/spmi/pmic@3", "reg", Reg, ARRAY_SIZE(Reg));
        ExpectActions(Blob, Down);
    }
    MakeTree(Blob);
    assert(fdt_setprop_u32(Blob, Node(Blob, "/spmi/pmic@3"), "reg", 3U) == 0);
    ExpectActions(Blob, Down);

    MakeTree(Blob);
    assert(fdt_setprop_u32(Blob, Node(Blob, "/spmi/pmic@3/pon@1300"), "reg", 0xfff0U) == 0);
    assert(fdt_setprop_u32(Blob, Node(Blob, "/spmi/pmic@3/gpio@b000"), "reg", 0xffffU) == 0);
    ExpectActions(Blob, Down);
    MakeTree(Blob);
    assert(fdt_setprop_u32(Blob, Node(Blob, "/spmi/pmic@3/pon@1300"), "reg", UINT32_MAX) == 0);
    assert(fdt_setprop_u32(Blob, Node(Blob, "/spmi/pmic@3/gpio@b000"), "reg", UINT32_MAX) == 0);
    ExpectActions(Blob, Down);
    MakeTree(Blob);
    assert(fdt_setprop_string(Blob, Node(Blob, "/spmi/pmic@3/gpio@b000"),
                              "compatible", "vendor,unrelated-gpio") == 0);
    ExpectActions(Blob, Down | Power);

    /* Recognizing "tlmm" in a string does not prove a linear GPIO layout. */
    const char *UnsupportedTlmm[] = {
        "vendor,unknown-tlmm", "qcom,sdm845-pinctrl", "qcom,sm8650-tlmm",
        "vendor,qcom,sm8450-tlmm", "qcom,sm8450-tlmm-unknown"
    };
    for (size_t Index = 0; Index < ARRAY_SIZE(UnsupportedTlmm); Index++) {
        MakeTree(Blob);
        assert(fdt_setprop_string(Blob, Node(Blob, "/tlmm@f100000"),
                                  "compatible", UnsupportedTlmm[Index]) == 0);
        ExpectActions(Blob, Up | Power);
    }

    /* Both supported layouts end at GPIO209; GPIO210 is UFS reset. */
    const char *SupportedTlmm[] = {"qcom,sm8450-tlmm", "qcom,sm8550-tlmm"};
    const uint32_t GpioBoundaries[] = {0U, 209U, 210U, UINT32_MAX};
    for (size_t Platform = 0; Platform < ARRAY_SIZE(SupportedTlmm); Platform++) {
        for (size_t Index = 0; Index < ARRAY_SIZE(GpioBoundaries); Index++) {
            uint32_t Gpio[] = {2U, GpioBoundaries[Index], 1U};
            MakeTree(Blob);
            Disable(Blob, "/duplicate-keys");
            assert(fdt_setprop_string(Blob, Node(Blob, "/tlmm@f100000"),
                                      "compatible", SupportedTlmm[Platform]) == 0);
            SetCells(Blob, "/volume-keys/down", "gpios", Gpio, ARRAY_SIZE(Gpio));
            if (GpioBoundaries[Index] >= 210U) {
                ExpectActions(Blob, Up | Power);
            } else {
                ExpectActions(Blob, Up | Down | Power);
                assert(QcomKeysBuildFromDeviceTree(Blob, Descriptors, 3U, &Count) == QCOM_KEYS_OK);
                assert(Count == 3U);
                assert(Descriptors[2].Base == UINT64_C(0x0f100004) +
                       UINT64_C(0x1000) * GpioBoundaries[Index]);
            }
        }
    }

    /* A 64-bit peripheral address must be rejected before token truncation. */
    MakeTree(Blob);
    uint32_t PonReg64[] = {1U, 0x1300U};
    uint32_t GpioReg64[] = {1U, 0xb000U};
    assert(fdt_setprop_u32(Blob, Node(Blob, "/spmi/pmic@3"), "#address-cells", 2U) == 0);
    SetCells(Blob, "/spmi/pmic@3/pon@1300", "reg", PonReg64, ARRAY_SIZE(PonReg64));
    SetCells(Blob, "/spmi/pmic@3/gpio@b000", "reg", GpioReg64, ARRAY_SIZE(GpioReg64));
    ExpectActions(Blob, Down);
}

static void TestHdkDtb(const char *Environment, const char *Board,
                       uint64_t UpToken, uint64_t PonToken,
                       uint32_t PowerMask, uint32_t DownMask)
{
    const char *Path = getenv(Environment);
    if (Path == NULL)
        return;
    FILE *File = fopen(Path, "rb");
    assert(File != NULL && fseek(File, 0, SEEK_END) == 0);
    long Length = ftell(File);
    assert(Length > 0 && fseek(File, 0, SEEK_SET) == 0);
    void *Blob = malloc((size_t)Length);
    assert(Blob != NULL && fread(Blob, 1, (size_t)Length, File) == (size_t)Length);
    assert(fclose(File) == 0 && fdt_check_header(Blob) == 0);
    assert(fdt_totalsize(Blob) <= (size_t)Length);
    QcomKeyDescriptor Keys[3];
    size_t Count = 0;
    assert(QcomKeysBuildFromDeviceTree(Blob, Keys, 3, &Count) == QCOM_KEYS_OK);
    assert(Count == 3);
    unsigned Seen = 0;
    for (size_t Index = 0; Index < Count; Index++) {
        QcomKeyDescriptor *Key = &Keys[Index];
        uint64_t Expected;
        if (Key->Action == QCOM_KEY_ACTION_UP) {
            Expected = UpToken;
            assert(Key->Mask == 1 && Key->ActiveLow == 1);
        } else {
            Expected = PonToken;
            assert(Key->Mask == (Key->Action == QCOM_KEY_ACTION_SELECT ? PowerMask : DownMask));
            assert(Key->ActiveLow == 0);
        }
        assert(Key->Base + Key->RegisterOffset == Expected);
        Seen |= 1U << Key->Action;
    }
    assert(Seen == ((1U << QCOM_KEY_ACTION_UP) | (1U << QCOM_KEY_ACTION_DOWN) |
                    (1U << QCOM_KEY_ACTION_SELECT)));
    free(Blob);
    printf("%s real DTB matches manual PON/PMIC GPIO descriptors\n", Board);
}

int main(void)
{
    TestKeyEvents();
    TestFailedReads();
    TestTokenBounds();
    TestFdt();
    TestHdkDtb("KSHIM_TEST_HDK_DTB", "HDK8450",
               QCOM_KEY_SPMI_TOKEN(1, 0x8d10), QCOM_KEY_SPMI_TOKEN(0, 0x1310),
               0x80, 0x40);
    TestHdkDtb("KSHIM_TEST_HDK8150_DTB", "HDK8150",
               QCOM_KEY_SPMI_TOKEN(0, 0xc510), QCOM_KEY_SPMI_TOKEN(0, 0x0810),
               0x1, 0x2);
    puts("Qualcomm key/FDT regressions passed");
    return 0;
}
