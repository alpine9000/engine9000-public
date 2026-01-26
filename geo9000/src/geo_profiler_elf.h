#ifndef GEO_PROFILER_ELF_H
#define GEO_PROFILER_ELF_H

#include <stdint.h>
#include <stddef.h>

// Compact line table structures used by the profiler
typedef struct LineRow { uint32_t start, end; uint32_t file; uint32_t line; } LineRow;
typedef struct LineTable { char **dirs; size_t ndirs; char **files; uint32_t *file_dir; size_t nfiles; LineRow *rows; size_t nrows; } LineTable;

// Load DWARF .debug_line into a LineTable from an ELF file. Returns non-zero on success.
int geo_elf_load_line_table(const char *elf_path, LineTable *out);

// Free resources owned by a LineTable
void geo_elf_free_line_table(LineTable *lt);

// Address lookup helper (binary search). Returns row if addr in [start,end), else NULL
static inline const LineRow* geo_line_find_row_addr(const LineRow *rows, size_t nrows, uint32_t addr) {
    size_t lo=0, hi=nrows;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        if (rows[mid].start <= addr) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return NULL;
    const LineRow *r = &rows[lo - 1];
    if (addr >= r->start && addr < r->end) return r;
    return NULL;
}

#endif // GEO_PROFILER_ELF_H

