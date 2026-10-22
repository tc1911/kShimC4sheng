#pragma once

#include <stdint.h>

void arm64_boot_linux(uint64_t linux_base,
                      uint64_t device_tree_address,
                      uint64_t entry_offset)
    __attribute__((noreturn));

void arm64_boot_image(uint64_t entry_address,
                      uint64_t image_arg0,
                      uint64_t image_arg1,
                      uint64_t image_arg2,
                      uint64_t image_arg3)
    __attribute__((noreturn));