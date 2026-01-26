#ifndef GEO_PROTECT_H
#define GEO_PROTECT_H

#include <stddef.h>
#include <stdint.h>

#define GEO_PROTECT_COUNT 64

#define GEO_PROTECT_MODE_BLOCK 0u
#define GEO_PROTECT_MODE_SET   1u

typedef struct geo_debug_protect
{
    uint32_t addr;
    uint32_t addrMask;
    uint32_t sizeBits; // protected region size: 8/16/32 (bits)
    uint32_t mode;     // GEO_PROTECT_MODE_*
    uint32_t value;    // set value (masked to sizeBits), ignored for BLOCK
} geo_debug_protect_t;

void
geo_protect_reset(void);

int
geo_protect_add(uint32_t addr24, uint32_t sizeBits, uint32_t mode, uint32_t value);

void
geo_protect_remove(uint32_t index);

size_t
geo_protect_read(geo_debug_protect_t *out, size_t cap);

uint64_t
geo_protect_getEnabledMask(void);

void
geo_protect_setEnabledMask(uint64_t mask);

void
geo_protect_filterWrite(uint32_t addr24, uint32_t sizeBits, uint32_t oldValue, int oldValueValid, uint32_t *inoutValue);

#endif // GEO_PROTECT_H
