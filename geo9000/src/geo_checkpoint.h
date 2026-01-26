#pragma once

#include <stddef.h>
#include <stdint.h>

#define GEO_CHECKPOINT_COUNT 64

typedef struct geo_debug_checkpoint {
    uint64_t current;
    uint64_t accumulator;
    uint64_t count;
    uint64_t average;
    uint64_t minimum;
    uint64_t maximum;
} geo_debug_checkpoint_t;

void
geo_checkpoint_reset(void);

void
geo_checkpoint_setEnabled(int enabled);

int
geo_checkpoint_isEnabled(void);

void
geo_checkpoint_state_save(uint8_t *st);

void
geo_checkpoint_state_load(uint8_t *st);

void
geo_checkpoint_write(uint8_t index);

void
geo_checkpoint_tick(uint64_t ticks);

size_t
geo_checkpoint_read(geo_debug_checkpoint_t *out, size_t cap);
