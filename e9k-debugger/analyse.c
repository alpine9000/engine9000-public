/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "analyse.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"

#define ANALYSE_MAP_INITIAL_CAP 1024

typedef struct {
    unsigned int pc;
    unsigned long long samples;
    unsigned long long cycles;
    unsigned long long lastSamples;
    unsigned long long lastCycles;
    int used;
} analyse_profile_entry;

typedef struct {
    char *function;
    char *file;
    int line;
    char *loc;
} analyse_frame;

typedef struct {
    char *file;
    int line;
    unsigned long long cycles;
    unsigned long long count;
    char address[16];
    char *source;
    char *chain;
    analyse_frame *frames;
    size_t frameCount;
    unsigned long long bestCycles;
    unsigned long long bestSamples;
} analyse_line_entry;

typedef struct {
    char address[16];
    unsigned long long samples;
    unsigned long long cycles;
    analyse_frame *frames;
    size_t frameCount;
    char *chain;
    char *source;
    const char *topFile;
    int topLine;
} analyse_resolved_entry;

typedef struct {
    unsigned int pc;
    char text[ANALYSE_LOCATION_TEXT_CAP];
} analyse_location_entry;

static const char *
analyse_stripSourceBase(const char *srcBase, const char *path);
static void
analyse_emitString(FILE *f, const char *value);
static void
analyse_printFrames(FILE *f, const analyse_frame *frames, size_t count, const char *srcBase);
static analyse_frame *
analyse_buildFrames(char **lines, size_t count, size_t *outCount);
static int
analyse_resolveFramesBatch(const char *elf, analyse_resolved_entry *entries, size_t count);
void
analyse_populateSampleLocations(analyse_profile_sample_entry *entries, size_t count);
static int
analyse_resolveLocations(const unsigned int *pcs, size_t count);
static analyse_location_entry *
analyse_locationLookup(unsigned int pc);
static analyse_location_entry *
analyse_locationAdd(unsigned int pc);
static void
analyse_locationSetFallback(analyse_location_entry *entry, unsigned int pc);
static void
analyse_locationSetFromResolved(analyse_location_entry *entry, const analyse_resolved_entry *resolved, unsigned int pc);

static unsigned int
analyse_adjustToolchainPc(unsigned int pc)
{
    uint32_t base = debugger.machine.textBaseAddr;
    if (base != 0 && pc >= base) {
        return pc - base;
    }
    return pc;
}

static analyse_profile_entry *analyse_profileMap = NULL;
static size_t analyse_profileCapacity = 0;
static size_t analyse_profileCount = 0;
static int analyse_profileReady = 0;
static analyse_location_entry *analyse_locationCache = NULL;
static size_t analyse_locationCacheCount = 0;
static size_t analyse_locationCacheCap = 0;

static char *
analyse_strndup(const char *str, size_t len)
{
    char *dup = alloc_alloc(len + 1);
    if (!dup) {
        return NULL;
    }
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

static int
analyse_profileMapInsert(unsigned int pc, unsigned long long samples, unsigned long long cycles)
{
    if (!analyse_profileMap || analyse_profileCapacity == 0) {
        return 0;
    }
    size_t idx = (size_t)pc % analyse_profileCapacity;
    for (size_t step = 0; step < analyse_profileCapacity; ++step) {
        analyse_profile_entry *entry = &analyse_profileMap[idx];
        if (!entry->used) {
            entry->used = 1;
            entry->pc = pc;
            entry->samples = samples;
            entry->cycles = cycles;
            entry->lastSamples = samples;
            entry->lastCycles = cycles;
            analyse_profileCount++;
            return 1;
        }
        if (entry->pc == pc) {
            unsigned long long deltaSamples = (samples >= entry->lastSamples) ? (samples - entry->lastSamples) : samples;
            unsigned long long deltaCycles = (cycles >= entry->lastCycles) ? (cycles - entry->lastCycles) : cycles;
            entry->lastSamples = samples;
            entry->lastCycles = cycles;
            entry->samples += deltaSamples;
            entry->cycles += deltaCycles;
            return 1;
        }
        idx = (idx + 1) % analyse_profileCapacity;
    }
    return 0;
}

static int
analyse_profileMapResize(size_t newCapacity)
{
    if (newCapacity < 16) {
        newCapacity = 16;
    }
    analyse_profile_entry *newEntries = alloc_calloc(newCapacity, sizeof(analyse_profile_entry));
    if (!newEntries) {
        return 0;
    }
    analyse_profile_entry *oldEntries = analyse_profileMap;
    size_t oldCapacity = analyse_profileCapacity;
    analyse_profileMap = newEntries;
    analyse_profileCapacity = newCapacity;
    analyse_profileCount = 0;
    if (oldEntries) {
        size_t migrated = 0;
        for (size_t i = 0; i < oldCapacity; ++i) {
            if (!oldEntries[i].used) {
                continue;
            }
            size_t idx = (size_t)oldEntries[i].pc % analyse_profileCapacity;
            while (newEntries[idx].used) {
                idx = (idx + 1) % analyse_profileCapacity;
            }
            newEntries[idx] = oldEntries[i];
            migrated++;
        }
        analyse_profileCount = migrated;
        alloc_free(oldEntries);
    }
    return 1;
}

int
analyse_init(void)
{
    if (analyse_profileReady) {
        return 1;
    }
    int ok = analyse_profileMapResize(ANALYSE_MAP_INITIAL_CAP);
    analyse_profileReady = ok;
    return ok;
}

void
analyse_shutdown(void)
{
    if (!analyse_profileMap) {
        return;
    }
    alloc_free(analyse_profileMap);
    analyse_profileMap = NULL;
    analyse_profileCapacity = 0;
    analyse_profileCount = 0;
    analyse_profileReady = 0;
    if (analyse_locationCache) {
        alloc_free(analyse_locationCache);
        analyse_locationCache = NULL;
    }
    analyse_locationCacheCount = 0;
    analyse_locationCacheCap = 0;
}

int
analyse_reset(void)
{
    analyse_shutdown();
    return analyse_init();
}

static int
analyse_ensureCapacity(void)
{
    if (!analyse_profileReady && !analyse_init()) {
        return 0;
    }
    if ((analyse_profileCount + 1) * 2 >= analyse_profileCapacity) {
        size_t next = analyse_profileCapacity * 2;
        if (next <= analyse_profileCapacity) {
            next = analyse_profileCapacity + 1;
        }
        if (!analyse_profileMapResize(next)) {
            return 0;
        }
    }
    return 1;
}

static unsigned int
analyse_parseHex(const char *value)
{
    if (!value) {
        return 0;
    }
    if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        value += 2;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 16);
    if (end == value) {
        return 0;
    }
    return (unsigned int)parsed;
}

static unsigned long long
analyse_parseDecimal(const char *value)
{
    if (!value) {
        return 0;
    }
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    (void)end;
    return parsed;
}

static int
analyse_consumeKeyValue(const char **cursor, char *keyBuf, size_t keyBufCap)
{
    const char *ptr = *cursor;
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr != '"') {
        return 0;
    }
    ptr++;
    const char *start = ptr;
    while (*ptr && *ptr != '"') {
        ptr++;
    }
    size_t len = (size_t)(ptr - start);
    if (len >= keyBufCap) {
        len = keyBufCap - 1;
    }
    memcpy(keyBuf, start, len);
    keyBuf[len] = '\0';
    if (*ptr == '"') {
        ptr++;
    }
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr == ':') {
        ptr++;
    }
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    *cursor = ptr;
    return 1;
}

static int
analyse_profileParseStreamLine(const char *line, size_t len)
{
    if (!line || len == 0) {
        return 1;
    }
    char *mutable = analyse_strndup(line, len);
    if (!mutable) {
        return 0;
    }
    if (!strstr(mutable, "\"stream\":\"profiler\"")) {
        alloc_free(mutable);
        return 1;
    }
    const char *hitsKey = strstr(mutable, "\"hits\"");
    if (!hitsKey) {
        alloc_free(mutable);
        return 1;
    }
    const char *open = strchr(hitsKey, '[');
    if (!open) {
        alloc_free(mutable);
        return 1;
    }
    const char *cursor = open + 1;
    while (*cursor) {
        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) {
            cursor++;
        }
        if (!*cursor || *cursor == ']') {
            break;
        }
        if (*cursor != '{') {
            cursor++;
            continue;
        }
        cursor++;
        unsigned int pc = 0;
        unsigned long long samples = 0;
        unsigned long long cycles = 0;
        int hasPc = 0;
        while (*cursor && *cursor != '}') {
            while (*cursor && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor == '}') {
                break;
            }
            if (*cursor != '"') {
                cursor++;
                continue;
            }
            char key[16] = {0};
            if (!analyse_consumeKeyValue(&cursor, key, sizeof(key))) {
                break;
            }
            if (strcmp(key, "pc") == 0) {
                if (*cursor == '"') {
                    cursor++;
                    const char *start = cursor;
                    while (*cursor && *cursor != '"') {
                        cursor++;
                    }
                    size_t lenValue = (size_t)(cursor - start);
                    char value[32] = {0};
                    if (lenValue >= sizeof(value)) {
                        lenValue = sizeof(value) - 1;
                    }
                    memcpy(value, start, lenValue);
                    if (*cursor == '"') {
                        cursor++;
                    }
                    pc = analyse_parseHex(value);
                    hasPc = 1;
                }
            } else if (strcmp(key, "samples") == 0) {
                samples = analyse_parseDecimal(cursor);
                while (*cursor && *cursor != ',' && *cursor != '}' && *cursor != ']') {
                    cursor++;
                }
            } else if (strcmp(key, "cycles") == 0) {
                cycles = analyse_parseDecimal(cursor);
                while (*cursor && *cursor != ',' && *cursor != '}' && *cursor != ']') {
                    cursor++;
                }
            } else {
                while (*cursor && *cursor != ',' && *cursor != '}' && *cursor != ']') {
                    cursor++;
                }
            }
            while (*cursor && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor == ',') {
                cursor++;
            }
        }
        if (*cursor == '}') {
            cursor++;
        }
        if (hasPc) {
            if (!analyse_ensureCapacity()) {
                alloc_free(mutable);
                debug_error("profile: unable to expand aggregator");
                return 0;
            }
            if (!analyse_profileMapInsert(pc, samples, cycles)) {
                alloc_free(mutable);
                debug_error("profile: unable to aggregate hits (out of memory)");
                return 0;
            }
        }
    }
    alloc_free(mutable);
    return 1;
}

int
analyse_handlePacket(const char *line, size_t len)
{
    if (!analyse_profileReady && !analyse_init()) {
        return 0;
    }
    return analyse_profileParseStreamLine(line, len);
}

static int
analyse_resolveFramesBatch(const char *elf, analyse_resolved_entry *entries, size_t count)
{
    if (!elf || !entries) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }
    char bin[PATH_MAX];
    if (!debugger_toolchainBuildBinary(bin, sizeof(bin), "addr2line")) {
        debug_error("profile: failed to resolve addr2line binary");
        return 0;
    }
    char exe[PATH_MAX];
    if (!file_findInPath(bin, exe, sizeof(exe))) {
        debug_error("profile: addr2line not found in PATH: %s", bin);
        return 0;
    }
    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0) {
        debug_error("profile: failed to open addr2line stdin: %s", strerror(errno));
        return 0;
    }
    if (pipe(from_child) != 0) {
        close(to_child[0]);
        close(to_child[1]);
        debug_error("profile: failed to open addr2line stdout: %s", strerror(errno));
        return 0;
    }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        char *const argv[] = {
            exe,
            (char*)"-e",
            (char*)elf,
            (char*)"-a",
            (char*)"-f",
            (char*)"-C",
            (char*)"-i",
            NULL
        };
        execv(exe, argv);
        _exit(127);
    }
    if (pid < 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        debug_error("profile: failed to spawn addr2line: %s", strerror(errno));
        return 0;
    }
    close(to_child[0]);
    close(from_child[1]);
    FILE *input = fdopen(to_child[1], "w");
    FILE *pipe = fdopen(from_child[0], "r");
    if (!input || !pipe) {
        if (input) {
            fclose(input);
        } else {
            close(to_child[1]);
        }
        if (pipe) {
            fclose(pipe);
        } else {
            close(from_child[0]);
        }
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        debug_error("profile: failed to open addr2line pipes");
        return 0;
    }
    struct sigaction oldAct;
    struct sigaction ignAct;
    memset(&ignAct, 0, sizeof(ignAct));
    ignAct.sa_handler = SIG_IGN;
    sigemptyset(&ignAct.sa_mask);
    sigaction(SIGPIPE, &ignAct, &oldAct);

    int writeOk = 1;
    for (size_t i = 0; i < count; ++i) {
        if (fprintf(input, "%s\n", entries[i].address) < 0) {
            writeOk = 0;
            break;
        }
    }
    if (writeOk && fflush(input) != 0) {
        writeOk = 0;
    }
    if (fclose(input) != 0) {
        writeOk = 0;
    }

    sigaction(SIGPIPE, &oldAct, NULL);

    if (!writeOk) {
        fclose(pipe);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        debug_error("profile: addr2line write failed");
        return 0;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t read = 0;
    char **currentLines = NULL;
    size_t currentCount = 0;
    size_t entryIdx = 0;
    int ok = 1;
    int entryStarted = 0;
    while (ok && (read = getline(&line, &cap, pipe)) != -1) {
        while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r')) {
            line[--read] = '\0';
        }
        if (read == 0) {
            continue;
        }
        if (read >= 2 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
            if (!entryStarted) {
                entryStarted = 1;
            } else {
                if (entryIdx >= count) {
                    ok = 0;
                    break;
                }
                size_t frameCount = 0;
                if (currentCount > 0) {
                    entries[entryIdx].frames = analyse_buildFrames(currentLines, currentCount, &frameCount);
                    entries[entryIdx].frameCount = frameCount;
                } else {
                    entries[entryIdx].frames = NULL;
                    entries[entryIdx].frameCount = 0;
                }
                currentLines = NULL;
                currentCount = 0;
                entryIdx++;
            }
            continue;
        }
        if (!entryStarted) {
            continue;
        }
        char *dup = alloc_strdup(line);
        if (!dup) {
            ok = 0;
            break;
        }
        char **tmpLines = alloc_realloc(currentLines, (currentCount + 1) * sizeof(char *));
        if (!tmpLines) {
            alloc_free(dup);
            ok = 0;
            break;
        }
        currentLines = tmpLines;
        currentLines[currentCount++] = dup;
    }
    if (ok && entryStarted) {
        if (entryIdx >= count) {
            ok = 0;
        } else {
            size_t frameCount = 0;
            if (currentCount > 0) {
                entries[entryIdx].frames = analyse_buildFrames(currentLines, currentCount, &frameCount);
                entries[entryIdx].frameCount = frameCount;
            } else {
                entries[entryIdx].frames = NULL;
                entries[entryIdx].frameCount = 0;
            }
            currentLines = NULL;
            currentCount = 0;
            entryIdx++;
        }
    }
    if (currentLines) {
        for (size_t i = 0; i < currentCount; ++i) {
            alloc_free(currentLines[i]);
        }
        alloc_free(currentLines);
    }
    free(line);
    fclose(pipe);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        ok = 0;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ok = 0;
    }
    if (entryIdx < count) {
        ok = 0;
    }
    return ok;
}

static analyse_frame *
analyse_buildFrames(char **lines, size_t count, size_t *outCount)
{
    if (!lines || count < 2 || !outCount) {
        if (lines) {
            for (size_t i = 0; i < count; ++i) {
                alloc_free(lines[i]);
            }
            alloc_free(lines);
        }
        return NULL;
    }
    size_t frameCount = count / 2;
    analyse_frame *frames = alloc_calloc(frameCount, sizeof(analyse_frame));
    if (!frames) {
        return NULL;
    }
    size_t idx = 0;
    for (size_t i = 0; i + 1 < count && idx < frameCount; i += 2) {
        char *func = lines[i];
        char *file = lines[i + 1];
        if (!func || !file) {
            continue;
        }
        char *funcDup = alloc_strdup(func);
        char *fileDup = alloc_strdup(file);
        if (!funcDup || !fileDup) {
            alloc_free(funcDup);
            alloc_free(fileDup);
            continue;
        }
        char *colon = strrchr(fileDup, ':');
        int lineNo = 0;
        if (colon) {
            *colon = '\0';
            lineNo = atoi(colon + 1);
        }
        const char *slash = strrchr(fileDup, '/');
        const char *base = slash ? slash + 1 : fileDup;
        size_t locLen = strlen(base) + 16;
        char *loc = alloc_alloc(locLen);
        if (loc) {
            snprintf(loc, locLen, "%s:%d", base, lineNo);
        }
        frames[idx].function = funcDup;
        frames[idx].file = fileDup;
        frames[idx].line = lineNo;
        frames[idx].loc = loc;
        idx++;
    }
    *outCount = idx;
    for (size_t i = 0; i < count; ++i) {
        alloc_free(lines[i]);
    }
    alloc_free(lines);
    if (idx == 0) {
        alloc_free(frames);
        return NULL;
    }
    for (size_t i = 0; i < idx / 2; ++i) {
        analyse_frame tmp = frames[i];
        frames[i] = frames[idx - 1 - i];
        frames[idx - 1 - i] = tmp;
    }
    return frames;
}

static char *
analyse_buildFunctionChain(const analyse_frame *frames, size_t count)
{
    if (!frames || count == 0) {
        return alloc_strdup("");
    }
    size_t total = 1;
    for (size_t i = 0; i < count; ++i) {
        const char *name = frames[i].function ? frames[i].function : "??";
        total += strlen(name);
        if (i + 1 < count) {
            total += 4;
        }
    }
    char *chain = alloc_alloc(total);
    if (!chain) {
        return NULL;
    }
    chain[0] = '\0';
    for (size_t i = 0; i < count; ++i) {
        const char *name = frames[i].function ? frames[i].function : "??";
        strcat(chain, name);
        if (i + 1 < count) {
            strcat(chain, " -> ");
        }
    }
    return chain;
}

static char *
analyse_readSourceLine(const char *srcBase, const char *filePath, int lineNo)
{
    if (!filePath || lineNo <= 0) {
        return NULL;
    }
    FILE *f = NULL;
    char path[PATH_MAX];
    if (srcBase && *srcBase) {
        const char *slash = strrchr(filePath, '/');
        const char *base = slash ? slash + 1 : filePath;
        if (base && *base) {
            snprintf(path, sizeof(path), "%s/%s", srcBase, base);
            f = fopen(path, "r");
        }
    }
    if (!f) {
        f = fopen(filePath, "r");
        if (!f) {
            return NULL;
        }
    }
    char *line = NULL;
    size_t cap = 0;
    int idx = 0;
    char *result = NULL;
    while (getline(&line, &cap, f) != -1) {
        idx++;
        if (idx == lineNo) {
            size_t len = strlen(line);
            while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            result = alloc_strdup(line);
            break;
        }
    }
    free(line);
    fclose(f);
    return result;
}

static void
analyse_freeFrames(analyse_frame *frames, size_t count)
{
    if (!frames) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        alloc_free(frames[i].function);
        alloc_free(frames[i].file);
        alloc_free(frames[i].loc);
    }
    alloc_free(frames);
}

static analyse_line_entry *
analyse_findLineEntry(analyse_line_entry *lines, size_t count, const char *file, int line)
{
    if (!lines || !file) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (lines[i].line == line && strcmp(lines[i].file, file) == 0) {
            return &lines[i];
        }
    }
    return NULL;
}

static int
analyse_resolvedEntryValid(const analyse_resolved_entry *entry)
{
    return entry && entry->frames && entry->frameCount > 0 && entry->topLine > 0 && entry->topFile && *entry->topFile && strcmp(entry->topFile, "??") != 0;
}

static void
analyse_freeLineEntries(analyse_line_entry *lines, size_t count)
{
    if (!lines) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (lines[i].file) {
            alloc_free(lines[i].file);
            lines[i].file = NULL;
        }
        if (lines[i].chain) {
            alloc_free(lines[i].chain);
            lines[i].chain = NULL;
        }
        if (lines[i].source) {
            alloc_free(lines[i].source);
            lines[i].source = NULL;
        }
        analyse_freeFrames(lines[i].frames, lines[i].frameCount);
    }
}

void
analyse_populateSampleLocations(analyse_profile_sample_entry *entries, size_t count)
{
    if (!entries || count == 0) {
        return;
    }
    unsigned int *pending = (unsigned int*)alloc_alloc(count * sizeof(unsigned int));
    size_t pendingCount = 0;
    if (pending) {
        for (size_t i = 0; i < count; ++i) {
            analyse_location_entry *cache = analyse_locationLookup(entries[i].pc);
            if (cache && cache->text[0]) {
                strncpy(entries[i].location, cache->text, ANALYSE_LOCATION_TEXT_CAP - 1);
                entries[i].location[ANALYSE_LOCATION_TEXT_CAP - 1] = '\0';
                continue;
            }
            int alreadyQueued = 0;
            for (size_t j = 0; j < pendingCount; ++j) {
                if (pending[j] == entries[i].pc) {
                    alreadyQueued = 1;
                    break;
                }
            }
            if (!alreadyQueued) {
                pending[pendingCount++] = entries[i].pc;
            }
        }
        if (pendingCount > 0) {
            analyse_resolveLocations(pending, pendingCount);
        }
        alloc_free(pending);
    }
    for (size_t i = 0; i < count; ++i) {
        analyse_location_entry *cache = analyse_locationLookup(entries[i].pc);
        if (cache && cache->text[0]) {
            strncpy(entries[i].location, cache->text, ANALYSE_LOCATION_TEXT_CAP - 1);
            entries[i].location[ANALYSE_LOCATION_TEXT_CAP - 1] = '\0';
        } else {
            snprintf(entries[i].location, ANALYSE_LOCATION_TEXT_CAP, "PC: 0x%06X", entries[i].pc);
        }
    }
}

static int
analyse_resolveLocations(const unsigned int *pcs, size_t count)
{
    if (!pcs || count == 0) {
        return 0;
    }
    const char *elfPath = debugger.libretro.exePath;
    analyse_resolved_entry *resolved = NULL;
    int didResolve = 0;
    if (elfPath && *elfPath) {
        resolved = (analyse_resolved_entry*)alloc_calloc(count, sizeof(analyse_resolved_entry));
        if (resolved) {
            for (size_t i = 0; i < count; ++i) {
                unsigned int adjusted = analyse_adjustToolchainPc(pcs[i]);
                snprintf(resolved[i].address, sizeof(resolved[i].address), "0x%06X", adjusted);
            }
            if (analyse_resolveFramesBatch(elfPath, resolved, count)) {
                didResolve = 1;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        analyse_location_entry *cache = analyse_locationLookup(pcs[i]);
        if (!cache) {
            cache = analyse_locationAdd(pcs[i]);
        }
        if (!cache) {
            continue;
        }
        if (didResolve && resolved) {
            analyse_locationSetFromResolved(cache, &resolved[i], pcs[i]);
        } else {
            analyse_locationSetFallback(cache, pcs[i]);
        }
    }
    if (resolved) {
        for (size_t i = 0; i < count; ++i) {
            analyse_freeFrames(resolved[i].frames, resolved[i].frameCount);
        }
        alloc_free(resolved);
    }
    return didResolve;
}

static analyse_location_entry *
analyse_locationLookup(unsigned int pc)
{
    for (size_t i = 0; i < analyse_locationCacheCount; ++i) {
        if (analyse_locationCache[i].pc == pc) {
            return &analyse_locationCache[i];
        }
    }
    return NULL;
}

static analyse_location_entry *
analyse_locationAdd(unsigned int pc)
{
    if (analyse_locationCacheCount == analyse_locationCacheCap) {
        size_t next = analyse_locationCacheCap ? (analyse_locationCacheCap * 2) : 64;
        analyse_location_entry *tmp = (analyse_location_entry*)alloc_realloc(analyse_locationCache, next * sizeof(analyse_location_entry));
        if (!tmp) {
            return NULL;
        }
        analyse_locationCache = tmp;
        analyse_locationCacheCap = next;
    }
    analyse_location_entry *entry = &analyse_locationCache[analyse_locationCacheCount++];
    entry->pc = pc;
    entry->text[0] = '\0';
    return entry;
}

static void
analyse_locationSetFallback(analyse_location_entry *entry, unsigned int pc)
{
    if (!entry) {
        return;
    }
    snprintf(entry->text, ANALYSE_LOCATION_TEXT_CAP, "PC: 0x%06X", pc);
}

static void
analyse_locationSetFromResolved(analyse_location_entry *entry, const analyse_resolved_entry *resolved, unsigned int pc)
{
    if (!entry) {
        return;
    }
    if (resolved && resolved->frames && resolved->frameCount > 0) {
        const analyse_frame *best = NULL;
        for (size_t j = resolved->frameCount; j-- > 0;) {
            const analyse_frame *frame = &resolved->frames[j];
            if (frame->file && frame->line > 0 && strcmp(frame->file, "??") != 0) {
                best = frame;
                break;
            }
            if (!best && frame->file && frame->line > 0) {
                best = frame;
            }
        }
        if (!best) {
            best = &resolved->frames[0];
        }
        if (best && best->file && best->line > 0) {
            const char *slash = strrchr(best->file, '/');
            const char *base = slash ? slash + 1 : best->file;
            if (!base || !*base) {
                base = best->file;
            }
            snprintf(entry->text, ANALYSE_LOCATION_TEXT_CAP, "%s:%d", base, best->line);
            return;
        }
    }
    analyse_locationSetFallback(entry, pc);
}

static int
analyse_writeResolvedJsonFromLines(const analyse_line_entry *lines, size_t count, const char *jsonPath, const char *srcBase)
{
    if (!lines || !jsonPath || !*jsonPath) {
        debug_error("profile: missing output path");
        return 0;
    }
    FILE *out = fopen(jsonPath, "w");
    if (!out) {
        debug_error("profile: failed to open %s: %s", jsonPath, strerror(errno));
        return 0;
    }
    fprintf(out, "[\n");
    int first = 1;
    for (size_t i = 0; i < count; ++i) {
        const analyse_line_entry *entry = &lines[i];
        if (!first) {
            fprintf(out, ",\n");
        }
        first = 0;
        fprintf(out, "  {\n");
        fprintf(out, "    \"pc\": ");
        analyse_emitString(out, entry->address);
        fprintf(out, ",\n");
        fprintf(out, "    \"address\": ");
        analyse_emitString(out, entry->address);
        fprintf(out, ",\n");
        fprintf(out, "    \"count\": %llu,\n", entry->count);
        fprintf(out, "    \"cycles\": %llu,\n", entry->cycles);
        fprintf(out, "    \"function_chain\": ");
        analyse_emitString(out, entry->chain ? entry->chain : "");
        fprintf(out, ",\n");
        fprintf(out, "    \"function_chain_frames\": [\n");
        if (entry->frames && entry->frameCount > 0) {
            analyse_printFrames(out, entry->frames, entry->frameCount, srcBase);
            fprintf(out, "\n");
        }
        fprintf(out, "    ],\n");
        fprintf(out, "    \"file\": ");
        analyse_emitString(out, analyse_stripSourceBase(srcBase, entry->file ? entry->file : ""));
        fprintf(out, ",\n");
        fprintf(out, "    \"line\": %d,\n", entry->line);
        fprintf(out, "    \"source\": ");
        analyse_emitString(out, entry->source ? entry->source : "");
        fprintf(out, "\n  }");
    }
    if (!first) {
        fprintf(out, "\n");
    }
    fprintf(out, "]\n");
    fclose(out);
    debug_printf("Profile analysis wrote JSON to %s\n", jsonPath);
    return 1;
}

static void
analyse_freeResolvedEntries(analyse_resolved_entry *entries, size_t count)
{
    if (!entries) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        analyse_resolved_entry *entry = &entries[i];
        analyse_freeFrames(entry->frames, entry->frameCount);
        alloc_free(entry->chain);
        alloc_free(entry->source);
    }
}

static void
analyse_emitString(FILE *f, const char *value)
{
    fputc('"', f);
    if (value) {
        for (const char *p = value; *p; ++p) {
            switch (*p) {
            case '\\':
                fputs("\\\\", f);
                break;
            case '"':
                fputs("\\\"", f);
                break;
            case '\n':
                fputs("\\n", f);
                break;
            case '\r':
                fputs("\\r", f);
                break;
            case '\t':
                fputs("\\t", f);
                break;
            default:
                fputc(*p, f);
                break;
            }
        }
    }
    fputc('"', f);
}

static void
analyse_printFrames(FILE *f, const analyse_frame *frames, size_t count, const char *srcBase)
{
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            fprintf(f, ",\n");
        }
        fprintf(f, "      {\n");
        fprintf(f, "        \"function\": ");
        analyse_emitString(f, frames[i].function ? frames[i].function : "");
        fprintf(f, ",\n");
        fprintf(f, "        \"file\": ");
        analyse_emitString(f, analyse_stripSourceBase(srcBase, frames[i].file ? frames[i].file : ""));
        fprintf(f, ",\n");
        fprintf(f, "        \"line\": %d,\n", frames[i].line);
        fprintf(f, "        \"loc\": ");
        analyse_emitString(f, frames[i].loc ? frames[i].loc : "");
        fprintf(f, "\n      }");
    }
}

static const char *
analyse_stripSourceBase(const char *srcBase, const char *path)
{
    if (!srcBase || !*srcBase || !path || !*path) {
        return path;
    }
    size_t baseLen = strlen(srcBase);
    size_t pathLen = strlen(path);
    if (pathLen <= baseLen) {
        return path;
    }
    int baseEndsWithSlash = srcBase[baseLen - 1] == '/' || srcBase[baseLen - 1] == '\\';
    if (!baseEndsWithSlash) {
        char next = path[baseLen];
        if (next != '/' && next != '\\') {
            return path;
        }
    }
    if (strncmp(path, srcBase, baseLen) != 0) {
        static char extPath[PATH_MAX];
        const char *trim = path;
        while (*trim == '/' || *trim == '\\') {
            trim++;
        }
        const char *last = trim;
        const char *slash = strrchr(trim, '/');
        const char *back = strrchr(trim, '\\');
        if (slash && back) {
            last = (slash > back) ? slash + 1 : back + 1;
        } else if (slash) {
            last = slash + 1;
        } else if (back) {
            last = back + 1;
        }
        if (!last || *last == '\0') {
            last = trim;
        }
        snprintf(extPath, sizeof(extPath), "<EXT>/%s", last);
        return extPath;
    }
    const char *remainder = path + baseLen;
    while (*remainder == '/' || *remainder == '\\') {
        remainder++;
    }
    return remainder;
}

int
analyse_writeFinalJson(const char *jsonPath)
{
    if (!jsonPath || !*jsonPath) {
        debug_error("profile: missing output path");
        return 0;
    }
    if (!analyse_ensureCapacity()) {
        return 0;
    }
    const char *elfPath = debugger.libretro.exePath;
    if (!elfPath || !*elfPath) {
        debug_error("profile: ELF path not configured");
        return 0;
    }
    const char *srcBase = debugger.libretro.sourceDir;
    size_t entryCap = analyse_profileCount;
    int ok = 1;
    analyse_resolved_entry *entries = alloc_calloc(entryCap ? entryCap : 1, sizeof(analyse_resolved_entry));
    if (!entries) {
        debug_error("profile: failed to allocate resolved entries");
        return 0;
    }
    size_t resolvedCount = 0;
    for (size_t i = 0; i < analyse_profileCapacity; ++i) {
        analyse_profile_entry *slot = &analyse_profileMap[i];
        if (!slot || !slot->used) {
            continue;
        }
        analyse_resolved_entry *entry = &entries[resolvedCount++];
        entry->samples = slot->samples;
        entry->cycles = slot->cycles;
        unsigned int adjusted = analyse_adjustToolchainPc(slot->pc);
        snprintf(entry->address, sizeof(entry->address), "0x%06X", adjusted);
    }
    analyse_line_entry *lines = NULL;
    size_t lineCount = 0;
    size_t lineCap = 0;
    if (!analyse_resolveFramesBatch(elfPath, entries, resolvedCount)) {
        debug_error("profile: failed to resolve symbols");
        ok = 0;
        goto cleanup;
    }
    for (size_t i = 0; i < resolvedCount; ++i) {
        analyse_resolved_entry *entry = &entries[i];
        entry->chain = analyse_buildFunctionChain(entry->frames, entry->frameCount);
        if (!entry->chain) {
            entry->chain = alloc_strdup("");
        }
        entry->topFile = "";
        entry->topLine = 0;
        if (entry->frames && entry->frameCount > 0) {
            const analyse_frame *best = NULL;
            for (size_t j = entry->frameCount; j-- > 0;) {
                const analyse_frame *f = &entry->frames[j];
                if (f->file && f->line > 0 && strcmp(f->file, "??") != 0) {
                    best = f;
                    break;
                }
                if (!best && f->file && f->line > 0) {
                    best = f;
                }
            }
            if (!best) {
                best = &entry->frames[0];
            }
            entry->topFile = best->file ? best->file : "";
            entry->topLine = best->line;
        }
        entry->source = analyse_readSourceLine(srcBase, entry->topFile, entry->topLine);
    }
    for (size_t i = 0; i < resolvedCount; ++i) {
        analyse_resolved_entry *entry = &entries[i];
        if (!analyse_resolvedEntryValid(entry)) {
            continue;
        }
        analyse_line_entry *line = analyse_findLineEntry(lines, lineCount, entry->topFile, entry->topLine);
        if (!line) {
            if (lineCount == lineCap) {
                size_t next = lineCap ? lineCap * 2 : 16;
                analyse_line_entry *tmp = (analyse_line_entry *)alloc_realloc(lines, next * sizeof(analyse_line_entry));
                if (!tmp) {
                    debug_error("profile: failed to expand line entries");
                    ok = 0;
                    goto cleanup;
                }
                lines = tmp;
                lineCap = next;
            }
            line = &lines[lineCount++];
            line->file = alloc_strdup(entry->topFile ? entry->topFile : "");
            if (!line->file) {
                debug_error("profile: failed to duplicate file name");
                ok = 0;
                goto cleanup;
            }
            line->line = entry->topLine;
            line->cycles = 0;
            line->count = 0;
            line->frames = NULL;
            line->frameCount = 0;
            line->chain = NULL;
            line->source = NULL;
            line->bestCycles = 0;
            line->bestSamples = 0;
            line->address[0] = '\0';
        }
        line->cycles += entry->cycles;
        line->count += entry->samples;
        int shouldReplace = 0;
        if (line->address[0] == '\0') {
            shouldReplace = 1;
        } else if (entry->cycles > line->bestCycles) {
            shouldReplace = 1;
        } else if (entry->cycles == line->bestCycles) {
            if (entry->cycles == 0 && entry->samples > line->bestSamples) {
                shouldReplace = 1;
            }
        }
        if (shouldReplace) {
            line->bestCycles = entry->cycles;
            line->bestSamples = entry->samples;
            strncpy(line->address, entry->address, sizeof(line->address));
            line->address[sizeof(line->address) - 1] = '\0';
            analyse_freeFrames(line->frames, line->frameCount);
            line->frames = entry->frames;
            line->frameCount = entry->frameCount;
            entry->frames = NULL;
            entry->frameCount = 0;
            alloc_free(line->chain);
            line->chain = entry->chain;
            entry->chain = NULL;
            alloc_free(line->source);
            line->source = entry->source;
            entry->source = NULL;
        } else {
            analyse_freeFrames(entry->frames, entry->frameCount);
            entry->frames = NULL;
            entry->frameCount = 0;
            alloc_free(entry->chain);
            entry->chain = NULL;
            alloc_free(entry->source);
            entry->source = NULL;
        }
    }

    if (ok && !analyse_writeResolvedJsonFromLines(lines, lineCount, jsonPath, srcBase)) {
        ok = 0;
    }
cleanup:
    analyse_freeLineEntries(lines, lineCount);
    alloc_free(lines);
    analyse_freeResolvedEntries(entries, resolvedCount);
    alloc_free(entries);
    return ok;
}

static int
analyse_profileSampleCompare(const void *a, const void *b)
{
    const analyse_profile_sample_entry *ea = (const analyse_profile_sample_entry*)a;
    const analyse_profile_sample_entry *eb = (const analyse_profile_sample_entry*)b;
    if (ea->samples > eb->samples) {
        return -1;
    }
    if (ea->samples < eb->samples) {
        return 1;
    }
    if (ea->pc < eb->pc) {
        return -1;
    }
    if (ea->pc > eb->pc) {
        return 1;
    }
    return 0;
}

int
analyse_profileSnapshot(analyse_profile_sample_entry **out, size_t *count)
{
    if (!out || !count) {
        return 0;
    }
    *out = NULL;
    *count = 0;
    if (!analyse_profileReady || analyse_profileCount == 0) {
        return 1;
    }
    analyse_profile_sample_entry *entries = (analyse_profile_sample_entry*)alloc_alloc(analyse_profileCount * sizeof(*entries));
    if (!entries) {
        return 0;
    }
    size_t idx = 0;
    for (size_t i = 0; i < analyse_profileCapacity && idx < analyse_profileCount; ++i) {
        analyse_profile_entry *slot = &analyse_profileMap[i];
        if (slot->used) {
            entries[idx].pc = slot->pc;
            entries[idx].samples = slot->samples;
            idx++;
        }
    }
    if (idx > 1) {
        qsort(entries, idx, sizeof(*entries), analyse_profileSampleCompare);
    }
    *out = entries;
    *count = idx;
    return 1;
}

void
analyse_profileSnapshotFree(analyse_profile_sample_entry *entries)
{
    if (entries) {
        alloc_free(entries);
    }
}
