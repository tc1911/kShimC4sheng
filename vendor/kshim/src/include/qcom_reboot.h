/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

enum kshim_qcom_reboot_target {
    KSHIM_QCOM_REBOOT_RECOVERY,
    KSHIM_QCOM_REBOOT_BOOTLOADER,
    KSHIM_QCOM_REBOOT_EDL,
};
enum kshim_qcom_reboot_error {
    KSHIM_QCOM_REBOOT_UNSUPPORTED = -100,
    KSHIM_QCOM_REBOOT_INVALID_DT = -101,
    KSHIM_QCOM_REBOOT_IO_FAILED = -102,
    KSHIM_QCOM_REBOOT_VERIFY_FAILED = -103,
    KSHIM_QCOM_REBOOT_RESET_RETURNED = -104,
    KSHIM_QCOM_REBOOT_ROLLBACK_FAILED = -105,
};

struct kshim_qcom_reboot_config {
    const void *fdt;
    uint64_t dload_address;
    uint16_t reason_address;
    uint16_t sdam_base, sdam_size;
    uint8_t controller, bus, sid;
    uint8_t reason_shift, reason_mask;
};

/* Caller supplies the already validated, immutable hardware DT used to
 * initialize resources. Only an explicit menu confirmation may call request.
 * Successful reset never returns; every returned value is a failure. */
int kshim_qcom_reboot_parse(const void *hardware_fdt,
                          struct kshim_qcom_reboot_config *config);
int kshim_qcom_reboot_request(const void *hardware_fdt,
                            enum kshim_qcom_reboot_target target);

/* Weak hardware boundaries allow host tests to observe exact transactions.
 * The default SPMI adapter permits only the DT-selected SDAM byte. */
void kshim_qcom_reboot_smccc(uint64_t registers[8]);
/* Last stage reached by kshim_qcom_reboot_request(); diagnostic only. */
extern int kshim_qcom_reboot_last_stage;
/* Sub-stage inside the SPMI prepare/read/write path. */
extern int kshim_qcom_reboot_spmi_detail;
int kshim_qcom_reboot_spmi_prepare(const struct kshim_qcom_reboot_config *config);

/* 4-byte aligned SDAM access used by the last-boot record. Returns 0 on
 * success; SCM IO failures are reported as KSHIM_QCOM_REBOOT_IO_FAILED. */
int kshim_qcom_nv_read(const void *hardware_fdt, uint32_t offset,
                       void *buffer, uint32_t length);
int kshim_qcom_nv_write(const void *hardware_fdt, uint32_t offset,
                        const void *buffer, uint32_t length);
/* Generic access to a named qcom,spmi-sdam instance (e.g. nvram@8400). */
int kshim_qcom_nv_config(const void *hardware_fdt, uint16_t sdam_base,
                         struct kshim_qcom_reboot_config *config);
int kshim_qcom_nv_read_cfg(const struct kshim_qcom_reboot_config *config,
                           uint16_t offset, void *buffer, uint32_t length);
int kshim_qcom_nv_write_cfg(const struct kshim_qcom_reboot_config *config,
                            uint16_t offset, const void *buffer, uint32_t length);
int kshim_qcom_reboot_spmi_read(const struct kshim_qcom_reboot_config *config,
                              uint8_t *value);
int kshim_qcom_reboot_spmi_write(const struct kshim_qcom_reboot_config *config,
                               uint8_t value);
