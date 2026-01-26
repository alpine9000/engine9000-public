#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#include "m68k/m68k.h"

#include "geo_debugger.h"
#include "geo_m68k.h"
#include "geo_profiler_elf.h" // reuse line table loader
#include "geo_protect.h"

// State
static int s_paused = 0;
static int s_step_frame = 0;   // step whole frame
static int s_step_instr = 0;   // step a single instruction
static int s_step_instr_after = 0; // break on next instruction hook
static int s_step_line  = 0;   // step to next source line
static uint32_t s_step_line_file = 0; // source file idx at step start (UINT32_MAX = wildcard)
static uint32_t s_step_line_num  = 0; // source line at step start (UINT32_MAX = wildcard)
static int s_step_next = 0;    // step to next source line after returning to same depth
static size_t s_step_next_depth = 0;
static int s_break_now = 0;    // immediate break requested (mid-frame)
static int s_break_requested = 0; // set in hook, latched at end of frame
static int s_resnap_needed = 0; // request base resnapshot after step/break

// Last PC seen and last-hit PC
static uint32_t s_last_pc = 0;
static uint32_t s_hit_pc = 0;

// Simple fixed-size breakpoint table
#define GEO_DBG_BP_MAX 64
static uint32_t s_bps[GEO_DBG_BP_MAX];
static size_t s_nbps = 0;
static uint32_t s_temp_bps[GEO_DBG_BP_MAX];
static size_t s_n_temp_bps = 0;
// Mirror call stack (return addresses)
#define GEO_DBG_STACK_MAX 512
static uint32_t s_callstack[GEO_DBG_STACK_MAX];
static size_t s_callstack_depth = 0;

// Fixed-size watchpoint table.
static geo_debug_watchpoint_t s_wps[GEO_WATCHPOINT_COUNT];
static uint64_t s_wps_enabled_mask = 0;
static geo_debug_watchbreak_t s_watchbreak = {0};
static int s_watchbreak_pending = 0;
static int s_watchbreak_requested = 0;
static int s_wps_suspend = 0;

static void log_bp_event(const char *verb, uint32_t pc24) {
    printf("Debugger: %s 0x%06x\n", verb, (unsigned)(pc24 & 0x00ffffffu));
    fflush(stdout);
}

// Line table for source mapping (optional)
static LineTable s_line_table = {0};
static const LineRow *s_rows = NULL; static size_t s_nrows = 0;

static inline const LineRow* lt_find(uint32_t addr){ return (s_rows && s_nrows) ? geo_line_find_row_addr(s_rows, s_nrows, addr) : NULL; }
// Forward declaration for next-line step
static void geo_debugger_step_next_line(void);

static void load_line_table_from_env(void) {
    const char *p = getenv("GEO_DBG_ELF");
    if (!p || !p[0]) p = getenv("GEO_PROF_ELF");
    if (!p || !p[0]) return;
    LineTable lt = {0};
    if (geo_elf_load_line_table(p, &lt)) {
        s_line_table = lt; s_rows = lt.rows; s_nrows = lt.nrows;
    }
}

void geo_debugger_init(void) {
    s_paused = 0; s_step_frame = 0; s_step_instr = 0; s_step_instr_after = 0; s_step_line = 0; s_step_line_file = 0; s_step_line_num = 0; s_step_next = 0; s_step_next_depth = 0; s_break_requested = 0; s_break_now = 0; s_resnap_needed = 0; s_last_pc = 0; s_hit_pc = 0;
    s_nbps = 0;
    s_n_temp_bps = 0;
    s_callstack_depth = 0;
    geo_debugger_reset_watchpoints();
    geo_protect_reset();
    load_line_table_from_env();
}

int  geo_debugger_is_paused(void) { return s_paused; }
void geo_debugger_continue(void) {
    s_paused = 0;
    s_step_frame = 0;
    s_step_instr_after = 0;
    s_step_line = 0;
    s_step_next = 0;
    s_break_requested = 0;
    s_watchbreak_requested = 0;
    s_break_now = 0;
}
void geo_debugger_step_frame(void) { s_paused = 0; s_step_frame = 1; }
void geo_debugger_step_instr(void) {
    s_paused = 0;
    s_step_instr_after = 0;
    s_step_instr = 1;
}
int  geo_debugger_break_now(void) { int r = s_break_now; s_break_now = 0; return r; }
int  geo_debugger_should_break_now(void) { return s_break_now; }
int  geo_debugger_consume_resnap_needed(void) { int r = s_resnap_needed; s_resnap_needed = 0; return r; }

static int has_breakpoint_locked(uint32_t pc24) {
    for (size_t i = 0; i < s_nbps; ++i) {
        if (s_bps[i] == pc24) return 1;
    }
    return 0;
}

static int consume_temp_breakpoint_locked(uint32_t pc24) {
    for (size_t i = 0; i < s_n_temp_bps; ++i) {
        if (s_temp_bps[i] == pc24) {
            s_temp_bps[i] = s_temp_bps[s_n_temp_bps - 1];
            s_n_temp_bps--;
            return 1;
        }
    }
    return 0;
}

static int has_breakpoint(uint32_t pc24) {
    return has_breakpoint_locked(pc24);
}

static void toggle_breakpoint(uint32_t pc24) {
    pc24 &= 0x00ffffffu;
    for (size_t i=0;i<s_nbps;i++) {
        if (s_bps[i] == pc24) {
            // remove by swap-with-last
            s_bps[i] = s_bps[s_nbps-1];
            s_nbps--;
            return;
        }
    }
    if (s_nbps < GEO_DBG_BP_MAX) {
        s_bps[s_nbps++] = pc24;
        log_bp_event("Breakpoint set", pc24);
    }
}

void geo_debugger_add_breakpoint(uint32_t pc24) {
    pc24 &= 0x00ffffffu;
    if (!has_breakpoint_locked(pc24) && s_nbps < GEO_DBG_BP_MAX) {
        s_bps[s_nbps++] = pc24;
        log_bp_event("Breakpoint set", pc24);
    }
}

void geo_debugger_remove_breakpoint(uint32_t pc24) {
    pc24 &= 0x00ffffffu;
    for (size_t i=0;i<s_nbps;i++) {
        if (s_bps[i] == pc24) {
            s_bps[i] = s_bps[s_nbps-1];
            s_nbps--;
            log_bp_event("Breakpoint cleared", pc24);
            return;
        }
    }
}

int geo_debugger_has_breakpoint(uint32_t pc24) { return has_breakpoint(pc24 & 0x00ffffffu); }

void geo_debugger_add_temp_breakpoint(uint32_t pc24) {
    pc24 &= 0x00ffffffu;
    for (size_t i = 0; i < s_n_temp_bps; ++i) {
        if (s_temp_bps[i] == pc24) {
            return;
        }
    }
    if (s_n_temp_bps < GEO_DBG_BP_MAX) {
        s_temp_bps[s_n_temp_bps++] = pc24;
    }
}

void geo_debugger_remove_temp_breakpoint(uint32_t pc24) {
    pc24 &= 0x00ffffffu;
    for (size_t i = 0; i < s_n_temp_bps; ++i) {
        if (s_temp_bps[i] == pc24) {
            s_temp_bps[i] = s_temp_bps[s_n_temp_bps - 1];
            s_n_temp_bps--;
            return;
        }
    }
}

// Begin step-until-next-source-line on next emulation frame
static void geo_debugger_step_next_line(void) {
    s_paused = 0;
    s_step_instr = 0;
    s_step_instr_after = 0;
    s_step_line = 1;
    s_step_next = 0;
    // Capture current mapping; if none, wildcard to any mapped line
    uint32_t pc24 = (uint32_t)(m68k_get_reg(NULL, M68K_REG_PC)) & 0x00ffffffu;
    const LineRow *r = lt_find(pc24);
    if (r) { s_step_line_file = r->file; s_step_line_num = r->line; }
    else { s_step_line_file = UINT32_MAX; s_step_line_num = UINT32_MAX; }
}

void geo_debugger_break_immediate(void) {
    s_paused = 1;
    s_step_frame = 0; s_step_instr = 0; s_step_instr_after = 0; s_step_line = 0; s_step_next = 0;
    s_break_requested = 1; s_break_now = 1; s_resnap_needed = 1;
    m68k_end_timeslice();
}

void geo_debugger_toggle_break(void) {
    if (!s_paused) geo_debugger_break_immediate();
    else geo_debugger_continue();
}

void geo_debugger_step_instr_cmd(void) {
    s_paused = 0;
    s_step_line = 0;
    s_step_next = 0;
    s_step_instr_after = 0;
    s_step_instr = 1;
}

void geo_debugger_step_next_line_cmd(void) {
    geo_debugger_step_next_line();
}

void geo_debugger_step_next_over_cmd(void) {
    s_paused = 0;
    s_step_instr = 0;
    s_step_instr_after = 0;
    s_step_line = 1;
    s_step_next = 1;
    s_step_next_depth = s_callstack_depth;
    uint32_t pc24 = (uint32_t)(m68k_get_reg(NULL, M68K_REG_PC)) & 0x00ffffffu;
    const LineRow *r = lt_find(pc24);
    if (r) { s_step_line_file = r->file; s_step_line_num = r->line; }
    else { s_step_line_file = UINT32_MAX; s_step_line_num = UINT32_MAX; }
}


void geo_debugger_instr_hook(unsigned pc) {
    // Called before executing instruction
    s_last_pc = pc & 0x00ffffffu;
    if (s_step_instr_after) {
        s_step_instr_after = 0;
        s_paused = 1;
        s_break_now = 1;
        s_resnap_needed = 1;
        m68k_end_timeslice();
        return;
    }
    // Disassembler reads should not trigger watchpoints.
    s_wps_suspend++;
    uint16_t op = (uint16_t)m68k_read_disassembler_16(s_last_pc);
    s_wps_suspend--;
    if ((op & 0xFFC0u) == 0x4E80u) {
        int mode = (op >> 3) & 7;
        int reg = op & 7;
        int ext = 0;
        if (mode == 5 || mode == 6) {
            ext = 2;
        } else if (mode == 7) {
            if (reg == 0 || reg == 2 || reg == 3) ext = 2;
            else if (reg == 1) ext = 4;
            else ext = -1;
        } else if (mode < 2) {
            ext = -1;
        }
        if (ext >= 0) {
            if (s_callstack_depth < GEO_DBG_STACK_MAX) {
                s_callstack[s_callstack_depth++] = s_last_pc & 0x00ffffffu;
            }
        }
    } else if ((op & 0xFF00u) == 0x6100u) {
        if (s_callstack_depth < GEO_DBG_STACK_MAX) {
            s_callstack[s_callstack_depth++] = s_last_pc & 0x00ffffffu;
        }
    } else if (op == 0x4E75u || op == 0x4E74u || op == 0x4E73u || op == 0x4E77u) {
        if (s_callstack_depth > 0) {
            s_callstack_depth--;
        }
    }
    // Single-instruction step: break on next instruction hook
    if (s_step_instr) {
        s_step_instr = 0;
        s_step_instr_after = 1;
        return;
    }
    // Step until next source line
    if (s_step_line) {
        const LineRow *r = lt_find(s_last_pc);
        int should_break = 0;
        if (r) {
            if (s_step_line_file == UINT32_MAX) {
                should_break = 1; // break on first mapped line
            } else if (r->file != s_step_line_file || r->line != s_step_line_num) {
                should_break = 1;
            }
        }
        if (should_break && (!s_step_next || s_callstack_depth <= s_step_next_depth)) {
            s_step_line = 0;
            s_step_next = 0;
            s_paused = 1;
            s_break_now = 1;
            s_resnap_needed = 1;
            m68k_end_timeslice();
            return;
        }
    }
    int hit_temp = consume_temp_breakpoint_locked(s_last_pc);
    int hit_bp = hit_temp || has_breakpoint_locked(s_last_pc);
    if (hit_bp) {
        // Mid-frame breakpoint: pause immediately and end timeslice
        s_break_requested = 1; // used for user notification at frame end
        s_hit_pc = s_last_pc;
        s_paused = 1;
        s_break_now = 1;
        s_resnap_needed = 1;
        m68k_end_timeslice();
        return;
    }
}

void geo_debugger_end_of_frame_update(void (*notify)(const char *msg, int frames)) {
    if (s_watchbreak_requested) {
        s_watchbreak_requested = 0;
        s_paused = 1;
        if (notify) notify("Watchpoint hit", 120);
        return;
    }
    if (s_break_requested) {
        s_break_requested = 0;
        s_paused = 1;
        if (notify) notify("Breakpoint hit", 120);
        return;
    }
    if (s_step_frame) {
        // We ran one frame after a step-frame request; re-pause now
        s_step_frame = 0;
        s_paused = 1;
        if (notify) notify("Step frame", 90);
    }
}

size_t geo_debugger_read_callstack(uint32_t *out, size_t cap) {
    if (!out || cap == 0) {
        return 0;
    }
    size_t count = s_callstack_depth;
    if (count > cap) {
        count = cap;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = s_callstack[i];
    }
    return count;
}

size_t
geo_debugger_readMemory(uint32_t addr, uint8_t *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    s_wps_suspend++;
    for (size_t i = 0; i < cap; ++i) {
        out[i] = (uint8_t)m68k_read_memory_8((unsigned)(addr + i));
    }
    s_wps_suspend--;
    return cap;
}

int
geo_debugger_writeMemory(uint32_t addr, uint32_t value, size_t size)
{
    s_wps_suspend++;
    if (size == 1) {
        m68k_write_memory_8((unsigned)addr, value & 0xffu);
        s_wps_suspend--;
        return 1;
    }
    if (size == 2) {
        m68k_write_memory_16((unsigned)addr, value & 0xffffu);
        s_wps_suspend--;
        return 1;
    }
    if (size == 4) {
        m68k_write_memory_32((unsigned)addr, value);
        s_wps_suspend--;
        return 1;
    }
    s_wps_suspend--;
    return 0;
}

static uint32_t
mask_value(uint32_t value, uint32_t size_bits)
{
    if (size_bits == 8) {
        return value & 0xffu;
    }
    if (size_bits == 16) {
        return value & 0xffffu;
    }
    return value;
}

static int
watchpoint_match(const geo_debug_watchpoint_t *wp, uint32_t access_addr, uint32_t access_kind, uint32_t access_size_bits, uint32_t value, uint32_t old_value, int old_value_valid)
{
    uint32_t op = wp->op_mask;
    if (!(op & (GEO_WATCH_OP_READ | GEO_WATCH_OP_WRITE))) {
        return 0;
    }
    if (access_kind == GEO_WATCH_ACCESS_READ) {
        if (!(op & GEO_WATCH_OP_READ)) {
            return 0;
        }
    } else if (access_kind == GEO_WATCH_ACCESS_WRITE) {
        if (!(op & GEO_WATCH_OP_WRITE)) {
            return 0;
        }
    } else {
        return 0;
    }

    if (op & GEO_WATCH_OP_ADDR_COMPARE_MASK) {
        uint32_t mask = wp->addr_mask_operand;
        if (mask != 0) {
            if ((access_addr & mask) != (wp->addr & mask)) {
                return 0;
            }
        }
    } else {
        if (access_addr != wp->addr) {
            return 0;
        }
    }

    if (op & GEO_WATCH_OP_ACCESS_SIZE) {
        if (wp->size_operand != 8 && wp->size_operand != 16 && wp->size_operand != 32) {
            return 0;
        }
        if (access_size_bits != wp->size_operand) {
            return 0;
        }
    }

    uint32_t v = mask_value(value, access_size_bits);
    uint32_t oldv = mask_value(old_value, access_size_bits);

    if (op & GEO_WATCH_OP_VALUE_EQ) {
        if (v != mask_value(wp->value_operand, access_size_bits)) {
            return 0;
        }
    }
    if (op & GEO_WATCH_OP_OLD_VALUE_EQ) {
        if (!old_value_valid) {
            return 0;
        }
        if (oldv != mask_value(wp->old_value_operand, access_size_bits)) {
            return 0;
        }
    }
    if (op & GEO_WATCH_OP_VALUE_NEQ_OLD) {
        if (!old_value_valid) {
            return 0;
        }
        if (oldv == mask_value(wp->diff_operand, access_size_bits)) {
            return 0;
        }
    }

    return 1;
}

void
geo_debugger_reset_watchpoints(void)
{
    memset(s_wps, 0, sizeof(s_wps));
    s_wps_enabled_mask = 0;
    memset(&s_watchbreak, 0, sizeof(s_watchbreak));
    s_watchbreak_pending = 0;
    s_watchbreak_requested = 0;
    s_wps_suspend = 0;
}

int
geo_debugger_add_watchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand)
{
    for (uint32_t i = 0; i < GEO_WATCHPOINT_COUNT; ++i) {
        uint64_t bit = (uint64_t)1u << i;
        if ((s_wps_enabled_mask & bit) != 0) {
            continue;
        }
        if (s_wps[i].op_mask != 0) {
            continue;
        }
        s_wps[i].addr = addr;
        s_wps[i].op_mask = op_mask;
        s_wps[i].diff_operand = diff_operand;
        s_wps[i].value_operand = value_operand;
        s_wps[i].old_value_operand = old_value_operand;
        s_wps[i].size_operand = size_operand;
        s_wps[i].addr_mask_operand = addr_mask_operand;
        s_wps_enabled_mask |= bit;
        return (int)i;
    }
    return -1;
}

void
geo_debugger_remove_watchpoint(uint32_t index)
{
    if (index >= GEO_WATCHPOINT_COUNT) {
        return;
    }
    s_wps_enabled_mask &= ~((uint64_t)1u << index);
    memset(&s_wps[index], 0, sizeof(s_wps[index]));
}

size_t
geo_debugger_read_watchpoints(geo_debug_watchpoint_t *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    size_t count = GEO_WATCHPOINT_COUNT;
    if (count > cap) {
        count = cap;
    }
    memcpy(out, s_wps, count * sizeof(out[0]));
    return count;
}

uint64_t
geo_debugger_get_watchpoint_enabled_mask(void)
{
    return s_wps_enabled_mask;
}

void
geo_debugger_set_watchpoint_enabled_mask(uint64_t mask)
{
    s_wps_enabled_mask = mask;
}

int
geo_debugger_consume_watchbreak(geo_debug_watchbreak_t *out)
{
    if (!out) {
        return 0;
    }
    if (!s_watchbreak_pending) {
        return 0;
    }
    *out = s_watchbreak;
    s_watchbreak_pending = 0;
    return 1;
}

static void
watchbreak_request(uint32_t index, uint32_t access_addr, uint32_t access_kind, uint32_t access_size_bits, uint32_t value, uint32_t old_value, int old_value_valid)
{
    if (s_watchbreak_pending) {
        return;
    }
    if (index >= GEO_WATCHPOINT_COUNT) {
        return;
    }
    geo_debug_watchpoint_t *wp = &s_wps[index];

    memset(&s_watchbreak, 0, sizeof(s_watchbreak));
    s_watchbreak.index = index;
    s_watchbreak.watch_addr = wp->addr;
    s_watchbreak.op_mask = wp->op_mask;
    s_watchbreak.diff_operand = wp->diff_operand;
    s_watchbreak.value_operand = wp->value_operand;
    s_watchbreak.old_value_operand = wp->old_value_operand;
    s_watchbreak.size_operand = wp->size_operand;
    s_watchbreak.addr_mask_operand = wp->addr_mask_operand;
    s_watchbreak.access_addr = access_addr;
    s_watchbreak.access_kind = access_kind;
    s_watchbreak.access_size = access_size_bits;
    s_watchbreak.value = mask_value(value, access_size_bits);
    s_watchbreak.old_value = mask_value(old_value, access_size_bits);
    s_watchbreak.old_value_valid = old_value_valid ? 1u : 0u;

    s_watchbreak_pending = 1;
    s_watchbreak_requested = 1;
    s_paused = 1;
    s_break_now = 1;
    s_resnap_needed = 1;
}

void
geo_debugger_watchpoint_read(uint32_t addr, uint32_t value, uint32_t size_bits)
{
    if (s_wps_suspend > 0) {
        return;
    }
    if (s_paused) {
        return;
    }
    if (s_wps_enabled_mask == 0) {
        return;
    }

    for (uint32_t index = 0; index < GEO_WATCHPOINT_COUNT; ++index) {
        if ((s_wps_enabled_mask & ((uint64_t)1u << index)) == 0) {
            continue;
        }
        if (watchpoint_match(&s_wps[index], addr, GEO_WATCH_ACCESS_READ, size_bits, value, value, 1)) {
            watchbreak_request(index, addr, GEO_WATCH_ACCESS_READ, size_bits, value, value, 1);
            return;
        }
    }
}

void
geo_debugger_watchpoint_write(uint32_t addr, uint32_t value, uint32_t old_value, uint32_t size_bits, int old_value_valid)
{
    if (s_wps_suspend > 0) {
        return;
    }
    if (s_paused) {
        return;
    }
    if (s_wps_enabled_mask == 0) {
        return;
    }

    for (uint32_t index = 0; index < GEO_WATCHPOINT_COUNT; ++index) {
        if ((s_wps_enabled_mask & ((uint64_t)1u << index)) == 0) {
            continue;
        }
        if (watchpoint_match(&s_wps[index], addr, GEO_WATCH_ACCESS_WRITE, size_bits, value, old_value, old_value_valid)) {
            watchbreak_request(index, addr, GEO_WATCH_ACCESS_WRITE, size_bits, value, old_value, old_value_valid);
            return;
        }
    }
}

void
geo_debugger_watchpoint_suspend(void)
{
    s_wps_suspend++;
}

void
geo_debugger_watchpoint_resume(void)
{
    if (s_wps_suspend > 0) {
        s_wps_suspend--;
    }
}
