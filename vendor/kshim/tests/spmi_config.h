#ifndef KSHIM_TEST_SPMI_CONFIG_H
#define KSHIM_TEST_SPMI_CONFIG_H

#include <stdint.h>

/* Ordinary host arrays keep simulated MMIO inside the sanitizer's bounds. */
#define TEST_SPMI_CORE_BYTES 0x3000U
#define TEST_SPMI_OBSERVER_BYTES 0x1000000U
#define TEST_SPMI_CONFIG_BYTES 0x1000U
#define TEST_SPMI_MAP_BYTES 0x8000U

extern uint32_t kshim_test_spmi_core[TEST_SPMI_CORE_BYTES / 4U];
extern uint32_t kshim_test_spmi_observer[TEST_SPMI_OBSERVER_BYTES / 4U];
extern uint32_t kshim_test_spmi_config[TEST_SPMI_CONFIG_BYTES / 4U];
extern uint32_t kshim_test_spmi_channels[1];
extern uint32_t kshim_test_spmi_map[TEST_SPMI_MAP_BYTES / 4U];
extern uint32_t kshim_test_spmi_owner[TEST_SPMI_MAP_BYTES / 4U];
extern uint64_t kshim_test_spmi_core_size;
extern uint64_t kshim_test_spmi_observer_size;
extern uint64_t kshim_test_spmi_config_size;
extern uint64_t kshim_test_spmi_map_size;
extern uint64_t kshim_test_spmi_owner_size;
extern unsigned kshim_test_spmi_ee;
extern unsigned kshim_test_spmi_bus;
extern unsigned kshim_test_spmi_channel;


#endif
