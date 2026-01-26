#include "geo_debug_text.h"
#include "geo_export.h"

#include <string.h>

#define GEO_DEBUG_TEXT_CAP 8192

static char s_buf[GEO_DEBUG_TEXT_CAP];
static size_t s_head;
static size_t s_tail;
static size_t s_count;

void
geo_debug_text_write(uint8_t byte)
{
    if (s_count == GEO_DEBUG_TEXT_CAP) {
        s_tail = (s_tail + 1) % GEO_DEBUG_TEXT_CAP;
        s_count--;
    }
    s_buf[s_head] = (char)byte;
    s_head = (s_head + 1) % GEO_DEBUG_TEXT_CAP;
    s_count++;
}

GEO_EXPORT size_t
geo_debug_text_read(char *out, size_t cap)
{
    if (!out || cap == 0 || s_count == 0) {
        return 0;
    }
    size_t n = s_count < cap ? s_count : cap;
    for (size_t i = 0; i < n; ++i) {
        out[i] = s_buf[s_tail];
        s_tail = (s_tail + 1) % GEO_DEBUG_TEXT_CAP;
    }
    s_count -= n;
    return n;
}
