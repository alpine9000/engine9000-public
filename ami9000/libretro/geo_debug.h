#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uae/types.h"

// Debug base register sections (passed to geo_set_debug_base_callback()).
#define GEO_DEBUG_BASE_SECTION_TEXT 0u
#define GEO_DEBUG_BASE_SECTION_DATA 1u
#define GEO_DEBUG_BASE_SECTION_BSS  2u

int
geo_debug_instructionHook(uaecptr pc, uae_u16 opcode);

void
geo_debug_pause(void);

void
geo_debug_resume(void);

int
geo_debug_is_paused(void);

void
geo_debug_step_instr(void);

void
geo_debug_step_line(void);

void
geo_debug_step_next(void);

size_t
geo_debug_read_callstack(uint32_t *out, size_t cap);

size_t
geo_debug_read_regs(uint32_t *out, size_t cap);

size_t
geo_debug_read_memory(uint32_t addr, uint8_t *out, size_t cap);

int
geo_debug_write_memory(uint32_t addr, uint32_t value, size_t size);

size_t
geo_debug_disassemble_quick(uint32_t pc, char *out, size_t cap);

uint64_t
geo_debug_read_cycle_count(void);

void
geo_debug_add_breakpoint(uint32_t addr);

void
geo_debug_remove_breakpoint(uint32_t addr);

void
geo_debug_add_temp_breakpoint(uint32_t addr);

void
geo_debug_remove_temp_breakpoint(uint32_t addr);

// Optional host callback invoked once per vblank/frame.
void
geo_set_vblank_callback(void (*cb)(void *), void *user);

void
geo_vblank_notify(void);

// Optional host callback invoked when the target writes a new relocatable base.
void
geo_set_debug_base_callback(void (*cb)(uint32_t section, uint32_t base));

// Optional host callback invoked when the target requests a breakpoint via a fake debug peripheral.
void
geo_set_debug_breakpoint_callback(void (*cb)(uint32_t addr));
