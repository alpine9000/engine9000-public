/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#include "debugger.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "file.h"

int
file_getExeDir(char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
#ifdef _WIN32
    return w64_getExeDir(out, cap);
#elif defined(__APPLE__)
    char path[PATH_MAX]; uint32_t sz = (uint32_t)sizeof(path);
    if (_NSGetExecutablePath(path, &sz) == 0) {
        char rpath[PATH_MAX];
        const char *pp = realpath(path, rpath) ? rpath : path;
        size_t len = strlen(pp);
        while (len > 0 && pp[len-1] != '/') len--;
        if (len == 0) return 0;
        if (len >= cap) len = cap - 1;
        memcpy(out, pp, len); out[len] = '\0';
        return 1;
    }
#else
    char path[PATH_MAX]; ssize_t r = readlink("/proc/self/exe", path, sizeof(path)-1);
    if (r > 0) {
        path[r] = '\0';
        size_t len = (size_t)r;
        while (len > 0 && path[len-1] != '/') len--;
        if (len == 0) return 0;
        if (len >= cap) len = cap - 1;
        memcpy(out, path, len); out[len] = '\0';
        return 1;
    }
#endif
    return 0;
}

int
file_getAssetPath(const char *rel, char *out, size_t cap)
{
    if (!rel || !*rel || !out || cap == 0) return 0;
    char base[PATH_MAX];
    if (!file_getExeDir(base, sizeof(base))) return 0;
    size_t n = strlen(base);
    if (n >= cap) n = cap-1;
    memcpy(out, base, n);
    if (n > 0 && out[n-1] != '/' && n < cap-1) out[n++] = '/';
    size_t rl = strlen(rel);
    if (n + rl >= cap) rl = cap - 1 - n;
    memcpy(out + n, rel, rl);
    out[n+rl] = '\0';
    return 1;
}

static int
file_isExecutableFile(const char *p)
{
    if (!p || !*p) return 0;
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
#ifdef _WIN32
    if (_access(p, 0) != 0) return 0;
#else
    if (access(p, X_OK) != 0) return 0;
#endif
    return 1;
}

int
file_findInPath(const char *prog, char *out, size_t cap)
{
    if (!prog || !*prog || !out || cap == 0) return 0;
#ifdef _WIN32
    const char path_sep = ';';
#else
    const char path_sep = ':';
#endif
    // If prog contains a path separator, check directly
    if (strchr(prog, '/') || strchr(prog, '\\')) {
        if (file_isExecutableFile(prog)) {
            size_t l = strlen(prog); if (l >= cap) l = cap - 1; memcpy(out, prog, l); out[l] = '\0';
            return 1;
        }
        return 0;
    }
    const char *path = getenv("PATH");
    if (!path || !*path) return 0;
    const char *p = path; const char *seg = p;
    char buf[PATH_MAX];
    while (*p) {
#ifdef _WIN32
        if (*p == path_sep) {
#else
        if (*p == path_sep) {
#endif
            size_t sl = (size_t)(p - seg);
            if (sl == 0) {
                // Empty segment means current directory
                if (snprintf(buf, sizeof(buf), "%s", prog) < (int)sizeof(buf) && file_isExecutableFile(buf)) {
                    size_t l = strlen(buf); if (l >= cap) l = cap - 1; memcpy(out, buf, l); out[l] = '\0'; return 1;
                }
            } else {
                if (sl >= sizeof(buf)) sl = sizeof(buf)-1;
                memcpy(buf, seg, sl); buf[sl] = '\0';
                size_t bl = strlen(buf);
                if (bl + 1 + strlen(prog) < sizeof(buf)) {
                    if (bl > 0 && buf[bl-1] != '/' && buf[bl-1] != '\\') { buf[bl++] = '/'; buf[bl] = '\0'; }
                    strncat(buf, prog, sizeof(buf) - bl - 1);
                    if (file_isExecutableFile(buf)) { size_t l = strlen(buf); if (l >= cap) l = cap - 1; memcpy(out, buf, l); out[l] = '\0'; return 1; }
                }
            }
            seg = p + 1;
        }
        p++;
    }
    // Last segment
    if (seg) {
        if (*seg == '\0') {
            if (file_isExecutableFile(prog)) { size_t l = strlen(prog); if (l >= cap) l = cap - 1; memcpy(out, prog, l); out[l] = '\0'; return 1; }
        } else {
            size_t sl = (size_t)(p - seg);
            if (sl >= sizeof(buf)) sl = sizeof(buf)-1;
            memcpy(buf, seg, sl); buf[sl] = '\0';
            size_t bl = strlen(buf);
            if (bl + 1 + strlen(prog) < sizeof(buf)) {
                if (bl > 0 && buf[bl-1] != '/') { buf[bl++] = '/'; buf[bl] = '\0'; }
                strncat(buf, prog, sizeof(buf) - bl - 1);
                if (file_isExecutableFile(buf)) { size_t l = strlen(buf); if (l >= cap) l = cap - 1; memcpy(out, buf, l); out[l] = '\0'; return 1; }
            }
        }
    }
    return 0;
}
