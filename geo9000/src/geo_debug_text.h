#ifndef GEO_DEBUG_TEXT_H
#define GEO_DEBUG_TEXT_H

#include <stddef.h>
#include <stdint.h>

void geo_debug_text_write(uint8_t byte);
size_t geo_debug_text_read(char *out, size_t cap);

#endif // GEO_DEBUG_TEXT_H
