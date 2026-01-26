// Simple 68K debugger (end-of-frame pause)
#ifndef GEO_DEBUGGER_H
#define GEO_DEBUGGER_H

#include <stdint.h>
#include <stddef.h>

#include "geo_watchpoint.h"

// Initialize debugger (reads optional env GEO_DBG_ELF or GEO_PROF_ELF)
void geo_debugger_init(void);

// Instruction hook (called via dispatcher)
void geo_debugger_instr_hook(unsigned pc);

// (Debugger is always enabled; window visibility controls display)

// Pause/continue state
int  geo_debugger_is_paused(void);
void geo_debugger_continue(void);

// Request a single-frame step (pause again after next frame)
void geo_debugger_step_frame(void);

// Request a single-instruction step (mid-frame halt via timeslice end)
void geo_debugger_step_instr(void);

// Query immediate break request for mid-frame halts (clears the flag)
int  geo_debugger_break_now(void);

// Peek immediate break request (does not clear the flag)
int  geo_debugger_should_break_now(void);

// Returns 1 if a step/break just modified the emulated frame and the
// frontend should resnapshot the base visible region; clears the flag.
int  geo_debugger_consume_resnap_needed(void);

// Called from retro layer at end of a frame to latch breakpoint hits
void geo_debugger_end_of_frame_update(void (*notify)(const char *msg, int frames));

// Direct control helpers (bypass UI edge detection)
void geo_debugger_break_immediate(void);     // break now (mid-frame), enable if needed
void geo_debugger_toggle_break(void);        // break if running, continue if paused
void geo_debugger_step_instr_cmd(void);      // arm single-instruction step
void geo_debugger_step_next_line_cmd(void);  // arm next-line step
void geo_debugger_step_next_over_cmd(void);  // arm next-over (C "next")

// Breakpoint control by address (PC is 24-bit for Neo Geo)
void geo_debugger_add_breakpoint(uint32_t pc24);
void geo_debugger_remove_breakpoint(uint32_t pc24);
int  geo_debugger_has_breakpoint(uint32_t pc24);
void geo_debugger_add_temp_breakpoint(uint32_t pc24);
void geo_debugger_remove_temp_breakpoint(uint32_t pc24);

// Mirror call stack (return addresses, bottom to top)
size_t geo_debugger_read_callstack(uint32_t *out, size_t cap);

size_t
geo_debugger_readMemory(uint32_t addr, uint8_t *out, size_t cap);

int
geo_debugger_writeMemory(uint32_t addr, uint32_t value, size_t size);

// Watchpoint table and watchbreak reporting.
void     geo_debugger_reset_watchpoints(void);
int      geo_debugger_add_watchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand);
void     geo_debugger_remove_watchpoint(uint32_t index);
size_t   geo_debugger_read_watchpoints(geo_debug_watchpoint_t *out, size_t cap);
uint64_t geo_debugger_get_watchpoint_enabled_mask(void);
void     geo_debugger_set_watchpoint_enabled_mask(uint64_t mask);
int      geo_debugger_consume_watchbreak(geo_debug_watchbreak_t *out);

// Memory access hooks (called by the bus implementation).
void geo_debugger_watchpoint_read(uint32_t addr, uint32_t value, uint32_t size_bits);
void geo_debugger_watchpoint_write(uint32_t addr, uint32_t value, uint32_t old_value, uint32_t size_bits, int old_value_valid);
void geo_debugger_watchpoint_suspend(void);
void geo_debugger_watchpoint_resume(void);

#endif // GEO_DEBUGGER_H
