/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "amiga_uae_options.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "libretro_host.h"

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct amiga_uae_kv {
    char *key;
    char *value;
} amiga_uae_kv_t;

static amiga_uae_kv_t *amiga_uae_entries = NULL;
static size_t amiga_uae_entryCount = 0;
static size_t amiga_uae_entryCap = 0;
static int amiga_uae_dirty = 0;
static char amiga_uae_loadedPath[PATH_MAX];
static char amiga_uae_floppy0[PATH_MAX];
static char amiga_uae_floppy1[PATH_MAX];

static void
amiga_uaeTrimRight(char *s)
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
amiga_uaeTrimLeft(char *s)
{
    if (!s) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int
amiga_uaeKeyIsPuae(const char *key)
{
    if (!key) {
        return 0;
    }
    return strncmp(key, "puae_", 5) == 0;
}

static int
amiga_uaeKeyIsFloppy(const char *key, int *out_drive)
{
    if (out_drive) {
        *out_drive = -1;
    }
    if (!key) {
        return 0;
    }
    if (strcmp(key, "floppy0") == 0) {
        if (out_drive) {
            *out_drive = 0;
        }
        return 1;
    }
    if (strcmp(key, "floppy1") == 0) {
        if (out_drive) {
            *out_drive = 1;
        }
        return 1;
    }
    return 0;
}

static int
amiga_uaeParseKeyValue(const char *line, char *outKey, size_t keyCap, char *outValue, size_t valueCap)
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
    amiga_uaeTrimRight(outKey);
    char *k = amiga_uaeTrimLeft(outKey);
    if (k != outKey) {
        memmove(outKey, k, strlen(k) + 1);
    }

    const char *v = eq + 1;
    while (*v == ' ' || *v == '\t') {
        v++;
    }
    strncpy(outValue, v, valueCap - 1);
    outValue[valueCap - 1] = '\0';
    amiga_uaeTrimRight(outValue);
    return outKey[0] ? 1 : 0;
}

static amiga_uae_kv_t *
amiga_uaeFindEntry(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
        if (amiga_uae_entries[i].key && strcmp(amiga_uae_entries[i].key, key) == 0) {
            return &amiga_uae_entries[i];
        }
    }
    return NULL;
}

static amiga_uae_kv_t *
amiga_uaeGetOrAddEntry(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    amiga_uae_kv_t *existing = amiga_uaeFindEntry(key);
    if (existing) {
        return existing;
    }
    if (amiga_uae_entryCount >= amiga_uae_entryCap) {
        size_t nextCap = amiga_uae_entryCap ? amiga_uae_entryCap * 2 : 64;
        amiga_uae_kv_t *next = (amiga_uae_kv_t *)alloc_realloc(amiga_uae_entries, nextCap * sizeof(*next));
        if (!next) {
            return NULL;
        }
        amiga_uae_entries = next;
        amiga_uae_entryCap = nextCap;
    }
    amiga_uae_kv_t *ent = &amiga_uae_entries[amiga_uae_entryCount++];
    memset(ent, 0, sizeof(*ent));
    ent->key = alloc_strdup(key);
    ent->value = alloc_strdup("");
    return ent;
}

static void
amiga_uaeRemoveEntry(const char *key)
{
    if (!key || !*key || !amiga_uae_entries) {
        return;
    }
    for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
        if (amiga_uae_entries[i].key && strcmp(amiga_uae_entries[i].key, key) == 0) {
            alloc_free(amiga_uae_entries[i].key);
            alloc_free(amiga_uae_entries[i].value);
            for (size_t j = i + 1; j < amiga_uae_entryCount; ++j) {
                amiga_uae_entries[j - 1] = amiga_uae_entries[j];
            }
            amiga_uae_entryCount--;
            return;
        }
    }
}

static int
amiga_uaeCompareEntriesByKey(const void *a, const void *b)
{
    const amiga_uae_kv_t *ea = (const amiga_uae_kv_t *)a;
    const amiga_uae_kv_t *eb = (const amiga_uae_kv_t *)b;
    const char *ka = ea && ea->key ? ea->key : "";
    const char *kb = eb && eb->key ? eb->key : "";
    return strcmp(ka, kb);
}

static int
amiga_uaeWriteAtomically(const char *dstPath, const char *tmpPath)
{
#ifdef _WIN32
    if (!MoveFileExA(tmpPath, dstPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return 0;
    }
    return 1;
#else
    return rename(tmpPath, dstPath) == 0 ? 1 : 0;
#endif
}

void
amiga_uaeClearPuaeOptions(void)
{
    if (amiga_uae_entries) {
        for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
            alloc_free(amiga_uae_entries[i].key);
            alloc_free(amiga_uae_entries[i].value);
        }
        alloc_free(amiga_uae_entries);
    }
    amiga_uae_entries = NULL;
    amiga_uae_entryCount = 0;
    amiga_uae_entryCap = 0;
    amiga_uae_dirty = 0;
    amiga_uae_loadedPath[0] = '\0';
    amiga_uae_floppy0[0] = '\0';
    amiga_uae_floppy1[0] = '\0';
}

int
amiga_uaeUaeOptionsDirty(void)
{
    return amiga_uae_dirty ? 1 : 0;
}

bool
amiga_uaeLoadUaeOptions(const char *uaePath)
{
    amiga_uaeClearPuaeOptions();
    if (!uaePath || !*uaePath) {
        return true;
    }
    strncpy(amiga_uae_loadedPath, uaePath, sizeof(amiga_uae_loadedPath) - 1);
    amiga_uae_loadedPath[sizeof(amiga_uae_loadedPath) - 1] = '\0';

    FILE *f = fopen(uaePath, "r");
    if (!f) {
        amiga_uae_dirty = 0;
        return true;
    }
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char key[1024];
        char value[7168];
        if (!amiga_uaeParseKeyValue(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        int drive = -1;
        if (amiga_uaeKeyIsFloppy(key, &drive)) {
            if (drive == 0) {
                strncpy(amiga_uae_floppy0, value, sizeof(amiga_uae_floppy0) - 1);
                amiga_uae_floppy0[sizeof(amiga_uae_floppy0) - 1] = '\0';
            } else if (drive == 1) {
                strncpy(amiga_uae_floppy1, value, sizeof(amiga_uae_floppy1) - 1);
                amiga_uae_floppy1[sizeof(amiga_uae_floppy1) - 1] = '\0';
            }
            continue;
        }
        if (!amiga_uaeKeyIsPuae(key)) {
            continue;
        }
        amiga_uae_kv_t *ent = amiga_uaeGetOrAddEntry(key);
        if (!ent) {
            continue;
        }
        alloc_free(ent->value);
        ent->value = alloc_strdup(value);
    }
    fclose(f);
    amiga_uae_dirty = 0;
    return true;
}

const char *
amiga_uaeGetPuaeOptionValue(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    amiga_uae_kv_t *ent = amiga_uaeFindEntry(key);
    return ent ? ent->value : NULL;
}

void
amiga_uaeSetPuaeOptionValue(const char *key, const char *value)
{
    if (!key || !*key) {
        return;
    }
    if (!amiga_uaeKeyIsPuae(key)) {
        return;
    }
    if (!value) {
        amiga_uaeRemoveEntry(key);
        amiga_uae_dirty = 1;
        return;
    }
    amiga_uae_kv_t *ent = amiga_uaeGetOrAddEntry(key);
    if (!ent) {
        return;
    }
    alloc_free(ent->value);
    ent->value = alloc_strdup(value);
    amiga_uae_dirty = 1;
}

const char *
amiga_uaeGetFloppyPath(int drive)
{
    if (drive == 0) {
        return amiga_uae_floppy0[0] ? amiga_uae_floppy0 : NULL;
    }
    if (drive == 1) {
        return amiga_uae_floppy1[0] ? amiga_uae_floppy1 : NULL;
    }
    return NULL;
}

void
amiga_uaeSetFloppyPath(int drive, const char *path)
{
    if (drive != 0 && drive != 1) {
        return;
    }
    const char *src = path ? path : "";
    if (drive == 0) {
        strncpy(amiga_uae_floppy0, src, sizeof(amiga_uae_floppy0) - 1);
        amiga_uae_floppy0[sizeof(amiga_uae_floppy0) - 1] = '\0';
    } else {
        strncpy(amiga_uae_floppy1, src, sizeof(amiga_uae_floppy1) - 1);
        amiga_uae_floppy1[sizeof(amiga_uae_floppy1) - 1] = '\0';
    }
    amiga_uae_dirty = 1;
}

bool
amiga_uaeWriteUaeOptionsToFile(const char *uaePath)
{
    if (!uaePath || !*uaePath) {
        return false;
    }
    char tmpPath[PATH_MAX];
    int tmpWritten = snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", uaePath);
    if (tmpWritten < 0 || tmpWritten >= (int)sizeof(tmpPath)) {
        return false;
    }

    FILE *in = fopen(uaePath, "r");
    FILE *out = fopen(tmpPath, "w");
    if (!out) {
        if (in) {
            fclose(in);
        }
        return false;
    }

    int lastWrittenHadNewline = 1;
    if (in) {
        char line[8192];
        while (fgets(line, sizeof(line), in)) {
            char key[1024];
            char value[7168];
            int isPuae = 0;
            if (amiga_uaeParseKeyValue(line, key, sizeof(key), value, sizeof(value))) {
                int drive = -1;
                if (amiga_uaeKeyIsPuae(key)) {
                    isPuae = 1;
                } else if (amiga_uaeKeyIsFloppy(key, &drive)) {
                    isPuae = 1;
                }
            }
            if (isPuae) {
                continue;
            }
            fputs(line, out);
            size_t len = strlen(line);
            lastWrittenHadNewline = (len > 0 && line[len - 1] == '\n') ? 1 : 0;
        }
        fclose(in);
    }

    if (!lastWrittenHadNewline) {
        fputc('\n', out);
    }

    if (amiga_uae_floppy0[0]) {
        fprintf(out, "floppy0=%s\n", amiga_uae_floppy0);
    }
    if (amiga_uae_floppy1[0]) {
        fprintf(out, "floppy1=%s\n", amiga_uae_floppy1);
    }

    if (amiga_uae_entryCount > 1) {
        qsort(amiga_uae_entries, amiga_uae_entryCount, sizeof(*amiga_uae_entries), amiga_uaeCompareEntriesByKey);
    }

    for (size_t i = 0; i < amiga_uae_entryCount; ++i) {
        const char *k = amiga_uae_entries[i].key;
        const char *v = amiga_uae_entries[i].value;
        if (!k || !*k) {
            continue;
        }
        if (!amiga_uaeKeyIsPuae(k)) {
            continue;
        }
        if (!v) {
            v = "";
        }
        fprintf(out, "%s=%s\n", k, v);
    }

    fclose(out);
    if (!amiga_uaeWriteAtomically(uaePath, tmpPath)) {
        remove(tmpPath);
        return false;
    }
    amiga_uae_dirty = 0;
    return true;
}

bool
amiga_uaeApplyPuaeOptionsToHost(const char *uaePath)
{
    if (!uaePath || !*uaePath) {
        return false;
    }
    FILE *f = fopen(uaePath, "r");
    if (!f) {
        return false;
    }
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char key[1024];
        char value[7168];
        if (!amiga_uaeParseKeyValue(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        if (!amiga_uaeKeyIsPuae(key)) {
            continue;
        }
        libretro_host_setCoreOption(key, value);
    }
    fclose(f);
    return true;
}
