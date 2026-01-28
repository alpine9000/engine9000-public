#pragma once

#include <stddef.h>
#include <stdint.h>

#define GEO_PROTECT_COUNT 64

#define GEO_PROTECT_MODE_BLOCK 0u
#define GEO_PROTECT_MODE_SET   1u

typedef struct geo_debug_protect
{
    uint32_t addr;
    uint32_t addrMask;
    uint32_t sizeBits;
    uint32_t mode;
    uint32_t value;
} geo_debug_protect_t;

