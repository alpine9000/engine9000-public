/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "clipboard.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

static void
clipboard_logWin32Error(const char *what)
{
    DWORD err = GetLastError();
    char msg[256] = {0};
    DWORD got = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, msg, sizeof(msg), NULL);
    if (got == 0) {
        fprintf(stderr, "clipboard: %s failed with error %lu\n", what, (unsigned long)err);
        return;
    }
    for (DWORD i = got; i > 0; --i) {
        char c = msg[i - 1];
        if (c != '\r' && c != '\n') {
            break;
        }
        msg[i - 1] = '\0';
    }
    fprintf(stderr, "clipboard: %s failed with error %lu: %s\n", what, (unsigned long)err, msg);
}

int
clipboard_setPng(const void *png_data, size_t png_size)
{
    if (!png_data || png_size == 0) {
        return 0;
    }
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, png_data, png_size)) {
        fprintf(stderr, "clipboard: libpng read error: %s\n", img.message);
        return 0;
    }
    img.format = PNG_FORMAT_BGRA;
    size_t rowbytes = PNG_IMAGE_ROW_STRIDE(img);
    size_t total_bytes = rowbytes * img.height;
    unsigned char *buffer = (unsigned char *)malloc(total_bytes);
    if (!buffer) {
        png_image_free(&img);
        return 0;
    }
    if (!png_image_finish_read(&img, NULL, buffer, rowbytes, NULL)) {
        fprintf(stderr, "clipboard: libpng finish read error: %s\n", img.message);
        png_image_free(&img);
        free(buffer);
        return 0;
    }
    size_t dib_size = sizeof(BITMAPV5HEADER) + total_bytes;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, dib_size);
    if (!hMem) {
        clipboard_logWin32Error("GlobalAlloc");
        free(buffer);
        return 0;
    }
    void *ptr = GlobalLock(hMem);
    if (!ptr) {
        clipboard_logWin32Error("GlobalLock");
        GlobalFree(hMem);
        free(buffer);
        return 0;
    }
    BITMAPV5HEADER *bih = (BITMAPV5HEADER *)ptr;
    ZeroMemory(bih, sizeof(*bih));
    bih->bV5Size = sizeof(BITMAPV5HEADER);
    bih->bV5Width = (LONG)img.width;
    bih->bV5Height = -((LONG)img.height);
    bih->bV5Planes = 1;
    bih->bV5BitCount = 32;
    bih->bV5Compression = BI_BITFIELDS;
    bih->bV5SizeImage = (DWORD)total_bytes;
    bih->bV5RedMask = 0x00FF0000;
    bih->bV5GreenMask = 0x0000FF00;
    bih->bV5BlueMask = 0x000000FF;
    bih->bV5AlphaMask = 0xFF000000;
    bih->bV5CSType = LCS_sRGB;
    bih->bV5Intent = LCS_GM_IMAGES;
    unsigned char *dst = (unsigned char *)(bih + 1);
    memcpy(dst, buffer, total_bytes);
    GlobalUnlock(hMem);
    free(buffer);
    if (!OpenClipboard(NULL)) {
        clipboard_logWin32Error("OpenClipboard");
        GlobalFree(hMem);
        return 0;
    }
    if (!EmptyClipboard()) {
        clipboard_logWin32Error("EmptyClipboard");
        CloseClipboard();
        GlobalFree(hMem);
        return 0;
    }
    if (!SetClipboardData(CF_DIBV5, hMem)) {
        clipboard_logWin32Error("SetClipboardData(CF_DIBV5)");
        CloseClipboard();
        GlobalFree(hMem);
        return 0;
    }
    CloseClipboard();
    return 1;
}
