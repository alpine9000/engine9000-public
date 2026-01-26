/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "clipboard.h"

#include <SDL.h>
#include <SDL_image.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "debug.h"

static int
clipboard_makeTempPath(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
#ifdef _WIN32
    char dir[MAX_PATH];
    DWORD dir_len = GetTempPathA(sizeof(dir), dir);
    if (dir_len == 0 || dir_len >= sizeof(dir)) {
        return 0;
    }
    char tmp[MAX_PATH];
    if (GetTempFileNameA(dir, "e9k", 0, tmp) == 0) {
        return 0;
    }
    int written = snprintf(out, cap, "%s", tmp);
    return (written > 0 && (size_t)written < cap) ? 1 : 0;
#else
    char tmpl[] = "/tmp/e9k-clipboard-XXXXXX.png";
    int fd = mkstemps(tmpl, 4);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    strncpy(out, tmpl, cap - 1);
    out[cap - 1] = '\0';
    return 1;
#endif
}

static int
clipboard_readFile(const char *path, uint8_t **out, size_t *out_size)
{
    if (!path || !*path || !out || !out_size) {
        return 0;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long len = ftell(f);
    if (len <= 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) {
        free(buf);
        return 0;
    }
    *out = buf;
    *out_size = (size_t)len;
    return 1;
}

int
clipboard_setImageXRGB8888(const uint8_t *data, int width, int height, size_t pitch)
{
    if (!data || width <= 0 || height <= 0 || pitch == 0) {
        return 0;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void *)data, width, height, 32, (int)pitch, SDL_PIXELFORMAT_XRGB8888);
    if (!surface) {
        debug_error("clipboard: SDL_CreateRGBSurfaceWithFormatFrom failed: %s", SDL_GetError());
        return 0;
    }
    char path[PATH_MAX];
    if (!clipboard_makeTempPath(path, sizeof(path))) {
        SDL_FreeSurface(surface);
        return 0;
    }
    if (IMG_SavePNG(surface, path) != 0) {
        debug_error("clipboard: IMG_SavePNG failed: %s", IMG_GetError());
        SDL_FreeSurface(surface);
#ifdef _WIN32
        _unlink(path);
#else
        unlink(path);
#endif
        return 0;
    }
    SDL_FreeSurface(surface);

    uint8_t *png_data = NULL;
    size_t png_size = 0;
    if (!clipboard_readFile(path, &png_data, &png_size)) {
#ifdef _WIN32
        _unlink(path);
#else
        unlink(path);
#endif
        return 0;
    }
#ifdef _WIN32
    _unlink(path);
#else
    unlink(path);
#endif

    int ok = clipboard_setPng(png_data, png_size);
    free(png_data);
    return ok;
}
