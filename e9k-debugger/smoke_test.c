/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include <SDL.h>
#include <SDL_image.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "smoke_test.h"
#include "debugger.h"
#include "debug.h"
#include "libretro_host.h"

static char smoke_test_folder[PATH_MAX];
static int smoke_test_enabled = 0;
static smoke_test_mode_t smoke_test_mode = SMOKE_TEST_MODE_NONE;
static int smoke_test_openOnFail = 0;

void
smoke_test_setFolder(const char *path)
{
    if (!path || !*path) {
        smoke_test_folder[0] = '\0';
        smoke_test_enabled = 0;
        return;
    }
    strncpy(smoke_test_folder, path, sizeof(smoke_test_folder) - 1);
    smoke_test_folder[sizeof(smoke_test_folder) - 1] = '\0';
}

void
smoke_test_setMode(smoke_test_mode_t mode)
{
    smoke_test_mode = mode;
}

smoke_test_mode_t
smoke_test_getMode(void)
{
    return smoke_test_mode;
}

void
smoke_test_setOpenOnFail(int enable)
{
    smoke_test_openOnFail = enable ? 1 : 0;
}

static int
smoke_test_makeDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
#ifdef _WIN32
    if (_mkdir(path) == 0) {
        return 1;
    }
    if (errno == EEXIST) {
        return 1;
    }
    return 0;
#else
    if (mkdir(path, 0755) == 0) {
        return 1;
    }
    if (errno == EEXIST) {
        return 1;
    }
    return 0;
#endif
}

static void
smoke_test_clearFolder(const char *path)
{
    if (!path || !*path) {
        return;
    }
#ifdef _WIN32
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*.*", path);
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
            continue;
        }
        const char *ext = strrchr(data.cFileName, '.');
        if (!ext) {
            continue;
        }
        if (_stricmp(ext, ".png") != 0 && _stricmp(ext, ".inp") != 0) {
            continue;
        }
        char full[PATH_MAX];
        if (!debugger_platform_pathJoin(full, sizeof(full), path, data.cFileName)) {
            continue;
        }
        DeleteFileA(full);
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR *dir = opendir(path);
    if (!dir) {
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext) {
            continue;
        }
        if (strcmp(ext, ".png") != 0 && strcmp(ext, ".inp") != 0) {
            continue;
        }
        char full[PATH_MAX];
        if (!debugger_platform_pathJoin(full, sizeof(full), path, ent->d_name)) {
            continue;
        }
        unlink(full);
    }
    closedir(dir);
#endif
}

int
smoke_test_init(void)
{
    if (!smoke_test_folder[0]) {
        smoke_test_enabled = 0;
        return 1;
    }
    if (!smoke_test_makeDir(smoke_test_folder)) {
        debug_error("smoke-test: failed to create folder %s", smoke_test_folder);
        smoke_test_enabled = 0;
        return 0;
    }
    if (smoke_test_mode == SMOKE_TEST_MODE_RECORD) {
        smoke_test_clearFolder(smoke_test_folder);
    }
    if (smoke_test_mode == SMOKE_TEST_MODE_NONE) {
        smoke_test_enabled = 0;
        return 1;
    }
    smoke_test_enabled = 1;
    return 1;
}

void
smoke_test_shutdown(void)
{
    smoke_test_enabled = 0;
    smoke_test_mode = SMOKE_TEST_MODE_NONE;
    smoke_test_openOnFail = 0;
}

int
smoke_test_isEnabled(void)
{
    return smoke_test_enabled ? 1 : 0;
}

int
smoke_test_getRecordPath(char *out, size_t cap)
{
    if (!out || cap == 0 || !smoke_test_folder[0]) {
        return 0;
    }
    return debugger_platform_pathJoin(out, cap, smoke_test_folder, "smoketest.inp");
}

static int
smoke_test_writeDiffImage(uint64_t frame, const uint8_t *data, int width, int height, size_t pitch,
                          char *outPath, size_t cap)
{
    char name[64];
    snprintf(name, sizeof(name), "diff-%llu.png", (unsigned long long)frame);
    char path[PATH_MAX];
    if (!debugger_platform_pathJoin(path, sizeof(path), smoke_test_folder, name)) {
        return 0;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void *)data, width, height, 32, (int)pitch, SDL_PIXELFORMAT_XRGB8888);
    if (!surface) {
        return 0;
    }
    if (IMG_SavePNG(surface, path) == 0 && outPath && cap > 0) {
        strncpy(outPath, path, cap - 1);
        outPath[cap - 1] = '\0';
    }
    SDL_FreeSurface(surface);
    return 1;
}

static int
smoke_test_writeDiffScript(uint64_t frame, const char *refPath, char *outMontage, size_t cap)
{
    if (!refPath || !*refPath) {
        return 0;
    }
    char testName[64];
    snprintf(testName, sizeof(testName), "diff-%llu.png", (unsigned long long)frame);
    char testPath[PATH_MAX];
    if (!debugger_platform_pathJoin(testPath, sizeof(testPath), smoke_test_folder, testName)) {
        return 0;
    }
    char compareName[64];
    snprintf(compareName, sizeof(compareName), "diff-%llu-compare.png", (unsigned long long)frame);
    char comparePath[PATH_MAX];
    if (!debugger_platform_pathJoin(comparePath, sizeof(comparePath), smoke_test_folder, compareName)) {
        return 0;
    }
    char montageName[64];
    snprintf(montageName, sizeof(montageName), "diff-%llu-triple.png", (unsigned long long)frame);
    char montagePath[PATH_MAX];
    if (!debugger_platform_pathJoin(montagePath, sizeof(montagePath), smoke_test_folder, montageName)) {
        return 0;
    }
#ifdef _WIN32
    char scriptName[64];
    snprintf(scriptName, sizeof(scriptName), "diff-%llu.cmd", (unsigned long long)frame);
#else
    char scriptName[64];
    snprintf(scriptName, sizeof(scriptName), "diff-%llu.sh", (unsigned long long)frame);
#endif
    char scriptPath[PATH_MAX];
    if (!debugger_platform_pathJoin(scriptPath, sizeof(scriptPath), smoke_test_folder, scriptName)) {
        return 0;
    }
    FILE *fp = fopen(scriptPath, "w");
    if (!fp) {
        return 0;
    }
#ifndef _WIN32
    fprintf(fp, "#!/bin/sh\n");
#endif
    fprintf(fp, "magick compare -metric AE \"%s\" \"%s\" \"%s\"\n",
            refPath, testPath, comparePath);
    fprintf(fp, "magick montage \"%s\" \"%s\" \"%s\" -tile 3x1 -geometry +0+0 \"%s\"\n",
            refPath, testPath, comparePath, montagePath);
    fclose(fp);
#ifdef _WIN32
    char cmdCompare[PATH_MAX * 3];
    char cmdMontage[PATH_MAX * 4];
    snprintf(cmdCompare, sizeof(cmdCompare),
             "magick compare -metric AE \"%s\" \"%s\" \"%s\"",
             refPath, testPath, comparePath);
    snprintf(cmdMontage, sizeof(cmdMontage),
             "magick montage \"%s\" \"%s\" \"%s\" -tile 3x1 -geometry +0+0 \"%s\"",
             refPath, testPath, comparePath, montagePath);
#else
    char cmdCompare[PATH_MAX * 3];
    char cmdMontage[PATH_MAX * 4];
    snprintf(cmdCompare, sizeof(cmdCompare),
             "magick compare -metric AE \"%s\" \"%s\" \"%s\" >/dev/null 2>&1",
             refPath, testPath, comparePath);
    snprintf(cmdMontage, sizeof(cmdMontage),
             "magick montage \"%s\" \"%s\" \"%s\" -tile 3x1 -geometry +0+0 \"%s\" >/dev/null 2>&1",
             refPath, testPath, comparePath, montagePath);
#endif
    system(cmdCompare);
    system(cmdMontage);
    if (outMontage && cap > 0) {
        strncpy(outMontage, montagePath, cap - 1);
        outMontage[cap - 1] = '\0';
    }
    return 1;
}

static void
smoke_test_openImage(const char *path)
{
    if (!path || !*path) {
        return;
    }
#ifdef _WIN32
    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", path);
#elif defined(__APPLE__)
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "open \"%s\" >/dev/null 2>&1", path);
#else
    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1", path);
#endif
    system(cmd);
}

static int
smoke_test_compareFrame(uint64_t frame, const uint8_t *data, int width, int height, size_t pitch)
{
    char diffPath[PATH_MAX];
    char montagePath[PATH_MAX];
    diffPath[0] = '\0';
    montagePath[0] = '\0';
    char name[64];
    snprintf(name, sizeof(name), "%llu.png", (unsigned long long)frame);
    char path[PATH_MAX];
    if (!debugger_platform_pathJoin(path, sizeof(path), smoke_test_folder, name)) {
        smoke_test_writeDiffImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        smoke_test_writeDiffScript(frame, path, montagePath, sizeof(montagePath));
        if (smoke_test_openOnFail && montagePath[0]) {
            smoke_test_openImage(montagePath);
        }
        debug_printf("Smoke test failed at frame #%llu (%s)", (unsigned long long)frame,
                     montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            return 2;
        }
        debug_error("smoke-test: stat failed for %s", path);
        smoke_test_writeDiffImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        smoke_test_writeDiffScript(frame, path, montagePath, sizeof(montagePath));
        if (smoke_test_openOnFail && montagePath[0]) {
            smoke_test_openImage(montagePath);
        }
        debug_printf("Smoke test failed at frame #%llu (%s)", (unsigned long long)frame,
                     montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }
    SDL_Surface *loaded = IMG_Load(path);
    if (!loaded) {
        debug_error("smoke-test: IMG_Load failed: %s", IMG_GetError());
        smoke_test_writeDiffImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        smoke_test_writeDiffScript(frame, path, montagePath, sizeof(montagePath));
        if (smoke_test_openOnFail && montagePath[0]) {
            smoke_test_openImage(montagePath);
        }
        debug_printf("Smoke test failed at frame #%llu (%s)", (unsigned long long)frame,
                     montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }
    SDL_Surface *converted = loaded;
    if (loaded->format->format != SDL_PIXELFORMAT_XRGB8888) {
        converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_XRGB8888, 0);
        SDL_FreeSurface(loaded);
    }
    if (!converted) {
        debug_error("smoke-test: SDL_ConvertSurfaceFormat failed: %s", SDL_GetError());
        smoke_test_writeDiffImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        smoke_test_writeDiffScript(frame, path, montagePath, sizeof(montagePath));
        if (smoke_test_openOnFail && montagePath[0]) {
            smoke_test_openImage(montagePath);
        }
        debug_printf("Smoke test failed at frame #%llu (%s)", (unsigned long long)frame,
                     montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }
    int fail = 0;
            if (SDL_MUSTLOCK(converted)) {
            if (SDL_LockSurface(converted) != 0) {
                debug_error("smoke-test: SDL_LockSurface failed: %s", SDL_GetError());
                smoke_test_writeDiffImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
                smoke_test_writeDiffScript(frame, path, montagePath, sizeof(montagePath));
                if (smoke_test_openOnFail && montagePath[0]) {
                    smoke_test_openImage(montagePath);
                }
                debug_printf("Smoke test failed at frame #%llu (%s)", (unsigned long long)frame,
                             montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
                SDL_FreeSurface(converted);
                return 1;
            }
        }
    if (converted->w != width || converted->h != height) {
        fail = 1;
    } else {
        const uint8_t *src = (const uint8_t *)converted->pixels;
        const uint32_t mask = 0x00ffffffu;
        for (int y = 0; y < height && !fail; ++y) {
            const uint8_t *row_a = src + (size_t)y * (size_t)converted->pitch;
            const uint8_t *row_b = data + (size_t)y * pitch;
            for (int x = 0; x < width; ++x) {
                const uint32_t *pa = (const uint32_t *)(row_a + (size_t)x * 4);
                const uint32_t *pb = (const uint32_t *)(row_b + (size_t)x * 4);
                if (((*pa) & mask) != ((*pb) & mask)) {
                    fail = 1;
                    break;
                }
            }
        }
    }
    if (SDL_MUSTLOCK(converted)) {
        SDL_UnlockSurface(converted);
    }
    SDL_FreeSurface(converted);
    if (fail) {
        smoke_test_writeDiffImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        smoke_test_writeDiffScript(frame, path, montagePath, sizeof(montagePath));
        if (smoke_test_openOnFail && montagePath[0]) {
            smoke_test_openImage(montagePath);
        }
        debug_printf("Smoke test failed at frame #%llu (%s)", (unsigned long long)frame,
                     montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }
    return 0;
}

int
smoke_test_captureFrame(uint64_t frame)
{
    if (!smoke_test_enabled) {
        return 0;
    }
    const uint8_t *data = NULL;
    int width = 0;
    int height = 0;
    size_t pitch = 0;
    if (!libretro_host_getFrame(&data, &width, &height, &pitch)) {
        return 0;
    }
    if (smoke_test_mode == SMOKE_TEST_MODE_COMPARE) {
        return smoke_test_compareFrame(frame, data, width, height, pitch);
    }
    char name[64];
    snprintf(name, sizeof(name), "%llu.png", (unsigned long long)frame);
    char path[PATH_MAX];
    if (!debugger_platform_pathJoin(path, sizeof(path), smoke_test_folder, name)) {
        return 0;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void *)data, width, height, 32, (int)pitch, SDL_PIXELFORMAT_XRGB8888);
    if (!surface) {
        debug_error("smoke-test: SDL_CreateRGBSurfaceWithFormatFrom failed: %s", SDL_GetError());
        return 0;
    }
    if (IMG_SavePNG(surface, path) != 0) {
        debug_error("smoke-test: IMG_SavePNG failed: %s", IMG_GetError());
    }
    SDL_FreeSurface(surface);
    return 0;
}
