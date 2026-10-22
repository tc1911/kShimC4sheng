#pragma once

#include <kshim.h>

void pl011_init(uint64_t mmio_base);

void pl011_write(
    uint64_t mmio_base, char *str, int len);

int pl011_try_read(uint64_t mmio_base, char *character);
