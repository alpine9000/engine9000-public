#pragma once

#include <stdint.h>

void
geo_cycles_reset(void);

void
geo_cycles_add(uint64_t cycles);

uint64_t
geo_cycles_get(void);

void
geo_cycles_state_save(uint8_t *st);

void
geo_cycles_state_load(uint8_t *st);
