/*
 * Qualcomm key input backend.
 *
 * The status-bit and active-low conventions match the Linux
 * pm8941-pwrkey/qpnp-power-on drivers (legacy KPDPWR uses PON_RT_STS bit 0;
 * gen3 descriptors select bits 6/7).  SPMI access is supplied by the
 * platform through QcomKeyRead32.
 */
#include <qcom_keys.h>

#include <stdint.h>

uint32_t QcomKeyReadMmio32(void *Context, uint64_t Address)
{
  (void)Context;
  return *(volatile uint32_t *)(uintptr_t)Address;
}

QcomKeyAction QcomKeyActionFromCode(uint32_t Code)
{
  switch (Code) {
  case QCOM_KEYCODE_VOLUMEUP:
    return QCOM_KEY_ACTION_UP;
  case QCOM_KEYCODE_VOLUMEDOWN:
    return QCOM_KEY_ACTION_DOWN;
  case QCOM_KEYCODE_POWER:
    return QCOM_KEY_ACTION_SELECT;
  default:
    return QCOM_KEY_ACTION_NONE;
  }
}

static int QcomKeyDescriptorValid(const QcomKeyDescriptor *Descriptor)
{
  QcomKeyAction DerivedAction;

  if (Descriptor == NULL || Descriptor->Mask == 0U)
    return 0;

  if (Descriptor->Action > QCOM_KEY_ACTION_SELECT)
    return 0;

  DerivedAction = QcomKeyActionFromCode(Descriptor->Code);
  if (DerivedAction == QCOM_KEY_ACTION_NONE)
    return 0;
  if (Descriptor->Action != QCOM_KEY_ACTION_NONE &&
      Descriptor->Action != DerivedAction)
    return 0;

  if (Descriptor->Source > QCOM_KEY_SOURCE_PMIC_GPIO)
    return 0;

  return 1;
}

int QcomKeyMakePon(
    QcomKeyDescriptor *Descriptor, uint64_t AddressToken, uint32_t Code)
{
  uint32_t StatusMask;

  switch (Code) {
  case QCOM_KEYCODE_POWER:
    StatusMask = QCOM_PON_KPDPWR_N_SET;
    break;
  case QCOM_KEYCODE_VOLUMEDOWN:
  case QCOM_KEYCODE_VOLUMEUP:
    /* Older PON blocks expose the secondary button as RESIN. */
    StatusMask = QCOM_PON_RESIN_N_SET;
    break;
  default:
    return QCOM_KEYS_EINVAL;
  }

  return QcomKeyMakePonSource(
      Descriptor, AddressToken, Code, StatusMask, 0U);
}

int QcomKeyMakePonSource(
    QcomKeyDescriptor *Descriptor, uint64_t AddressToken, uint32_t Code,
    uint32_t StatusMask, uint8_t ActiveLow)
{
  if (Descriptor == NULL || StatusMask == 0U ||
      QcomKeyActionFromCode(Code) == QCOM_KEY_ACTION_NONE)
    return QCOM_KEYS_EINVAL;
  if (QCOM_KEY_IS_SPMI_TOKEN(AddressToken) &&
      (QCOM_KEY_SPMI_TOKEN_SID(AddressToken) > 0x0fU ||
       QCOM_KEY_SPMI_TOKEN_ADDRESS(AddressToken) >
           UINT16_MAX - QCOM_PON_RT_STS_OFFSET))
    return QCOM_KEYS_EINVAL;

  *Descriptor = (QcomKeyDescriptor){
      .Base = AddressToken,
      .RegisterOffset = QCOM_PON_RT_STS_OFFSET,
      .Mask = StatusMask,
      .Code = Code,
      .Action = QcomKeyActionFromCode(Code),
      .Source = QCOM_KEY_SOURCE_PON,
      .ActiveLow = ActiveLow ? 1U : 0U,
      .DebounceSamples = 0,
  };
  return QCOM_KEYS_OK;
}

int QcomKeyMakeGpioAt(
    QcomKeyDescriptor *Descriptor, uint64_t RegisterBase, uint32_t Mask,
    uint32_t Code, uint8_t ActiveLow)
{
  if (Descriptor == NULL || Mask == 0U ||
      QcomKeyActionFromCode(Code) == QCOM_KEY_ACTION_NONE)
    return QCOM_KEYS_EINVAL;
  if (QCOM_KEY_IS_SPMI_TOKEN(RegisterBase) &&
      (QCOM_KEY_SPMI_TOKEN_SID(RegisterBase) > 0x0fU ||
       QCOM_KEY_SPMI_TOKEN_ADDRESS(RegisterBase) > UINT16_MAX))
    return QCOM_KEYS_EINVAL;

  *Descriptor = (QcomKeyDescriptor){
      .Base = RegisterBase,
      .RegisterOffset = 0U,
      .Mask = Mask,
      .Code = Code,
      .Action = QcomKeyActionFromCode(Code),
      .Source = QCOM_KEY_IS_SPMI_TOKEN(RegisterBase)
                    ? QCOM_KEY_SOURCE_PMIC_GPIO
                    : QCOM_KEY_SOURCE_GPIO,
      .ActiveLow = ActiveLow ? 1U : 0U,
      .DebounceSamples = 0,
  };
  return QCOM_KEYS_OK;
}

int QcomKeyMakeSpmiGpio(
    QcomKeyDescriptor *Descriptor, uint64_t AddressToken,
    uint32_t PhysicalGpio, uint32_t Code, uint8_t ActiveLow)
{
  uint64_t GpioOffset;
  uint32_t PeripheralAddress;
  uint32_t RegisterAddress;
  uint8_t Sid;

  if (Descriptor == NULL || PhysicalGpio < QCOM_PMIC_GPIO_PHYSICAL_OFFSET ||
      QcomKeyActionFromCode(Code) == QCOM_KEY_ACTION_NONE ||
      !QCOM_KEY_IS_SPMI_TOKEN(AddressToken))
    return QCOM_KEYS_EINVAL;
  Sid = QCOM_KEY_SPMI_TOKEN_SID(AddressToken);
  PeripheralAddress = QCOM_KEY_SPMI_TOKEN_ADDRESS(AddressToken);
  GpioOffset = (uint64_t)(PhysicalGpio - QCOM_PMIC_GPIO_PHYSICAL_OFFSET) *
               (uint64_t)QCOM_PMIC_GPIO_STRIDE;
  if (Sid > 0x0fU || GpioOffset > UINT16_MAX ||
      PeripheralAddress > UINT16_MAX - QCOM_PMIC_GPIO_RT_STS_OFFSET ||
      GpioOffset > UINT16_MAX - QCOM_PMIC_GPIO_RT_STS_OFFSET -
                       PeripheralAddress)
    return QCOM_KEYS_EINVAL;

  RegisterAddress = PeripheralAddress + (uint32_t)GpioOffset;
  *Descriptor = (QcomKeyDescriptor){
      .Base = (AddressToken & ~UINT64_C(0xffffffff)) | RegisterAddress,
      .RegisterOffset = QCOM_PMIC_GPIO_RT_STS_OFFSET,
      .Mask = 1U,
      .Code = Code,
      .Action = QcomKeyActionFromCode(Code),
      .Source = QCOM_KEY_SOURCE_PMIC_GPIO,
      .ActiveLow = ActiveLow ? 1U : 0U,
      .DebounceSamples = 0,
  };
  return QCOM_KEYS_OK;
}

int QcomKeyMakeGpio(
    QcomKeyDescriptor *Descriptor, uint64_t TlmmBase, uint32_t Gpio,
    uint32_t Code, uint8_t ActiveLow)
{
  uint64_t RegisterBase;

  if (Descriptor == NULL || QcomKeyActionFromCode(Code) == QCOM_KEY_ACTION_NONE)
    return QCOM_KEYS_EINVAL;
  if ((uint64_t)Gpio >
      (UINT64_MAX - TlmmBase) / (uint64_t)QCOM_TLMM_GPIO_STRIDE)
    return QCOM_KEYS_EINVAL;

  /* TLMM has one 4 KiB GPIO register block per line. */
  RegisterBase = TlmmBase +
                 ((uint64_t)Gpio * (uint64_t)QCOM_TLMM_GPIO_STRIDE);
  if (QcomKeyMakeGpioAt(
          Descriptor, RegisterBase, 1U, Code, ActiveLow) != QCOM_KEYS_OK)
    return QCOM_KEYS_EINVAL;
  Descriptor->RegisterOffset = QCOM_TLMM_GPIO_IN_OFFSET;
  return QCOM_KEYS_OK;
}

static int QcomKeysHasAction(
    const QcomKeyDescriptor *Descriptors, size_t Count,
    QcomKeyAction Action)
{
  size_t Index;

  for (Index = 0; Index < Count; Index++) {
    if (Descriptors[Index].Action == Action)
      return 1;
  }
  return 0;
}

static int QcomKeysAppendPonNodes(
    const QcomKeyFdtOps *Ops, const char *const *Compatibles,
    size_t CompatibleCount, uint8_t Resin, uint8_t Gen3,
    QcomKeyDescriptor *Descriptors, size_t Capacity, size_t *Count)
{
  size_t CompatibleIndex;

  for (CompatibleIndex = 0; CompatibleIndex < CompatibleCount;
       CompatibleIndex++) {
    int StartNode = -1;

    for (;;) {
      int Node;
      uint64_t AddressToken;
      uint32_t Code;
      uint32_t Mask;
      QcomKeyDescriptor Descriptor;

      Node = Ops->FindCompatible(
          Ops->Context, Compatibles[CompatibleIndex], StartNode);
      if (Node < 0)
        break;
      StartNode = Node;

      if (Ops->GetReg(Ops->Context, Node, &AddressToken) != 0)
        continue;

      Code = Resin ? QCOM_KEYCODE_VOLUMEDOWN : QCOM_KEYCODE_POWER;
      if (Ops->GetU32 != NULL &&
          Ops->GetU32(Ops->Context, Node, "linux,code", &Code) != 0) {
        /* linux,code is optional for the standard PON power key. */
        Code = Resin ? QCOM_KEYCODE_VOLUMEDOWN : QCOM_KEYCODE_POWER;
      }

      if (Resin)
        Mask = Gen3 ? QCOM_PON_GEN3_RESIN_N_SET : QCOM_PON_RESIN_N_SET;
      else
        Mask = Gen3 ? QCOM_PON_GEN3_KPDPWR_N_SET : QCOM_PON_KPDPWR_N_SET;

      if (QcomKeyMakePonSource(
              &Descriptor, AddressToken, Code, Mask, 0U) != QCOM_KEYS_OK)
        continue;
      if (QcomKeysHasAction(Descriptors, *Count, Descriptor.Action))
        continue;
      if (*Count >= Capacity)
        return QCOM_KEYS_ENOSPC;
      Descriptors[(*Count)++] = Descriptor;
    }
  }
  return QCOM_KEYS_OK;
}

int QcomKeysBuildFromFdt(
    const QcomKeyFdtOps *Ops, QcomKeyDescriptor *Descriptors,
    size_t Capacity, size_t *Count)
{
  static const char *const PwrkeyCompatibles[] = {
      "qcom,pm8941-pwrkey",
  };
  static const char *const Gen3PwrkeyCompatibles[] = {
      "qcom,pmk8350-pwrkey",
      "qcom,pmk8550-pwrkey",
      "qcom,pmk8850-pwrkey",
  };
  static const char *const ResinCompatibles[] = {
      "qcom,pm8941-resin",
  };
  static const char *const Gen3ResinCompatibles[] = {
      "qcom,pmk8350-resin",
      "qcom,pmk8550-resin",
      "qcom,pmk8850-resin",
  };
  size_t Index;
  int Parent;

  if (Ops == NULL || Descriptors == NULL || Count == NULL || Capacity == 0U ||
      Ops->FindCompatible == NULL)
    return QCOM_KEYS_EINVAL;

  *Count = 0U;
  if (Ops->GetReg != NULL) {
    if (QcomKeysAppendPonNodes(
            Ops, PwrkeyCompatibles,
            sizeof(PwrkeyCompatibles) / sizeof(PwrkeyCompatibles[0]), 0U, 0U,
            Descriptors, Capacity, Count) != QCOM_KEYS_OK)
      return QCOM_KEYS_ENOSPC;
    if (QcomKeysAppendPonNodes(
            Ops, Gen3PwrkeyCompatibles,
            sizeof(Gen3PwrkeyCompatibles) / sizeof(Gen3PwrkeyCompatibles[0]),
            0U, 1U, Descriptors, Capacity, Count) != QCOM_KEYS_OK)
      return QCOM_KEYS_ENOSPC;
    if (QcomKeysAppendPonNodes(
            Ops, ResinCompatibles,
            sizeof(ResinCompatibles) / sizeof(ResinCompatibles[0]), 1U, 0U,
            Descriptors, Capacity, Count) != QCOM_KEYS_OK)
      return QCOM_KEYS_ENOSPC;
    if (QcomKeysAppendPonNodes(
            Ops, Gen3ResinCompatibles,
            sizeof(Gen3ResinCompatibles) / sizeof(Gen3ResinCompatibles[0]),
            1U, 1U, Descriptors, Capacity, Count) != QCOM_KEYS_OK)
      return QCOM_KEYS_ENOSPC;
  }

  /* Android Qualcomm PON exposes key type/code on children of one PMIC
   * peripheral. Use the same descriptors as the mainline pwrkey bindings. */
  if (Ops->GetChildren && Ops->GetU32 && Ops->GetReg) {
    int start = -1;
    while ((Parent = Ops->FindCompatible(Ops->Context, "qcom,qpnp-power-on", start)) >= 0) {
      start = Parent;
      int nodes[QCOM_KEYS_MAX]; size_t nr = 0;
      uint64_t address;
      if (Ops->GetReg(Ops->Context, Parent, &address) ||
          Ops->GetChildren(Ops->Context, Parent, nodes, QCOM_KEYS_MAX, &nr)) continue;
      for (size_t i = 0; i < nr && i < QCOM_KEYS_MAX; ++i) {
        uint32_t type, code;
        if (Ops->GetU32(Ops->Context, nodes[i], "qcom,pon-type", &type) || type > 1 ||
            Ops->GetU32(Ops->Context, nodes[i], "linux,code", &code) ||
            QcomKeysHasAction(Descriptors, *Count, QcomKeyActionFromCode(code))) continue;
        QcomKeyDescriptor d;
        if (QcomKeyMakePonSource(&d, address, code, type ? QCOM_PON_RESIN_N_SET : QCOM_PON_KPDPWR_N_SET, 0)) continue;
        if (*Count >= Capacity) return QCOM_KEYS_ENOSPC;
        Descriptors[(*Count)++] = d;
      }
    }
  }

  /* gpio-keys children are resolved by the platform's GPIO provider. Some
   * boards use separate containers for hall sensors and physical buttons, so
   * scan every available gpio-keys node instead of stopping at the first. */
  if (Ops->GetChildren != NULL && Ops->GetGpio != NULL) {
    int Nodes[QCOM_KEYS_MAX];
    int StartParent = -1;

    for (;;) {
      size_t NodeCount = 0U;

      Parent = Ops->FindCompatible(
          Ops->Context, "gpio-keys", StartParent);
      if (Parent < 0)
        break;
      StartParent = Parent;
      if (Ops->GetChildren(
              Ops->Context, Parent, Nodes, QCOM_KEYS_MAX, &NodeCount) != 0)
        continue;
      if (NodeCount > QCOM_KEYS_MAX)
        NodeCount = QCOM_KEYS_MAX;
      for (Index = 0; Index < NodeCount; Index++) {
        uint32_t Code;
        QcomKeyAction Action;
        uint64_t RegisterBase;
        uint32_t Mask;
        uint8_t ActiveLow;
        QcomKeyDescriptor Descriptor;

        if (Ops->GetU32 == NULL ||
            Ops->GetU32(
                Ops->Context, Nodes[Index], "linux,code", &Code) != 0)
          continue;
        Action = QcomKeyActionFromCode(Code);
        if (Action == QCOM_KEY_ACTION_NONE ||
            QcomKeysHasAction(Descriptors, *Count, Action))
          continue;
        if (Ops->GetGpio(
                Ops->Context, Nodes[Index], &RegisterBase, &Mask,
                &ActiveLow) != 0)
          continue;
        if (QcomKeyMakeGpioAt(
                &Descriptor, RegisterBase, Mask, Code, ActiveLow) !=
            QCOM_KEYS_OK)
          continue;
        if (*Count >= Capacity)
          return QCOM_KEYS_ENOSPC;
        Descriptors[(*Count)++] = Descriptor;
      }
    }
  }

  return *Count == 0U ? QCOM_KEYS_EIO : QCOM_KEYS_OK;
}

int QcomKeyReadPressed(
    const QcomKeys *Keys, const QcomKeyDescriptor *Descriptor,
    uint8_t *Pressed)
{
  QcomKeyRead32 Read32;
  uint64_t Address;
  uint32_t Value;
  int Status;

  if (Keys == NULL || Descriptor == NULL || Pressed == NULL ||
      !QcomKeyDescriptorValid(Descriptor))
    return QCOM_KEYS_EINVAL;

  Read32 = Keys->Read32 != NULL ? Keys->Read32 : QcomKeyReadMmio32;
  if (QCOM_KEY_IS_SPMI_TOKEN(Descriptor->Base)) {
    uint32_t PeripheralAddress =
        QCOM_KEY_SPMI_TOKEN_ADDRESS(Descriptor->Base);
    uint8_t Sid = QCOM_KEY_SPMI_TOKEN_SID(Descriptor->Base);

    if (Sid > 0x0fU || Descriptor->RegisterOffset > UINT16_MAX ||
        PeripheralAddress > UINT16_MAX - Descriptor->RegisterOffset)
      return QCOM_KEYS_EINVAL;
    Address = (Descriptor->Base & ~UINT64_C(0xffffffff)) |
        (PeripheralAddress + Descriptor->RegisterOffset);
  } else {
    /* Check the addition so a malformed FDT cannot wrap into another MMIO. */
    if ((uint64_t)Descriptor->RegisterOffset >
        UINT64_MAX - Descriptor->Base)
      return QCOM_KEYS_EINVAL;
    Address = Descriptor->Base + (uint64_t)Descriptor->RegisterOffset;
  }
  if (Keys->Read32Status != NULL) {
    Status = Keys->Read32Status(Keys->ReadContext, Address, &Value);
    if (Status != 0)
      return QCOM_KEYS_EIO;
  } else {
    Value = Read32(Keys->ReadContext, Address);
  }
  Value = (Value & Descriptor->Mask) != 0U ? 1U : 0U;
  if (Descriptor->ActiveLow)
    Value = Value ? 0U : 1U;
  *Pressed = (uint8_t)Value;
  return QCOM_KEYS_OK;
}

int QcomKeysInit(
    QcomKeys *Keys, const QcomKeyDescriptor *Descriptors, size_t Count,
    QcomKeyRead32 Read32, void *ReadContext)
{
  return QcomKeysInitEx(Keys, Descriptors, Count, Read32, NULL, ReadContext);
}

int QcomKeysInitEx(
    QcomKeys *Keys, const QcomKeyDescriptor *Descriptors, size_t Count,
    QcomKeyRead32 Read32, QcomKeyRead32Status Read32Status,
    void *ReadContext)
{
  size_t Index;

  if (Keys == NULL || Descriptors == NULL || Count == 0U)
    return QCOM_KEYS_EINVAL;
  if (Count > QCOM_KEYS_MAX)
    return QCOM_KEYS_ENOSPC;

  *Keys = (QcomKeys){
      .DefaultDebounceSamples = QCOM_KEYS_DEFAULT_DEBOUNCE,
      .EmitRelease = 0,
      .Count = (uint8_t)Count,
      .Read32 = Read32,
      .Read32Status = Read32Status,
      .ReadContext = ReadContext,
  };

  for (Index = 0; Index < Count; Index++) {
    if (!QcomKeyDescriptorValid(&Descriptors[Index])) {
      *Keys = (QcomKeys){0};
      return QCOM_KEYS_EINVAL;
    }
    Keys->Descriptors[Index] = Descriptors[Index];
  }
  return QCOM_KEYS_OK;
}

void QcomKeysSetDebounce(QcomKeys *Keys, uint8_t Samples)
{
  if (Keys == NULL)
    return;
  Keys->DefaultDebounceSamples = Samples == 0U ? 1U : Samples;
}

void QcomKeysSetEmitRelease(QcomKeys *Keys, uint8_t Enable)
{
  if (Keys != NULL)
    Keys->EmitRelease = Enable ? 1U : 0U;
}

static uint8_t QcomKeyDebounceLimit(
    const QcomKeys *Keys, const QcomKeyDescriptor *Descriptor)
{
  uint8_t Limit = Descriptor->DebounceSamples;

  if (Limit == 0U)
    Limit = Keys->DefaultDebounceSamples;
  return Limit == 0U ? 1U : Limit;
}

static void QcomKeyQueueEdge(
    QcomKeys *Keys, size_t Index, uint8_t Pressed)
{
  uint16_t Bit = (uint16_t)(1U << Index);

  if (Pressed)
    Keys->PendingPress |= Bit;
  else if (Keys->EmitRelease)
    Keys->PendingRelease |= Bit;
}

static int QcomKeyDequeue(
    QcomKeys *Keys, QcomKeyEvent *Event)
{
  uint16_t Pending;
  size_t Index;
  uint8_t Pressed;

  Pending = Keys->PendingPress;
  if (Pending != 0U) {
    Pressed = 1U;
  } else {
    Pending = Keys->PendingRelease;
    if (Pending == 0U)
      return QCOM_KEYS_NO_EVENT;
    Pressed = 0U;
  }

  Index = 0U;
  while ((Pending & 1U) == 0U) {
    Pending >>= 1;
    Index++;
  }

  if (Pressed)
    Keys->PendingPress &= (uint16_t)~(1U << Index);
  else
    Keys->PendingRelease &= (uint16_t)~(1U << Index);

  *Event = (QcomKeyEvent){
      .Type = Pressed ? QCOM_KEY_EVENT_PRESS : QCOM_KEY_EVENT_RELEASE,
      .Action = Keys->Descriptors[Index].Action != QCOM_KEY_ACTION_NONE
                    ? Keys->Descriptors[Index].Action
                    : QcomKeyActionFromCode(Keys->Descriptors[Index].Code),
      .Code = Keys->Descriptors[Index].Code,
      .Pressed = Pressed,
      .Descriptor = (uint8_t)Index,
  };
  return QCOM_KEYS_EVENT;
}

int QcomKeysPoll(QcomKeys *Keys, QcomKeyEvent *Event)
{
  size_t Index;
  int Status;
  int FirstError = QCOM_KEYS_NO_EVENT;
  uint8_t Sampled = 0U;

  if (Keys == NULL || Event == NULL || Keys->Count == 0U)
    return QCOM_KEYS_EINVAL;

  /* An event queued by the previous poll is returned before sampling again. */
  if (QcomKeyDequeue(Keys, Event) == QCOM_KEYS_EVENT)
    return QCOM_KEYS_EVENT;

  for (Index = 0; Index < Keys->Count; Index++) {
    uint8_t Pressed;
    uint8_t Limit;

    Status = QcomKeyReadPressed(Keys, &Keys->Descriptors[Index], &Pressed);
    if (Status != QCOM_KEYS_OK) {
      /* One unavailable transport must not suppress independent GPIO keys. */
      if (FirstError == QCOM_KEYS_NO_EVENT)
        FirstError = Status;
      continue;
    }
    Sampled = 1U;

    Limit = QcomKeyDebounceLimit(Keys, &Keys->Descriptors[Index]);
    if (Pressed != Keys->Candidate[Index]) {
      Keys->Candidate[Index] = Pressed;
      Keys->Samples[Index] = 1U;
    } else if (Keys->Samples[Index] < Limit) {
      Keys->Samples[Index]++;
    }

    if (Keys->Samples[Index] >= Limit &&
        Keys->Stable[Index] != Keys->Candidate[Index]) {
      Keys->Stable[Index] = Keys->Candidate[Index];
      QcomKeyQueueEdge(Keys, Index, Keys->Stable[Index]);
    }
  }

  Status = QcomKeyDequeue(Keys, Event);
  if (Status == QCOM_KEYS_EVENT)
    return Status;
  return Sampled != 0U ? QCOM_KEYS_NO_EVENT : FirstError;
}
