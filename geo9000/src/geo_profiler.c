/*
 Simple 68K profiler: samples PC every N instructions and aggregates per source line.
 Outputs JSON on demand and a live on-screen overlay.
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include "geo_notify.h"
#include <errno.h>

#include "geo_profiler.h"
#include "geo_profiler_elf.h"

// Forward declarations for file-scope constants defined later
static const char *k_prof_elf_path;
static const char *k_prof_out_path;
static const char *k_prof_json_path;

// Default: sample every instruction; can downsample via env in init
#define GEO_PROF_SAMPLING_SHIFT 0
static uint32_t g_sampling_mask = 0u; // 0 => every instruction

// Profiler version (internal)

// Forward declarations and types for line table aggregation
typedef struct LAgg { char *key; uint64_t cycles; } LAgg;
static int cmp_lagg_desc(const void *a, const void *b) {
    const LAgg *pa = (const LAgg*)a, *pb = (const LAgg*)b;
    if (pa->cycles < pb->cycles) return 1;
    if (pa->cycles > pb->cycles) return -1;
    return strcmp(pa->key, pb->key);
}

// Global line table for live aggregation
static LineTable g_line_table = {0};
static const LineRow *g_line_rows = NULL;
static size_t g_line_nrows = 0;

// Live per-line aggregation (hash table)
typedef struct LineAgg { uint32_t key; uint64_t cycles; uint64_t samples; } LineAgg;
#define LINE_AGG_SZ 8192
static LineAgg g_line_agg[LINE_AGG_SZ];
// Forward declaration
static void build_global_line_table(void);

// Cached source line text per file:line for overlay
typedef struct SrcCacheEntry { uint32_t key; char *text; } SrcCacheEntry;
#define SRC_CACHE_SZ 1024u
static SrcCacheEntry g_src_cache[SRC_CACHE_SZ];

static inline uint32_t src_cache_hash(uint32_t key) {
    uint32_t x = key * 2654435761u; x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x & (SRC_CACHE_SZ - 1u);
}

static void src_cache_clear(void) {
    for (size_t i=0;i<SRC_CACHE_SZ;i++) { if (g_src_cache[i].key) { if (g_src_cache[i].text) free(g_src_cache[i].text); g_src_cache[i].key = 0; g_src_cache[i].text = NULL; } }
}

// Resolve a file path and read a single line; returns malloc'd truncated string or NULL
static char *read_source_line(uint32_t file_idx, uint32_t line) {
    if (!g_line_table.nfiles || file_idx == 0 || file_idx > g_line_table.nfiles) return NULL;
    const char *fname = g_line_table.files[file_idx - 1];
    if (!fname || !*fname) return NULL;
    const char *dir = NULL;
    if (g_line_table.file_dir && file_idx-1 < g_line_table.nfiles) {
        uint32_t di = g_line_table.file_dir[file_idx - 1];
        if (di > 0 && g_line_table.dirs && di <= g_line_table.ndirs) dir = g_line_table.dirs[di - 1];
    }

    // Derive ELF dir and parent once
    static int s_elf_dirs_inited = 0; static char s_elf_dir[1024]; static char s_elf_parent[1024];
    if (!s_elf_dirs_inited) {
        memset(s_elf_dir, 0, sizeof(s_elf_dir)); memset(s_elf_parent, 0, sizeof(s_elf_parent));
        if (k_prof_elf_path && k_prof_elf_path[0]) {
            strncpy(s_elf_dir, k_prof_elf_path, sizeof(s_elf_dir)-1);
            char *slash = strrchr(s_elf_dir, '/'); if (slash) *slash = '\0';
            strncpy(s_elf_parent, s_elf_dir, sizeof(s_elf_parent)-1);
            slash = strrchr(s_elf_parent, '/'); if (slash) *slash = '\0';
        }
        s_elf_dirs_inited = 1;
    }

    char fullpath[1024] = {0};
    char buf1[1024] = {0}, buf2[1024] = {0}, buf3[1024] = {0};
    const char *cand[4] = {NULL,NULL,NULL,NULL}; size_t ncand = 0;
    if (dir && dir[0] == '/') {
        snprintf(buf1, sizeof(buf1), "%s/%s", dir, fname); cand[ncand++] = buf1;
    } else if (dir && dir[0]) {
        snprintf(buf1, sizeof(buf1), "%s/%s/%s", s_elf_parent, dir, fname); cand[ncand++] = buf1;
        snprintf(buf2, sizeof(buf2), "%s/%s/%s", s_elf_dir, dir, fname); cand[ncand++] = buf2;
    }
    if (s_elf_parent[0]) { snprintf(buf3, sizeof(buf3), "%s/%s", s_elf_parent, fname); cand[ncand++] = buf3; }
    cand[ncand++] = fname;

    FILE *sf = NULL;
    for (size_t i=0; i<ncand && !sf; ++i) { if (cand[i] && cand[i][0]) { snprintf(fullpath, sizeof(fullpath), "%s", cand[i]); sf = fopen(fullpath, "rb"); } }
    if (!sf) return NULL;

    // Read specific line
    char tmp[512]; tmp[0] = '\0';
    unsigned cur = 1; while (fgets(tmp, sizeof(tmp), sf)) { if (cur == line) break; cur++; }
    fclose(sf);
    if (cur != line) return NULL;

    // Trim newline and leading spaces/tabs
    size_t len = strlen(tmp); while (len && (tmp[len-1]=='\n' || tmp[len-1]=='\r')) tmp[--len] = '\0';
    char *p = tmp; while (*p == ' ' || *p == '\t') p++;
    // Collapse tabs to spaces for overlay
    char norm[256]; size_t nl = 0; for (; *p && nl < sizeof(norm)-1; ++p) { norm[nl++] = (*p=='\t') ? ' ' : *p; }
    norm[nl] = '\0';
    // Truncate to a reasonable overlay length
    const size_t kMax = 120; // before font clipping; overlay box will cut further
    if (nl > kMax) { nl = kMax; }
    char *out = (char*)malloc(nl + 1);
    if (!out) return NULL;
    memcpy(out, norm, nl); out[nl] = '\0';
    return out;
}

static const char *get_source_cached(uint32_t file_idx, uint32_t line) {
    uint32_t key = ((file_idx & 0xFFFFu) << 16) | (line & 0xFFFFu);
    uint32_t idx = src_cache_hash(key);
    for (size_t p=0; p<SRC_CACHE_SZ; ++p) {
        SrcCacheEntry *e = &g_src_cache[idx];
        if (e->key == 0) break;
        if (e->key == key) return e->text; // may be NULL to indicate miss
        idx = (idx + 1u) & (SRC_CACHE_SZ - 1u);
    }
    // Miss: load and insert (open addressing; allow overwrite if table gets full)
    char *txt = read_source_line(file_idx, line);
    idx = src_cache_hash(key);
    for (size_t p=0; p<SRC_CACHE_SZ; ++p) {
        SrcCacheEntry *e = &g_src_cache[idx];
        if (e->key == 0 || e->key == key) { if (e->text) free(e->text); e->key = key; e->text = txt; return txt; }
        idx = (idx + 1u) & (SRC_CACHE_SZ - 1u);
    }
    // Table full: drop caching politely
    if (txt) free(txt);
    return NULL;
}

static inline uint32_t line_key(uint32_t file_idx, uint32_t line) {
    return ((file_idx & 0xFFFF) << 16) | (line & 0xFFFF);
}

static void line_agg_add(uint32_t file_idx, uint32_t line, uint64_t cy, uint64_t samp) {
    uint32_t key = line_key(file_idx, line);
    uint32_t idx = (key * 2654435761u) & (LINE_AGG_SZ - 1);
    for (size_t i=0;i<LINE_AGG_SZ;i++) {
        LineAgg *e = &g_line_agg[idx];
        if (e->key == 0 || e->key == key) { e->key = key; e->cycles += cy; e->samples += samp; return; }
        idx = (idx + 1) & (LINE_AGG_SZ - 1);
    }
}

// Temp struct and comparator for sorting top lines
typedef struct ProfLineTmp { uint32_t key; uint64_t cycles; uint64_t samples; } ProfLineTmp;
static int cmp_profline_tmp_desc(const void *a, const void *b) {
    const ProfLineTmp *pa = (const ProfLineTmp*)a;
    const ProfLineTmp *pb = (const ProfLineTmp*)b;
    if (pa->cycles < pb->cycles) return 1;
    if (pa->cycles > pb->cycles) return -1;
    return 0;
}

// Build line table once for live updates (ELF helpers in geo_profiler_elf.c)

// Paths are provided via environment variables at init; no defaults
static const char *k_prof_elf_path = NULL;
static const char *k_prof_out_path = NULL;
static const char *k_prof_json_path = NULL;

// Build line table once for live updates
static void build_global_line_table(void) {
    if (g_line_rows) return;
    if (!k_prof_elf_path || !k_prof_elf_path[0]) return;
    LineTable tmplt = {0};
    if (geo_elf_load_line_table(k_prof_elf_path, &tmplt)) {
        g_line_table = tmplt; g_line_rows = g_line_table.rows; g_line_nrows = g_line_table.nrows;
    }
}

// Very simple open-addressing hash table mapping 24-bit PC -> 64-bit count
typedef struct ProfEntry {
    uint32_t key;     // 24-bit address, 0 means empty; store key+1 to keep 0 usable
    uint64_t samples; // sample count (PC sampler)
    uint64_t cycles;  // total cycles attributed to this PC
} ProfEntry;

#define PROF_TABLE_BITS 17u
#define PROF_TABLE_SIZE (1u << PROF_TABLE_BITS) // 131072 entries (~2MB)
#define PROF_TABLE_MASK (PROF_TABLE_SIZE - 1u)

static ProfEntry *g_table = NULL;
static uint32_t g_used = 0;
static uint32_t g_sample_accum = 0;
static int g_enabled = 0;
static int g_ever_enabled = 0; // tracks if profiler was ever enabled during this session

// Stream verification state (only present when VERIFICATION enabled)
#ifdef GEO_PROF_STREAM_VERIFY
static ProfEntry *g_stream_capture = NULL;
static ProfEntry *g_stream_capture_prev = NULL;
static uint32_t g_stream_capture_used = 0;
#endif

static uint32_t *g_entry_epochs = NULL;
static uint32_t *g_stream_delta_idxs = NULL;
static size_t g_stream_delta_count = 0;
static uint32_t g_stream_epoch = 1;
static int g_stream_enabled = 0;

static void stream_epoch_advance(void) {
    g_stream_delta_count = 0;
    g_stream_epoch++;
    if (g_stream_epoch == 0) {
        if (g_entry_epochs) memset(g_entry_epochs, 0, PROF_TABLE_SIZE * sizeof(uint32_t));
        g_stream_epoch = 1;
    }
}

static void stream_mark_idx(uint32_t idx) {
    if (!g_stream_enabled || !g_stream_delta_idxs || !g_entry_epochs || idx >= PROF_TABLE_SIZE) return;
    if (g_entry_epochs[idx] == g_stream_epoch) return;
    g_entry_epochs[idx] = g_stream_epoch;
    if (g_stream_delta_count < PROF_TABLE_SIZE) {
        g_stream_delta_idxs[g_stream_delta_count++] = idx;
    }
}

static inline uint32_t prof_hash(uint32_t addr) {
    // mix 24-bit addr
    uint32_t x = addr;
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x & PROF_TABLE_MASK;
}

#ifdef GEO_PROF_STREAM_VERIFY
static ProfEntry *stream_capture_lookup(ProfEntry *table, uint32_t addr, int create, uint32_t *used) {
    if (!table) return NULL;
    uint32_t idx = prof_hash(addr);
    for (uint32_t nprobe = 0; nprobe < PROF_TABLE_SIZE; ++nprobe) {
        ProfEntry *e = &table[idx];
        if (e->key == addr + 1u) {
            return e;
        }
        if (create && e->key == 0) {
            e->key = addr + 1u;
            e->samples = 0;
            e->cycles = 0;
            if (used) ++*used;
            return e;
        }
        idx = (idx + 1u) & PROF_TABLE_MASK;
    }
    return NULL;
}

static void stream_capture_reset(void) {
    if (g_stream_capture) memset(g_stream_capture, 0, PROF_TABLE_SIZE * sizeof(ProfEntry));
    if (g_stream_capture_prev) memset(g_stream_capture_prev, 0, PROF_TABLE_SIZE * sizeof(ProfEntry));
    g_stream_capture_used = 0;
}

static void stream_capture_free(void) {
    free(g_stream_capture);
    g_stream_capture = NULL;
    free(g_stream_capture_prev);
    g_stream_capture_prev = NULL;
    g_stream_capture_used = 0;
}

static void stream_capture_record_hits(const geo_profiler_stream_hit_t *hits, size_t count) {
    if (!hits || count == 0) return;
    if (!g_stream_capture || !g_stream_capture_prev) return;
    for (size_t i = 0; i < count; ++i) {
        uint32_t addr = hits[i].pc & 0x00ffffffu;
        uint64_t new_samples = hits[i].samples;
        uint64_t new_cycles = hits[i].cycles;
        ProfEntry *prev = stream_capture_lookup(g_stream_capture_prev, addr, 1, NULL);
        uint64_t last_samples = prev ? prev->samples : 0;
        uint64_t last_cycles = prev ? prev->cycles : 0;
        uint64_t delta_samples = (new_samples >= last_samples) ? new_samples - last_samples : new_samples;
        uint64_t delta_cycles = (new_cycles >= last_cycles) ? new_cycles - last_cycles : new_cycles;
        ProfEntry *agg = stream_capture_lookup(g_stream_capture, addr, 1, &g_stream_capture_used);
        if (agg) {
            agg->samples += delta_samples;
            agg->cycles += delta_cycles;
        }
        if (prev) {
            prev->samples = new_samples;
            prev->cycles = new_cycles;
        }
    }
}

void geo_profiler_capture_stream_hits(const geo_profiler_stream_hit_t *hits, size_t count) {
    stream_capture_record_hits(hits, count);
}
#else
static void stream_capture_reset(void) {}
static void stream_capture_free(void) {}
void geo_profiler_capture_stream_hits(const geo_profiler_stream_hit_t *hits, size_t count) {
    (void)hits;
    (void)count;
}
#endif

void geo_profiler_init(void) {
    if (g_table)
        return;
    // Allow environment overrides for paths
    const char *e_elf = getenv("GEO_PROF_ELF");
    if (e_elf && e_elf[0]) k_prof_elf_path = e_elf;
    const char *e_txt = getenv("GEO_PROF_TXT");
    if (e_txt && e_txt[0]) k_prof_out_path = e_txt;
    const char *e_json = getenv("GEO_PROF_JSON");
    if (e_json && e_json[0]) k_prof_json_path = e_json;
    // Sampling controls: GEO_PROF_SAMPLE_EVERY=1 forces per-instruction sampling.
    // Or override shift via GEO_PROF_SAMPLING_SHIFT=0..n (0 means every instruction).
    {
        const char *e_every = getenv("GEO_PROF_SAMPLE_EVERY");
        const char *e_shift = getenv("GEO_PROF_SAMPLING_SHIFT");
        if (e_every && e_every[0] && e_every[0] != '0') {
            g_sampling_mask = 0u;
        } else if (e_shift && e_shift[0]) {
            int sh = atoi(e_shift);
            if (sh <= 0) g_sampling_mask = 0u;
            else if (sh >= 16) g_sampling_mask = 0xffffffffu; // guard against overflow
            else g_sampling_mask = (uint32_t)((1u << sh) - 1u);
        }
    }
    g_table = (ProfEntry*)calloc(PROF_TABLE_SIZE, sizeof(ProfEntry));
    g_used = 0;
    g_sample_accum = 0;
    g_enabled = 0; // start disabled; enable via toggle
#ifdef GEO_PROF_STREAM_VERIFY
    g_stream_capture = (ProfEntry*)calloc(PROF_TABLE_SIZE, sizeof(ProfEntry));
    g_stream_capture_prev = (ProfEntry*)calloc(PROF_TABLE_SIZE, sizeof(ProfEntry));
    g_stream_capture_used = 0;
#endif
    g_entry_epochs = (uint32_t*)calloc(PROF_TABLE_SIZE, sizeof(uint32_t));
    g_stream_delta_idxs = (uint32_t*)malloc(PROF_TABLE_SIZE * sizeof(uint32_t));
    g_stream_delta_count = 0;
    g_stream_epoch = 1;
    g_stream_enabled = 0;
    // Build line table now for live overlay aggregation
    build_global_line_table();
    memset(g_src_cache, 0, sizeof(g_src_cache));
}

void geo_profiler_instr_hook(unsigned pc) {
    if (!g_enabled)
        return;

    // Sample every (mask+1) instructions; mask=0 means every instruction
    if ((g_sample_accum++ & g_sampling_mask) != 0)
        return;

    // 68000 uses 24-bit address space
    uint32_t a = pc & 0x00ffffffu;

    uint32_t idx = prof_hash(a);
        for (uint32_t nprobe = 0; nprobe < PROF_TABLE_SIZE; ++nprobe) {
            ProfEntry *e = &g_table[idx];
            if (e->key == 0) {
                // insert
                e->key = a + 1u; // avoid zero reserved value
                e->samples = 1u;
                e->cycles = 0u;
                ++g_used;
                stream_mark_idx(idx);
                return;
            }
            if ((e->key - 1u) == a) {
                ++e->samples;
                stream_mark_idx(idx);
                return;
            }
            idx = (idx + 1u) & PROF_TABLE_MASK;
        }
    // Table full; drop sample silently
}

// Called from the CPU core after each instruction with the previous PC and
// the exact cycles used by that instruction (including dynamic adds).
void geo_profiler_account(unsigned pc, unsigned cycles) {
    if (!g_table)
        return;
    if (!g_enabled)
        return;

    uint32_t a = pc & 0x00ffffffu;
    uint32_t idx = prof_hash(a);
        for (uint32_t nprobe = 0; nprobe < PROF_TABLE_SIZE; ++nprobe) {
            ProfEntry *e = &g_table[idx];
            if (e->key == 0) {
                e->key = a + 1u;
                e->samples = 0u;
                e->cycles = (uint64_t)cycles;
                ++g_used;
                stream_mark_idx(idx);
                return;
            }
            if ((e->key - 1u) == a) {
                e->cycles += (uint64_t)cycles;
                stream_mark_idx(idx);
                return;
            }
            idx = (idx + 1u) & PROF_TABLE_MASK;
        }
    // Live line aggregation by cycles
    if (g_line_rows && g_line_nrows) {
        const LineRow *r = geo_line_find_row_addr(g_line_rows, g_line_nrows, a);
        if (r) line_agg_add(r->file, r->line, cycles, 0);
    }
}

void geo_profiler_set_enabled(int enabled) {
    g_enabled = enabled ? 1 : 0;
    if (g_enabled) g_ever_enabled = 1;
}

int geo_profiler_get_enabled(void) {
    return g_enabled;
}

// UI/render state lives in geo_profiler_render.c

void geo_profiler_reset(void) {
    if (!g_table) return;
    memset(g_table, 0, PROF_TABLE_SIZE * sizeof(ProfEntry));
    g_used = 0;
    g_sample_accum = 0;
    memset(g_line_agg, 0, sizeof(g_line_agg));
    src_cache_clear();
    if (g_entry_epochs) memset(g_entry_epochs, 0, PROF_TABLE_SIZE * sizeof(uint32_t));
    g_stream_delta_count = 0;
    g_stream_epoch = 1;
    g_stream_enabled = 0;
    stream_capture_reset();
}

size_t geo_profiler_top_lines(geo_prof_line_hit_t *out, size_t max) {
    if (!out || max == 0) return 0;
    // Ensure line table is built
    build_global_line_table();
    // Rebuild aggregation from current PC table for immediacy
    memset(g_line_agg, 0, sizeof(g_line_agg));
    if (g_line_rows && g_line_nrows) {
        for (size_t i = 0; i < PROF_TABLE_SIZE; ++i) {
            if (g_table && g_table[i].key) {
                uint32_t addr = (g_table[i].key - 1u) & 0x00ffffffu;
                const LineRow *r = geo_line_find_row_addr(g_line_rows, g_line_nrows, addr);
                if (r && (g_table[i].cycles)) line_agg_add(r->file, r->line, g_table[i].cycles, g_table[i].samples);
            }
        }
    }
    // Collect non-empty entries
    size_t ntemp = 0;
    ProfLineTmp *tmp = (ProfLineTmp*)malloc(LINE_AGG_SZ * sizeof(ProfLineTmp));
    if (!tmp) return 0;
    for (size_t i=0;i<LINE_AGG_SZ;i++) if (g_line_agg[i].key && g_line_agg[i].cycles) { tmp[ntemp].key = g_line_agg[i].key; tmp[ntemp].cycles = g_line_agg[i].cycles; tmp[ntemp].samples = g_line_agg[i].samples; ntemp++; }
    // Sort by cycles desc
    qsort(tmp, ntemp, sizeof(ProfLineTmp), cmp_profline_tmp_desc);
    // Fill out with top N
    size_t filled = 0;
    for (size_t i=0; i<ntemp && filled<max; i++) {
        uint32_t key = tmp[i].key;
        uint32_t fidx = (key >> 16) & 0xFFFF;
        uint32_t lno = key & 0xFFFF;
        const char *fname = (g_line_table.nfiles && fidx>0 && fidx<=g_line_table.nfiles) ? g_line_table.files[fidx-1] : "?";
        out[filled].file = fname;
        out[filled].line = lno;
        out[filled].cycles = tmp[i].cycles;
        out[filled].count = tmp[i].samples;
        out[filled].source = get_source_cached(fidx, lno);
        filled++;
    }
    free(tmp);
    return filled;
}

void geo_profiler_stream_enable(int enable) {
    g_stream_enabled = enable ? 1 : 0;
    if (!g_stream_enabled) {
        stream_epoch_advance();
    }
}

size_t geo_profiler_stream_collect(geo_profiler_stream_hit_t *out, size_t max) {
    if (!g_table || !g_stream_delta_idxs) {
        stream_epoch_advance();
        return 0;
    }
    size_t avail = g_stream_delta_count;
    size_t take = max == 0 ? avail : (max < avail ? max : avail);
    if (out && take > 0) {
        for (size_t i = 0; i < take; ++i) {
            uint32_t idx = g_stream_delta_idxs[i];
            const ProfEntry *e = &g_table[idx];
            out[i].pc = e->key ? (e->key - 1u) : 0u;
            out[i].samples = e->samples;
            out[i].cycles = e->cycles;
        }
    }
    stream_epoch_advance();
    return take;
}

size_t geo_profiler_stream_pending(void) {
    return g_stream_delta_count;
}

static int append_printf(char *buf, size_t bufsize, size_t *pos, const char *fmt, ...) {
    if (!buf || !bufsize || !pos) return 0;
    if (*pos >= bufsize) return 0;
    va_list ap; va_start(ap, fmt);
    int res = vsnprintf(buf + *pos, bufsize - *pos, fmt, ap);
    va_end(ap);
    if (res < 0 || (size_t)res >= bufsize - *pos) {
        *pos = bufsize - 1;
        buf[*pos] = '\0';
        return 0;
    }
    *pos += (size_t)res;
    return 1;
}

size_t geo_profiler_stream_format_json(char *out, size_t cap) {
    if (!out || cap == 0) {
        return 0;
    }
    if (!g_stream_enabled) {
        return 0;
    }
    size_t pending = geo_profiler_stream_pending();
    if (pending == 0) {
        return 0;
    }
    geo_profiler_stream_hit_t *hits = (geo_profiler_stream_hit_t*)calloc(pending, sizeof(geo_profiler_stream_hit_t));
    if (!hits) {
        return 0;
    }
    size_t count = geo_profiler_stream_collect(hits, 0);
    if (count == 0) {
        free(hits);
        return 0;
    }
    geo_profiler_capture_stream_hits(hits, count);
    size_t pos = 0;
    const char *enabled = geo_profiler_get_enabled() ? "enabled" : "disabled";
    if (!append_printf(out, cap, &pos, "{\"stream\":\"profiler\",\"enabled\":\"%s\",\"hits\":[", enabled)) {
        free(hits);
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            if (pos + 1 >= cap) {
                out[cap - 1] = '\0';
                free(hits);
                return 0;
            }
            out[pos++] = ',';
        }
        if (!append_printf(out, cap, &pos,
                           "{\"pc\":\"0x%06X\",\"samples\":%llu,\"cycles\":%llu}",
                           hits[i].pc & 0x00ffffffu,
                           (unsigned long long)hits[i].samples,
                           (unsigned long long)hits[i].cycles)) {
            free(hits);
            return 0;
        }
    }
    if (pos + 2 >= cap) {
        out[cap - 1] = '\0';
        free(hits);
        return 0;
    }
    out[pos++] = ']';
    out[pos++] = '}';
    out[pos] = '\0';
    free(hits);
    return pos;
}

// -------------------- UI handling --------------------


// Rendering moved to geo_profiler_render.c

typedef struct OutPair {
    uint32_t addr;
    uint64_t samples;
    uint64_t cycles;
} OutPair;

static int cmp_desc_count(const void *a, const void *b) {
    const OutPair *pa = (const OutPair*)a;
    const OutPair *pb = (const OutPair*)b;
    if (pa->samples < pb->samples) return 1;
    if (pa->samples > pb->samples) return -1;
    if (pa->addr < pb->addr) return -1;
    if (pa->addr > pb->addr) return 1;
    return 0;
}


// Comparator for NameAgg cycles descending
typedef struct { const char *name; uint64_t cycles; } NameAgg;
static int cmp_nameagg_desc(const void *a, const void *b) {
    const NameAgg *pa = (const NameAgg*)a;
    const NameAgg *pb = (const NameAgg*)b;
    if (pa->cycles < pb->cycles) return 1;
    if (pa->cycles > pb->cycles) return -1;
    return strcmp(pa->name, pb->name);
}

// JSON string escaping helper
static void json_write_escaped(FILE *fj, const char *s) {
    if (!s) { fputs("\"\"", fj); return; }
    fputc('"', fj);
    const unsigned char *p = (const unsigned char*)s;
    while (*p) {
        unsigned char c = *p++;
        switch (c) {
            case '"': case '\\': fputc('\\', fj); fputc(c, fj); break;
            case '\n': fputs("\\n", fj); break;
            case '\r': fputs("\\r", fj); break;
            case '\t': fputs("\\t", fj); break;
            default:
                if (c < 0x20) fprintf(fj, "\\u%04x", c);
                else fputc(c, fj);
        }
    }
    fputc('"', fj);
}


// ------------------ ELF symbol parsing (minimal) ------------------

typedef struct FuncSym {
    uint32_t start; // 24-bit effective address
    uint32_t end;   // exclusive end (start+size or next start)
    char *name;     // heap-allocated
    uint64_t tally; // aggregated samples
} FuncSym;

static int cmp_func_start(const void *a, const void *b) {
    const FuncSym *fa = (const FuncSym*)a;
    const FuncSym *fb = (const FuncSym*)b;
    if (fa->start < fb->start) return -1;
    if (fa->start > fb->start) return 1;
    return 0;
}

static int cmp_func_tally_desc(const void *a, const void *b) {
    const FuncSym *fa = (const FuncSym*)a;
    const FuncSym *fb = (const FuncSym*)b;
    if (fa->tally < fb->tally) return 1;
    if (fa->tally > fb->tally) return -1;
    if (fa->start < fb->start) return -1;
    if (fa->start > fb->start) return 1;
    return 0;
}

// Read helpers for endianness-safe parsing
static inline uint16_t r_u16(const uint8_t *p, int be) {
    return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t r_u32(const uint8_t *p, int be) {
    if (be)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    else
        return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

// Minimal ELF loader: extracts STT_FUNC symbols from SHT_SYMTAB
static FuncSym *load_func_symbols(const char *path, size_t *out_count) {
    *out_count = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) { fclose(f); return NULL; }

    uint8_t *buf = (uint8_t*)malloc((size_t)fsz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) { free(buf); fclose(f); return NULL; }
    fclose(f);

    if (fsz < 52) { free(buf); return NULL; }
    // Check ELF magic
    if (!(buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F')) {
        free(buf); return NULL;
    }
    uint8_t ei_class = buf[4]; // 1=32, 2=64
    uint8_t ei_data  = buf[5]; // 1=LE, 2=BE
    int be = (ei_data == 2);

    if (ei_class != 1) { // only ELF32 supported for now
        free(buf); return NULL;
    }

    // ELF32 header offsets
    const uint8_t *eh = buf;
    uint32_t e_shoff = r_u32(eh + 32, be);
    uint16_t e_shentsize = r_u16(eh + 46, be);
    uint16_t e_shnum = r_u16(eh + 48, be);
    uint16_t e_shstrndx = r_u16(eh + 50, be);

    if (e_shoff == 0 || e_shentsize == 0 || e_shnum == 0) { free(buf); return NULL; }
    if ((uint64_t)e_shoff + (uint64_t)e_shentsize * e_shnum > (uint64_t)fsz) { free(buf); return NULL; }

    const uint8_t *shdr = buf + e_shoff;
    // Section header fields layout for ELF32:
    // sh_name(0) sh_type(4) sh_flags(8) sh_addr(12) sh_offset(16) sh_size(20)
    // sh_link(24) sh_info(28) sh_addralign(32) sh_entsize(36)

    // Find SHT_SYMTAB
    int symtab_idx = -1;
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const uint8_t *sh = shdr + i * e_shentsize;
        uint32_t sh_type = r_u32(sh + 4, be);
        if (sh_type == 2) { // SHT_SYMTAB
            symtab_idx = (int)i;
            break;
        }
    }
    if (symtab_idx < 0) { free(buf); return NULL; }

    const uint8_t *sh_sym = shdr + symtab_idx * e_shentsize;
    uint32_t sym_off = r_u32(sh_sym + 16, be);
    uint32_t sym_size = r_u32(sh_sym + 20, be);
    uint32_t sym_entsz = r_u32(sh_sym + 36, be);
    uint32_t strtab_idx = r_u32(sh_sym + 24, be);

    if (sym_off == 0 || sym_size == 0 || sym_entsz == 0) { free(buf); return NULL; }
    if ((uint64_t)sym_off + (uint64_t)sym_size > (uint64_t)fsz) { free(buf); return NULL; }
    if (strtab_idx >= e_shnum) { free(buf); return NULL; }

    const uint8_t *sh_str = shdr + strtab_idx * e_shentsize;
    uint32_t str_off = r_u32(sh_str + 16, be);
    uint32_t str_size = r_u32(sh_str + 20, be);
    if (str_off == 0 || str_size == 0) { free(buf); return NULL; }
    if ((uint64_t)str_off + (uint64_t)str_size > (uint64_t)fsz) { free(buf); return NULL; }

    const uint8_t *symtab = buf + sym_off;
    const uint8_t *strtab = buf + str_off;
    uint32_t nsym = sym_size / sym_entsz;

    FuncSym *out = NULL;
    size_t out_cap = 0, out_cnt = 0;

    for (uint32_t i = 0; i < nsym; ++i) {
        const uint8_t *se = symtab + i * sym_entsz;
        uint32_t st_name  = r_u32(se + 0, be);
        uint32_t st_value = r_u32(se + 4, be);
        uint32_t st_size  = r_u32(se + 8, be);
        uint8_t  st_info  = se[12];
        uint16_t st_shndx = r_u16(se + 14, be);

        uint8_t st_type = st_info & 0x0f; // lower nibble
        if (st_shndx == 0) continue; // undefined
        if (st_type != 2 /*STT_FUNC*/ && !(st_type == 0 && st_size != 0)) continue;
        if (st_name >= str_size) continue;

        const char *nm = (const char*)(strtab + st_name);
        if (!nm || !*nm) continue;

        if (out_cnt == out_cap) {
            size_t new_cap = out_cap ? (out_cap * 2) : 128;
            FuncSym *new_out = (FuncSym*)realloc(out, new_cap * sizeof(FuncSym));
            if (!new_out) { // cleanup and abort
                for (size_t k = 0; k < out_cnt; ++k) free(out[k].name);
                free(out);
                free(buf);
                return NULL;
            }
            out = new_out;
            out_cap = new_cap;
        }

        out[out_cnt].start = st_value & 0x00ffffffu; // 24-bit map
        out[out_cnt].end   = (st_size ? (out[out_cnt].start + (st_size & 0x00ffffffu)) : 0);
        out[out_cnt].tally = 0;
        size_t nlen = strlen(nm) + 1;
        out[out_cnt].name = (char*)malloc(nlen);
        if (!out[out_cnt].name) {
            for (size_t k = 0; k < out_cnt; ++k) free(out[k].name);
            free(out);
            free(buf);
            return NULL;
        }
        memcpy(out[out_cnt].name, nm, nlen);
        ++out_cnt;
    }

    // We no longer need the ELF buffer
    free(buf);

    if (out_cnt == 0) { free(out); return NULL; }

    // Sort by start and infer missing sizes using next start
    qsort(out, out_cnt, sizeof(FuncSym), cmp_func_start);
    for (size_t i = 0; i < out_cnt; ++i) {
        if (out[i].end == 0) {
            uint32_t next_start = (i + 1 < out_cnt) ? out[i + 1].start : 0x01000000u;
            if (next_start > out[i].start)
                out[i].end = next_start;
            else
                out[i].end = out[i].start + 1; // minimal non-zero
        }
    }

    *out_count = out_cnt;
    return out;
}

static const FuncSym *find_func(const FuncSym *arr, size_t n, uint32_t addr) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        if (arr[mid].start <= addr) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return NULL;
    const FuncSym *cand = &arr[lo - 1];
    if (addr >= cand->start && addr < cand->end) return cand;
    return NULL;
}

// ------------------ DWARF v5 minimal parsing (inline ranges) ------------------

// DWARF constants (subset)
#define DW_TAG_compile_unit        0x11
#define DW_TAG_subprogram          0x2e
#define DW_TAG_inlined_subroutine  0x1d

#define DW_CHILDREN_no             0x00
#define DW_CHILDREN_yes            0x01

#define DW_AT_name                 0x03
#define DW_AT_linkage_name         0x6e
#define DW_AT_str_offsets_base     0x72
#define DW_AT_low_pc               0x11
#define DW_AT_high_pc              0x12
#define DW_AT_abstract_origin      0x31
#define DW_AT_ranges               0x55
#define DW_AT_rnglists_base        0x74
#define DW_AT_call_file            0x58
#define DW_AT_call_line            0x59

#define DW_FORM_addr               0x01
#define DW_FORM_strp               0x0e
#define DW_FORM_ref4               0x13
#define DW_FORM_ref_udata          0x15
#define DW_FORM_sec_offset         0x17
#define DW_FORM_exprloc            0x18
#define DW_FORM_flag_present       0x19
#define DW_FORM_strx               0x1a
#define DW_FORM_strx1              0x25
#define DW_FORM_strx2              0x26
#define DW_FORM_strx4              0x27
#define DW_FORM_strx8              0x28
#define DW_FORM_addrx              0x1b
#define DW_FORM_rnglistx           0x1d
#define DW_FORM_data1              0x0b
#define DW_FORM_data2              0x05
#define DW_FORM_data4              0x06
#define DW_FORM_data8              0x07
#define DW_FORM_sdata              0x0d
#define DW_FORM_udata              0x0f
#define DW_FORM_line_strp          0x1f
#define DW_FORM_ref_addr           0x10
#define DW_FORM_indirect           0x16

#define DW_UT_compile              0x01

// rnglists encodings (subset)
#define DW_RLE_end_of_list         0x00
#define DW_RLE_base_addressx       0x01
#define DW_RLE_startx_endx         0x02
#define DW_RLE_startx_length       0x03
#define DW_RLE_offset_pair         0x04
#define DW_RLE_base_address        0x05
#define DW_RLE_start_end           0x06
#define DW_RLE_start_length        0x07

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    int be; // 0 = little, 1 = big
} Buf;

static inline int buf_ok(Buf *b, size_t n) { return (size_t)(b->end - b->p) >= n; }
static inline uint8_t rd_u8(Buf *b){ return buf_ok(b,1)? *b->p++ : 0; }
static inline uint16_t rd_u16b(Buf *b){ if(!buf_ok(b,2)) return 0; uint16_t v; if (b->be) v=(uint16_t)((b->p[0]<<8)|b->p[1]); else v=(uint16_t)(b->p[0]|(b->p[1]<<8)); b->p+=2; return v; }
static inline uint32_t rd_u32b(Buf *b){ if(!buf_ok(b,4)) return 0; uint32_t v; if (b->be) v=((uint32_t)b->p[0]<<24)|((uint32_t)b->p[1]<<16)|((uint32_t)b->p[2]<<8)|b->p[3]; else v=b->p[0]|(b->p[1]<<8)|(b->p[2]<<16)|(b->p[3]<<24); b->p+=4; return v; }
static inline uint32_t rd_addr(Buf *b, uint8_t addr_size){ if(!buf_ok(b,addr_size)) return 0; uint32_t a=0; if (b->be){ for (int i=0;i<addr_size;i++){ a = (a<<8) | b->p[i]; } } else { for (int i=0;i<addr_size;i++){ a |= (uint32_t)b->p[i] << (i*8); } } b->p += addr_size; return a; }

static uint64_t rd_uleb(Buf *b){ uint64_t v=0; int shift=0; for(;;){ if(!buf_ok(b,1)) return v; uint8_t byte=*b->p++; v|=(uint64_t)(byte & 0x7f) << shift; if(!(byte & 0x80)) break; shift += 7; } return v; }
static int64_t rd_sleb(Buf *b){ int64_t v=0; int shift=0; uint8_t byte; for(;;){ if(!buf_ok(b,1)) return v; byte=*b->p++; v |= (int64_t)(byte & 0x7f) << shift; shift += 7; if(!(byte & 0x80)) break; } if ((byte & 0x40) && shift < 64) v |= -((int64_t)1 << shift); return v; }

typedef struct { uint32_t start, end; char *name; } InlineRange;

typedef struct {
    const uint8_t *sec; size_t secsz; // .debug_rnglists
    uint32_t base_off; // from CU's DW_AT_rnglists_base
    uint8_t addr_size;
    int be;
} RngCtx;

// Parse rnglists at base_off to resolve index -> list offset table, then entries
static int rnglists_resolve(RngCtx *rc, uint32_t index, InlineRange **out, size_t *out_n) {
    *out = NULL; *out_n = 0;
    if (!rc->sec || rc->secsz < rc->base_off + 12) return 0;
    Buf b = { rc->sec + rc->base_off, rc->sec + rc->secsz, rc->be };
    // unit_length (u32), version(u16), addr_size(u8), seg_size(u8), offset_entry_count(u32)
    uint32_t unit_length = rd_u32b(&b); (void)unit_length;
    uint16_t version = rd_u16b(&b);
    uint8_t addr_size = rd_u8(&b); uint8_t seg = rd_u8(&b); (void)seg;
    uint32_t off_count = rd_u32b(&b);
    if (version < 5 || addr_size != rc->addr_size) return 0;
    if (!buf_ok(&b, off_count * 4)) return 0;
    const uint8_t *off_table = b.p;
    if (index >= off_count) return 0;
    // Read list offset with correct endianness
    Buf tob = { off_table + index*4, off_table + index*4 + 4, rc->be };
    uint32_t list_off = rd_u32b(&tob);
    Buf rb = { rc->sec + rc->base_off + list_off, rc->sec + rc->secsz, rc->be };
    uint64_t base = 0;
    InlineRange *ranges = NULL; size_t n=0, cap=0;
    for(;;){ if(!buf_ok(&rb,1)) break; uint8_t op = rd_u8(&rb); if(op == DW_RLE_end_of_list) break; switch(op){
        case DW_RLE_base_address: {
            // absolute base address (addr_size bytes)
            uint32_t a = rd_addr(&rb, rc->addr_size); base = a;
            break; }
        case DW_RLE_start_length: {
            uint64_t start = rd_addr(&rb, rc->addr_size); uint64_t len = rd_uleb(&rb);
            uint64_t s = start & 0x00ffffffu; uint64_t e = (start + len) & 0x00ffffffu;
            if (s<e){ if (n==cap){ size_t nc = cap?cap*2:8; InlineRange* nr = (InlineRange*)realloc(ranges, nc*sizeof(*nr)); if(!nr){ free(ranges); return 0;} ranges=nr; cap=nc; }
                ranges[n].start=(uint32_t)s; ranges[n].end=(uint32_t)e; ranges[n].name=NULL; n++; }
            break; }
        case DW_RLE_offset_pair: {
            uint64_t a1 = rd_uleb(&rb), a2 = rd_uleb(&rb);
            uint64_t s = (base + a1) & 0x00ffffffu; uint64_t e = (base + a2) & 0x00ffffffu;
            if (s<e){ if (n==cap){ size_t nc = cap?cap*2:8; InlineRange* nr = (InlineRange*)realloc(ranges, nc*sizeof(*nr)); if(!nr){ free(ranges); return 0;} ranges=nr; cap=nc; }
                ranges[n].start=(uint32_t)s; ranges[n].end=(uint32_t)e; ranges[n].name=NULL; n++; }
            break; }
        default: /* skip unsupported ops */ return 0; }
    }
    *out = ranges; *out_n = n; return 1;
}

// Parse a range list starting at absolute offset within .debug_rnglists
static int rnglists_parse_at(const RngCtx *rc, uint32_t abs_off, InlineRange **out, size_t *out_n) {
    *out = NULL; *out_n = 0;
    if (!rc->sec || abs_off >= rc->secsz) return 0;
    Buf rb = { rc->sec + abs_off, rc->sec + rc->secsz, rc->be };
    uint64_t base = 0;
    InlineRange *ranges = NULL; size_t n=0, cap=0;
    for(;;){ if(!buf_ok(&rb,1)) break; uint8_t op = rd_u8(&rb); if(op == DW_RLE_end_of_list) break; switch(op){
        case DW_RLE_base_address: { uint32_t a = rd_addr(&rb, rc->addr_size); base = a; break; }
        case DW_RLE_start_length: { uint64_t start = rd_addr(&rb, rc->addr_size); uint64_t len = rd_uleb(&rb); uint64_t s = start & 0x00ffffffu; uint64_t e = (start + len) & 0x00ffffffu; if (s<e){ if (n==cap){ size_t nc=cap?cap*2:8; InlineRange* nr=(InlineRange*)realloc(ranges, nc*sizeof(*nr)); if(!nr){ free(ranges); return 0;} ranges=nr; cap=nc; } ranges[n].start=(uint32_t)s; ranges[n].end=(uint32_t)e; ranges[n].name=NULL; n++; } break; }
        case DW_RLE_offset_pair: { uint64_t a1=rd_uleb(&rb), a2=rd_uleb(&rb); uint64_t s=(base + a1) & 0x00ffffffu; uint64_t e=(base + a2) & 0x00ffffffu; if (s<e){ if (n==cap){ size_t nc=cap?cap*2:8; InlineRange* nr=(InlineRange*)realloc(ranges, nc*sizeof(*nr)); if(!nr){ free(ranges); return 0;} ranges=nr; cap=nc; } ranges[n].start=(uint32_t)s; ranges[n].end=(uint32_t)e; ranges[n].name=NULL; n++; } break; }
        default: /* unsupported ops */ return 0; }
    }
    *out = ranges; *out_n = n; return 1;
}

// DWARF v4 .debug_ranges parser at absolute offset
static int ranges_parse_v4(const uint8_t *sec, size_t secsz, uint32_t off, uint8_t addr_size, int be, uint32_t cu_base, InlineRange **out, size_t *out_n) {
    *out = NULL; *out_n = 0;
    if (!sec || off >= secsz) return 0;
    Buf b = { sec + off, sec + secsz, be };
    uint64_t base = cu_base;
    uint64_t max = (addr_size == 4) ? 0xffffffffULL : 0xffffffffffffffffULL; // we only handle 4 here, but keep generic
    InlineRange *ranges = NULL; size_t n=0, cap=0;
    for (;;) {
        if (!buf_ok(&b, addr_size*2)) break;
        uint64_t start = rd_addr(&b, addr_size);
        uint64_t end   = rd_addr(&b, addr_size);
        if (start == 0 && end == 0) break; // end of list
        if (start == max) { base = end; continue; } // base address selection
        uint64_t s = base ? (base + start) : start;
        uint64_t e = base ? (base + end)   : end;
        s &= 0x00ffffffu; e &= 0x00ffffffu;
        if (s < e) {
            if (n == cap) { size_t nc = cap?cap*2:8; InlineRange* nr=(InlineRange*)realloc(ranges, nc*sizeof(*nr)); if(!nr){ free(ranges); return 0; } ranges=nr; cap=nc; }
            ranges[n].start = (uint32_t)s;
            ranges[n].end   = (uint32_t)e;
            ranges[n].name  = NULL;
            n++;
        }
    }
    *out = ranges; *out_n = n; return 1;
}

typedef struct { uint32_t code; uint32_t tag; uint8_t children; struct { uint32_t name, form; } attr[16]; int nattr; } Abbrev;

typedef struct { const uint8_t *info; size_t infosz; const uint8_t *abbrev; size_t abbrevsz; const uint8_t *str; size_t strsz; const uint8_t *line_str; size_t line_strsz; const uint8_t *str_offs; size_t str_offssz; const uint8_t *rnglists; size_t rnglistsz; const uint8_t *ranges; size_t rangessz; int be; } DwarfSecs;

static const uint8_t* find_section(const uint8_t *buf, size_t fsz, const char *sname, size_t *out_sz, int be) {
    // ELF32 only
    if (fsz < 52) return NULL;
    const uint8_t *eh = buf;
    uint16_t e_shentsize = r_u16(eh + 46, be);
    uint32_t e_shoff = r_u32(eh + 32, be);
    uint16_t e_shnum = r_u16(eh + 48, be);
    uint16_t e_shstrndx = r_u16(eh + 50, be);
    if (e_shoff == 0 || e_shentsize == 0 || e_shnum == 0) return NULL;
    if ((uint64_t)e_shoff + (uint64_t)e_shentsize * e_shnum > (uint64_t)fsz) return NULL;
    const uint8_t *shdr = buf + e_shoff;
    if (e_shstrndx >= e_shnum) return NULL;
    const uint8_t *sh_str = shdr + e_shstrndx * e_shentsize;
    uint32_t shstr_off = r_u32(sh_str + 16, be);
    uint32_t shstr_size = r_u32(sh_str + 20, be);
    if ((uint64_t)shstr_off + shstr_size > (uint64_t)fsz) return NULL;
    const char *strtab = (const char*)(buf + shstr_off);
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const uint8_t *sh = shdr + i * e_shentsize;
        uint32_t sh_name = r_u32(sh + 0, be);
        uint32_t sh_offset = r_u32(sh + 16, be);
        uint32_t sh_size = r_u32(sh + 20, be);
        if (sh_name < shstr_size) {
            const char *nm = strtab + sh_name;
            if (strcmp(nm, sname) == 0) {
                if ((uint64_t)sh_offset + sh_size > (uint64_t)fsz) return NULL;
                *out_sz = sh_size;
                return buf + sh_offset;
            }
        }
    }
    return NULL;
}

typedef struct { uint32_t start, end; char *name; } InlineEntry;

// Comparator for inline entries by start, end, then name
static int cmp_inline_entry(const void *a, const void *b) {
    const InlineEntry *pa = (const InlineEntry*)a;
    const InlineEntry *pb = (const InlineEntry*)b;
    if (pa->start < pb->start) return -1;
    if (pa->start > pb->start) return 1;
    if (pa->end < pb->end) return -1;
    if (pa->end > pb->end) return 1;
    return strcmp(pa->name, pb->name);
}

static Abbrev* find_abbrev(Abbrev *arr, int n, uint32_t code) {
    for (int i = 0; i < n; ++i) if (arr[i].code == code) return &arr[i];
    return NULL;
}

static InlineEntry *load_inline_ranges(const char *path, size_t *out_count) {
    *out_count = 0;
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t*)malloc((size_t)fsz); if (!buf){ fclose(f); return NULL; }
    if (fread(buf,1,(size_t)fsz,f)!=(size_t)fsz){ free(buf); fclose(f); return NULL; } fclose(f);

    int be = (buf[5] == 2);
    DwarfSecs ds = {0}; size_t sz;
    ds.be = be;
    ds.info = find_section(buf, fsz, ".debug_info", &ds.infosz, be);
    ds.abbrev = find_section(buf, fsz, ".debug_abbrev", &ds.abbrevsz, be);
    ds.str = find_section(buf, fsz, ".debug_str", &sz, be);
    ds.strsz = sz;
    ds.line_str = find_section(buf, fsz, ".debug_line_str", &ds.line_strsz, be);
    ds.str_offs = find_section(buf, fsz, ".debug_str_offsets", &ds.str_offssz, be);
    ds.rnglists = find_section(buf, fsz, ".debug_rnglists", &ds.rnglistsz, be);
    ds.ranges = find_section(buf, fsz, ".debug_ranges", &ds.rangessz, be);
    ds.ranges = find_section(buf, fsz, ".debug_ranges", &ds.rangessz, be);
    if (!ds.info || !ds.abbrev) { free(buf); return NULL; }

    InlineEntry *out = NULL; size_t out_n=0, out_cap=0;
    Buf ib = { ds.info, ds.info + ds.infosz, be };
    while (ib.p < ib.end) {
        // CU header (supports DWARF v4 and v5)
        if (!buf_ok(&ib, 4)) break; uint32_t unit_length = rd_u32b(&ib);
        if (unit_length == 0xffffffffu) { /* 64-bit DWARF not supported */ break; }
        const uint8_t *cu_end = ib.p + unit_length;
        if (cu_end > ib.end) break;
        uint16_t version = rd_u16b(&ib);
        uint8_t addr_size = 0;
        uint32_t abbrev_off = 0;
        if (version >= 5) {
            uint8_t unit_type = rd_u8(&ib);
            addr_size = rd_u8(&ib);
            abbrev_off = rd_u32b(&ib);
            if (unit_type != DW_UT_compile) { ib.p = cu_end; continue; }
        } else {
            // DWARF v4 header: abbrev_off (u32), address_size (u8)
            abbrev_off = rd_u32b(&ib);
            addr_size = rd_u8(&ib);
        }

        // Build abbrev table for this CU
        if (!ds.abbrev || abbrev_off >= ds.abbrevsz) { ib.p = cu_end; continue; }
        Buf ab = { ds.abbrev + abbrev_off, ds.abbrev + ds.abbrevsz, be };
        Abbrev abv[64]; int nabv=0; memset(abv,0,sizeof(abv));
        for (;;) {
            uint64_t code = rd_uleb(&ab); if (code == 0) break; if (nabv >= (int)(sizeof(abv)/sizeof(abv[0]))) break;
            uint64_t tag = rd_uleb(&ab); uint8_t children = rd_u8(&ab);
            Abbrev *A = &abv[nabv++]; A->code = (uint32_t)code; A->tag = (uint32_t)tag; A->children=children; A->nattr=0;
            for(;;){ uint64_t name = rd_uleb(&ab); uint64_t form = rd_uleb(&ab); if (name==0 && form==0) break; if (A->nattr < (int)(sizeof(A->attr)/sizeof(A->attr[0]))){ A->attr[A->nattr].name=(uint32_t)name; A->attr[A->nattr].form=(uint32_t)form; A->nattr++; } }
        }

    // Parse CU DIEs
    RngCtx rc = { ds.rnglists, (size_t)ds.rnglistsz, 0, addr_size, be };
    // Map DIE absolute offset to name (subprograms)
    typedef struct { uint32_t off_abs; char *name; } NameMap;
    NameMap *namemap=NULL; size_t nn=0, nc=0;

    // CU DIE begins here; record CU base for namemap offsets
    const uint8_t *cu_start = ib.p;
    uint32_t cu_str_off_base = 0;
    uint32_t cu_low_pc_base = 0;

        // Walk DIE tree linearly, track depth via null entries
        int depth = 0;
        while (ib.p < cu_end) {
            const uint8_t *die_pos = ib.p;
            uint64_t acode = rd_uleb(&ib);
            if (acode == 0) { if (depth>0) depth--; continue; }
            Abbrev *A = find_abbrev(abv, nabv, (uint32_t)acode); if (!A) { ib.p = cu_end; break; }
            // Collect attributes of interest
            uint32_t tag = A->tag; uint32_t low_pc=0, high_pc=0; uint8_t have_low=0, have_high=0; uint32_t rnglistx=UINT32_MAX; uint32_t ao_ref=0; int ao_is_abs=0; uint32_t rnglists_base=0; char *name=NULL; uint8_t have_name=0;
            uint32_t ranges_sec_off = UINT32_MAX;
            for (int ai=0; ai<A->nattr; ++ai) {
                uint32_t an = A->attr[ai].name, fm = A->attr[ai].form;
                // resolve indirect
                if (fm == DW_FORM_indirect) fm = (uint32_t)rd_uleb(&ib);
                switch (an) {
                    case DW_AT_low_pc: {
                        // address_size bytes
                        uint32_t a = rd_addr(&ib, addr_size); low_pc = a & 0x00ffffffu; have_low=1; break; }
                    case DW_AT_high_pc: {
                        // may be address or udata length; infer by form
                        if (fm == DW_FORM_addr) { uint32_t a = rd_addr(&ib, addr_size); high_pc = a & 0x00ffffffu; have_high=1; }
                        else if (fm == DW_FORM_data4 || fm == DW_FORM_data8 || fm == DW_FORM_udata) { uint64_t len = (fm==DW_FORM_data4)? rd_u32b(&ib) : (fm==DW_FORM_data8? (rd_u32b(&ib) | ((uint64_t)rd_u32b(&ib)<<32)) : rd_uleb(&ib)); high_pc = (low_pc + (uint32_t)len) & 0x00ffffffu; have_high=1; }
                        else { /* skip */ }
                        break; }
                    case DW_AT_ranges: {
                        if (fm == DW_FORM_rnglistx) { rnglistx = (uint32_t)rd_uleb(&ib); }
                        else if (fm == DW_FORM_sec_offset) { ranges_sec_off = rd_u32b(&ib); }
                        else { /* skip size */ if (fm==DW_FORM_data4) rd_u32b(&ib); else if (fm==DW_FORM_data2) rd_u16b(&ib); else if (fm==DW_FORM_data1) rd_u8(&ib); }
                        break; }
                    case DW_AT_abstract_origin: {
                        if (fm == DW_FORM_ref4) { ao_ref = rd_u32b(&ib); ao_is_abs = 0; }
                        else if (fm == DW_FORM_ref_udata) { ao_ref = (uint32_t)rd_uleb(&ib); ao_is_abs = 0; }
                        else if (fm == DW_FORM_ref_addr) { ao_ref = rd_u32b(&ib); ao_is_abs = 1; }
                        break; }
                    case DW_AT_rnglists_base: {
                        rnglists_base = rd_u32b(&ib); rc.base_off = rnglists_base; break; }
                    case DW_AT_str_offsets_base: {
                        cu_str_off_base = rd_u32b(&ib); break; }
                    case DW_AT_linkage_name:
                    case DW_AT_name: {
                        const char *nm = NULL;
                        if (fm == DW_FORM_strp && ds.str) { uint32_t off = rd_u32b(&ib); if (off < ds.strsz) nm = (const char*)ds.str + off; }
                        else if (fm == DW_FORM_line_strp && ds.line_str) { uint32_t off = rd_u32b(&ib); if (off < ds.line_strsz) nm = (const char*)ds.line_str + off; }
                        else if ((fm == DW_FORM_strx || fm == DW_FORM_strx1 || fm == DW_FORM_strx2 || fm == DW_FORM_strx4 || fm == DW_FORM_strx8) && ds.str_offs && ds.str) {
                            uint64_t idx = 0;
                            if (fm == DW_FORM_strx) idx = rd_uleb(&ib);
                            else if (fm == DW_FORM_strx1) idx = rd_u8(&ib);
                            else if (fm == DW_FORM_strx2) idx = rd_u16b(&ib);
                            else if (fm == DW_FORM_strx4) idx = rd_u32b(&ib);
                            else if (fm == DW_FORM_strx8) { uint32_t hi = rd_u32b(&ib); uint32_t lo = rd_u32b(&ib); idx = ((uint64_t)hi << 32) | lo; }
                            uint32_t ent_off = cu_str_off_base + (uint32_t)(idx * 4);
                            if (ent_off + 4 <= ds.str_offssz) {
                                uint32_t str_off = r_u32(ds.str_offs + ent_off, ds.be);
                                if (str_off < ds.strsz) nm = (const char*)ds.str + str_off;
                            }
                        }
                        else if (fm == DW_FORM_udata) { (void)rd_uleb(&ib); }
                        if (nm && !have_name) { size_t l=strlen(nm)+1; name=(char*)malloc(l); if(name){ memcpy(name,nm,l); have_name=1; } }
                        break; }
                    default: {
                        // skip value based on form
                        switch (fm){ case DW_FORM_addr: (void)rd_addr(&ib,addr_size); break; case DW_FORM_data1: rd_u8(&ib); break; case DW_FORM_data2: rd_u16b(&ib); break; case DW_FORM_data4: rd_u32b(&ib); break; case DW_FORM_data8: rd_u32b(&ib); rd_u32b(&ib); break; case DW_FORM_udata: rd_uleb(&ib); break; case DW_FORM_sdata: rd_sleb(&ib); break; case DW_FORM_flag_present: break; case DW_FORM_strp: rd_u32b(&ib); break; case DW_FORM_line_strp: rd_u32b(&ib); break; case DW_FORM_ref4: rd_u32b(&ib); break; case DW_FORM_ref_udata: rd_uleb(&ib); break; default: /* unhandled */ break; }
                        break; }
                }
            }

            uint32_t this_off_abs = (uint32_t)(die_pos - ds.info);

            if (tag == DW_TAG_compile_unit) {
                if (have_low) cu_low_pc_base = low_pc;
                // DW_AT_rnglists_base handled above via rc.base_off on this DIE
                continue;
            }

            if (tag == DW_TAG_subprogram) {
                // record name for abstract origins
                if (have_name) {
                    if (nn==nc){ size_t nc2=nc?nc*2:64; NameMap* nnm=(NameMap*)realloc(namemap,nc2*sizeof(*nnm)); if(!nnm){ /* out */ } else { namemap=nnm; nc=nc2; } }
                    if (nn<nc){ namemap[nn].off_abs = this_off_abs; namemap[nn].name = name; nn++; name=NULL; }
                }
            }
            else if (tag == DW_TAG_inlined_subroutine) {
                // resolve ranges
                InlineRange *ranges=NULL; size_t nr=0;
                if (have_low && have_high) {
                    ranges = (InlineRange*)malloc(sizeof(InlineRange)); if(ranges){ ranges[0].start=low_pc; ranges[0].end=high_pc; ranges[0].name=NULL; nr=1; }
                } else if (rnglistx != UINT32_MAX && rc.sec && rc.base_off) {
                    rnglists_resolve(&rc, rnglistx, &ranges, &nr);
                } else if (ranges_sec_off != UINT32_MAX) {
                    if (ds.rnglists) {
                        rnglists_parse_at(&rc, ranges_sec_off, &ranges, &nr);
                    } else if (ds.ranges) {
                        ranges_parse_v4(ds.ranges, ds.rangessz, ranges_sec_off, addr_size, be, cu_low_pc_base, &ranges, &nr);
                    }
                }
                // resolve name from abstract_origin
                char *iname = NULL;
                if (ao_ref && namemap) {
                    uint32_t ao_abs = ao_is_abs ? ao_ref : (uint32_t)(ao_ref + (uint32_t)(cu_start - ds.info));
                    for (size_t i=0;i<nn;i++) if (namemap[i].off_abs == ao_abs) { size_t l=strlen(namemap[i].name)+1; iname=(char*)malloc(l); if(iname) memcpy(iname, namemap[i].name, l); break; }
                }
                if (!iname && have_name) iname = name, name=NULL;
                // store entries
                if (ranges) {
                    if (!iname && nr > 0) {
                        // Synthesize a name if none provided
                        char tmp[32]; snprintf(tmp, sizeof(tmp), "inline_0x%06x", (unsigned)ranges[0].start);
                        size_t l = strlen(tmp) + 1; iname = (char*)malloc(l); if (iname) memcpy(iname, tmp, l);
                    }
                }
                if (ranges && iname) {
                    for (size_t i=0;i<nr;i++) {
                        if (out_n==out_cap){ size_t nc2=out_cap?out_cap*2:64; InlineEntry* ne=(InlineEntry*)realloc(out, nc2*sizeof(*ne)); if(!ne){ break; } out=ne; out_cap=nc2; }
                        if (out_n<out_cap){ out[out_n].start=ranges[i].start; out[out_n].end=ranges[i].end; out[out_n].name=(char*)malloc(strlen(iname)+1); if(out[out_n].name) strcpy(out[out_n].name,iname); out_n++; }
                    }
                }
                if (ranges){ free(ranges); }
                if (iname) free(iname);
            }
            if (A->children == DW_CHILDREN_yes) depth++;
        }

        // cleanup namemap
        if (namemap){ for (size_t i=0;i<nn;i++) free(namemap[i].name); free(namemap); }

        ib.p = cu_end;
    }

    // Sort and coalesce duplicates
    if (out_n) {
        qsort(out, out_n, sizeof(InlineEntry), cmp_inline_entry);
    }

    free(buf);
    *out_count = out_n;
    return out;
}

// Build function ranges from DWARF subprogram DIEs (preferred under -O2/-O3)
static FuncSym *load_func_symbols_dwarf(const char *path, size_t *out_count) {
    *out_count = 0;
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t*)malloc((size_t)fsz); if (!buf){ fclose(f); return NULL; }
    if (fread(buf,1,(size_t)fsz,f)!=(size_t)fsz){ free(buf); fclose(f); return NULL; } fclose(f);

    int be = (buf[5] == 2);
    DwarfSecs ds = {0}; size_t sz;
    ds.be = be;
    ds.info = find_section(buf, fsz, ".debug_info", &ds.infosz, be);
    ds.abbrev = find_section(buf, fsz, ".debug_abbrev", &ds.abbrevsz, be);
    ds.str = find_section(buf, fsz, ".debug_str", &sz, be); ds.strsz = sz;
    ds.line_str = find_section(buf, fsz, ".debug_line_str", &ds.line_strsz, be);
    ds.rnglists = find_section(buf, fsz, ".debug_rnglists", &ds.rnglistsz, be);
    if (!ds.info || !ds.abbrev) { free(buf); return NULL; }

    FuncSym *out = NULL; size_t out_n=0, out_cap=0;
    Buf ib = { ds.info, ds.info + ds.infosz, be };
    while (ib.p < ib.end) {
        if (!buf_ok(&ib, 4)) break; uint32_t unit_length = rd_u32b(&ib);
        if (unit_length == 0xffffffffu) { break; }
        const uint8_t *cu_end = ib.p + unit_length; if (cu_end > ib.end) break;
        uint16_t version = rd_u16b(&ib);
        uint8_t addr_size = 0;
        uint32_t abbrev_off = 0;
        if (version >= 5) {
            uint8_t unit_type = rd_u8(&ib);
            addr_size = rd_u8(&ib);
            abbrev_off = rd_u32b(&ib);
            if (unit_type != DW_UT_compile) { ib.p = cu_end; continue; }
        } else {
            abbrev_off = rd_u32b(&ib);
            addr_size = rd_u8(&ib);
        }

        if (!ds.abbrev || abbrev_off >= ds.abbrevsz) { ib.p = cu_end; continue; }
        Buf ab = { ds.abbrev + abbrev_off, ds.abbrev + ds.abbrevsz, be };
        int nabv = 0, cabv = 128; Abbrev *abv = (Abbrev*)calloc(cabv, sizeof(Abbrev)); if (!abv) { ib.p = cu_end; continue; }
        for (;;) {
            uint64_t code = rd_uleb(&ab); if (code==0) break;
            if (nabv == cabv) { cabv *= 2; Abbrev *tmp=(Abbrev*)realloc(abv, cabv*sizeof(Abbrev)); if (!tmp) { free(abv); abv=NULL; break; } abv = tmp; }
            uint64_t tag = rd_uleb(&ab); uint8_t children = rd_u8(&ab);
            Abbrev *A=&abv[nabv++]; A->code=(uint32_t)code; A->tag=(uint32_t)tag; A->children=children; A->nattr=0;
            for(;;){ uint64_t name=rd_uleb(&ab); uint64_t form=rd_uleb(&ab); if (name==0 && form==0) break; if (A->nattr < (int)(sizeof(A->attr)/sizeof(A->attr[0]))){ A->attr[A->nattr].name=(uint32_t)name; A->attr[A->nattr].form=(uint32_t)form; A->nattr++; } }
        }
        if (!abv) { ib.p = cu_end; continue; }

        RngCtx rc = { ds.rnglists, (size_t)ds.rnglistsz, 0, addr_size, be };
        const uint8_t *cu_start = ib.p;

        while (ib.p < cu_end) {
            uint64_t acode = rd_uleb(&ib); if (acode == 0) { continue; }
            Abbrev *A = find_abbrev(abv, nabv, (uint32_t)acode); if (!A) { ib.p = cu_end; break; }
            uint32_t tag = A->tag; uint32_t low_pc=0, high_pc=0; uint8_t have_low=0, have_high=0; uint32_t rnglistx=UINT32_MAX; uint32_t ranges_sec_off = UINT32_MAX; char *name=NULL; uint8_t have_name=0;
            for (int ai=0; ai<A->nattr; ++ai) {
                uint32_t an=A->attr[ai].name, fm=A->attr[ai].form; if (fm==DW_FORM_indirect) fm=(uint32_t)rd_uleb(&ib);
                switch (an) {
                    case DW_AT_low_pc: { uint32_t a = rd_addr(&ib, addr_size); low_pc = a & 0x00ffffffu; have_low=1; break; }
                    case DW_AT_high_pc: { if (fm==DW_FORM_addr){ uint32_t a = rd_addr(&ib, addr_size); high_pc=a & 0x00ffffffu; have_high=1; } else if (fm==DW_FORM_data4 || fm==DW_FORM_data8 || fm==DW_FORM_udata){ uint64_t len=(fm==DW_FORM_data4)? rd_u32b(&ib): (fm==DW_FORM_data8? (rd_u32b(&ib)|((uint64_t)rd_u32b(&ib)<<32)) : rd_uleb(&ib)); high_pc=(low_pc + (uint32_t)len) & 0x00ffffffu; have_high=1; } else { /* skip */ } break; }
                    case DW_AT_ranges: { if (fm==DW_FORM_rnglistx) rnglistx=(uint32_t)rd_uleb(&ib); else if (fm==DW_FORM_sec_offset) { ranges_sec_off = rd_u32b(&ib); } else { if (fm==DW_FORM_data4) rd_u32b(&ib); else if (fm==DW_FORM_data2) rd_u16b(&ib); else if (fm==DW_FORM_data1) rd_u8(&ib); } break; }
                    case DW_AT_rnglists_base: { uint32_t base=rd_u32b(&ib); rc.base_off=base; break; }
                    case DW_AT_linkage_name:
                    case DW_AT_name: { const char *nm=NULL; if (fm==DW_FORM_strp && ds.str){ uint32_t off=rd_u32b(&ib); if (off < ds.strsz) nm=(const char*)ds.str+off; } else if (fm==DW_FORM_line_strp && ds.line_str){ uint32_t off=rd_u32b(&ib); if (off < ds.line_strsz) nm=(const char*)ds.line_str+off; } else if (fm==DW_FORM_udata) { (void)rd_uleb(&ib); } if (nm && !have_name){ size_t l=strlen(nm)+1; name=(char*)malloc(l); if(name){ memcpy(name,nm,l); have_name=1; } } break; }
                    default: { switch (fm){ case DW_FORM_addr: (void)rd_addr(&ib,addr_size); break; case DW_FORM_data1: rd_u8(&ib); break; case DW_FORM_data2: rd_u16b(&ib); break; case DW_FORM_data4: rd_u32b(&ib); break; case DW_FORM_data8: rd_u32b(&ib); rd_u32b(&ib); break; case DW_FORM_udata: rd_uleb(&ib); break; case DW_FORM_sdata: rd_sleb(&ib); break; case DW_FORM_flag_present: break; case DW_FORM_strp: rd_u32b(&ib); break; case DW_FORM_line_strp: rd_u32b(&ib); break; default: break;} break; }
                }
            }

            if (tag == DW_TAG_subprogram) {
                if (have_low && have_high) {
                    if (out_n==out_cap){ size_t nc2=out_cap?out_cap*2:128; FuncSym* nf=(FuncSym*)realloc(out,nc2*sizeof(*nf)); if(!nf){ /* oom */ } else { out=nf; out_cap=nc2; } }
                    if (out_n<out_cap){ out[out_n].start=low_pc; out[out_n].end=high_pc; if (!have_name){ char tmp[32]; snprintf(tmp,sizeof(tmp),"sub_0x%06x", (unsigned)low_pc); size_t l=strlen(tmp)+1; name=(char*)malloc(l); if(name) memcpy(name,tmp,l); }
                        out[out_n].name=name; out[out_n].tally=0; out_n++; name=NULL; }
                }
                else if (rnglistx != UINT32_MAX && rc.sec && rc.base_off) {
                    InlineRange *ranges=NULL; size_t nr=0; if (rnglists_resolve(&rc, rnglistx, &ranges, &nr) && ranges){
                        for (size_t i=0;i<nr;i++){
                            if (out_n==out_cap){ size_t nc2=out_cap?out_cap*2:128; FuncSym* nf=(FuncSym*)realloc(out,nc2*sizeof(*nf)); if(!nf){ break; } out=nf; out_cap=nc2; }
                            if (out_n<out_cap){ out[out_n].start=ranges[i].start; out[out_n].end=ranges[i].end; if (!have_name){ char tmp[32]; snprintf(tmp,sizeof(tmp),"sub_0x%06x", (unsigned)ranges[i].start); size_t l=strlen(tmp)+1; name=(char*)malloc(l); if(name) memcpy(name,tmp,l); }
                                out[out_n].name=(char*)malloc(strlen(name)+1); if(out[out_n].name) strcpy(out[out_n].name,name); out[out_n].tally=0; out_n++; }
                        }
                        free(ranges);
                        free(name); name=NULL;
                    }
                }
                else if (ranges_sec_off != UINT32_MAX) {
                    InlineRange *ranges=NULL; size_t nr=0; int ok=0;
                    if (ds.rnglists) ok = rnglists_parse_at(&rc, ranges_sec_off, &ranges, &nr);
                    else if (ds.ranges) ok = ranges_parse_v4(ds.ranges, ds.rangessz, ranges_sec_off, addr_size, be, have_low ? low_pc : 0, &ranges, &nr);
                    if (ok && ranges){
                        for (size_t i=0;i<nr;i++){
                            if (out_n==out_cap){ size_t nc2=out_cap?out_cap*2:128; FuncSym* nf=(FuncSym*)realloc(out,nc2*sizeof(*nf)); if(!nf){ break; } out=nf; out_cap=nc2; }
                            if (out_n<out_cap){ out[out_n].start=ranges[i].start; out[out_n].end=ranges[i].end; if (!have_name){ char tmp[32]; snprintf(tmp,sizeof(tmp),"sub_0x%06x", (unsigned)ranges[i].start); size_t l=strlen(tmp)+1; name=(char*)malloc(l); if(name) memcpy(name,tmp,l); }
                                out[out_n].name=(char*)malloc(strlen(name)+1); if(out[out_n].name) strcpy(out[out_n].name,name); out[out_n].tally=0; out_n++; }
                        }
                        free(ranges);
                        free(name); name=NULL;
                    }
                }
                else {
                    if (name) free(name);
                }
            } else {
                if (name) free(name);
            }
        }
        free(abv);
    }

    // Sort and normalize
    if (out_n) {
        qsort(out, out_n, sizeof(FuncSym), cmp_func_start);
        for (size_t i=0;i<out_n;i++){
            if (out[i].end == 0) {
                uint32_t next = (i+1<out_n)? out[i+1].start : 0x01000000u;
                out[i].end = next>out[i].start ? next : out[i].start+1;
            }
        }
    }

    free(buf);
    *out_count = out_n;
    return out;
}

static int elf_endian_be(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 0; unsigned char hdr[6] = {0}; size_t r=fread(hdr,1,6,f); fclose(f); if (r<6) return 0; return hdr[5] == 2; }

static OutPair *collect_pairs_from_table(const ProfEntry *table, uint32_t used_estimate, size_t *out_count) {
    if (!table || !out_count) return NULL;
    size_t cap = used_estimate ? used_estimate : 1;
    OutPair *pairs = (OutPair*)malloc(cap * sizeof(OutPair));
    if (!pairs) return NULL;
    size_t count = 0;
    for (size_t i = 0; i < PROF_TABLE_SIZE; ++i) {
        if (table[i].key != 0 && (table[i].samples || table[i].cycles)) {
            if (count >= cap) {
                size_t nc = cap ? cap * 2 : 1;
                OutPair *tmp = (OutPair*)realloc(pairs, nc * sizeof(OutPair));
                if (!tmp) {
                    free(pairs);
                    return NULL;
                }
                pairs = tmp;
                cap = nc;
            }
            pairs[count].addr = table[i].key - 1u;
            pairs[count].samples = table[i].samples;
            pairs[count].cycles = table[i].cycles;
            ++count;
        }
    }
    qsort(pairs, count, sizeof(OutPair), cmp_desc_count);
    *out_count = count;
    return pairs;
}

static char *make_test_json_path(const char *path) {
    if (!path || !path[0]) return NULL;
    const char *last_sep = strrchr(path, '/');
    size_t dir_len = last_sep ? (size_t)(last_sep - path + 1) : 0;
    const char *name = last_sep ? last_sep + 1 : path;
    const char *prefix = "test-";
    size_t len = dir_len + strlen(prefix) + strlen(name) + 1;
    char *out = (char*)malloc(len);
    if (!out) return NULL;
    if (dir_len) memcpy(out, path, dir_len);
    size_t pos = dir_len;
    memcpy(out + pos, prefix, strlen(prefix));
    pos += strlen(prefix);
    memcpy(out + pos, name, strlen(name) + 1);
    return out;
}

static int write_profiler_json_from_pairs(const char *path, const OutPair *pairs, size_t npairs, LineTable *lt) {
    if (!path) return 0;
    FILE *fj = fopen(path, "wb");
    if (!fj) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Profiler dump failed: cannot open %s", path);
        geo_notify(msg, 240);
        return -1;
    }
    bool wrote = false;
    fputc('[', fj);
    size_t lnrows = lt ? lt->nrows : 0;
    LineRow *lrows = lt ? lt->rows : NULL;
    if (lnrows > 0 && pairs && npairs > 0) {
        typedef struct { uint32_t file_idx; uint32_t line; uint64_t cycles; uint64_t samples; uint32_t best_addr; uint64_t best_addr_cycles; } LRec;
        LRec *jrecs = NULL;
        size_t jn = 0, jc = 0;
        for (size_t i = 0; i < npairs; ++i) {
            const LineRow *r = geo_line_find_row_addr(lrows, lnrows, pairs[i].addr);
            if (!r) continue;
            uint32_t fidx = r->file;
            uint32_t lno = r->line;
            size_t k = 0;
            for (; k < jn; ++k) {
                if (jrecs[k].file_idx == fidx && jrecs[k].line == lno) break;
            }
            if (k == jn) {
                if (jn == jc) {
                    size_t nc = jc ? jc * 2 : 64;
                    LRec *nt = (LRec*)realloc(jrecs, nc * sizeof(LRec));
                    if (!nt) break;
                    jrecs = nt;
                    jc = nc;
                }
                if (jn < jc) {
                    jrecs[jn].file_idx = fidx;
                    jrecs[jn].line = lno;
                    jrecs[jn].cycles = 0;
                    jrecs[jn].samples = 0;
                    jrecs[jn].best_addr = 0;
                    jrecs[jn].best_addr_cycles = 0;
                    k = jn++;
                }
            }
            if (k < jn) {
                jrecs[k].cycles += pairs[i].cycles;
                jrecs[k].samples += pairs[i].samples;
                if (pairs[i].cycles > jrecs[k].best_addr_cycles) {
                    jrecs[k].best_addr = pairs[i].addr;
                    jrecs[k].best_addr_cycles = pairs[i].cycles;
                }
            }
        }
        for (size_t i = 0; i < jn; ++i) {
            const char *fname = (jrecs[i].file_idx > 0 && lt && jrecs[i].file_idx <= lt->nfiles) ? lt->files[jrecs[i].file_idx - 1] : "?";
            char srcbuf[512] = {0};
            if (fname && fname[0]) {
                const char *dir = NULL;
                if (lt && lt->file_dir && jrecs[i].file_idx > 0 && jrecs[i].file_idx <= lt->nfiles) {
                    uint32_t di = lt->file_dir[jrecs[i].file_idx - 1];
                    if (di > 0 && lt->dirs && di <= lt->ndirs) dir = lt->dirs[di - 1];
                }

                char elf_dir[1024] = {0};
                char elf_parent[1024] = {0};
                if (k_prof_elf_path && k_prof_elf_path[0]) {
                    strncpy(elf_dir, k_prof_elf_path, sizeof(elf_dir) - 1);
                    char *slash = strrchr(elf_dir, '/');
                    if (slash) *slash = '\0';
                    strncpy(elf_parent, elf_dir, sizeof(elf_parent) - 1);
                    slash = strrchr(elf_parent, '/');
                    if (slash) *slash = '\0';
                }

                char fullpath[1024] = {0};
                const char *c1 = NULL, *c2 = NULL, *c3 = NULL, *c4 = NULL;
                char buf1[1024] = {0}, buf2[1024] = {0}, buf3[1024] = {0};
                if (dir && dir[0] == '/') {
                    snprintf(buf1, sizeof(buf1), "%s/%s", dir, fname);
                    c1 = buf1;
                } else if (dir && dir[0]) {
                    snprintf(buf1, sizeof(buf1), "%s/%s/%s", elf_parent, dir, fname); c1 = buf1;
                    snprintf(buf2, sizeof(buf2), "%s/%s/%s", elf_dir, dir, fname); c2 = buf2;
                }
                snprintf(buf3, sizeof(buf3), "%s/%s", elf_parent, fname); c3 = buf3;
                c4 = fname;

                const char *cands[4] = { c1, c2, c3, c4 };
                FILE *sf = NULL;
                for (size_t ci = 0; ci < 4 && !sf; ++ci) {
                    if (!cands[ci] || !cands[ci][0]) continue;
                    snprintf(fullpath, sizeof(fullpath), "%s", cands[ci]);
                    sf = fopen(fullpath, "rb");
                }

                if (sf) {
                    unsigned cur = 1;
                    while (fgets(srcbuf, sizeof(srcbuf), sf)) {
                        if (cur == jrecs[i].line) break;
                        cur++;
                    }
                    fclose(sf);
                    size_t sl = strlen(srcbuf);
                    while (sl && (srcbuf[sl - 1] == '\n' || srcbuf[sl - 1] == '\r')) srcbuf[--sl] = '\0';
                }
            }

            if (i) fputc(',', fj);
            fputc('{', fj);
            fputs("\"file\":", fj); json_write_escaped(fj, fname);
            fprintf(fj, ",\"line\":%u", (unsigned)jrecs[i].line);
            fprintf(fj, ",\"cycles\":%llu", (unsigned long long)jrecs[i].cycles);
            fprintf(fj, ",\"count\":%llu", (unsigned long long)jrecs[i].samples);
            fprintf(fj, ",\"address\":\"0x%06x\"", (unsigned)jrecs[i].best_addr);
            fputs(",\"source\":", fj); json_write_escaped(fj, srcbuf);
            fputc('}', fj);
            wrote = true;
        }
        free(jrecs);
    }
    if (!wrote && pairs && npairs > 0) {
        for (size_t i = 0; i < npairs; ++i) {
            if (i || wrote) fputc(',', fj);
            fprintf(fj, "{\"pc\":\"0x%06x\",\"samples\":%llu,\"cycles\":%llu}",
                    (unsigned)(pairs[i].addr & 0x00ffffffu),
                    (unsigned long long)pairs[i].samples,
                    (unsigned long long)pairs[i].cycles);
            wrote = true;
        }
    }
    fputc(']', fj);
    fclose(fj);
    return 1;
}

int geo_profiler_dump(void) {
    if (!g_ever_enabled)
        return 0;
    if (!g_table)
        return 0;

    size_t npairs = 0;
    OutPair *pairs = collect_pairs_from_table(g_table, g_used, &npairs);
    if (!pairs)
        goto cleanup;

    build_global_line_table();
    LineTable lt = {0};
    if (k_prof_elf_path && k_prof_elf_path[0]) {
        geo_elf_load_line_table(k_prof_elf_path, &lt);
    }

    const char *json_path = (k_prof_json_path && k_prof_json_path[0]) ? k_prof_json_path :
                            (k_prof_out_path && k_prof_out_path[0]) ? k_prof_out_path : NULL;
    int rc = 0;
    if (json_path) {
        int wrote = write_profiler_json_from_pairs(json_path, pairs, npairs, &lt);
        if (wrote < 0)
            rc = -1;
        else if (wrote > 0 && rc <= 0)
            rc = 1;

        #ifdef GEO_PROF_STREAM_VERIFY
        if (g_stream_capture_used > 0) {
            char *test_path = make_test_json_path(json_path);
            if (test_path) {
                size_t test_npairs = 0;
                OutPair *test_pairs = collect_pairs_from_table(g_stream_capture, g_stream_capture_used, &test_npairs);
                if (test_pairs) {
                    write_profiler_json_from_pairs(test_path, test_pairs, test_npairs, &lt);
                    free(test_pairs);
                }
                free(test_path);
            }
        }
        #endif
    }

    geo_elf_free_line_table(&lt);
    free(pairs);

cleanup:
    free(g_table);
    g_table = NULL;
    g_used = 0;
    g_sample_accum = 0;
    g_enabled = 0;
    stream_capture_free();
    return rc;
}

const char *geo_profiler_dump_path(void) {
    if (k_prof_json_path && k_prof_json_path[0]) return k_prof_json_path;
    if (k_prof_out_path && k_prof_out_path[0]) return k_prof_out_path;
    return NULL;
}
// ------------------ DWARF .debug_line (v2-v4) minimal parser ------------------

static void free_line_table(LineTable *lt) {
    if (!lt) return;
    if (lt->dirs) { for (size_t i=0;i<lt->ndirs;i++) free(lt->dirs[i]); free(lt->dirs); }
    if (lt->files) { for (size_t i=0;i<lt->nfiles;i++) free(lt->files[i]); free(lt->files); }
    if (lt->file_dir) free(lt->file_dir);
    if (lt->rows) free(lt->rows);
    memset(lt, 0, sizeof(*lt));
}

static int parse_debug_line_v4(const uint8_t *sec, size_t secsz, int be, LineTable *out) {
    Buf b = { sec, sec + secsz, be };
    LineTable lt = {0};
    while (b.p < b.end) {
        if (!buf_ok(&b, 4)) break; uint32_t unit_length = rd_u32b(&b); if (unit_length == 0xffffffffu) break; const uint8_t *cu_end = b.p + unit_length; if (cu_end > b.end) break;
        if (!buf_ok(&b, 2)) break; uint16_t version = rd_u16b(&b);
        if (version >= 5) { b.p = cu_end; continue; }
        uint32_t header_length = rd_u32b(&b);
        const uint8_t *hdr_end = b.p + header_length;
        uint8_t min_inst_len = rd_u8(&b);
        uint8_t default_is_stmt = rd_u8(&b);
        int8_t line_base = (int8_t)rd_u8(&b);
        uint8_t line_range = rd_u8(&b);
        uint8_t opcode_base = rd_u8(&b);
        // skip standard opcode lengths
        for (uint8_t i=1; i<opcode_base; ++i) { if (!buf_ok(&b,1)) { free_line_table(&lt); return 0; } (void)rd_u8(&b); }
        // include directories (null-terminated strings)
        while (b.p < hdr_end && *b.p) {
            const char *s = (const char*)b.p; size_t l = strlen(s) + 1; char *ns = (char*)malloc(l); if (!ns) { free_line_table(&lt); return 0; } memcpy(ns, s, l); lt.dirs = (char**)realloc(lt.dirs, (lt.ndirs+1)*sizeof(char*)); lt.dirs[lt.ndirs++] = ns; b.p += l;
        }
        if (b.p < hdr_end) b.p++; // skip null
        // file names: name (string), dir_index (uleb), time (uleb), size (uleb)
        while (b.p < hdr_end && *b.p) {
            const char *s = (const char*)b.p; size_t l = strlen(s) + 1; char *ns = (char*)malloc(l); if (!ns) { free_line_table(&lt); return 0; } memcpy(ns, s, l); b.p += l;
            uint64_t dir_index = rd_uleb(&b); (void)rd_uleb(&b); (void)rd_uleb(&b);
            lt.files = (char**)realloc(lt.files, (lt.nfiles+1)*sizeof(char*)); lt.file_dir = (uint32_t*)realloc(lt.file_dir, (lt.nfiles+1)*sizeof(uint32_t));
            lt.files[lt.nfiles] = ns; lt.file_dir[lt.nfiles] = (uint32_t)dir_index; lt.nfiles++;
        }
        if (b.p < hdr_end) b.p++; // skip null
        // Now the program
        uint32_t address = 0; uint32_t file = 1; int32_t line = 1; uint8_t is_stmt = default_is_stmt; int end_seq = 0;
        // Emit helper
        #define EMIT_ROW() do { if (!end_seq) { size_t n = lt.nrows; lt.rows = (LineRow*)realloc(lt.rows, (n+1)*sizeof(LineRow)); lt.rows[n].start = address; lt.rows[n].end = address; lt.rows[n].file = file; lt.rows[n].line = (uint32_t)(line > 0 ? line : 0); lt.nrows = n+1; } } while(0)
        while (b.p < cu_end) {
                uint8_t op = rd_u8(&b);
                if (op >= opcode_base) {
                uint8_t adj = op - opcode_base; int32_t line_inc = line_base + (adj % line_range); uint32_t addr_inc = (adj / line_range) * min_inst_len; line += line_inc; address += addr_inc; EMIT_ROW();
                } else if (op == 0) {
                uint64_t ext_len = rd_uleb(&b); const uint8_t *ext_end = b.p + ext_len; if (ext_end > cu_end) { break; }
                uint8_t sub = rd_u8(&b);
                switch (sub) {
                    case 1: { // DW_LNE_end_sequence
                        end_seq = 1; if (lt.nrows) lt.rows[lt.nrows-1].end = address; // mark end on last row
                        // reset state
                        address = 0; file = 1; line = 1; is_stmt = default_is_stmt; end_seq = 0; b.p = ext_end; break; }
                    case 2: { // DW_LNE_set_address
                        // address_size assumed 4 here
                        address = rd_u32b(&b); b.p = ext_end; break; }
                    case 3: { // DW_LNE_define_file (v4)
                        // name
                        const char *s2 = (const char*)b.p; size_t l2 = strlen(s2) + 1; char *ns2 = (char*)malloc(l2); if (!ns2) { free_line_table(&lt); return 0; } memcpy(ns2, s2, l2); b.p += l2;
                        uint64_t dir_index2 = rd_uleb(&b); (void)rd_uleb(&b); (void)rd_uleb(&b);
                        lt.files = (char**)realloc(lt.files, (lt.nfiles+1)*sizeof(char*)); lt.file_dir = (uint32_t*)realloc(lt.file_dir, (lt.nfiles+1)*sizeof(uint32_t));
                        lt.files[lt.nfiles] = ns2; lt.file_dir[lt.nfiles] = (uint32_t)dir_index2; lt.nfiles++;
                        b.p = ext_end; break; }
                    default: { b.p = ext_end; break; }
                }
            } else {
                switch (op) {
                    case 1: { // copy
                        EMIT_ROW(); break; }
                    case 2: { // advance_pc
                        uint64_t ad = rd_uleb(&b); address += (uint32_t)(ad * min_inst_len); break; }
                    case 3: { // advance_line
                        int64_t dl = rd_sleb(&b); line += (int32_t)dl; break; }
                    case 4: { // set_file
                        file = (uint32_t)rd_uleb(&b); break; }
                    case 5: { // set_column
                        (void)rd_uleb(&b); break; }
                    case 6: { // negate_stmt
                        is_stmt = !is_stmt; break; }
                    case 7: { // set_basic_block
                        break; }
                    case 8: { // const_add_pc
                        uint8_t adj2 = 255 - opcode_base; uint32_t addr_inc2 = (adj2 / line_range) * min_inst_len; address += addr_inc2; break; }
                    case 9: { // fixed_advance_pc
                        uint16_t fa = rd_u16b(&b); address += fa; break; }
                    default: { // skip ops with operands we don't consume fully; best effort
                        break; }
                }
            }
        }
        // Finalize end of rows: set end = next start
        for (size_t i = 1; i < lt.nrows; ++i) if (lt.rows[i-1].end == lt.rows[i-1].start) lt.rows[i-1].end = lt.rows[i].start;
        b.p = cu_end;
    }
    *out = lt; return lt.nrows > 0;
}
