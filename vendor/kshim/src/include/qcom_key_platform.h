#pragma once

#include <stdint.h>

/* Board code may override this weak hook with its SPMI transport. */
int kshim_qcom_spmi_read32(void *Context, uint8_t Sid, uint32_t Address,
                           uint32_t *Value);

/* One-microsecond PMIC Arbiter polling delay; board code may override it. */
void kshim_qcom_spmi_delay_us(uint32_t Microseconds);

/* Decode an SPMI token or return the CrDK TLMM input bit at an IO address. */
uint32_t kshim_qcom_key_read32(void *Context, uint64_t Address);

/* Status-preserving form used by QcomKeysInitEx(). */
int kshim_qcom_key_read32_status(
    void *Context, uint64_t Address, uint32_t *Value);

#include <Library/spmi.h>
void kshim_qcom_spmi_bind(SpmiDeviceContext **contexts, unsigned count);
