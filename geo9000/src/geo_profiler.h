// Minimal 68K profiler (PC sampling) interface
#ifndef GEO_PROFILER_H
#define GEO_PROFILER_H

#include <stddef.h>
#include <stdint.h>

// Initialize profiler state
void geo_profiler_init(void);

// Instruction hook called before each instruction (wired via Musashi config)
void geo_profiler_instr_hook(unsigned pc);

// Dump results to hard-coded path and reset state
int  geo_profiler_dump(void);
const char *geo_profiler_dump_path(void);

// Enable/disable sampling at runtime
void geo_profiler_set_enabled(int enabled);
int  geo_profiler_get_enabled(void);

// Optional: clear accumulated counts without dumping
void geo_profiler_reset(void);

// Live top lines API (for on-screen display)
typedef struct geo_prof_line_hit_s {
    const char *file;
    uint32_t line;
    uint64_t cycles;
    uint64_t count;
    const char *source; // optional cached source text for this line (may be NULL)
} geo_prof_line_hit_t;

typedef struct geo_profiler_stream_hit_s {
    uint32_t pc;
    uint64_t samples;
    uint64_t cycles;
} geo_profiler_stream_hit_t;

// Fill up to max entries with current top lines by cycles; returns count filled
size_t geo_profiler_top_lines(geo_prof_line_hit_t *out, size_t max);

// Streaming helpers (collect hits since last flush, enable/disable stream tracking)
void geo_profiler_stream_enable(int enable);
size_t geo_profiler_stream_collect(geo_profiler_stream_hit_t *out, size_t max);
size_t geo_profiler_stream_pending(void);
void geo_profiler_capture_stream_hits(const geo_profiler_stream_hit_t *hits, size_t count);
size_t geo_profiler_stream_format_json(char *out, size_t cap);


#endif // GEO_PROFILER_H
