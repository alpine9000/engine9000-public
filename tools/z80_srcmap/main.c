/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "list.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct string_list {
    list_t *head;
    list_t *tail;
    char **sorted;
    int count;
} string_list_t;

typedef struct symbol_entry {
    char *name;
    uint16_t addr;
} symbol_entry_t;

typedef struct symbol_list {
    list_t *head;
    list_t *tail;
    int count;
} symbol_list_t;

typedef struct source_line {
    int line;
    char *text;
    char *path;
} source_line_t;

typedef struct source_line_list {
    list_t *head;
    list_t *tail;
    source_line_t **sorted;
    int count;
} source_line_list_t;

typedef struct area_entry {
    char *name;
    int hasDelta;
    uint16_t delta;
} area_entry_t;

typedef struct area_list {
    list_t *head;
    list_t *tail;
    int count;
} area_list_t;

typedef struct map_entry {
    uint16_t addr;
    char *path;
    int line;
} map_entry_t;

typedef struct map_entry_list {
    list_t *head;
    list_t *tail;
    map_entry_t **sorted;
    int count;
} map_entry_list_t;

typedef enum listing_format {
    listing_format_asxxxx,
    listing_format_sjasm
} listing_format_t;

typedef struct args {
    const char *buildDir;
    string_list_t sourceDirs;
    string_list_t listingDirs;
    const char *outPath;
    const char *outNoiPath;
    listing_format_t listingFormat;
} args_t;

static void main_makeParentDirs(const char *path);

static int
main_hasSuffix(const char *s, const char *suffix)
{
    size_t slen;
    size_t suffixLen;

    if (!s || !suffix) {
        return 0;
    }
    slen = strlen(s);
    suffixLen = strlen(suffix);
    if (suffixLen > slen) {
        return 0;
    }
    return strcmp(s + slen - suffixLen, suffix) == 0;
}

static int
main_pathJoin(char *out, size_t cap, const char *a, const char *b)
{
    size_t len;
    size_t bLen;
    int needSlash;

    if (!out || cap == 0 || !a || !b) {
        return 0;
    }
    out[0] = '\0';
    len = strlen(a);
    bLen = strlen(b);
    needSlash = len > 0 && a[len - 1] != '/';
    if (len + (needSlash ? 1u : 0u) + bLen + 1u > cap) {
        return 0;
    }
    memcpy(out, a, len);
    out[len] = '\0';
    if (needSlash) {
        out[len++] = '/';
        out[len] = '\0';
    }
    memcpy(out + len, b, bLen + 1u);
    return 1;
}

static char *
main_realPathDup(const char *path)
{
    char resolved[PATH_MAX];

    if (!path) {
        return NULL;
    }
#ifdef _WIN32
    if (_fullpath(resolved, path, sizeof(resolved))) {
        return strdup(resolved);
    }
#else
    if (realpath(path, resolved)) {
        return strdup(resolved);
    }
#endif
    return strdup(path);
}

static int
main_readLine(FILE *fp, char **line, size_t *cap)
{
    size_t len = 0;
    int ch;

    if (!fp || !line || !cap) {
        return 0;
    }
    if (!*line || *cap == 0) {
        *cap = 256;
        *line = (char *)malloc(*cap);
    }
    while ((ch = fgetc(fp)) != EOF) {
        if (len + 1u >= *cap) {
            size_t nextCap = *cap * 2u;
            char *next = (char *)realloc(*line, nextCap);
            *line = next;
            *cap = nextCap;
        }
        (*line)[len++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (len == 0 && ch == EOF) {
        return 0;
    }
    (*line)[len] = '\0';
    return 1;
}

static void
main_makeDir(const char *path)
{
#ifdef _WIN32
    (void)mkdir(path);
#else
    (void)mkdir(path, 0777);
#endif
}

static void
main_realPathList(string_list_t *list)
{
    if (!list) {
        return;
    }
    free(list->sorted);
    list->sorted = NULL;
    for (list_t *node = list->head; node; node = node->next) {
        char *realPath = main_realPathDup((const char *)node->data);
        free(node->data);
        node->data = realPath;
    }
}

static void
main_stringListAdd(string_list_t *list, const char *s)
{
    char *dup;

    if (!list || !s) {
        return;
    }
    dup = strdup(s);
    list_appendTail(&list->head, &list->tail, dup);
    free(list->sorted);
    list->sorted = NULL;
    list->count++;
}

static void
main_stringListAddUnique(string_list_t *list, const char *s)
{
    if (!list || !s) {
        return;
    }
    for (list_t *node = list->head; node; node = node->next) {
        if (strcmp((const char *)node->data, s) == 0) {
            return;
        }
    }
    main_stringListAdd(list, s);
}

static int
main_stringCompare(const void *a, const void *b)
{
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;

    return strcmp(sa, sb);
}

static void
main_stringListSort(string_list_t *list)
{
    int i = 0;

    if (!list) {
        return;
    }
    free(list->sorted);
    list->sorted = NULL;
    if (list->count <= 0) {
        return;
    }
    list->sorted = (char **)malloc((size_t)list->count * sizeof(*list->sorted));
    for (list_t *node = list->head; node; node = node->next) {
        list->sorted[i++] = (char *)node->data;
    }
    if (list->count > 1) {
        qsort(list->sorted, (size_t)list->count, sizeof(*list->sorted), main_stringCompare);
    }
}

static void
main_stringListFree(string_list_t *list)
{
    if (!list) {
        return;
    }
    list_free(&list->head, 1);
    list->tail = NULL;
    free(list->sorted);
    list->sorted = NULL;
    list->count = 0;
}

static int
main_isDir(const char *path)
{
    struct stat st;

    if (!path || !path[0]) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int
main_isRegular(const char *path)
{
    struct stat st;

    if (!path || !path[0]) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) ? 1 : 0;
}

static void
main_collectRecursive(const char *root, const char *suffixA, const char *suffixB, string_list_t *out)
{
    DIR *dir;
    struct dirent *ent;

    if (!root || !root[0] || !main_isDir(root)) {
        return;
    }
    dir = opendir(root);
    if (!dir) {
        return;
    }
    while ((ent = readdir(dir)) != NULL) {
        char path[PATH_MAX];

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!main_pathJoin(path, sizeof(path), root, ent->d_name)) {
            continue;
        }
        if (main_isDir(path)) {
            main_collectRecursive(path, suffixA, suffixB, out);
        } else if (main_isRegular(path) &&
                   (main_hasSuffix(path, suffixA) ||
                    (suffixB && main_hasSuffix(path, suffixB)))) {
            char *real = main_realPathDup(path);
            main_stringListAdd(out, real);
            free(real);
        }
    }
    closedir(dir);
}

static void
main_collectImmediate(const char *root, const char *suffix, string_list_t *out)
{
    DIR *dir;
    struct dirent *ent;

    if (!root || !root[0] || !main_isDir(root)) {
        return;
    }
    dir = opendir(root);
    if (!dir) {
        return;
    }
    while ((ent = readdir(dir)) != NULL) {
        char path[PATH_MAX];

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (!main_pathJoin(path, sizeof(path), root, ent->d_name)) {
            continue;
        }
        if (main_isRegular(path) && main_hasSuffix(path, suffix)) {
            char *real = main_realPathDup(path);
            main_stringListAdd(out, real);
            free(real);
        }
    }
    closedir(dir);
}

static int
main_pathHasPart(const char *path, const char *part)
{
    const char *p;
    size_t partLen;

    if (!path || !part || !part[0]) {
        return 0;
    }
    partLen = strlen(part);
    p = path;
    while ((p = strstr(p, part)) != NULL) {
        int leftOk = p == path || p[-1] == '/';
        int rightOk = p[partLen] == '\0' || p[partLen] == '/';
        if (leftOk && rightOk) {
            return 1;
        }
        p++;
    }
    return 0;
}

static int
main_isPackListing(const char *buildDir, const char *path)
{
    const char *rel;

    if (!buildDir || !path) {
        return 0;
    }
    rel = path;
    if (strncmp(path, buildDir, strlen(buildDir)) == 0) {
        rel = path + strlen(buildDir);
    }
    return main_pathHasPart(rel, "pack");
}

static int
main_parseHex16(const char *s, uint16_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (!s || !*s || !out) {
        return 0;
    }
    if (s[0] == '$') {
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    errno = 0;
    value = strtoul(s, &end, 16);
    if (errno != 0 || !end || *end != '\0' || value > 0xfffful) {
        return 0;
    }
    *out = (uint16_t)value;
    return 1;
}

static void
main_symbolAdd(symbol_list_t *list, const char *name, uint16_t addr)
{
    symbol_entry_t *entry;

    if (!list || !name || !name[0]) {
        return;
    }
    for (list_t *node = list->head; node; node = node->next) {
        entry = (symbol_entry_t *)node->data;
        if (strcmp(entry->name, name) == 0) {
            return;
        }
    }
    entry = (symbol_entry_t *)malloc(sizeof(*entry));
    entry->name = strdup(name);
    entry->addr = addr;
    list_appendTail(&list->head, &list->tail, entry);
    list->count++;
}

static int
main_symbolFind(const symbol_list_t *list, const char *name, uint16_t *out)
{
    if (!list || !name) {
        return 0;
    }
    for (list_t *node = list->head; node; node = node->next) {
        symbol_entry_t *entry = (symbol_entry_t *)node->data;
        if (strcmp(entry->name, name) == 0) {
            if (out) {
                *out = entry->addr;
            }
            return 1;
        }
    }
    return 0;
}

static void
main_symbolFree(symbol_list_t *list)
{
    if (!list) {
        return;
    }
    for (list_t *node = list->head; node; node = node->next) {
        symbol_entry_t *entry = (symbol_entry_t *)node->data;
        free(entry->name);
    }
    list_free(&list->head, 1);
    list->tail = NULL;
    list->count = 0;
}

static int
main_symbolCompare(const void *a, const void *b)
{
    const symbol_entry_t *ea = *(const symbol_entry_t * const *)a;
    const symbol_entry_t *eb = *(const symbol_entry_t * const *)b;
    int cmp;

    if (ea->addr < eb->addr) {
        return -1;
    }
    if (ea->addr > eb->addr) {
        return 1;
    }
    cmp = strcmp(ea->name, eb->name);
    return cmp;
}

static void
main_writeNoiOutput(const char *path, const symbol_list_t *symbols)
{
    FILE *fp;
    symbol_entry_t **sorted;
    int itemIndex = 0;

    if (!path || !path[0] || !symbols) {
        return;
    }
    main_makeParentDirs(path);
    fp = fopen(path, "w");
    if (!fp) {
        printf("z80_srcmap: failed to create NoICE output\n");
        exit(1);
    }
    sorted = NULL;
    if (symbols->count > 0) {
        sorted = (symbol_entry_t **)malloc((size_t)symbols->count * sizeof(*sorted));
        for (list_t *node = symbols->head; node; node = node->next) {
            sorted[itemIndex++] = (symbol_entry_t *)node->data;
        }
        if (symbols->count > 1) {
            qsort(sorted, (size_t)symbols->count, sizeof(*sorted), main_symbolCompare);
        }
    }
    for (int i = 0; i < symbols->count; i++) {
        fprintf(fp, "DEF %s 0x%04X\n", sorted[i]->name, sorted[i]->addr);
    }
    free(sorted);
    fclose(fp);
}

static void
main_readNoiSymbols(const char *buildDir, const char *skipPath, symbol_list_t *symbols)
{
    string_list_t paths = {0};
    char *skipPathReal = NULL;

    if (skipPath) {
        skipPathReal = main_realPathDup(skipPath);
    }
    main_collectImmediate(buildDir, ".noi", &paths);
    main_stringListSort(&paths);
    for (int i = 0; i < paths.count; i++) {
        FILE *fp;
        char *line = NULL;
        size_t cap = 0;

        if (skipPathReal && strcmp(paths.sorted[i], skipPathReal) == 0) {
            continue;
        }
        fp = fopen(paths.sorted[i], "r");
        if (!fp) {
            continue;
        }
        while (main_readLine(fp, &line, &cap)) {
            char tag[16];
            char name[256];
            char addrText[64];
            uint16_t addr;

            tag[0] = '\0';
            name[0] = '\0';
            addrText[0] = '\0';
            if (sscanf(line, "%15s %255s %63s", tag, name, addrText) == 3 &&
                strcmp(tag, "DEF") == 0 &&
                main_parseHex16(addrText, &addr)) {
                main_symbolAdd(symbols, name, addr);
            }
        }
        free(line);
        fclose(fp);
    }
    free(skipPathReal);
    main_stringListFree(&paths);
}

static char *
main_trimDup(const char *s)
{
    const char *start;
    const char *end;
    size_t len;
    char *out;

    if (!s) {
        return strdup("");
    }
    start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    len = (size_t)(end - start);
    out = (char *)malloc(len + 1u);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static void
main_sourceLineAdd(source_line_list_t *list, int line, const char *text, const char *path)
{
    source_line_t *entry;

    if (!list || !text || !path) {
        return;
    }
    entry = (source_line_t *)malloc(sizeof(*entry));
    entry->line = line;
    entry->text = main_trimDup(text);
    entry->path = strdup(path);
    list_appendTail(&list->head, &list->tail, entry);
    free(list->sorted);
    list->sorted = NULL;
    list->count++;
}

static int
main_sourceLineCompare(const void *a, const void *b)
{
    const source_line_t *la = *(const source_line_t * const *)a;
    const source_line_t *lb = *(const source_line_t * const *)b;
    int cmp;

    if (la->line < lb->line) {
        return -1;
    }
    if (la->line > lb->line) {
        return 1;
    }
    cmp = strcmp(la->text, lb->text);
    if (cmp != 0) {
        return cmp;
    }
    return strcmp(la->path, lb->path);
}

static void
main_sourceLineSort(source_line_list_t *list)
{
    int i = 0;

    if (!list) {
        return;
    }
    free(list->sorted);
    list->sorted = NULL;
    if (list->count <= 0) {
        return;
    }
    list->sorted = (source_line_t **)malloc((size_t)list->count * sizeof(*list->sorted));
    for (list_t *node = list->head; node; node = node->next) {
        list->sorted[i++] = (source_line_t *)node->data;
    }
    if (list->count > 1) {
        qsort(list->sorted, (size_t)list->count, sizeof(*list->sorted), main_sourceLineCompare);
    }
}

static void
main_sourceLineFree(source_line_list_t *list)
{
    if (!list) {
        return;
    }
    for (list_t *node = list->head; node; node = node->next) {
        source_line_t *entry = (source_line_t *)node->data;
        free(entry->text);
        free(entry->path);
    }
    list_free(&list->head, 1);
    list->tail = NULL;
    free(list->sorted);
    list->sorted = NULL;
    list->count = 0;
}

static void
main_buildLineIndex(const string_list_t *sourceFiles, source_line_list_t *lines)
{
    for (int i = 0; i < sourceFiles->count; i++) {
        FILE *fp = fopen(sourceFiles->sorted[i], "r");
        char *line = NULL;
        size_t cap = 0;
        int lineNo = 1;

        if (!fp) {
            continue;
        }
        while (main_readLine(fp, &line, &cap)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            main_sourceLineAdd(lines, lineNo, line, sourceFiles->sorted[i]);
            lineNo++;
        }
        free(line);
        fclose(fp);
    }
    main_sourceLineSort(lines);
}

static void
main_stem(char *out, size_t cap, const char *path)
{
    const char *base;
    const char *dot;
    size_t len;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!path) {
        return;
    }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, base, len);
    out[len] = '\0';
}

static void
main_withSuffix(char *out, size_t cap, const char *path, const char *suffix)
{
    const char *base;
    const char *dot;
    size_t prefixLen;
    size_t suffixLen;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    prefixLen = dot ? (size_t)(dot - base) : strlen(base);
    suffixLen = suffix ? strlen(suffix) : 0;
    if (prefixLen + suffixLen + 1u > cap) {
        return;
    }
    memcpy(out, base, prefixLen);
    memcpy(out + prefixLen, suffix, suffixLen + 1u);
}

static int
main_pathScore(const char *path, const char *listingPath)
{
    char pathStem[PATH_MAX];
    char listingStem[PATH_MAX];
    char listingS[PATH_MAX];
    int score = 0;
    size_t pathStemLen;
    const char *base;

    main_stem(pathStem, sizeof(pathStem), path);
    main_stem(listingStem, sizeof(listingStem), listingPath);
    main_withSuffix(listingS, sizeof(listingS), listingPath, ".s");
    pathStemLen = strlen(pathStem);
    if (strcmp(pathStem, listingStem) == 0) {
        score += 100;
    }
    if (strncmp(listingStem, pathStem, pathStemLen) == 0 && listingStem[pathStemLen] == '-') {
        score += 90;
    }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strcmp(base, listingS) == 0) {
        score += 50;
    }
    if (main_pathHasPart(path, "build") && main_pathHasPart(listingPath, "build")) {
        score += 10;
    }
    return score;
}

static int
main_findCandidates(const source_line_list_t *lines, int line, const char *text, int *first, int *count)
{
    int lo = 0;
    int hi = lines ? lines->count - 1 : -1;
    int found = -1;

    if (first) {
        *first = 0;
    }
    if (count) {
        *count = 0;
    }
    if (!lines || !text) {
        return 0;
    }
    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        source_line_t *entry = lines->sorted[mid];
        int cmp;

        if (entry->line < line) {
            lo = mid + 1;
            continue;
        }
        if (entry->line > line) {
            hi = mid - 1;
            continue;
        }
        cmp = strcmp(entry->text, text);
        if (cmp == 0) {
            found = mid;
            break;
        }
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (found < 0) {
        return 0;
    }
    while (found > 0 &&
           lines->sorted[found - 1]->line == line &&
           strcmp(lines->sorted[found - 1]->text, text) == 0) {
        found--;
    }
    *first = found;
    *count = 0;
    while (found + *count < lines->count &&
           lines->sorted[found + *count]->line == line &&
           strcmp(lines->sorted[found + *count]->text, text) == 0) {
        (*count)++;
    }
    return *count > 0;
}

static const char *
main_chooseSourcePath(const source_line_list_t *lines, int first, int count, const char *listingPath)
{
    const char *bestPath = NULL;
    int bestScore = -1;

    for (int i = 0; i < count; i++) {
        const char *path = lines->sorted[first + i]->path;
        int score = main_pathScore(path, listingPath);
        if (!bestPath || score > bestScore ||
            (score == bestScore && strcmp(path, bestPath) < 0)) {
            bestPath = path;
            bestScore = score;
        }
    }
    return bestPath;
}

static char *
main_parseLabel(const char *sourceText)
{
    char *trimmed = main_trimDup(sourceText);
    char *space;
    size_t len;

    if (!trimmed[0] || trimmed[0] == '.') {
        free(trimmed);
        return NULL;
    }
    space = trimmed;
    while (*space && !isspace((unsigned char)*space)) {
        space++;
    }
    *space = '\0';
    len = strlen(trimmed);
    if (len == 0 || trimmed[len - 1] != ':') {
        free(trimmed);
        return NULL;
    }
    while (len > 0 && trimmed[len - 1] == ':') {
        trimmed[--len] = '\0';
    }
    if (len == 0) {
        free(trimmed);
        return NULL;
    }
    return trimmed;
}

static char *
main_parseArea(const char *sourceText)
{
    char *trimmed = main_trimDup(sourceText);
    char *p;
    char *name;
    char *end;

    if (strncmp(trimmed, ".area", 5) != 0 || !isspace((unsigned char)trimmed[5])) {
        free(trimmed);
        return NULL;
    }
    p = trimmed + 5;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!*p) {
        free(trimmed);
        return NULL;
    }
    name = p;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    end = p;
    *end = '\0';
    name = strdup(name);
    free(trimmed);
    return name;
}

static char *
main_parseAreaFromRawLine(const char *line)
{
    const char *p;

    if (!line) {
        return NULL;
    }
    p = strstr(line, ".area");
    if (!p) {
        return NULL;
    }
    return main_parseArea(p);
}

static area_entry_t *
main_areaGet(area_list_t *areas, const char *name)
{
    area_entry_t *entry;

    if (!areas || !name || !name[0]) {
        return NULL;
    }
    for (list_t *node = areas->head; node; node = node->next) {
        entry = (area_entry_t *)node->data;
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    entry = (area_entry_t *)malloc(sizeof(*entry));
    entry->name = strdup(name);
    entry->hasDelta = 0;
    entry->delta = 0;
    list_appendTail(&areas->head, &areas->tail, entry);
    areas->count++;
    return entry;
}

static void
main_areaApplySourceAttrs(area_entry_t *area, const char *sourceText)
{
    if (!area || !sourceText) {
        return;
    }
    if (strstr(sourceText, "(ABS)")) {
        area->delta = 0;
        area->hasDelta = 1;
    }
}

static void
main_areaFree(area_list_t *areas)
{
    if (!areas) {
        return;
    }
    for (list_t *node = areas->head; node; node = node->next) {
        area_entry_t *entry = (area_entry_t *)node->data;
        free(entry->name);
    }
    list_free(&areas->head, 1);
    areas->tail = NULL;
    areas->count = 0;
}

static void
main_mapEntryAdd(map_entry_list_t *list, uint16_t addr, const char *path, int line)
{
    map_entry_t *entry;

    if (!list || !path || line <= 0) {
        return;
    }
    entry = (map_entry_t *)malloc(sizeof(*entry));
    entry->addr = addr;
    entry->path = strdup(path);
    entry->line = line;
    list_appendTail(&list->head, &list->tail, entry);
    free(list->sorted);
    list->sorted = NULL;
    list->count++;
}

static int
main_mapEntryCompare(const void *a, const void *b)
{
    const map_entry_t *ea = *(const map_entry_t * const *)a;
    const map_entry_t *eb = *(const map_entry_t * const *)b;
    int cmp;

    if (ea->addr < eb->addr) {
        return -1;
    }
    if (ea->addr > eb->addr) {
        return 1;
    }
    cmp = strcmp(ea->path, eb->path);
    if (cmp != 0) {
        return cmp;
    }
    if (ea->line < eb->line) {
        return -1;
    }
    if (ea->line > eb->line) {
        return 1;
    }
    return 0;
}

static int
main_mapEntrySame(const map_entry_t *a, const map_entry_t *b)
{
    return a->addr == b->addr && a->line == b->line && strcmp(a->path, b->path) == 0;
}

static void
main_mapEntryFree(map_entry_list_t *list)
{
    if (!list) {
        return;
    }
    for (list_t *node = list->head; node; node = node->next) {
        map_entry_t *entry = (map_entry_t *)node->data;
        free(entry->path);
    }
    list_free(&list->head, 1);
    list->tail = NULL;
    free(list->sorted);
    list->sorted = NULL;
    list->count = 0;
}

static int
main_isHexChar(char c)
{
    return isxdigit((unsigned char)c) ? 1 : 0;
}

static int
main_isTwoHexToken(const char *start, const char *end)
{
    return end - start == 2 && main_isHexChar(start[0]) && main_isHexChar(start[1]);
}

static int
main_parseListingLine(char *line, uint16_t *outAddr, int *outEmitted, int *outLine, char **outSource)
{
    char *p = line;
    char *addrStart;
    char *addrEnd;
    char saved;
    int spaces;
    int emitted = 0;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    addrStart = p;
    for (int i = 0; i < 6; i++) {
        if (!main_isHexChar(p[i])) {
            return 0;
        }
    }
    p += 6;
    addrEnd = p;
    if (*p && !isspace((unsigned char)*p)) {
        return 0;
    }
    saved = *addrEnd;
    *addrEnd = '\0';
    if (!main_parseHex16(addrStart, outAddr)) {
        *addrEnd = saved;
        return 0;
    }
    *addrEnd = saved;

    spaces = 0;
    while (*p && isspace((unsigned char)*p)) {
        spaces++;
        p++;
    }
    if (spaces <= 3) {
        while (*p) {
            char *tokStart = p;
            char *tokEnd;

            while (*p && !isspace((unsigned char)*p)) {
                p++;
            }
            tokEnd = p;
            if (!main_isTwoHexToken(tokStart, tokEnd)) {
                p = tokStart;
                break;
            }
            emitted = 1;
            spaces = 0;
            while (*p && isspace((unsigned char)*p)) {
                spaces++;
                p++;
            }
            if (spaces > 3) {
                break;
            }
        }
    }

    if (*p == '[') {
        while (*p && *p != ']') {
            p++;
        }
        if (*p == ']') {
            p++;
        }
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    *outLine = (int)strtol(p, &p, 10);
    if (*outLine <= 0) {
        return 0;
    }
    if (*p && isspace((unsigned char)*p)) {
        p++;
    }
    *outEmitted = emitted;
    *outSource = p;
    return 1;
}

static void
main_parseListing(const char *path,
                      const symbol_list_t *symbols,
                      const source_line_list_t *lineIndex,
                      map_entry_list_t *out)
{
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    area_list_t areas = {0};
    area_entry_t *currentArea;
    size_t lineLen;

    fp = fopen(path, "r");
    if (!fp) {
        return;
    }
    currentArea = main_areaGet(&areas, "CODE");
    while (main_readLine(fp, &line, &cap)) {
        uint16_t localAddr;
        int emitted;
        int lineNo;
        char *sourceText;
        char *area;
        char *label;

        lineLen = strlen(line);
        while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
            line[--lineLen] = '\0';
        }
        if (!main_parseListingLine(line, &localAddr, &emitted, &lineNo, &sourceText)) {
            area = main_parseAreaFromRawLine(line);
            if (area) {
                currentArea = main_areaGet(&areas, area);
                main_areaApplySourceAttrs(currentArea, line);
                free(area);
            }
            continue;
        }

        area = main_parseArea(sourceText);
        if (area) {
            currentArea = main_areaGet(&areas, area);
            main_areaApplySourceAttrs(currentArea, sourceText);
            free(area);
            continue;
        }

        label = main_parseLabel(sourceText);
        if (!label && !emitted) {
            continue;
        }
        if (label) {
            uint16_t symbolAddr;
            if (main_symbolFind(symbols, label, &symbolAddr) && currentArea) {
                currentArea->delta = (uint16_t)((symbolAddr - localAddr) & 0xffffu);
                currentArea->hasDelta = 1;
            }
            free(label);
        }

        if (emitted && currentArea && currentArea->hasDelta) {
            char *trimmed = main_trimDup(sourceText);
            int first;
            int count;

            if (main_findCandidates(lineIndex, lineNo, trimmed, &first, &count)) {
                const char *sourcePath = main_chooseSourcePath(lineIndex, first, count, path);
                if (sourcePath) {
                    uint16_t runtimeAddr = (uint16_t)((localAddr + currentArea->delta) & 0xffffu);
                    main_mapEntryAdd(out, runtimeAddr, sourcePath, lineNo);
                }
            }
            free(trimmed);
        }
    }
    free(line);
    fclose(fp);
    main_areaFree(&areas);
}

static const char *
main_stripAsmComment(char *s)
{
    int inString = 0;
    char quote = '\0';

    if (!s) {
        return "";
    }
    for (char *p = s; *p; p++) {
        if (inString) {
            if (*p == quote) {
                inString = 0;
            }
        } else if (*p == '\'' || *p == '"') {
            inString = 1;
            quote = *p;
        } else if (*p == ';') {
            *p = '\0';
            break;
        }
    }
    return s;
}

static int
main_firstAsmToken(char *out, size_t cap, const char *sourceText)
{
    char *trimmed;
    char *p;
    size_t len;

    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    trimmed = main_trimDup(sourceText);
    main_stripAsmComment(trimmed);
    p = trimmed;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    *p = '\0';
    len = strlen(trimmed);
    while (len > 0 && trimmed[len - 1] == ':') {
        trimmed[--len] = '\0';
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, trimmed, len);
    out[len] = '\0';
    free(trimmed);
    return out[0] != '\0';
}

static int
main_stringEqualsAny(const char *s, const char * const *items, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(s, items[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
main_asciiCaseCmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ac = toupper((unsigned char)*a);
        int bc = toupper((unsigned char)*b);

        if (ac != bc) {
            return ac - bc;
        }
        a++;
        b++;
    }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

static int
main_isHexOnly(const char *s)
{
    if (!s || !s[0]) {
        return 0;
    }
    for (const char *p = s; *p; p++) {
        if (!main_isHexChar(*p)) {
            return 0;
        }
    }
    return 1;
}

static int
main_isSymbolName(const char *s)
{
    if (!s || !s[0]) {
        return 0;
    }
    if (!isalpha((unsigned char)s[0]) && s[0] != '_' && s[0] != '.') {
        return 0;
    }
    for (const char *p = s + 1; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '.') {
            return 0;
        }
    }
    return 1;
}

static int
main_secondAsmToken(char *out, size_t cap, const char *sourceText)
{
    char *trimmed;
    char *p;
    size_t len;

    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    trimmed = main_trimDup(sourceText);
    main_stripAsmComment(trimmed);
    p = trimmed;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    len = 0;
    while (p[len] && !isspace((unsigned char)p[len])) {
        len++;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    free(trimmed);
    return out[0] != '\0';
}

static int
main_isSjasmEqu(const char *sourceText)
{
    char token[256];

    return main_secondAsmToken(token, sizeof(token), sourceText) &&
        main_asciiCaseCmp(token, "EQU") == 0;
}

static int
main_parseSjasmLabel(const char *sourceText, char *out, size_t cap)
{
    static const char * const directives[] = {
        "ALIGN", "ASSERT", "BINARY", "BLOCK", "BYTE", "CODE", "DB", "DD",
        "DEFINE", "DEFB", "DEFS", "DEFW", "DEPHASE", "DEVICE", "DISP",
        "DISPLAY", "DM", "DS", "DWORD", "ELSE", "EMPTYTAP", "ENDIF",
        "ENDLUA", "ENDM", "ENDMODULE", "ENDR", "ENDSLUA", "ENDSTRUCT",
        "ENT", "EQU", "EXPORT", "FIELD", "IF", "IFDEF", "IFNDEF",
        "IFUSED", "INCBIN", "INCHOB", "INCLUDE", "INCLUDELUA", "INSERT",
        "LABELSLIST", "LUA", "MACRO", "MAP", "MEMORYMAP", "MMU", "MODULE",
        "ORG", "OUTPUT", "PAGE", "PHASE", "REPT", "SAVEBIN", "SAVEBLOCK",
        "SAVECDT", "SAVEDEV", "SAVENEX", "SAVESNA", "SAVESNAPSHOT",
        "SAVETAP", "SAVEWAV", "SLUA", "STRUCT", "TAPEND", "TAPOUT",
        "TEXT", "UNDEFINE", "WORD"
    };
    static const char * const opcodes[] = {
        "ADC", "ADD", "AND", "BIT", "CALL", "CCF", "CP", "CPD", "CPDR", "CPI",
        "CPIR", "CPL", "DAA", "DB", "DEC", "DEFB", "DEFM", "DEFS", "DEFW",
        "DI", "DJNZ", "DM", "DS", "DW", "EI", "EX", "EXX", "HALT", "IM",
        "IN", "INC", "IND", "INDR", "INI", "INIR", "JP", "JR", "LD", "LDD",
        "LDDR", "LDI", "LDIR", "NEG", "NOP", "OR", "OTDR", "OTIR", "OUT",
        "OUTD", "OUTI", "POP", "PUSH", "RES", "RET", "RETI", "RETN", "RL",
        "RLA", "RLC", "RLCA", "RLD", "RR", "RRA", "RRC", "RRCA", "RRD",
        "RST", "SBC", "SCF", "SET", "SLA", "SLL", "SRA", "SRL", "SUB", "XOR"
    };
    char token[256];
    char upper[256];
    size_t len;

    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!sourceText || !sourceText[0] || isspace((unsigned char)sourceText[0])) {
        return 0;
    }
    if (!main_firstAsmToken(token, sizeof(token), sourceText)) {
        return 0;
    }
    if (!main_isSymbolName(token) || main_isHexOnly(token)) {
        return 0;
    }
    len = strlen(token);
    if (len >= sizeof(upper)) {
        len = sizeof(upper) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        upper[i] = (char)toupper((unsigned char)token[i]);
    }
    upper[len] = '\0';
    if (main_stringEqualsAny(upper, directives, sizeof(directives) / sizeof(directives[0])) ||
        main_stringEqualsAny(upper, opcodes, sizeof(opcodes) / sizeof(opcodes[0]))) {
        return 0;
    }
    if (main_isSjasmEqu(sourceText)) {
        return 0;
    }
    if (strlen(token) + 1u > cap) {
        return 0;
    }
    strcpy(out, token);
    return 1;
}

static int
main_parseSjasmListingLine(char *line,
                           uint16_t *outAddr,
                           int *outEmitted,
                           int *outMacroExpanded,
                           int *outIncludedSource,
                           int *outLine,
                           char **outSource)
{
    char lineText[16];
    char addrText[16];
    int byteStart = 12;
    int byteEnd = 36;
    int sourceCol;
    int emitted = 0;
    size_t len;

    len = strlen(line);
    if (len < 11) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char)line[i])) {
            return 0;
        }
    }
    if (!isspace((unsigned char)line[4]) && line[4] != '+') {
        return 0;
    }
    for (int i = 7; i < 11; i++) {
        if (!main_isHexChar(line[i])) {
            return 0;
        }
    }
    memcpy(lineText, line, 4);
    lineText[4] = '\0';
    memcpy(addrText, line + 7, 4);
    addrText[4] = '\0';
    if (!main_parseHex16(addrText, outAddr)) {
        return 0;
    }
    *outLine = atoi(lineText);
    if (*outLine <= 0) {
        return 0;
    }
    if (len > (size_t)byteStart) {
        char *p = line + byteStart;
        while ((int)(p - line) < byteEnd && *p && isspace((unsigned char)*p)) {
            p++;
        }
        emitted = (int)(p - line) < byteEnd && main_isHexChar(p[0]) && main_isHexChar(p[1]) &&
            (!p[2] || isspace((unsigned char)p[2]));
    }
    sourceCol = emitted ? 36 : 24;
    if (sourceCol > (int)len) {
        sourceCol = (int)len;
    }
    *outEmitted = emitted;
    *outMacroExpanded = 0;
    *outIncludedSource = (line[4] == '+' || line[11] == '~') ? 1 : 0;
    for (int i = byteStart; i < byteEnd && i < (int)len; i++) {
        if (line[i] == '>') {
            *outMacroExpanded = 1;
            break;
        }
    }
    *outSource = line + sourceCol;
    return 1;
}

static const char *
main_baseName(const char *path)
{
    const char *base;

    if (!path) {
        return "";
    }
    base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static int
main_pathMatchesIncludeName(const char *path, const char *includeName)
{
    const char *pathBase;
    const char *includeBase;

    if (!path || !includeName || !includeName[0]) {
        return 0;
    }
    pathBase = main_baseName(path);
    includeBase = main_baseName(includeName);
    return strcmp(pathBase, includeBase) == 0;
}

static int
main_parseSjasmIncludeName(const char *sourceText, char *out, size_t cap)
{
    char token[256];
    char *trimmed;
    char *p;
    char quote;
    size_t len;

    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!main_firstAsmToken(token, sizeof(token), sourceText) ||
        main_asciiCaseCmp(token, "INCLUDE") != 0) {
        return 0;
    }
    trimmed = main_trimDup(sourceText);
    main_stripAsmComment(trimmed);
    p = trimmed;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    quote = '\0';
    if (*p == '"' || *p == '\'') {
        quote = *p++;
    }
    len = 0;
    while (p[len] && ((quote && p[len] != quote) || (!quote && !isspace((unsigned char)p[len])))) {
        len++;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    free(trimmed);
    return out[0] != '\0';
}

static const char *
main_findSourcePathForInclude(const source_line_list_t *lines, const char *includeName, const char *listingPath)
{
    const char *bestPath = NULL;
    int bestScore = -1;

    if (!lines || !includeName || !includeName[0]) {
        return NULL;
    }
    for (int i = 0; i < lines->count; i++) {
        const char *path = lines->sorted[i]->path;
        int score;

        if (!main_pathMatchesIncludeName(path, includeName)) {
            continue;
        }
        score = main_pathScore(path, listingPath);
        if (!bestPath || score > bestScore ||
            (score == bestScore && strcmp(path, bestPath) < 0)) {
            bestPath = path;
            bestScore = score;
        }
    }
    return bestPath;
}

static const source_line_t *
main_chooseSourceLineFromCandidates(const source_line_list_t *lines,
                                    int first,
                                    int count,
                                    const char *preferredPath,
                                    const char *preferredIncludeName,
                                    const char *listingPath)
{
    const source_line_t *best = NULL;
    int bestScore = -1;

    if (!lines) {
        return NULL;
    }
    if (preferredIncludeName && preferredIncludeName[0]) {
        for (int i = 0; i < count; i++) {
            source_line_t *entry = lines->sorted[first + i];

            if (main_pathMatchesIncludeName(entry->path, preferredIncludeName)) {
                return entry;
            }
        }
    }
    if (preferredPath) {
        for (int i = 0; i < count; i++) {
            source_line_t *entry = lines->sorted[first + i];

            if (strcmp(entry->path, preferredPath) == 0) {
                return entry;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        source_line_t *entry = lines->sorted[first + i];
        int score = main_pathScore(entry->path, listingPath);

        if (!best || score > bestScore ||
            (score == bestScore && strcmp(entry->path, best->path) < 0) ||
            (score == bestScore && strcmp(entry->path, best->path) == 0 && entry->line < best->line)) {
            best = entry;
            bestScore = score;
        }
    }
    return best;
}

static const source_line_t *
main_chooseSourceLineByText(const source_line_list_t *lines, const char *text, const char *listingPath)
{
    const source_line_t *best = NULL;
    int bestScore = -1;

    if (!lines || !text) {
        return NULL;
    }
    for (list_t *node = lines->head; node; node = node->next) {
        source_line_t *entry = (source_line_t *)node->data;
        int score;

        if (strcmp(entry->text, text) != 0) {
            continue;
        }
        score = main_pathScore(entry->path, listingPath);
        if (!best || score > bestScore ||
            (score == bestScore && strcmp(entry->path, best->path) < 0) ||
            (score == bestScore && strcmp(entry->path, best->path) == 0 && entry->line < best->line)) {
            best = entry;
            bestScore = score;
        }
    }
    return best;
}

static const source_line_t *
main_chooseMacroSourceLineByText(const source_line_list_t *lines, const char *text, const char *listingPath)
{
    const source_line_t *best = NULL;
    char listingStem[PATH_MAX];
    int bestScore = -1;

    if (!lines || !text) {
        return NULL;
    }
    main_stem(listingStem, sizeof(listingStem), listingPath);
    for (list_t *node = lines->head; node; node = node->next) {
        source_line_t *entry = (source_line_t *)node->data;
        char pathStem[PATH_MAX];
        int score;

        if (strcmp(entry->text, text) != 0) {
            continue;
        }
        main_stem(pathStem, sizeof(pathStem), entry->path);
        score = main_pathScore(entry->path, listingPath);
        if (strcmp(pathStem, listingStem) != 0) {
            score += 1000;
        }
        if (main_hasSuffix(entry->path, ".i80") || main_hasSuffix(entry->path, ".inc")) {
            score += 100;
        }
        if (!best || score > bestScore ||
            (score == bestScore && strcmp(entry->path, best->path) < 0) ||
            (score == bestScore && strcmp(entry->path, best->path) == 0 && entry->line < best->line)) {
            best = entry;
            bestScore = score;
        }
    }
    return best;
}

static void
main_parseSjasmListing(const char *path,
                       symbol_list_t *symbols,
                       const source_line_list_t *lineIndex,
                       map_entry_list_t *out)
{
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    size_t lineLen;
    source_line_list_t macroLines = {0};
    const char *currentIncludedPath = NULL;
    char currentIncludeName[PATH_MAX] = "";

    fp = fopen(path, "r");
    if (!fp) {
        return;
    }
    while (main_readLine(fp, &line, &cap)) {
        uint16_t addr;
        int emitted;
        int macroExpanded;
        int includedSource;
        int lineNo;
        char *sourceText;
        char label[256];
        char *trimmed;
        int first;
        int count;
        char includeName[PATH_MAX];
        const source_line_t *sourceLine = NULL;

        lineLen = strlen(line);
        while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
            line[--lineLen] = '\0';
        }
        if (!main_parseSjasmListingLine(line, &addr, &emitted, &macroExpanded, &includedSource, &lineNo, &sourceText)) {
            continue;
        }
        if (!includedSource && main_parseSjasmIncludeName(sourceText, includeName, sizeof(includeName))) {
            strcpy(currentIncludeName, includeName);
            currentIncludedPath = main_findSourcePathForInclude(lineIndex, includeName, path);
        }
        if (main_parseSjasmLabel(sourceText, label, sizeof(label))) {
            main_symbolAdd(symbols, label, addr);
        }
        trimmed = main_trimDup(sourceText);
        if (includedSource && currentIncludedPath &&
            main_findCandidates(lineIndex, lineNo, trimmed, &first, &count)) {
            sourceLine = main_chooseSourceLineFromCandidates(lineIndex,
                                                            first,
                                                            count,
                                                            currentIncludedPath,
                                                            currentIncludeName,
                                                            path);
            if (sourceLine) {
                main_sourceLineAdd(&macroLines, sourceLine->line, sourceLine->text, sourceLine->path);
            }
        }
        if (emitted) {
            if (macroExpanded) {
                sourceLine = main_chooseSourceLineByText(&macroLines, trimmed, path);
            }
            if (!sourceLine && main_findCandidates(lineIndex, lineNo, trimmed, &first, &count)) {
                sourceLine = main_chooseSourceLineFromCandidates(lineIndex,
                                                                first,
                                                                count,
                                                                includedSource ? currentIncludedPath : NULL,
                                                                includedSource ? currentIncludeName : NULL,
                                                                path);
            }
            if (!sourceLine && macroExpanded) {
                sourceLine = main_chooseMacroSourceLineByText(lineIndex, trimmed, path);
            }
            if (sourceLine) {
                main_mapEntryAdd(out, addr, sourceLine->path, sourceLine->line);
            }
        }
        free(trimmed);
    }
    free(line);
    fclose(fp);
    main_sourceLineFree(&macroLines);
}

static void
main_collectSourceFiles(const args_t *args, string_list_t *out)
{
    for (int i = 0; i < args->sourceDirs.count; i++) {
        main_collectRecursive(args->sourceDirs.sorted[i], ".s", ".inc", out);
        main_collectRecursive(args->sourceDirs.sorted[i], ".s80", ".i80", out);
    }
    main_collectRecursive(args->buildDir, ".s", ".inc", out);
    main_collectRecursive(args->buildDir, ".s80", ".i80", out);
    main_stringListSort(out);
}

static void
main_collectListingFiles(const args_t *args, string_list_t *out)
{
    string_list_t buildListings = {0};

    main_collectRecursive(args->buildDir, ".lst", NULL, &buildListings);
    main_stringListSort(&buildListings);
    for (int i = 0; i < buildListings.count; i++) {
        if (!main_isPackListing(args->buildDir, buildListings.sorted[i])) {
            main_stringListAdd(out, buildListings.sorted[i]);
        }
    }
    main_stringListFree(&buildListings);
    for (int i = 0; i < args->sourceDirs.count; i++) {
        main_collectImmediate(args->sourceDirs.sorted[i], ".lst", out);
    }
    for (int i = 0; i < args->listingDirs.count; i++) {
        main_collectRecursive(args->listingDirs.sorted[i], ".lst", NULL, out);
    }
    main_stringListSort(out);
}

static void
main_makeParentDirs(const char *path)
{
    char tmp[PATH_MAX];
    size_t len;

    if (!path) {
        return;
    }
    len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return;
    }
    memcpy(tmp, path, len + 1u);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            main_makeDir(tmp);
            *p = '/';
        }
    }
}

static void
main_writeOutput(const char *path, map_entry_list_t *entries)
{
    FILE *fp;
    int itemIndex = 0;

    free(entries->sorted);
    entries->sorted = NULL;
    if (entries->count > 0) {
        entries->sorted = (map_entry_t **)malloc((size_t)entries->count * sizeof(*entries->sorted));
        for (list_t *node = entries->head; node; node = node->next) {
            entries->sorted[itemIndex++] = (map_entry_t *)node->data;
        }
        if (entries->count > 1) {
            qsort(entries->sorted, (size_t)entries->count, sizeof(*entries->sorted), main_mapEntryCompare);
        }
    }
    main_makeParentDirs(path);
    fp = fopen(path, "w");
    if (!fp) {
        printf("z80_srcmap: failed to create output\n");
        exit(1);
    }
    fprintf(fp, "# engine9000 z80 source map v1\n");
    for (int i = 0; i < entries->count; i++) {
        if (i > 0 && main_mapEntrySame(entries->sorted[i - 1], entries->sorted[i])) {
            continue;
        }
        fprintf(fp, "%04X\t%s\t%d\n", entries->sorted[i]->addr, entries->sorted[i]->path, entries->sorted[i]->line);
    }
    fclose(fp);
}

static int
main_parseArgs(int argc, char **argv, args_t *args)
{
    memset(args, 0, sizeof(*args));
    args->listingFormat = listing_format_asxxxx;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--build-dir") == 0 && i + 1 < argc) {
            args->buildDir = argv[++i];
        } else if (strncmp(arg, "--build-dir=", 12) == 0) {
            args->buildDir = arg + 12;
        } else if (strcmp(arg, "--source-dir") == 0 && i + 1 < argc) {
            main_stringListAddUnique(&args->sourceDirs, argv[++i]);
        } else if (strncmp(arg, "--source-dir=", 13) == 0) {
            main_stringListAddUnique(&args->sourceDirs, arg + 13);
        } else if (strcmp(arg, "--listing-dir") == 0 && i + 1 < argc) {
            main_stringListAddUnique(&args->listingDirs, argv[++i]);
        } else if (strncmp(arg, "--listing-dir=", 14) == 0) {
            main_stringListAddUnique(&args->listingDirs, arg + 14);
        } else if (strcmp(arg, "--out") == 0 && i + 1 < argc) {
            args->outPath = argv[++i];
        } else if (strncmp(arg, "--out=", 6) == 0) {
            args->outPath = arg + 6;
        } else if (strcmp(arg, "--out-noi") == 0 && i + 1 < argc) {
            args->outNoiPath = argv[++i];
        } else if (strncmp(arg, "--out-noi=", 10) == 0) {
            args->outNoiPath = arg + 10;
        } else if ((strcmp(arg, "--listing-format") == 0 || strcmp(arg, "--format") == 0) && i + 1 < argc) {
            const char *format = argv[++i];
            if (strcmp(format, "asxxxx") == 0) {
                args->listingFormat = listing_format_asxxxx;
            } else if (strcmp(format, "sjasm") == 0) {
                args->listingFormat = listing_format_sjasm;
            } else {
                return 0;
            }
        } else if (strncmp(arg, "--listing-format=", 17) == 0 || strncmp(arg, "--format=", 9) == 0) {
            const char *format = strchr(arg, '=') + 1;
            if (strcmp(format, "asxxxx") == 0) {
                args->listingFormat = listing_format_asxxxx;
            } else if (strcmp(format, "sjasm") == 0) {
                args->listingFormat = listing_format_sjasm;
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return args->buildDir && args->sourceDirs.count > 0 && args->outPath;
}

int
main(int argc, char **argv)
{
    args_t args;
    symbol_list_t symbols = {0};
    string_list_t sourceFiles = {0};
    string_list_t listingFiles = {0};
    source_line_list_t lineIndex = {0};
    map_entry_list_t entries = {0};
    char *buildDirReal;

    if (!main_parseArgs(argc, argv, &args)) {
        printf("usage: z80_srcmap --build-dir DIR --source-dir DIR [--source-dir DIR ...] [--listing-dir DIR ...] [--listing-format asxxxx|sjasm] --out FILE [--out-noi FILE]\n");
        main_stringListFree(&args.sourceDirs);
        main_stringListFree(&args.listingDirs);
        return 1;
    }

    buildDirReal = main_realPathDup(args.buildDir);
    args.buildDir = buildDirReal;
    main_realPathList(&args.sourceDirs);
    main_realPathList(&args.listingDirs);
    main_stringListSort(&args.sourceDirs);
    main_stringListSort(&args.listingDirs);

    main_readNoiSymbols(args.buildDir, args.outNoiPath, &symbols);
    main_collectSourceFiles(&args, &sourceFiles);
    main_buildLineIndex(&sourceFiles, &lineIndex);
    main_collectListingFiles(&args, &listingFiles);
    for (int i = 0; i < listingFiles.count; i++) {
        if (args.listingFormat == listing_format_sjasm) {
            main_parseSjasmListing(listingFiles.sorted[i], &symbols, &lineIndex, &entries);
        } else {
            main_parseListing(listingFiles.sorted[i], &symbols, &lineIndex, &entries);
        }
    }
    main_writeOutput(args.outPath, &entries);
    if (args.outNoiPath) {
        main_writeNoiOutput(args.outNoiPath, &symbols);
    }

    main_mapEntryFree(&entries);
    main_sourceLineFree(&lineIndex);
    main_stringListFree(&listingFiles);
    main_stringListFree(&sourceFiles);
    main_symbolFree(&symbols);
    main_stringListFree(&args.listingDirs);
    main_stringListFree(&args.sourceDirs);
    free(buildDirReal);
    return 0;
}
