#ifndef GEO_DEBUG_ROM_H
#define GEO_DEBUG_ROM_H

#include <stddef.h>
#include <stdint.h>

typedef struct geo_debug_rom_region {
    const uint8_t *data;
    size_t size;
} geo_debug_rom_region_t;

size_t
geo_debug_get_p1_rom(geo_debug_rom_region_t *out, size_t cap);

size_t
geo_debug_disassemble_quick(uint32_t pc, char *out, size_t cap);

#endif // GEO_DEBUG_ROM_H
