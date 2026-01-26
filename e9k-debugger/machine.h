/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>


typedef struct machine_reg {
    char name[16];
    unsigned long value;
} machine_reg_t;

typedef struct machine_frame {
    int level;
    char func[128];
    char file[512];
    int line;
    char source[512];
    uint32_t addr;
} machine_frame_t;

#define MACHINE_BREAKPOINT_FILE_LEN 512
#define MACHINE_BREAKPOINT_FUNC_LEN 128
#define MACHINE_BREAKPOINT_COND_LEN 256
#define MACHINE_BREAKPOINT_ADDR_LEN 32
#define MACHINE_BREAKPOINT_TYPE_LEN 32
#define MACHINE_BREAKPOINT_DISP_LEN 16

typedef struct machine_breakpoint {
    int number;
    char enabled;
    char type[MACHINE_BREAKPOINT_TYPE_LEN];
    char disp[MACHINE_BREAKPOINT_DISP_LEN];
    unsigned long long addr;
    char addr_text[MACHINE_BREAKPOINT_ADDR_LEN];
    char func[MACHINE_BREAKPOINT_FUNC_LEN];
    char file[MACHINE_BREAKPOINT_FILE_LEN];
    int line;
    char cond[MACHINE_BREAKPOINT_COND_LEN];
} machine_breakpoint_t;

typedef struct machine {
    machine_reg_t  *regs;
    int             reg_count;
    machine_frame_t *frames;
    int             frame_count;
    char          **reg_names;
    int             reg_name_count;
    machine_breakpoint_t *breakpoints;
    int                  breakpoint_count;
    int                  next_breakpoint_id;
    int             running;
} machine_t;

#define machine_getRunningState(m) (&(m).running)
#define machine_getRunning(m) ((m).running)

void
machine_setRunning(machine_t *m, int running);

void
machine_init(machine_t *m);
void
machine_shutdown(machine_t *m);

int
machine_getRegs(machine_t *m, const machine_reg_t **out, int *count);
int
machine_getStack(machine_t *m, const machine_frame_t **out, int *count);

void
machine_clearBreakpoints(machine_t *m);

int
machine_getBreakpoints(machine_t *m, const machine_breakpoint_t **out, int *count);

machine_breakpoint_t *
machine_addBreakpoint(machine_t *m, uint32_t addr, int enabled);

machine_breakpoint_t *
machine_findBreakpointByAddr(machine_t *m, uint32_t addr);

machine_breakpoint_t *
machine_findBreakpointByNumber(machine_t *m, int number);

int
machine_setBreakpointEnabled(machine_t *m, int number, int enabled, uint32_t *out_addr);

int
machine_removeBreakpointByAddr(machine_t *m, uint32_t addr);

int
machine_findReg(machine_t *m, const char *name, unsigned long *out_value);

int
machine_refresh(void);


