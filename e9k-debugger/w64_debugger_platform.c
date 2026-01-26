/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "w64_debugger_platform.h"
#include "debugger.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <errno.h>

int
debugger_platform_pathJoin(char *out, size_t cap, const char *dir, const char *name)
{
    if (!out || cap == 0 || !dir || !*dir || !name || !*name) {
        return 0;
    }
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int need_sep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    size_t total = dlen + (need_sep ? 1 : 0) + nlen;
    if (total + 1 > cap) {
        return 0;
    }
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (need_sep) {
        out[pos++] = '\\';
    }
    memcpy(out + pos, name, nlen);
    out[pos + nlen] = '\0';
    return 1;
}

int
debugger_platform_scanFolder(const char *folder, int (*cb)(const char *path, void *user), void *user)
{
    if (!folder || !*folder || !cb) {
        return 0;
    }
    char pattern[PATH_MAX];
    if (!debugger_platform_pathJoin(pattern, sizeof(pattern), folder, "*")) {
        return 0;
    }
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        char full[PATH_MAX];
        if (!debugger_platform_pathJoin(full, sizeof(full), folder, data.cFileName)) {
            continue;
        }
        if (!cb(full, user)) {
            FindClose(h);
            return 0;
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
    return 1;
}

char *
debugger_configPath(void)
{
    static char pathbuf[1024];
    const char *home = getenv("APPDATA");
    if (!home || !*home) {
        home = getenv("USERPROFILE");
    }
    if (!home || !*home) {
        return NULL;
    }
    snprintf(pathbuf, sizeof(pathbuf), "%s\\e9k-debugger.cfg", home);
    return pathbuf;
}

void
debugger_platform_setDefaults(e9k_path_config_t *config)
{
    if (!config) {
        return;
    }
    snprintf(config->corePath, sizeof(config->corePath), "./system/geolith_libretro.dll");
    snprintf(config->biosDir, sizeof(config->biosDir), "./system");
    snprintf(config->savesDir, sizeof(config->savesDir), "./saves");
    snprintf(config->sourceDir, sizeof(config->sourceDir), ".");
    config->audioBufferMs = 250;
    config->skipBiosLogo = 0;
    strncpy(config->systemType, "aes", sizeof(config->systemType) - 1);
    config->systemType[sizeof(config->systemType) - 1] = '\0';
    config->elfPath[0] = '\0';
}

ssize_t
w64_getline(char **lineptr, size_t *n, FILE *stream)
{
    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }
    if (!*lineptr || *n == 0) {
        *n = 128;
        *lineptr = (char*)malloc(*n);
        if (!*lineptr) {
            errno = ENOMEM;
            return -1;
        }
    }
    size_t pos = 0;
    int ch;
    while ((ch = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_cap = (*n) * 2;
            char *tmp = (char*)realloc(*lineptr, new_cap);
            if (!tmp) {
                errno = ENOMEM;
                return -1;
            }
            *lineptr = tmp;
            *n = new_cap;
        }
        (*lineptr)[pos++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (pos == 0 && ch == EOF) {
        return -1;
    }
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}

int
w64_getExeDir(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    char path[PATH_MAX];
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (len == 0 || len >= (DWORD)sizeof(path)) {
        return 0;
    }
    size_t slen = (size_t)len;
    while (slen > 0 && path[slen - 1] != '\\' && path[slen - 1] != '/') {
        slen--;
    }
    if (slen == 0) {
        return 0;
    }
    if (slen >= cap) {
        slen = cap - 1;
    }
    memcpy(out, path, slen);
    out[slen] = '\0';
    return 1;
}
