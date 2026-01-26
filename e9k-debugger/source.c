/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct source_cache_entry {
    char  *path;
    char **lines;      // array of strdup'd lines (no trailing newlines)
    int    line_count; // number of lines
} source_cache_entry_t;

static struct {
    source_cache_entry_t *entries;
    int count;
    int cap;
    int inited;
} g_source;

static void
source_ensureInit(void)
{
    if (!g_source.inited) {
        g_source.entries = NULL;
        g_source.count = 0;
        g_source.cap = 0;
        g_source.inited = 1;
    }
}

void
source_init(void)
{
    source_ensureInit();
}

static void
source_freeEntry(source_cache_entry_t *e)
{
    if (!e) return;
    if (e->path) alloc_free(e->path);
    if (e->lines) {
        for (int i=0; i<e->line_count; ++i) alloc_free(e->lines[i]);
        alloc_free(e->lines);
    }
    e->path = NULL; e->lines = NULL; e->line_count = 0;
}

void
source_shutdown(void)
{
    if (!g_source.inited) return;
    for (int i=0; i<g_source.count; ++i) source_freeEntry(&g_source.entries[i]);
    alloc_free(g_source.entries);
    g_source.entries = NULL; g_source.count = g_source.cap = 0; g_source.inited = 0;
}

static int
source_readAll(const char *path, char **out_buf, size_t *out_len)
{
    if (!path || !out_buf || !out_len) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f); if (sz < 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    char *buf = (char*)alloc_alloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    *out_buf = buf; *out_len = rd;
    return 1;
}

static int
source_splitLines(char *buf, size_t len, char ***out_lines, int *out_count)
{
    if (!buf || !out_lines || !out_count) return 0;
    int cap = 64; int n = 0; char **lines = (char**)alloc_alloc((size_t)cap * sizeof(char*));
    if (!lines) return 0;
    size_t i = 0; size_t start = 0;
    while (i < len) {
        if (buf[i] == '\n') {
            size_t l = (i > start && buf[i-1] == '\r') ? (i - start - 1) : (i - start);
            char *line = (char*)alloc_alloc(l + 1);
            if (!line) { /* cleanup */ for (int k=0;k<n;k++) alloc_free(lines[k]); alloc_free(lines); return 0; }
            if (l > 0) memcpy(line, buf + start, l);
            line[l] = '\0';
            if (n == cap) { cap = cap ? cap * 2 : 64; char **tmp = (char**)alloc_realloc(lines, (size_t)cap * sizeof(char*)); if (!tmp) { for (int k=0;k<n;k++) alloc_free(lines[k]); alloc_free(lines); return 0; } lines = tmp; }
            lines[n++] = line;
            i++; start = i; continue;
        }
        i++;
    }
    // last line (even if empty) if file does not end with newline
    if (start <= len) {
        size_t l = len - start;
        // trim possible trailing CR if present (e.g., last line ended with CR only)
        if (l > 0 && buf[start + l - 1] == '\r') l--;
        char *line = (char*)alloc_alloc(l + 1);
        if (!line) { for (int k=0;k<n;k++) alloc_free(lines[k]); alloc_free(lines); return 0; }
        if (l > 0) memcpy(line, buf + start, l);
        line[l] = '\0';
        if (n == cap) { cap = cap ? cap * 2 : 64; char **tmp = (char**)alloc_realloc(lines, (size_t)cap * sizeof(char*)); if (!tmp) { for (int k=0;k<n;k++) alloc_free(lines[k]); alloc_free(lines); return 0; } lines = tmp; }
        lines[n++] = line;
    }
    *out_lines = lines; *out_count = n; return 1;
}

static source_cache_entry_t*
source_load(const char *filename)
{
    char *buf = NULL; size_t len = 0;
    if (!source_readAll(filename, &buf, &len)) return NULL;
    char **lines = NULL; int count = 0;
    int ok = source_splitLines(buf, len, &lines, &count);
    alloc_free(buf);
    if (!ok) return NULL;
    if (g_source.count == g_source.cap) {
        int nc = g_source.cap ? g_source.cap * 2 : 8;
        source_cache_entry_t *tmp = (source_cache_entry_t*)alloc_realloc(g_source.entries, (size_t)nc * sizeof(source_cache_entry_t));
        if (!tmp) { for (int i=0;i<count;i++) alloc_free(lines[i]); alloc_free(lines); return NULL; }
        g_source.entries = tmp; g_source.cap = nc;
    }
    source_cache_entry_t *e = &g_source.entries[g_source.count++];
    e->path = alloc_strdup(filename);
    e->lines = lines;
    e->line_count = count;
    return e;
}

static source_cache_entry_t*
source_find(const char *filename)
{
    for (int i=0;i<g_source.count;i++) {
        if (g_source.entries[i].path && strcmp(g_source.entries[i].path, filename) == 0) return &g_source.entries[i];
    }
    return NULL;
}

int
source_getTotalLines(const char *filename)
{
    if (!filename || !*filename) return 0;
    source_ensureInit();
    source_cache_entry_t *e = source_find(filename);
    if (!e) e = source_load(filename);
    if (!e) return 0;
    return e->line_count;
}

int
source_getRange(const char *filename, int start_line, int end_line,
                const char ***out_lines, int *out_count, int *out_first, int *out_total)
{
    if (!filename || !*filename || !out_lines || !out_count || !out_first) return 0;
    source_ensureInit();
    source_cache_entry_t *e = source_find(filename);
    if (!e) e = source_load(filename);
    if (!e) return 0;
    int total = e->line_count;
    if (out_total) *out_total = total;
    if (total <= 0) { *out_lines = NULL; *out_count = 0; *out_first = 0; return 1; }
    if (start_line < 1) start_line = 1;
    if (end_line < start_line) end_line = start_line;
    if (start_line > total) start_line = total;
    if (end_line > total) end_line = total;
    int first = start_line - 1; // 0-based index
    int count = end_line - start_line + 1;
    *out_lines = (const char**)(e->lines + first);
    *out_count = count;
    *out_first = start_line;
    return 1;
}

