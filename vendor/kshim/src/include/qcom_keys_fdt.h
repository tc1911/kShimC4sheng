/*
 * libfdt adapter for the transport-neutral Qualcomm key backend.
 *
 * This adapter only interprets the device tree.  It does not touch SPMI or
 * MMIO; the resulting descriptors still need a QcomKeyRead32 callback.
 */
#pragma once

#include <stddef.h>

#include <qcom_keys.h>

typedef struct {
  const void    *Blob;
  QcomKeyFdtOps  Ops;
} QcomKeyFdtAdapter;

/*
 * Validate Blob and initialize the callback table embedded in Adapter.
 * Keep Adapter at a stable address while using Adapter.Ops directly.
 */
int QcomKeyFdtAdapterInit(QcomKeyFdtAdapter *Adapter, const void *Blob);

/* Build descriptors directly from a validated FDT blob. */
int QcomKeysBuildFromDeviceTree(
    const void *Blob, QcomKeyDescriptor *Descriptors, size_t Capacity,
    size_t *Count);
