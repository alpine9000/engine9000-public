#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct geo_debug_rom_region {
    const uint8_t *data;
    size_t size;
} geo_debug_rom_region_t;

