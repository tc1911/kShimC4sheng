/*
 * Qualcomm key device-tree adapter.
 *
 * The standard Qualcomm bindings describe PMIC PON keys as children of a
 * `pon` peripheral and volume keys as children of `gpio-keys`.  PMIC `reg`
 * values are SPMI peripheral offsets rather than CPU addresses; this file
 * tags them and encodes SID plus offset so a platform SPMI reader can decode
 * the token without losing the PMIC identity.
 */
#include <qcom_keys_fdt.h>
#include <qcom_dt.h>

#include <libfdt.h>

#include <stdint.h>

static int QcomFdtStringContains(
    const char *Value, int Length, const char *Needle)
{
  int Start;
  int NeedleLength = 0;

  if (Value == NULL || Needle == NULL || Length <= 0)
    return 0;
  while (Needle[NeedleLength] != '\0')
    NeedleLength++;
  if (NeedleLength == 0 || NeedleLength > Length)
    return 0;

  for (Start = 0; Start + NeedleLength <= Length; Start++) {
    int Index;
    for (Index = 0; Index < NeedleLength; Index++) {
      if (Value[Start + Index] != Needle[Index])
        break;
    }
    if (Index == NeedleLength)
      return 1;
  }
  return 0;
}

/* Accept only Qualcomm SPMI/PMIC GPIO compatibles here.  A generic
 * `*-gpio` controller may use a completely different register layout and
 * must not be interpreted as a PMIC peripheral. */
static int QcomFdtIsPmicGpioCompatible(const char *Value, int Length)
{
  int Start = 0;

  if (Value == NULL || Length <= 0)
    return 0;
  while (Start < Length) {
    int End = Start;
    int Prefix;

    while (End < Length && Value[End] != '\0')
      End++;
    Prefix = QcomFdtStringContains(Value + Start, End - Start, "qcom,pm") ||
             QcomFdtStringContains(Value + Start, End - Start, "qcom,spmi");
    if (Prefix && End - Start >= 5 &&
        Value[End - 5] == '-' && Value[End - 4] == 'g' &&
        Value[End - 3] == 'p' && Value[End - 2] == 'i' &&
        Value[End - 1] == 'o')
      return 1;
    if (End == Length)
      break;
    Start = End + 1;
  }
  return 0;
}

#define QcomFdtNodeAvailable dt_available

/* Only these verified TLMM layouts use base + gpio * 0x1000 + 4 for
 * ordinary GPIO inputs.  The Qualcomm vendor canoe driver adds an extra
 * 0x100000 register-bank bias on top of its 0xf000000 DT base, so the
 * canoe linear fallback adds 0x100000 to the pin offset; kaanapali's DT
 * base (0xf100000) already folds in that shift and must not be biased
 * again.  Regular GPIOs cover 0..216 on canoe and kaanapali (0..209 on
 * sm8450 and sm8550); the final pin (217, resp. 210) is a special
 * UFS-reset output and is excluded from the linear count.  Other SoCs
 * can have multiple tiles and require manual input register descriptors
 * until their complete layout is supported here. */
static uint32_t QcomFdtLinearTlmmGpioCount(const void *Blob, int Provider)
{
  static const struct {
    const char *Compatible;
    uint32_t GpioCount;
  } Layouts[] = {
      {"qcom,canoe-tlmm", 217U},
      {"qcom,kaanapali-tlmm", 217U},
      {"qcom,sm8450-tlmm", 210U},
      {"qcom,sm8550-tlmm", 210U},
  };
  size_t Index;

  for (Index = 0; Index < sizeof(Layouts) / sizeof(Layouts[0]); Index++) {
    if (fdt_node_check_compatible(Blob, Provider,
                                  Layouts[Index].Compatible) == 0)
      return Layouts[Index].GpioCount;
  }
  return 0U;
}

#define QcomFdtReadU32 dt_u32

/* PMIC offsets use the SPMI binding; they are never CPU addresses. */
static int QcomFdtReadReg(const void *f, int node, uint64_t *address, int *owner)
{
    for (int n = node; n > 0; n = dt_parent_offset(f, n)) {
        if (!fdt_getprop(f, n, "reg", NULL)) continue;
        uint16_t offset;
        if (kshim_dt_spmi_reg(f, n, &offset)) return -1;
        *address = offset; *owner = n;
        return 0;
    }
    return -1;
}

static int QcomFdtGetCompatible(
    const void *Blob, int Node, const char **Compatible, int *Length)
{
  if (Blob == NULL || Compatible == NULL || Length == NULL)
    return -1;
  *Compatible = fdt_getprop(Blob, Node, "compatible", Length);
  return *Compatible == NULL ? -1 : 0;
}

static int QcomFdtFindCompatible(
    void *Context, const char *Compatible, int StartNode)
{
  QcomKeyFdtAdapter *Adapter = (QcomKeyFdtAdapter *)Context;
  int Node;

  if (Adapter == NULL || Adapter->Blob == NULL || Compatible == NULL)
    return -1;
  Node = StartNode;
  for (;;) {
    Node = dt_node_offset_by_compatible(
        Adapter->Blob, StartNode, Compatible);
    if (Node < 0 || QcomFdtNodeAvailable(Adapter->Blob, Node))
      return Node;
    StartNode = Node;
  }
}

static int QcomFdtGetReg(
    void *Context, int Node, uint64_t *AddressToken)
{
  QcomKeyFdtAdapter *Adapter = (QcomKeyFdtAdapter *)Context;
  uint64_t Address;
  int Owner;
  uint8_t Sid, Controller, Bus;

  if (Adapter == NULL || Adapter->Blob == NULL || AddressToken == NULL ||
      QcomFdtReadReg(Adapter->Blob, Node, &Address, &Owner) != 0)
    return -1;

  if (Address > UINT16_MAX ||
      kshim_dt_spmi_identity(Adapter->Blob, Owner, &Controller, &Bus, &Sid) != 0)
    return -1;
  *AddressToken = QCOM_KEY_SPMI_RESOURCE(Controller, Bus, Sid, Address);
  return 0;
}

static int QcomFdtGetU32(
    void *Context, int Node, const char *Property, uint32_t *Value)
{
  QcomKeyFdtAdapter *Adapter = (QcomKeyFdtAdapter *)Context;

  if (Adapter == NULL || Adapter->Blob == NULL)
    return -1;
  return QcomFdtReadU32(Adapter->Blob, Node, Property, Value);
}

static int QcomFdtGetChildren(
    void *Context, int ParentNode, int *Nodes, size_t Capacity,
    size_t *Count)
{
  QcomKeyFdtAdapter *Adapter = (QcomKeyFdtAdapter *)Context;
  int Node;
  size_t Used = 0;

  if (Adapter == NULL || Adapter->Blob == NULL || Nodes == NULL ||
      Count == NULL || Capacity == 0U)
    return -1;

  for (Node = fdt_first_subnode(Adapter->Blob, ParentNode); Node >= 0;
       Node = fdt_next_subnode(Adapter->Blob, Node)) {
    uint32_t Code;

    if (!QcomFdtNodeAvailable(Adapter->Blob, Node))
      continue;
    /* gpio-keys commonly contains LEDs, lid switches, and wake sources in
     * addition to the three actions understood by this shim.  Filter those
     * entries before applying the small descriptor capacity. */
    if (QcomFdtReadU32(Adapter->Blob, Node, "linux,code", &Code) != 0 ||
        QcomKeyActionFromCode(Code) == QCOM_KEY_ACTION_NONE)
      continue;
    if (Used >= Capacity)
      break;
    Nodes[Used++] = Node;
  }
  *Count = Used;
  return 0;
}

static int QcomFdtGetGpio(
    void *Context, int Node, uint64_t *RegisterBase, uint32_t *Mask,
    uint8_t *ActiveLow)
{
  QcomKeyFdtAdapter *Adapter = (QcomKeyFdtAdapter *)Context;
  const fdt32_t *Cells;
  const char *Compatible;
  int Provider;
  int CellLength;
  int CompatibleLength;
  uint32_t GpioCellCount;
  int AddressOwner;
  uint32_t Flags = 0;
  uint32_t Gpio;
  uint32_t TlmmGpioCount;
  uint64_t ProviderBase;
  uint64_t Offset;
  uint8_t Sid, Controller, Bus;

  if (Adapter == NULL || Adapter->Blob == NULL || RegisterBase == NULL ||
      Mask == NULL || ActiveLow == NULL)
    return -1;

  Cells = fdt_getprop(Adapter->Blob, Node, "gpios", &CellLength);
  if (Cells == NULL || CellLength < (int)(2 * sizeof(fdt32_t)))
    return -1;
  Provider = dt_phandle(
      Adapter->Blob, fdt32_to_cpu(Cells[0]));
  if (Provider < 0 || !QcomFdtNodeAvailable(Adapter->Blob, Provider) ||
      QcomFdtReadU32(Adapter->Blob, Provider, "#gpio-cells",
                     &GpioCellCount) != 0 ||
      GpioCellCount < 1U || GpioCellCount > 4U ||
      CellLength != (int)((1U + GpioCellCount) * sizeof(fdt32_t)))
    return -1;

  Gpio = fdt32_to_cpu(Cells[1]);
  if (GpioCellCount > 1U)
    Flags = fdt32_to_cpu(Cells[2]);
  *ActiveLow = (Flags & 1U) != 0U ? 1U : 0U;
  *Mask = 1U;

  if (QcomFdtGetCompatible(
          Adapter->Blob, Provider, &Compatible, &CompatibleLength) != 0)
    return -1;
  uintptr_t pin_address;
  if (kshim_dt_gpio(Adapter->Blob, Provider, Gpio, &pin_address, NULL, NULL) == 0) {
    *RegisterBase = pin_address + QCOM_TLMM_GPIO_IN_OFFSET;
    return 0;
  }
  TlmmGpioCount = QcomFdtLinearTlmmGpioCount(Adapter->Blob, Provider);
  if (TlmmGpioCount != 0U) {
    struct dt_range range;
    if (dt_reg(Adapter->Blob, Provider, 0, &range)) return -1;
    ProviderBase = range.base;
    if (Gpio >= TlmmGpioCount)
      return -1;
    Offset = (uint64_t)Gpio * (uint64_t)QCOM_TLMM_GPIO_STRIDE;
    if (fdt_node_check_compatible(Adapter->Blob, Provider,
                                  "qcom,canoe-tlmm") == 0)
      Offset += UINT64_C(0x100000);
    if (Offset > UINT64_MAX - ProviderBase ||
        ProviderBase + Offset > UINT64_MAX - QCOM_TLMM_GPIO_IN_OFFSET)
      return -1;
    if (!dt_contains(range, ProviderBase + Offset, 8))
      return -1;
    *RegisterBase = ProviderBase + Offset + QCOM_TLMM_GPIO_IN_OFFSET;
    return 0;
  }

  /* qcom,spmi-gpio and pm8xxx-gpio providers use one-based GPIO numbers. */
  if (!QcomFdtIsPmicGpioCompatible(Compatible, CompatibleLength))
    return -1;
  if (QcomFdtReadReg(Adapter->Blob, Provider, &ProviderBase, &AddressOwner)) return -1;
  if (Gpio < QCOM_PMIC_GPIO_PHYSICAL_OFFSET)
    return -1;
  Offset = (uint64_t)(Gpio - QCOM_PMIC_GPIO_PHYSICAL_OFFSET) *
           (uint64_t)QCOM_PMIC_GPIO_STRIDE;
  if (Offset > UINT64_MAX - ProviderBase ||
      ProviderBase + Offset > UINT64_MAX - QCOM_PMIC_GPIO_RT_STS_OFFSET)
    return -1;
  if (ProviderBase + Offset + QCOM_PMIC_GPIO_RT_STS_OFFSET > UINT16_MAX)
    return -1;
  if (kshim_dt_spmi_identity(Adapter->Blob, AddressOwner, &Controller, &Bus, &Sid) != 0)
    return -1;
  *RegisterBase = QCOM_KEY_SPMI_RESOURCE(
      Controller, Bus, Sid, ProviderBase + Offset + QCOM_PMIC_GPIO_RT_STS_OFFSET) | QCOM_KEY_PMIC_GPIO_FLAG;
  return 0;
}

int QcomKeyFdtAdapterInit(QcomKeyFdtAdapter *Adapter, const void *Blob)
{
  if (Adapter == NULL || Blob == NULL || dt_validate(Blob, fdt_totalsize(Blob)) != 0)
    return QCOM_KEYS_EINVAL;

  *Adapter = (QcomKeyFdtAdapter){
      .Blob = Blob,
      .Ops = {
          .Context = Adapter,
          .FindCompatible = QcomFdtFindCompatible,
          .GetReg = QcomFdtGetReg,
          .GetU32 = QcomFdtGetU32,
          .GetChildren = QcomFdtGetChildren,
          .GetGpio = QcomFdtGetGpio,
      },
  };
  return QCOM_KEYS_OK;
}

int QcomKeysBuildFromDeviceTree(
    const void *Blob, QcomKeyDescriptor *Descriptors, size_t Capacity,
    size_t *Count)
{
  QcomKeyFdtAdapter Adapter;
  int Status;

  Status = QcomKeyFdtAdapterInit(&Adapter, Blob);
  if (Status != QCOM_KEYS_OK)
    return Status;
  return QcomKeysBuildFromFdt(&Adapter.Ops, Descriptors, Capacity, Count);
}
