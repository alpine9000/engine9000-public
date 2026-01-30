/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "neogeo_core_options.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "libretro_host.h"

typedef struct neogeo_core_options_kv {
    char *key;
    char *value;
} neogeo_core_options_kv_t;

static neogeo_core_options_kv_t *neogeo_coreOptions_entries = NULL;
static size_t neogeo_coreOptions_entryCount = 0;
static size_t neogeo_coreOptions_entryCap = 0;
static int neogeo_coreOptions_dirty = 0;

static const char *
neogeo_coreOptions_basename(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *sep = slash > back ? slash : back;
    return sep ? sep + 1 : path;
}

static void
neogeo_coreOptions_trimRight(char *s)
{
    if (!s) {
        return;
    }
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[len - 1] = '\0';
            len--;
            continue;
        }
        break;
    }
}

static char *
neogeo_coreOptions_trimLeft(char *s)
{
    if (!s) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static neogeo_core_options_kv_t *
neogeo_coreOptions_findEntry(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    for (size_t i = 0; i < neogeo_coreOptions_entryCount; ++i) {
        if (neogeo_coreOptions_entries[i].key && strcmp(neogeo_coreOptions_entries[i].key, key) == 0) {
            return &neogeo_coreOptions_entries[i];
        }
    }
    return NULL;
}

static neogeo_core_options_kv_t *
neogeo_coreOptions_getOrAddEntry(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    neogeo_core_options_kv_t *existing = neogeo_coreOptions_findEntry(key);
    if (existing) {
        return existing;
    }
    if (neogeo_coreOptions_entryCount >= neogeo_coreOptions_entryCap) {
        size_t nextCap = neogeo_coreOptions_entryCap ? neogeo_coreOptions_entryCap * 2 : 64;
        neogeo_core_options_kv_t *next =
            (neogeo_core_options_kv_t *)alloc_realloc(neogeo_coreOptions_entries, nextCap * sizeof(*next));
        if (!next) {
            return NULL;
        }
        neogeo_coreOptions_entries = next;
        neogeo_coreOptions_entryCap = nextCap;
    }
    neogeo_core_options_kv_t *ent = &neogeo_coreOptions_entries[neogeo_coreOptions_entryCount++];
    memset(ent, 0, sizeof(*ent));
    ent->key = alloc_strdup(key);
    ent->value = alloc_strdup("");
    return ent;
}

static void
neogeo_coreOptions_removeEntry(const char *key)
{
    if (!key || !*key || !neogeo_coreOptions_entries) {
        return;
    }
    for (size_t i = 0; i < neogeo_coreOptions_entryCount; ++i) {
        if (neogeo_coreOptions_entries[i].key && strcmp(neogeo_coreOptions_entries[i].key, key) == 0) {
            alloc_free(neogeo_coreOptions_entries[i].key);
            alloc_free(neogeo_coreOptions_entries[i].value);
            for (size_t j = i + 1; j < neogeo_coreOptions_entryCount; ++j) {
                neogeo_coreOptions_entries[j - 1] = neogeo_coreOptions_entries[j];
            }
            neogeo_coreOptions_entryCount--;
            return;
        }
    }
}

static int
neogeo_coreOptions_parseLine(const char *line, char *outKey, size_t keyCap, char *outValue, size_t valueCap)
{
    if (!line || !outKey || keyCap == 0 || !outValue || valueCap == 0) {
        return 0;
    }
    outKey[0] = '\0';
    outValue[0] = '\0';

    const char *p = line;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0' || *p == ';' || *p == '#') {
        return 0;
    }
    const char *eq = strchr(p, '=');
    if (!eq) {
        return 0;
    }

    size_t keyLen = (size_t)(eq - p);
    if (keyLen >= keyCap) {
        keyLen = keyCap - 1;
    }
    memcpy(outKey, p, keyLen);
    outKey[keyLen] = '\0';
    neogeo_coreOptions_trimRight(outKey);
    char *k = neogeo_coreOptions_trimLeft(outKey);
    if (k != outKey) {
        memmove(outKey, k, strlen(k) + 1);
    }
    if (!outKey[0]) {
        return 0;
    }

    const char *v = eq + 1;
    while (*v == ' ' || *v == '\t') {
        v++;
    }
    strncpy(outValue, v, valueCap - 1);
    outValue[valueCap - 1] = '\0';
    neogeo_coreOptions_trimRight(outValue);
    return 1;
}

int
neogeo_coreOptionsDirty(void)
{
    return neogeo_coreOptions_dirty ? 1 : 0;
}

void
neogeo_coreOptionsClear(void)
{
    if (neogeo_coreOptions_entries) {
        for (size_t i = 0; i < neogeo_coreOptions_entryCount; ++i) {
            alloc_free(neogeo_coreOptions_entries[i].key);
            alloc_free(neogeo_coreOptions_entries[i].value);
        }
        alloc_free(neogeo_coreOptions_entries);
        neogeo_coreOptions_entries = NULL;
    }
    neogeo_coreOptions_entryCount = 0;
    neogeo_coreOptions_entryCap = 0;
    neogeo_coreOptions_dirty = 0;
}

const char *
neogeo_coreOptionsGetValue(const char *key)
{
    neogeo_core_options_kv_t *ent = neogeo_coreOptions_findEntry(key);
    return ent ? ent->value : NULL;
}

void
neogeo_coreOptionsSetValue(const char *key, const char *value)
{
    if (!key || !*key) {
        return;
    }
    if (!value) {
        neogeo_coreOptions_removeEntry(key);
        neogeo_coreOptions_dirty = 1;
        return;
    }
    neogeo_core_options_kv_t *ent = neogeo_coreOptions_getOrAddEntry(key);
    if (!ent) {
        return;
    }
    alloc_free(ent->value);
    ent->value = alloc_strdup(value);
    neogeo_coreOptions_dirty = 1;
}

int
neogeo_coreOptionsBuildPath(char *out, size_t cap, const char *saveDir, const char *romPath)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!saveDir || !*saveDir || !romPath || !*romPath) {
        return 0;
    }
    const char *base = neogeo_coreOptions_basename(romPath);
    if (!base || !*base) {
        return 0;
    }
    char sep = '/';
    if (strchr(saveDir, '\\')) {
        sep = '\\';
    }
    int needsSep = 1;
    size_t len = strlen(saveDir);
    if (len > 0 && (saveDir[len - 1] == '/' || saveDir[len - 1] == '\\')) {
        needsSep = 0;
    }
    int written = 0;
    if (needsSep) {
        written = snprintf(out, cap, "%s%c%s.core_options", saveDir, sep, base);
    } else {
        written = snprintf(out, cap, "%s%s.core_options", saveDir, base);
    }
    if (written < 0 || (size_t)written >= cap) {
        out[cap - 1] = '\0';
        return 0;
    }
    return 1;
}

int
neogeo_coreOptionsLoadFromFile(const char *saveDir, const char *romPath)
{
    char path[PATH_MAX];
    if (!neogeo_coreOptionsBuildPath(path, sizeof(path), saveDir, romPath)) {
        neogeo_coreOptionsClear();
        return 0;
    }
    neogeo_coreOptionsClear();
    FILE *f = fopen(path, "r");
    if (!f) {
        neogeo_coreOptions_dirty = 0;
        return 1;
    }
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char key[1024];
        char value[3072];
        if (!neogeo_coreOptions_parseLine(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        if (strcmp(key, "geolith_system_type") == 0) {
            continue;
        }
        neogeo_core_options_kv_t *ent = neogeo_coreOptions_getOrAddEntry(key);
        if (!ent) {
            continue;
        }
        alloc_free(ent->value);
        ent->value = alloc_strdup(value);
    }
    fclose(f);
    neogeo_coreOptions_dirty = 0;
    return 1;
}

int
neogeo_coreOptionsWriteToFile(const char *saveDir, const char *romPath)
{
    char path[PATH_MAX];
    if (!neogeo_coreOptionsBuildPath(path, sizeof(path), saveDir, romPath)) {
        return 0;
    }
    if (neogeo_coreOptions_entryCount == 0) {
        remove(path);
        neogeo_coreOptions_dirty = 0;
        return 1;
    }
    char tmpPath[PATH_MAX];
    int tmpWritten = snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    if (tmpWritten < 0 || tmpWritten >= (int)sizeof(tmpPath)) {
        return 0;
    }
    FILE *out = fopen(tmpPath, "w");
    if (!out) {
        return 0;
    }
    for (size_t i = 0; i < neogeo_coreOptions_entryCount; ++i) {
        const char *k = neogeo_coreOptions_entries[i].key;
        const char *v = neogeo_coreOptions_entries[i].value;
        if (!k || !*k) {
            continue;
        }
        if (strcmp(k, "geolith_system_type") == 0) {
            continue;
        }
        fprintf(out, "%s=%s\n", k, v ? v : "");
    }
    fclose(out);
    remove(path);
    if (rename(tmpPath, path) != 0) {
        remove(tmpPath);
        return 0;
    }
    neogeo_coreOptions_dirty = 0;
    return 1;
}

int
neogeo_coreOptionsApplyFileToHost(const char *saveDir, const char *romPath)
{
    char path[PATH_MAX];
    if (!neogeo_coreOptionsBuildPath(path, sizeof(path), saveDir, romPath)) {
        return 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return 1;
    }
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char key[1024];
        char value[3072];
        if (!neogeo_coreOptions_parseLine(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        if (strcmp(key, "geolith_system_type") == 0) {
            continue;
        }
        libretro_host_setCoreOption(key, value);
    }
    fclose(f);
    return 1;
}

