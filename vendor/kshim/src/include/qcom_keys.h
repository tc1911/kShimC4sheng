/*
 * Qualcomm early-boot key input.
 *
 * Qualcomm volume keys are normally exposed by a gpio-keys node while the
 * power key is exposed by the PMIC PON block.  The latter is commonly behind
 * SPMI, so this interface deliberately takes a read callback instead of
 * assuming that a device-tree "reg" value is directly CPU mapped.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Linux input codes used by Qualcomm device trees. */
#define QCOM_KEYCODE_VOLUMEDOWN 114U
#define QCOM_KEYCODE_VOLUMEUP   115U
#define QCOM_KEYCODE_POWER      116U
#define QCOM_KEYCODE_KPDPWR     QCOM_KEYCODE_POWER

/* PMIC PON and TLMM offsets used by the standard Qualcomm key drivers. */
#define QCOM_PON_RT_STS_OFFSET       0x10U
#define QCOM_PON_KPDPWR_N_SET        (1U << 0)
#define QCOM_PON_RESIN_N_SET         (1U << 1)
#define QCOM_PON_CBLPWR_N_SET        (1U << 2)
#define QCOM_PON_RESIN_BARK_N_SET    (1U << 4)
#define QCOM_PON_KPDPWR_RESIN_BARK_N_SET (1U << 5)
#define QCOM_PON_GEN3_RESIN_N_SET    (1U << 6)
#define QCOM_PON_GEN3_KPDPWR_N_SET   (1U << 7)
#define QCOM_TLMM_GPIO_IN_OFFSET     0x04U
#define QCOM_TLMM_GPIO_STRIDE        0x1000U
#define QCOM_PMIC_GPIO_RT_STS_OFFSET 0x10U
#define QCOM_PMIC_GPIO_STRIDE        0x100U
#define QCOM_PMIC_GPIO_PHYSICAL_OFFSET 1U

/* Address token format used by the optional FDT adapter for SPMI accesses. */
#define QCOM_KEY_SPMI_TOKEN_CONTROLLER(Token) (((uint64_t)(Token) >> 40) & 0xffU)
#define QCOM_KEY_SPMI_TOKEN_BUS(Token) (((uint64_t)(Token) >> 48) & 0xffU)
#define QCOM_KEY_SPMI_RESOURCE(Controller, Bus, Sid, Address) \
  (QCOM_KEY_SPMI_TOKEN(Sid, Address) | ((uint64_t)(Controller) << 40) | ((uint64_t)(Bus) << 48))
#define QCOM_KEY_PMIC_GPIO_FLAG UINT64_C(0x4000000000000000)
#define QCOM_KEY_SPMI_TOKEN_FLAG UINT64_C(0x8000000000000000)
#define QCOM_KEY_SPMI_TOKEN(Sid, Address) \
  (QCOM_KEY_SPMI_TOKEN_FLAG | (((uint64_t)(Sid) & UINT64_C(0xff)) << 32) | \
   ((uint64_t)(Address) & UINT64_C(0xffffffff)))
#define QCOM_KEY_IS_SPMI_TOKEN(Token) \
  ((((uint64_t)(Token)) & QCOM_KEY_SPMI_TOKEN_FLAG) != 0U)
#define QCOM_KEY_SPMI_TOKEN_SID(Token) \
  ((uint8_t)(((uint64_t)(Token) >> 32) & UINT64_C(0xff)))
#define QCOM_KEY_SPMI_TOKEN_ADDRESS(Token) ((uint32_t)(Token))

#define QCOM_KEYS_MAX                8U
#define QCOM_KEYS_DEFAULT_DEBOUNCE  2U

typedef enum {
  QCOM_KEY_ACTION_NONE = 0,
  QCOM_KEY_ACTION_UP,
  QCOM_KEY_ACTION_DOWN,
  QCOM_KEY_ACTION_SELECT,
} QcomKeyAction;

/* Confirm is the menu-facing name for the physical Power key. */
#define QCOM_KEY_ACTION_CONFIRM QCOM_KEY_ACTION_SELECT

typedef enum {
  QCOM_KEY_SOURCE_MMIO = 0,
  QCOM_KEY_SOURCE_PON,
  QCOM_KEY_SOURCE_GPIO,
  QCOM_KEY_SOURCE_PMIC_GPIO,
} QcomKeySource;

typedef enum {
  QCOM_KEY_EVENT_NONE = 0,
  QCOM_KEY_EVENT_PRESS,
  QCOM_KEY_EVENT_RELEASE,
} QcomKeyEventType;

/*
 * Address is a transport token.  For MMIO/GPIO it is a physical address; for
 * PON/SPMI it may instead be an encoded slave/offset understood by Read32.
 */
typedef uint32_t (*QcomKeyRead32)(void *Context, uint64_t Address);

/* Status-aware reader used by transports such as SPMI. A non-zero return
 * means that the sample is unavailable and must not be interpreted as a key
 * state. QcomKeysInit() remains available for legacy value-only readers. */
typedef int (*QcomKeyRead32Status)(
    void *Context, uint64_t Address, uint32_t *Value);

/* Volatile fallback reader for descriptors that resolve to CPU MMIO. */
uint32_t QcomKeyReadMmio32(void *Context, uint64_t Address);

typedef struct {
  uint64_t       Base;
  uint32_t       RegisterOffset;
  uint32_t       Mask;
  uint32_t       Code;       /* KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_POWER. */
  QcomKeyAction  Action;     /* Optional; zero derives it from Code. */
  QcomKeySource  Source;
  uint8_t        ActiveLow;
  uint8_t        DebounceSamples;
  uint8_t        Reserved[2];
} QcomKeyDescriptor;

typedef struct {
  QcomKeyEventType Type;
  QcomKeyAction    Action;
  uint32_t         Code;
  uint8_t          Pressed;
  uint8_t          Descriptor;
  uint8_t          Reserved[2];
} QcomKeyEvent;

typedef struct {
  QcomKeyDescriptor Descriptors[QCOM_KEYS_MAX];
  uint8_t           Stable[QCOM_KEYS_MAX];
  uint8_t           Candidate[QCOM_KEYS_MAX];
  uint8_t           Samples[QCOM_KEYS_MAX];
  uint16_t          PendingPress;
  uint16_t          PendingRelease;
  uint8_t           DefaultDebounceSamples;
  uint8_t           EmitRelease;
  uint8_t           Count;
  uint8_t           Reserved;
  QcomKeyRead32     Read32;
  void             *ReadContext;
  /* Appended for source/layout compatibility with the original value-only
   * aggregate: old positional initializers still map ReadContext correctly. */
  QcomKeyRead32Status Read32Status;
} QcomKeys;

/*
 * Small adapter surface for a libfdt (or firmware-native FDT) walker.  The
 * key backend does not depend on libfdt directly because SPMI/GPIO providers
 * differ between Qualcomm targets.  GetReg may resolve an inherited parent
 * reg (PON key child nodes normally do not carry their own reg property).
 * GetGpio resolves a `gpios` specifier into the register base/mask consumed
 * by QcomKeyMakeGpioAt; this also covers PMIC SPMI GPIO providers.
 */
typedef struct {
  void *Context;
  int (*FindCompatible)(
      void *Context, const char *Compatible, int StartNode);
  int (*GetReg)(void *Context, int Node, uint64_t *AddressToken);
  int (*GetU32)(
      void *Context, int Node, const char *Property, uint32_t *Value);
  int (*GetChildren)(
      void *Context, int ParentNode, int *Nodes, size_t Capacity,
      size_t *Count);
  int (*GetGpio)(
      void *Context, int Node, uint64_t *RegisterBase, uint32_t *Mask,
      uint8_t *ActiveLow);
} QcomKeyFdtOps;

/* Return values for QcomKeysInit/QcomKeysPoll. */
#define QCOM_KEYS_OK        0
#define QCOM_KEYS_EVENT     1
#define QCOM_KEYS_NO_EVENT  0
#define QCOM_KEYS_EINVAL   (-1)
#define QCOM_KEYS_ENOSPC   (-2)
#define QCOM_KEYS_EIO      (-3)

/* Translate a Linux input code into the menu action used by kShimC. */
QcomKeyAction QcomKeyActionFromCode(uint32_t Code);

/* Descriptor constructors for the two Qualcomm key paths. */
int QcomKeyMakePon(
    QcomKeyDescriptor *Descriptor, uint64_t AddressToken, uint32_t Code);
int QcomKeyMakePonSource(
    QcomKeyDescriptor *Descriptor, uint64_t AddressToken, uint32_t Code,
    uint32_t StatusMask, uint8_t ActiveLow);
int QcomKeyMakeGpioAt(
    QcomKeyDescriptor *Descriptor, uint64_t RegisterBase, uint32_t Mask,
    uint32_t Code, uint8_t ActiveLow);
/* Physical PMIC GPIO numbers in DT are one-based (gpio1 == 1). */
int QcomKeyMakeSpmiGpio(
    QcomKeyDescriptor *Descriptor, uint64_t AddressToken,
    uint32_t PhysicalGpio, uint32_t Code, uint8_t ActiveLow);
int QcomKeyMakeGpio(
    QcomKeyDescriptor *Descriptor, uint64_t TlmmBase, uint32_t Gpio,
    uint32_t Code, uint8_t ActiveLow);

/*
 * Initialize a poller.  Descriptors are copied, so callers may use a local
 * table assembled from the FDT.  Read32==NULL selects direct 32-bit MMIO.
 */
int QcomKeysInit(
    QcomKeys *Keys, const QcomKeyDescriptor *Descriptors, size_t Count,
    QcomKeyRead32 Read32, void *ReadContext);

/* Variant for transports that can report a bus error separately from data. */
int QcomKeysInitEx(
    QcomKeys *Keys, const QcomKeyDescriptor *Descriptors, size_t Count,
    QcomKeyRead32 Read32, QcomKeyRead32Status Read32Status,
    void *ReadContext);

/* Build PON and gpio-keys descriptors from standard Qualcomm DT bindings. */
int QcomKeysBuildFromFdt(
    const QcomKeyFdtOps *Ops, QcomKeyDescriptor *Descriptors,
    size_t Capacity, size_t *Count);

/* Set debounce and release policy after QcomKeysInit. */
void QcomKeysSetDebounce(QcomKeys *Keys, uint8_t Samples);
void QcomKeysSetEmitRelease(QcomKeys *Keys, uint8_t Enable);

/*
 * Poll all sources once and return at most one edge.  Call repeatedly until
 * QCOM_KEYS_NO_EVENT to drain simultaneous transitions.  Only press edges
 * are queued by default, which is convenient for menu navigation.
 * A failed source is skipped when another source can still be sampled; an
 * error is returned only when no source produced a sample.
 */
int QcomKeysPoll(QcomKeys *Keys, QcomKeyEvent *Event);

/* Helpers for integrations that need to inspect a descriptor's current bit. */
int QcomKeyReadPressed(
    const QcomKeys *Keys, const QcomKeyDescriptor *Descriptor,
    uint8_t *Pressed);
