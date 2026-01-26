#include "geo_hooks.h"

#include "geo_checkpoint.h"
#include "geo_profiler.h"
#include "geo_debugger.h"

void geo_instr_hook_dispatch(unsigned pc) {
    // Call profiler first so samples reflect pre-break behavior consistently
    geo_profiler_instr_hook(pc);
    // Then debugger to detect breakpoints
    geo_debugger_instr_hook(pc);
}
