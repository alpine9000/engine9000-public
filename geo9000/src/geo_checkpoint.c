#include "geo_checkpoint.h"

#include <string.h>

#include "geo_serial.h"

static geo_debug_checkpoint_t geo_checkpoint_data[GEO_CHECKPOINT_COUNT];
static int geo_checkpoint_active = -1;
static int geo_checkpoint_enabled = 0;

void
geo_checkpoint_reset(void)
{
    memset(geo_checkpoint_data, 0, sizeof(geo_checkpoint_data));
    geo_checkpoint_active = -1;
}

void
geo_checkpoint_setEnabled(int enabled)
{
    geo_checkpoint_enabled = enabled ? 1 : 0;
    if (!geo_checkpoint_enabled) {
        geo_checkpoint_active = -1;
    }
}

int
geo_checkpoint_isEnabled(void)
{
    return geo_checkpoint_enabled;
}

void
geo_checkpoint_state_save(uint8_t *st)
{
    if (!st) {
        return;
    }
    geo_serial_push8(st, (uint8_t)geo_checkpoint_enabled);
    geo_serial_push32(st, (uint32_t)geo_checkpoint_active);
    for (size_t i = 0; i < GEO_CHECKPOINT_COUNT; ++i) {
        geo_serial_push64(st, geo_checkpoint_data[i].current);
        geo_serial_push64(st, geo_checkpoint_data[i].accumulator);
        geo_serial_push64(st, geo_checkpoint_data[i].count);
        geo_serial_push64(st, geo_checkpoint_data[i].average);
        geo_serial_push64(st, geo_checkpoint_data[i].minimum);
        geo_serial_push64(st, geo_checkpoint_data[i].maximum);
    }
}

void
geo_checkpoint_state_load(uint8_t *st)
{
    if (!st) {
        return;
    }
    geo_checkpoint_enabled = geo_serial_pop8(st) ? 1 : 0;
    geo_checkpoint_active = (int)geo_serial_pop32(st);
    for (size_t i = 0; i < GEO_CHECKPOINT_COUNT; ++i) {
        geo_checkpoint_data[i].current = geo_serial_pop64(st);
        geo_checkpoint_data[i].accumulator = geo_serial_pop64(st);
        geo_checkpoint_data[i].count = geo_serial_pop64(st);
        geo_checkpoint_data[i].average = geo_serial_pop64(st);
        geo_checkpoint_data[i].minimum = geo_serial_pop64(st);
        geo_checkpoint_data[i].maximum = geo_serial_pop64(st);
    }
    if (!geo_checkpoint_enabled) {
        geo_checkpoint_active = -1;
    }
    if (geo_checkpoint_active < -1 || geo_checkpoint_active >= (int)GEO_CHECKPOINT_COUNT) {
        geo_checkpoint_active = -1;
    }
}

void
geo_checkpoint_write(uint8_t index)
{
    if (!geo_checkpoint_enabled) {
        return;
    }
    if (index >= GEO_CHECKPOINT_COUNT) {
        return;
    }
    if (geo_checkpoint_active >= 0) {
        geo_debug_checkpoint_t *prev = &geo_checkpoint_data[geo_checkpoint_active];
        uint64_t sample = prev->current;
        if (prev->count == 0) {
            prev->minimum = sample;
            prev->maximum = sample;
        } else {
            if (sample < prev->minimum) {
                prev->minimum = sample;
            }
            if (sample > prev->maximum) {
                prev->maximum = sample;
            }
        }
        prev->count += 1;
        prev->accumulator += sample;
        prev->average = prev->count ? (prev->accumulator / prev->count) : 0;
        prev->current = 0;
    }
    geo_checkpoint_active = (int)index;
    geo_checkpoint_data[index].current = 0;
}

void
geo_checkpoint_tick(uint64_t ticks)
{
    if (!geo_checkpoint_enabled) {
        return;
    }
    if (geo_checkpoint_active < 0) {
        return;
    }
    geo_checkpoint_data[geo_checkpoint_active].current += ticks;
}

size_t
geo_checkpoint_read(geo_debug_checkpoint_t *out, size_t cap)
{
    if (!out || cap < sizeof(geo_checkpoint_data)) {
        return 0;
    }
    memcpy(out, geo_checkpoint_data, sizeof(geo_checkpoint_data));
    return sizeof(geo_checkpoint_data);
}
