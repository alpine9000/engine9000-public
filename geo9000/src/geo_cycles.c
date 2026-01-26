#include "geo_cycles.h"

#include "geo_serial.h"

static uint64_t geo_cycles_total = 0;

void
geo_cycles_reset(void)
{
    geo_cycles_total = 0;
}

void
geo_cycles_add(uint64_t cycles)
{
    geo_cycles_total += cycles;
}

uint64_t
geo_cycles_get(void)
{
    return geo_cycles_total;
}

void
geo_cycles_state_save(uint8_t *st)
{
    if (!st) {
        return;
    }
    geo_serial_push64(st, geo_cycles_total);
}

void
geo_cycles_state_load(uint8_t *st)
{
    if (!st) {
        return;
    }
    geo_cycles_total = geo_serial_pop64(st);
}
