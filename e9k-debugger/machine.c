// Central snapshot of target machine state (registers, stack frames)
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <limits.h>
#ifdef _WIN32
#define getline w64_getline
#endif
#include "resource.h"
#include "machine.h"
#include "debugger.h"
#include "ui.h"
#include "alloc.h"
#include "debug.h"
#include "libretro_host.h"
#include "addr2line.h"

static void
machine_clearRegs(machine_t *m)
{
    if (!m) {
        return;
    }
    alloc_free(m->regs);
    m->regs = NULL;
    m->reg_count = 0;
}

static void
machine_clearStack(machine_t *m)
{
    if (!m) {
        return;
    }
    alloc_free(m->frames);
    m->frames = NULL;
    m->frame_count = 0;
}

void
machine_clearBreakpoints(machine_t *m)
{
    if (!m) {
        return;
    }
    alloc_free(m->breakpoints);
    m->breakpoints = NULL;
    m->breakpoint_count = 0;
}

void
machine_init(machine_t *m)
{
    if (!m) {
        return;
    }
    memset(m, 0, sizeof(*m));
    m->next_breakpoint_id = 1;
}

void
machine_shutdown(machine_t *m)
{
    if (!m) {
        return;
    }
    machine_clearRegs(m);
    machine_clearStack(m);
    machine_clearBreakpoints(m);
    if (m->reg_names) {
        for (int i = 0; i < m->reg_name_count; i++) {
            alloc_free(m->reg_names[i]);
        }
        alloc_free(m->reg_names);
        m->reg_names = NULL;
        m->reg_name_count = 0;
    }
}

int
machine_getRegs(machine_t *m, const machine_reg_t **out, int *count)
{
    if (!m || !out || !count) {
        return 0;
    }
    *out = m->regs;
    *count = m->reg_count;
    return 1;
}

int
machine_getStack(machine_t *m, const machine_frame_t **out, int *count)
{
    if (!m || !out || !count) {
        return 0;
    }
    *out = m->frames;
    *count = m->frame_count;
    return 1;
}

int
machine_getBreakpoints(machine_t *m, const machine_breakpoint_t **out, int *count)
{
    if (!m || !out || !count) {
        return 0;
    }
    *out = m->breakpoints;
    *count = m->breakpoint_count;
    return 1;
}

machine_breakpoint_t *
machine_findBreakpointByAddr(machine_t *m, uint32_t addr)
{
    if (!m) {
        return NULL;
    }
    addr &= 0x00ffffffu;
    for (int i = 0; i < m->breakpoint_count; ++i) {
        if ((uint32_t)m->breakpoints[i].addr == addr) {
            return &m->breakpoints[i];
        }
    }
    return NULL;
}

machine_breakpoint_t *
machine_findBreakpointByNumber(machine_t *m, int number)
{
    if (!m) {
        return NULL;
    }
    for (int i = 0; i < m->breakpoint_count; ++i) {
        if (m->breakpoints[i].number == number) {
            return &m->breakpoints[i];
        }
    }
    return NULL;
}

machine_breakpoint_t *
machine_addBreakpoint(machine_t *m, uint32_t addr, int enabled)
{
    if (!m) {
        return NULL;
    }
    addr &= 0x00ffffffu;
    machine_breakpoint_t *existing = machine_findBreakpointByAddr(m, addr);
    if (existing) {
        if (enabled) {
            existing->enabled = 1;
        }
        return existing;
    }
    int new_count = m->breakpoint_count + 1;
    machine_breakpoint_t *new_arr = (machine_breakpoint_t*)alloc_realloc(m->breakpoints,
                                                                          (size_t)new_count * sizeof(*new_arr));
    if (!new_arr) {
        return NULL;
    }
    m->breakpoints = new_arr;
    machine_breakpoint_t *bp = &m->breakpoints[m->breakpoint_count];
    memset(bp, 0, sizeof(*bp));
    bp->number = m->next_breakpoint_id++;
    bp->enabled = enabled ? 1 : 0;
    bp->addr = addr;
    snprintf(bp->addr_text, sizeof(bp->addr_text), "0x%06X", (unsigned)addr);
    strncpy(bp->type, "breakpoint", sizeof(bp->type) - 1);
    strncpy(bp->disp, "keep", sizeof(bp->disp) - 1);
    m->breakpoint_count = new_count;
    return bp;
}

int
machine_setBreakpointEnabled(machine_t *m, int number, int enabled, uint32_t *out_addr)
{
    if (out_addr) {
        *out_addr = 0;
    }
    machine_breakpoint_t *bp = machine_findBreakpointByNumber(m, number);
    if (!bp) {
        return 0;
    }
    if (out_addr) {
        *out_addr = (uint32_t)bp->addr;
    }
    bp->enabled = enabled ? 1 : 0;
    return 1;
}

int
machine_removeBreakpointByAddr(machine_t *m, uint32_t addr)
{
    if (!m) {
        return 0;
    }
    addr &= 0x00ffffffu;
    for (int i = 0; i < m->breakpoint_count; ++i) {
        if ((uint32_t)m->breakpoints[i].addr == addr) {
            int last = m->breakpoint_count - 1;
            if (i != last) {
                memmove(&m->breakpoints[i], &m->breakpoints[i + 1],
                        (size_t)(last - i) * sizeof(*m->breakpoints));
            }
            m->breakpoint_count--;
            return 1;
        }
    }
    return 0;
}

int
machine_findReg(machine_t *m, const char *name, unsigned long *out_value)
{
    if (!m || !name || !*name || !out_value) {
        return 0;
    }
    int ok = 0;
    for (int i = 0; i < m->reg_count; i++) {
        if (strcasecmp(m->regs[i].name, name) == 0) {
            *out_value = m->regs[i].value;
            ok = 1;
            break;
        }
    }
    return ok;
}

void
machine_setRunning(machine_t *m, int running)
{
    if (!m) {
        return;
    }
    int was_running = m->running;
    m->running = running ? 1 : 0;
    if (was_running && !m->running) {
        ui_refreshOnPause();
    }
}

static int
machine_coreFetchRegs(void)
{
    enum { kGeoRegCount = 18 };
    uint32_t values[kGeoRegCount];
    size_t count = 0;
    if (!libretro_host_readRegs(values, kGeoRegCount, &count) || count == 0) {
        return 0;
    }
    static const char *names[kGeoRegCount] = {
        "D0","D1","D2","D3","D4","D5","D6","D7",
        "A0","A1","A2","A3","A4","A5","A6","A7",
        "SR","PC"
    };
    if (count > kGeoRegCount) {
        count = kGeoRegCount;
    }
    machine_clearRegs(&debugger.machine);
    debugger.machine.regs = (machine_reg_t*)alloc_calloc(count, sizeof(machine_reg_t));
    if (!debugger.machine.regs) {
        debugger.machine.reg_count = 0;
        return 0;
    }
    debugger.machine.reg_count = (int)count;
    for (size_t i = 0; i < count; ++i) {
        strncpy(debugger.machine.regs[i].name, names[i], sizeof(debugger.machine.regs[i].name) - 1);
        debugger.machine.regs[i].value = (unsigned long)values[i];
    }
    return 1;
}

static void
machine_fillFrame(machine_frame_t *frame, int level, uint32_t addr, const char *elf)
{
    if (!frame) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
    frame->level = level;
    uint32_t addr24 = addr & 0x00ffffffu;
    frame->addr = addr24;
    snprintf(frame->func, sizeof(frame->func), "0x%06X", (unsigned)addr24);
    if (!elf || !*elf || !debugger.elfValid) {
        return;
    }
    if (!addr2line_start(elf)) {
        return;
    }
    char path[512];
    int line = 0;
    if (!addr2line_resolve((uint64_t)addr24, path, sizeof(path), &line) && addr24 >= 2) {
        addr2line_resolve((uint64_t)(addr24 - 2u), path, sizeof(path), &line);
    }
    if (path[0] && line > 0) {
        const char *full_path = path;
        const char *base = path;
        const char *slash = strrchr(path, '/');
        if (slash && slash[1]) {
            base = slash + 1;
        }
        strncpy(frame->file, base, sizeof(frame->file) - 1);
        frame->file[sizeof(frame->file) - 1] = '\0';
        frame->line = line;
        if (line > 0) {
            const char *src_base = debugger.config.sourceDir;
            FILE *f = NULL;
            if (src_base && *src_base && base && *base) {
                char src_path[PATH_MAX];
                snprintf(src_path, sizeof(src_path), "%s/%s", src_base, base);
                f = fopen(src_path, "r");
            }
            if (!f && full_path && *full_path) {
                f = fopen(full_path, "r");
            }
            if (f) {
                char *linebuf = NULL;
                size_t cap = 0;
                int idx = 0;
                while (getline(&linebuf, &cap, f) != -1) {
                    idx++;
                    if (idx == line) {
                        size_t len = strlen(linebuf);
                        while (len && (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r')) {
                            linebuf[--len] = '\0';
                        }
                        strncpy(frame->source, linebuf, sizeof(frame->source) - 1);
                        frame->source[sizeof(frame->source) - 1] = '\0';
                        break;
                    }
                }
                free(linebuf);
                fclose(f);
            }
        }
    }
}

static int
machine_coreFetchStack(void)
{
    enum { kMaxFrames = 256 };
    uint32_t addrs[kMaxFrames];
    size_t count = 0;
    const char *elf = debugger.config.elfPath;

    machine_clearStack(&debugger.machine);
    if (!libretro_host_debugReadCallstack(addrs, kMaxFrames, &count)) {
        return 0;
    }

    size_t total = count + 1;
    if (total > kMaxFrames) {
        total = kMaxFrames;
    }
    debugger.machine.frames = (machine_frame_t*)alloc_calloc(total, sizeof(machine_frame_t));
    if (!debugger.machine.frames) {
        debugger.machine.frame_count = 0;
        return 0;
    }
    debugger.machine.frame_count = (int)total;

    unsigned long pc = 0;
    (void)machine_findReg(&debugger.machine, "PC", &pc);
    machine_fillFrame(&debugger.machine.frames[0], 0, (uint32_t)pc, elf);

    size_t idx = 1;
    for (size_t i = 0; i < count && idx < total; ++i) {
        size_t src = (count - 1u) - i;
        uint32_t addr = addrs[src];
        machine_fillFrame(&debugger.machine.frames[idx], (int)idx, addr, elf);
        idx++;
    }
    return 1;
}

int
machine_refresh(void)
{
    int regs_ok = machine_coreFetchRegs();
    machine_coreFetchStack();
    return regs_ok;
}
