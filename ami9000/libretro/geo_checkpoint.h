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

