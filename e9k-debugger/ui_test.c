/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_image.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "ui_test.h"
#include "debugger.h"
#include "debug.h"
#include "config.h"
#include "input_record.h"
#include "libretro_host.h"
#include "strutil.h"

typedef struct ui_test_raw_frame_header_v2 {
    char magic[8];
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t dataSize;
    uint32_t flags;
    uint32_t reserved;
} ui_test_raw_frame_header_v2_t;

static char ui_test_folder[PATH_MAX];
static int ui_test_enabled = 0;
static ui_test_mode_t ui_test_mode = UI_TEST_MODE_NONE;
static int ui_test_failed = 0;
static int ui_test_openOnFail = 0;
static int ui_test_exitCode = -1;
static uint8_t *ui_test_prevFrame = NULL;
static size_t ui_test_prevStride = 0;
static int ui_test_prevWidth = 0;
static int ui_test_prevHeight = 0;
static int ui_test_prevValid = 0;
static int ui_test_pendingMissingFrameValid = 0;
static uint64_t ui_test_pendingMissingFrame = 0;
static uint8_t *ui_test_captureFrameBuf = NULL;
static size_t ui_test_captureFrameCap = 0;
static uint8_t *ui_test_expectedRawFrameBuf = NULL;
static size_t ui_test_expectedRawFrameCap = 0;
static uint8_t *ui_test_rawCompressedBuf = NULL;
static size_t ui_test_rawCompressedCap = 0;
static const char ui_test_sessionConfigName[] = ".e9k-debugger.cfg";
static const char ui_test_rawMagicV2[8] = { 'E', '9', 'K', 'R', 'A', 'Z', '0', '1' };

static void
ui_test_clearTempConfigOnFirstRun(void);

static int
ui_test_restartCount(void)
{
    int count = debugger_getTestRestartCount();
    return count > 0 ? count : 0;
}

static void
ui_test_prefix(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    int restartCount = ui_test_restartCount();
    if (restartCount <= 0) {
        out[0] = '\0';
        return;
    }
    snprintf(out, cap, "r%d-", restartCount);
}

static void
ui_test_formatFrameName(char *out, size_t cap, uint64_t frame)
{
    if (!out || cap == 0) {
        return;
    }
    char prefix[32];
    ui_test_prefix(prefix, sizeof(prefix));
    if (prefix[0]) {
        snprintf(out, cap, "%s%llu.png", prefix, (unsigned long long)frame);
    } else {
        snprintf(out, cap, "%llu.png", (unsigned long long)frame);
    }
}

static void
ui_test_formatFrameRawName(char *out, size_t cap, uint64_t frame)
{
    if (!out || cap == 0) {
        return;
    }
    char prefix[32];
    ui_test_prefix(prefix, sizeof(prefix));
    if (prefix[0]) {
        snprintf(out, cap, "%s%llu.rawz", prefix, (unsigned long long)frame);
    } else {
        snprintf(out, cap, "%llu.rawz", (unsigned long long)frame);
    }
}

static void
ui_test_copySanitizedPathPart(char *out, size_t cap, const char *path, size_t start, size_t end, const char *fallback)
{
    if (!out || cap == 0) {
        return;
    }
    size_t pos = 0;
    for (size_t i = start; i < end && pos < cap - 1; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            out[pos++] = (char)c;
        } else {
            out[pos++] = '-';
        }
    }
    out[pos] = '\0';
    if (!out[0]) {
        strutil_strlcpy(out, cap, fallback);
    }
}

static void
ui_test_formatArtifactIdentity(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    const char *path = ui_test_folder;
    size_t pathEnd = path ? strlen(path) : 0;
    while (pathEnd > 0 && (path[pathEnd - 1] == '/' || path[pathEnd - 1] == '\\')) {
        pathEnd--;
    }
    size_t testStart = pathEnd;
    while (testStart > 0 && path[testStart - 1] != '/' && path[testStart - 1] != '\\') {
        testStart--;
    }
    size_t parentEnd = testStart;
    while (parentEnd > 0 && (path[parentEnd - 1] == '/' || path[parentEnd - 1] == '\\')) {
        parentEnd--;
    }
    size_t parentStart = parentEnd;
    while (parentStart > 0 && path[parentStart - 1] != '/' && path[parentStart - 1] != '\\') {
        parentStart--;
    }

    char parentName[64];
    char testName[96];
    ui_test_copySanitizedPathPart(parentName, sizeof(parentName), path, parentStart, parentEnd, "test");
    ui_test_copySanitizedPathPart(testName, sizeof(testName), path, testStart, pathEnd, "case");
    strutil_join3Trunc(out, cap, parentName, "-", testName);
}

static void
ui_test_formatArtifactName(char *out, size_t cap, uint64_t frame, const char *suffix)
{
    if (!out || cap == 0) {
        return;
    }
    char identity[192];
    ui_test_formatArtifactIdentity(identity, sizeof(identity));
    char prefix[32];
    ui_test_prefix(prefix, sizeof(prefix));
    char framePart[64];
    snprintf(framePart, sizeof(framePart), "%llu", (unsigned long long)frame);
    char prefixedFrame[96];
    strutil_join2Trunc(prefixedFrame, sizeof(prefixedFrame), prefix, framePart);
    char artifactBase[256];
    strutil_join3Trunc(artifactBase, sizeof(artifactBase), identity, "-", prefixedFrame);
    strutil_join2Trunc(out, cap, artifactBase, suffix ? suffix : "");
}

void
ui_test_setFolder(const char *path)
{
    if (!path || !*path) {
        ui_test_folder[0] = '\0';
        ui_test_enabled = 0;
        return;
    }
    strncpy(ui_test_folder, path, sizeof(ui_test_folder) - 1);
    ui_test_folder[sizeof(ui_test_folder) - 1] = '\0';
}

void
ui_test_setMode(ui_test_mode_t mode)
{
    ui_test_mode = mode;
}

ui_test_mode_t
ui_test_getMode(void)
{
    return ui_test_mode;
}

void
ui_test_setOpenOnFail(int enable)
{
    ui_test_openOnFail = enable ? 1 : 0;
}

void
ui_test_registerRequestedMode(const char *folder, ui_test_mode_t mode)
{
    ui_test_setFolder(folder);
    ui_test_setMode(mode);
}

const char *
ui_test_getFolder(void)
{
    return ui_test_folder[0] ? ui_test_folder : NULL;
}

int
ui_test_hasFailed(void)
{
    return ui_test_failed ? 1 : 0;
}

int
ui_test_getExitCode(void)
{
    return ui_test_exitCode;
}

static int
ui_test_makeDir(const char *path)
{
    return debugger_platform_makeDir(path);
}

static void
ui_test_basename(const char *path, const char **nameOut)
{
    if (!nameOut) {
        return;
    }
    if (!path || !*path) {
        *nameOut = "";
        return;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *base = slash > back ? slash : back;
    *nameOut = base ? base + 1 : path;
}

static int
ui_test_hasSuffixCaseInsensitive(const char *name, const char *suffix)
{
    if (!name || !suffix) {
        return 0;
    }
    size_t nameLen = strlen(name);
    size_t suffixLen = strlen(suffix);
    if (suffixLen == 0 || nameLen < suffixLen) {
        return 0;
    }
    const char *tail = name + (nameLen - suffixLen);
    for (size_t i = 0; i < suffixLen; ++i) {
        if (tolower((unsigned char)tail[i]) != tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static int
ui_test_clearFolderEntry(const char *path, void *user)
{
    (void)user;
    const char *name = NULL;
    ui_test_basename(path, &name);
    if (ui_test_hasSuffixCaseInsensitive(name, ".png") ||
        ui_test_hasSuffixCaseInsensitive(name, ".rawz") ||
        ui_test_hasSuffixCaseInsensitive(name, ".raw") ||
        ui_test_hasSuffixCaseInsensitive(name, ".inp") ||
        ui_test_hasSuffixCaseInsensitive(name, ".evt") ||
        ui_test_hasSuffixCaseInsensitive(name, ".json")) {
        remove(path);
    }
    return 1;
}

static void
ui_test_clearFolder(const char *path)
{
    if (!path || !*path) {
        return;
    }
    debugger_platform_scanFolder(path, ui_test_clearFolderEntry, NULL);
}

static int
ui_test_clearPngOnlyEntry(const char *path, void *user)
{
    (void)user;
    const char *name = NULL;
    ui_test_basename(path, &name);
    if (ui_test_hasSuffixCaseInsensitive(name, ".png") ||
        ui_test_hasSuffixCaseInsensitive(name, ".rawz") ||
        ui_test_hasSuffixCaseInsensitive(name, ".raw")) {
        remove(path);
    }
    return 1;
}

static void
ui_test_clearPngOnly(const char *path)
{
    if (!path || !*path) {
        return;
    }
    debugger_platform_scanFolder(path, ui_test_clearPngOnlyEntry, NULL);
}

static int
ui_test_clearMismatchEntry(const char *path, void *user)
{
    (void)user;
    const char *name = NULL;
    ui_test_basename(path, &name);
    char identity[192];
    ui_test_formatArtifactIdentity(identity, sizeof(identity));
    char identityPrefix[256];
    strutil_join2Trunc(identityPrefix, sizeof(identityPrefix), identity, "-");
    if (strstr(name, "mismatch-") ||
        strncmp(name, identityPrefix, strlen(identityPrefix)) == 0) {
        remove(path);
    }
    return 1;
}

static void
ui_test_clearMismatchArtifacts(const char *path)
{
    if (!path || !*path) {
        return;
    }
    debugger_platform_scanFolder(path, ui_test_clearMismatchEntry, NULL);
}

int
ui_test_init(void)
{
    ui_test_prevValid = 0;
    ui_test_prevStride = 0;
    ui_test_prevWidth = 0;
    ui_test_prevHeight = 0;
    ui_test_failed = 0;
    ui_test_exitCode = -1;
    ui_test_pendingMissingFrameValid = 0;
    ui_test_pendingMissingFrame = 0;
    if (!ui_test_folder[0]) {
        ui_test_enabled = 0;
        return 1;
    }
    if (!ui_test_makeDir(ui_test_folder)) {
        debug_error("ui-test: failed to create folder %s", ui_test_folder);
        ui_test_enabled = 0;
        return 0;
    }
    ui_test_clearTempConfigOnFirstRun();
    if (ui_test_restartCount() == 0) {
        ui_test_clearMismatchArtifacts(ui_test_folder);
    }
    if (ui_test_mode == UI_TEST_MODE_RECORD && ui_test_restartCount() == 0) {
        ui_test_clearFolder(ui_test_folder);
    } else if (ui_test_mode == UI_TEST_MODE_REMAKE && ui_test_restartCount() == 0) {
        ui_test_clearPngOnly(ui_test_folder);
    }
    if (ui_test_mode == UI_TEST_MODE_NONE) {
        ui_test_enabled = 0;
        return 1;
    }
    ui_test_enabled = 1;
    return 1;
}

void
ui_test_shutdown(void)
{
    ui_test_enabled = 0;
    ui_test_mode = UI_TEST_MODE_NONE;
    ui_test_failed = 0;
    ui_test_exitCode = -1;
    if (ui_test_prevFrame) {
        free(ui_test_prevFrame);
        ui_test_prevFrame = NULL;
    }
    if (ui_test_captureFrameBuf) {
        free(ui_test_captureFrameBuf);
        ui_test_captureFrameBuf = NULL;
    }
    if (ui_test_expectedRawFrameBuf) {
        free(ui_test_expectedRawFrameBuf);
        ui_test_expectedRawFrameBuf = NULL;
    }
    if (ui_test_rawCompressedBuf) {
        free(ui_test_rawCompressedBuf);
        ui_test_rawCompressedBuf = NULL;
    }
    ui_test_captureFrameCap = 0;
    ui_test_expectedRawFrameCap = 0;
    ui_test_rawCompressedCap = 0;
    ui_test_prevStride = 0;
    ui_test_prevWidth = 0;
    ui_test_prevHeight = 0;
    ui_test_prevValid = 0;
    ui_test_pendingMissingFrameValid = 0;
    ui_test_pendingMissingFrame = 0;
}

int
ui_test_isEnabled(void)
{
    return ui_test_enabled ? 1 : 0;
}

static void
ui_test_copyPath(char *dest, size_t cap, const char *src)
{
    size_t len = 0;

    if (!dest || cap == 0) {
        return;
    }
    if (!src || !*src) {
        dest[0] = '\0';
        return;
    }
    len = strlen(src);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static int
ui_test_copyFile(const char *srcPath, const char *dstPath)
{
    if (!srcPath || !*srcPath || !dstPath || !*dstPath) {
        return 0;
    }

    FILE *src = fopen(srcPath, "rb");
    if (!src) {
        return 0;
    }
    FILE *dst = fopen(dstPath, "wb");
    if (!dst) {
        fclose(src);
        return 0;
    }

    int ok = 1;
    char buf[8192];
    while (!feof(src)) {
        size_t n = fread(buf, 1, sizeof(buf), src);
        if (n == 0) {
            if (ferror(src)) {
                ok = 0;
            }
            break;
        }
        if (fwrite(buf, 1, n, dst) != n) {
            ok = 0;
            break;
        }
    }

    fclose(dst);
    fclose(src);
    return ok;
}

static int
ui_test_copySessionConfigForRecord(void)
{
    if (!ui_test_folder[0]) {
        return 0;
    }

    char dstPath[PATH_MAX];
    if (!debugger_platform_pathJoin(dstPath, sizeof(dstPath), ui_test_folder, ui_test_sessionConfigName)) {
        return 0;
    }

    struct stat st;
    if (stat(dstPath, &st) == 0) {
        if (!S_ISREG(st.st_mode)) {
            debug_error("ui-test: config path is not a file: %s", dstPath);
            return 0;
        }
        return 1;
    }

    const char *srcPath = debugger_defaultConfigPath();
    if (srcPath && *srcPath) {
        if (ui_test_copyFile(srcPath, dstPath)) {
            return 1;
        }
    }

    FILE *dst = fopen(dstPath, "w");
    if (!dst) {
        debug_error("ui-test: failed to create session config %s", dstPath);
        return 0;
    }
    fclose(dst);
    return 1;
}

int
ui_test_prepareSessionConfig(void)
{
    if (ui_test_mode != UI_TEST_MODE_RECORD) {
        return 1;
    }
    // Freeze the initial config before config_loadConfig reads it, so recording
    // and replay load the same settings even when the test folder is new.
    if (!ui_test_makeDir(ui_test_folder)) {
        debug_error("ui-test: failed to create folder %s", ui_test_folder);
        return 0;
    }
    return ui_test_copySessionConfigForRecord();
}

static void
ui_test_clearTempConfigOnFirstRun(void)
{
    if (ui_test_restartCount() != 0) {
        return;
    }
    const char *tempPath = debugger_configTempPath();
    if (!tempPath || !*tempPath) {
        return;
    }

    {
        char tempSaveDir[PATH_MAX];
        int written = snprintf(tempSaveDir, sizeof(tempSaveDir), "%s.rom", tempPath);
        if (written > 0 && (size_t)written < sizeof(tempSaveDir)) {
            struct stat st;
            if (stat(tempSaveDir, &st) == 0 && S_ISDIR(st.st_mode)) {
                debugger_platform_scanFolder(tempSaveDir, ui_test_clearFolderEntry, NULL);
                debugger_platform_removeDir(tempSaveDir);
            }
        }
    }

    errno = 0;
    if (remove(tempPath) != 0) {
        if (errno != ENOENT) {
            debug_error("ui-test: failed to clear temp config %s: %s", tempPath, strerror(errno));
        }
        return;
    }
}

static int
ui_test_verifySessionConfigForReplay(void)
{
    if (!ui_test_folder[0]) {
        return 0;
    }

    char cfgPath[PATH_MAX];
    if (!debugger_platform_pathJoin(cfgPath, sizeof(cfgPath), ui_test_folder, ui_test_sessionConfigName)) {
        return 0;
    }

    struct stat st;
    if (stat(cfgPath, &st) != 0) {
        debug_error("ui-test: missing %s (create with --make-test first)", cfgPath);
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        debug_error("ui-test: config path is not a file: %s", cfgPath);
        return 0;
    }
    return 1;
}

int
ui_test_bootstrap(void)
{
    if (ui_test_mode == UI_TEST_MODE_NONE) {
        return 1;
    }
    if (ui_test_mode == UI_TEST_MODE_RECORD) {
        if (debugger.playbackPath[0]) {
            debug_error("make-test: cannot use --playback with --make-test");
            return 0;
        }
    } else if (ui_test_mode == UI_TEST_MODE_COMPARE || ui_test_mode == UI_TEST_MODE_REMAKE) {
        if (debugger.recordPath[0] || debugger.playbackPath[0]) {
            debug_error("test: cannot combine with --record or --playback");
            return 0;
        }
    }

    srand(0u);
    if (!ui_test_init()) {
        return 0;
    }
    input_record_setUiEventQueueMode(ui_test_mode == UI_TEST_MODE_COMPARE || ui_test_mode == UI_TEST_MODE_REMAKE);

    char path[PATH_MAX];
    if (ui_test_getRecordPath(path, sizeof(path))) {
        if (ui_test_mode == UI_TEST_MODE_RECORD) {
            if (!ui_test_copySessionConfigForRecord()) {
                return 0;
            }
            ui_test_copyPath(debugger.recordPath, sizeof(debugger.recordPath), path);
        } else if (ui_test_mode == UI_TEST_MODE_COMPARE || ui_test_mode == UI_TEST_MODE_REMAKE) {
            if (!ui_test_verifySessionConfigForReplay()) {
                return 0;
            }
            ui_test_copyPath(debugger.playbackPath, sizeof(debugger.playbackPath), path);
        }
    }
    if (ui_test_getUiEventPath(path, sizeof(path))) {
        input_record_setUiEventPath(path);
    }
    return 1;
}

int
ui_test_checkPlaybackComplete(void)
{
    if ((ui_test_mode != UI_TEST_MODE_COMPARE && ui_test_mode != UI_TEST_MODE_REMAKE) || ui_test_failed) {
        return 0;
    }
    if (!input_record_isUiEventPlaybackComplete()) {
        return 0;
    }
    if (ui_test_pendingMissingFrameValid) {
        debug_printf("ui-test: ignoring missing final reference frame #%llu",
                     (unsigned long long)ui_test_pendingMissingFrame);
        ui_test_pendingMissingFrameValid = 0;
    }
    ui_test_exitCode = 0;
    if (ui_test_mode == UI_TEST_MODE_COMPARE) {
        debug_printf("*** UI TEST PASSED ***");
    }
    return 1;
}

int
ui_test_getRecordPath(char *out, size_t cap)
{
    if (!out || cap == 0 || !ui_test_folder[0]) {
        return 0;
    }
    char name[256];
    char prefix[32];
    ui_test_prefix(prefix, sizeof(prefix));
    if (prefix[0]) {
        snprintf(name, sizeof(name), "%suitest.inp", prefix);
    } else {
        snprintf(name, sizeof(name), "uitest.inp");
    }
    return debugger_platform_pathJoin(out, cap, ui_test_folder, name);
}

int
ui_test_getUiEventPath(char *out, size_t cap)
{
    if (!out || cap == 0 || !ui_test_folder[0]) {
        return 0;
    }
    char name[256];
    char prefix[32];
    ui_test_prefix(prefix, sizeof(prefix));
    if (prefix[0]) {
        snprintf(name, sizeof(name), "%suitest.evt", prefix);
    } else {
        snprintf(name, sizeof(name), "uitest.evt");
    }
    return debugger_platform_pathJoin(out, cap, ui_test_folder, name);
}

static int
ui_test_pathExistsFile(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int
ui_test_getFramePath(char *out, size_t cap, uint64_t frame)
{
    char name[128];
    ui_test_formatFrameName(name, sizeof(name), frame);
    return debugger_platform_pathJoin(out, cap, ui_test_folder, name);
}

static int
ui_test_getFrameRawPath(char *out, size_t cap, uint64_t frame)
{
    char name[128];
    ui_test_formatFrameRawName(name, sizeof(name), frame);
    return debugger_platform_pathJoin(out, cap, ui_test_folder, name);
}

static int
ui_test_writeFramePng(const char *path, const uint8_t *data, int width, int height, int pitch)
{
    if (!path || !path[0] || !data || width <= 0 || height <= 0 || pitch <= 0) {
        return 0;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void *)data, width, height, 32, pitch, SDL_PIXELFORMAT_XRGB8888);
    if (!surface) {
        debug_error("ui-test: SDL_CreateRGBSurfaceWithFormatFrom failed: %s", SDL_GetError());
        return 0;
    }
    int ok = (IMG_SavePNG(surface, path) == 0) ? 1 : 0;
    if (!ok) {
        debug_error("ui-test: IMG_SavePNG failed: %s", IMG_GetError());
    }
    SDL_FreeSurface(surface);
    return ok;
}

static int
ui_test_writeFrameRaw(const char *path, const uint8_t *data, int width, int height, int pitch)
{
    if (!path || !path[0] || !data || width <= 0 || height <= 0 || pitch <= 0) {
        return 0;
    }

    size_t stride = (size_t)pitch;
    size_t pixelBytes = stride * (size_t)height;
    if (pixelBytes == 0 || pixelBytes > (size_t)UINT32_MAX) {
        return 0;
    }

    if (!ui_test_rawCompressedBuf || ui_test_rawCompressedCap < compressBound((uLong)pixelBytes)) {
        size_t wanted = (size_t)compressBound((uLong)pixelBytes);
        uint8_t *buf = (uint8_t *)realloc(ui_test_rawCompressedBuf, wanted);
        if (!buf) {
            return 0;
        }
        ui_test_rawCompressedBuf = buf;
        ui_test_rawCompressedCap = wanted;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }

    uLongf compressedSize = (uLongf)ui_test_rawCompressedCap;
    int zResult = compress2((Bytef *)ui_test_rawCompressedBuf,
                            &compressedSize,
                            (const Bytef *)data,
                            (uLong)pixelBytes,
                            1);
    if (zResult != Z_OK || compressedSize == 0 || compressedSize > (uLongf)UINT32_MAX) {
        fclose(fp);
        return 0;
    }

    ui_test_raw_frame_header_v2_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, ui_test_rawMagicV2, sizeof(header.magic));
    header.version = 2u;
    header.width = (uint32_t)width;
    header.height = (uint32_t)height;
    header.stride = (uint32_t)stride;
    header.dataSize = (uint32_t)compressedSize;
    header.flags = 1u;
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    if (fwrite(ui_test_rawCompressedBuf, 1, (size_t)compressedSize, fp) != (size_t)compressedSize) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int
ui_test_loadFrameRaw(const char *path, const uint8_t **outPixels, size_t *outPitch, int *outWidth, int *outHeight)
{
    if (!path || !path[0] || !outPixels || !outPitch || !outWidth || !outHeight) {
        return 0;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }

    char magic[8];
    if (fread(magic, 1, sizeof(magic), fp) != sizeof(magic)) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    if (memcmp(magic, ui_test_rawMagicV2, sizeof(magic)) == 0) {
        ui_test_raw_frame_header_v2_t header;
        if (fread(&header, sizeof(header), 1, fp) != 1) {
            fclose(fp);
            return 0;
        }
        if (header.version != 2u ||
            header.width == 0 ||
            header.height == 0 ||
            header.stride < (header.width * 4u) ||
            header.dataSize == 0) {
            fclose(fp);
            return 0;
        }

        size_t stride = (size_t)header.stride;
        size_t height = (size_t)header.height;
        if (stride > (SIZE_MAX / height)) {
            fclose(fp);
            return 0;
        }
        size_t bytes = stride * height;
        if (!ui_test_expectedRawFrameBuf || ui_test_expectedRawFrameCap < bytes) {
            uint8_t *buf = (uint8_t *)realloc(ui_test_expectedRawFrameBuf, bytes);
            if (!buf) {
                fclose(fp);
                return 0;
            }
            ui_test_expectedRawFrameBuf = buf;
            ui_test_expectedRawFrameCap = bytes;
        }

        size_t dataSize = (size_t)header.dataSize;
        if (!ui_test_rawCompressedBuf || ui_test_rawCompressedCap < dataSize) {
            uint8_t *buf = (uint8_t *)realloc(ui_test_rawCompressedBuf, dataSize);
            if (!buf) {
                fclose(fp);
                return 0;
            }
            ui_test_rawCompressedBuf = buf;
            ui_test_rawCompressedCap = dataSize;
        }
        if (fread(ui_test_rawCompressedBuf, 1, dataSize, fp) != dataSize) {
            fclose(fp);
            return 0;
        }

        if ((header.flags & 1u) != 0u) {
            size_t destSize = bytes;
            int zResult = debugger_platform_uncompressBuffer(ui_test_expectedRawFrameBuf,
                                                             &destSize,
                                                             ui_test_rawCompressedBuf,
                                                             dataSize);
            if (zResult != Z_OK || (size_t)destSize != bytes) {
                fclose(fp);
                return 0;
            }
        } else {
            if (dataSize != bytes) {
                fclose(fp);
                return 0;
            }
            memcpy(ui_test_expectedRawFrameBuf, ui_test_rawCompressedBuf, dataSize);
        }

        fclose(fp);
        *outPixels = ui_test_expectedRawFrameBuf;
        *outPitch = stride;
        *outWidth = (int)header.width;
        *outHeight = (int)header.height;
        return 1;
    }

    fclose(fp);
    return 0;
}

static int
ui_test_writeMismatchImage(uint64_t frame, const uint8_t *data, int width, int height, size_t pitch,
                            char *outPath, size_t cap)
{
    char name[128];
    ui_test_formatArtifactName(name, sizeof(name), frame, "-actual.png");
    char path[PATH_MAX];
    if (!debugger_platform_pathJoin(path, sizeof(path), ui_test_folder, name)) {
        return 0;
    }
    if (ui_test_writeFramePng(path, data, width, height, (int)pitch) && outPath && cap > 0) {
        strncpy(outPath, path, cap - 1);
        outPath[cap - 1] = '\0';
    }
    return 1;
}

static void
ui_test_openImage(const char *path)
{
    if (!path || !*path) {
        return;
    }
    char absolutePath[PATH_MAX];
    if (path[0] == '/' || path[0] == '\\' || path[1] == ':') {
        strutil_strlcpy(absolutePath, sizeof(absolutePath), path);
    } else {
        char cwd[PATH_MAX];
        if (!debugger_platform_getCurrentDir(cwd, sizeof(cwd)) ||
            !debugger_platform_pathJoin(absolutePath, sizeof(absolutePath), cwd, path)) {
            strutil_strlcpy(absolutePath, sizeof(absolutePath), path);
        }
    }
    char url[PATH_MAX + 16];
    strutil_join2Trunc(url, sizeof(url), "file://", absolutePath);
    if (SDL_OpenURL(url) != 0) {
        SDL_OpenURL(absolutePath);
    }
}

static void
ui_test_openFailureImage(const char *montagePath, const char *diffPath)
{
    if (!ui_test_openOnFail) {
        return;
    }
    if (montagePath && *montagePath) {
        ui_test_openImage(montagePath);
    } else if (diffPath && *diffPath) {
        ui_test_openImage(diffPath);
    }
}

static int
ui_test_writeDiffScript(uint64_t frame, const char *refPath, const char *testPath,
                        char *outMontage, size_t cap)
{
    if (outMontage && cap > 0) {
        outMontage[0] = '\0';
    }
    if (!refPath || !*refPath || !testPath || !*testPath) {
        return 0;
    }

    char compareName[256];
    ui_test_formatArtifactName(compareName, sizeof(compareName), frame, "-compare.png");
    char comparePath[PATH_MAX];
    if (!debugger_platform_pathJoin(comparePath, sizeof(comparePath), ui_test_folder, compareName)) {
        return 0;
    }

    char montageName[256];
    ui_test_formatArtifactName(montageName, sizeof(montageName), frame, ".png");
    char montagePath[PATH_MAX];
    if (!debugger_platform_pathJoin(montagePath, sizeof(montagePath), ui_test_folder, montageName)) {
        return 0;
    }

    char scriptName[256];
    ui_test_formatArtifactName(scriptName, sizeof(scriptName), frame, ".cmd");
    char scriptPath[PATH_MAX];
    if (!debugger_platform_pathJoin(scriptPath, sizeof(scriptPath), ui_test_folder, scriptName)) {
        return 0;
    }

    FILE *fp = fopen(scriptPath, "w");
    if (!fp) {
        return 0;
    }
    fprintf(fp, "magick compare -metric AE \"%s\" \"%s\" \"%s\"\n",
            refPath, testPath, comparePath);
    fprintf(fp, "magick montage \"%s\" \"%s\" \"%s\" -tile 3x1 -geometry +0+0 \"%s\"\n",
            refPath, testPath, comparePath, montagePath);
    fclose(fp);

    char cmdCompare[PATH_MAX * 3];
    char cmdMontage[PATH_MAX * 4];
    snprintf(cmdCompare, sizeof(cmdCompare),
             "magick compare -metric AE \"%s\" \"%s\" \"%s\"",
             refPath, testPath, comparePath);
    snprintf(cmdMontage, sizeof(cmdMontage),
             "magick montage \"%s\" \"%s\" \"%s\" -tile 3x1 -geometry +0+0 \"%s\"",
             refPath, testPath, comparePath, montagePath);
    int ignored = system(cmdCompare);
    ignored = system(cmdMontage);
    (void)ignored;
    remove(scriptPath);

    if (outMontage && cap > 0) {
        strncpy(outMontage, montagePath, cap - 1);
        outMontage[cap - 1] = '\0';
    }
    return 1;
}

static int
ui_test_isDifferentToPrev(const uint8_t *data, int width, int height, size_t pitch)
{
    if (!ui_test_prevValid || width != ui_test_prevWidth || height != ui_test_prevHeight) {
        return 1;
    }
    const uint32_t mask = 0x00ffffffu;
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = data + (size_t)y * pitch;
        const uint8_t *prev = ui_test_prevFrame + (size_t)y * ui_test_prevStride;
        for (int x = 0; x < width; ++x) {
            const uint32_t *pa = (const uint32_t *)(row + (size_t)x * 4);
            const uint32_t *pb = (const uint32_t *)(prev + (size_t)x * 4);
            if (((*pa) & mask) != ((*pb) & mask)) {
                return 1;
            }
        }
    }
    return 0;
}

static void
ui_test_updatePrevFrame(const uint8_t *data, int width, int height, size_t pitch)
{
    if (!data || width <= 0 || height <= 0) {
        ui_test_prevValid = 0;
        return;
    }
    size_t stride = (size_t)width * 4;
    size_t bytes = stride * (size_t)height;
    if (!ui_test_prevFrame || ui_test_prevStride != stride ||
        ui_test_prevWidth != width || ui_test_prevHeight != height) {
        uint8_t *buf = (uint8_t *)realloc(ui_test_prevFrame, bytes);
        if (!buf) {
            ui_test_prevValid = 0;
            return;
        }
        ui_test_prevFrame = buf;
        ui_test_prevStride = stride;
        ui_test_prevWidth = width;
        ui_test_prevHeight = height;
    }
    for (int y = 0; y < height; ++y) {
        memcpy(ui_test_prevFrame + (size_t)y * ui_test_prevStride,
               data + (size_t)y * pitch,
               ui_test_prevStride);
    }
    ui_test_prevValid = 1;
}

static int
ui_test_compareFramePixelsDualPitch(const uint8_t *actual,
                                    size_t actualPitch,
                                    const uint8_t *expected,
                                    size_t expectedPitch,
                                    int width,
                                    int height)
{
    if (!actual || !expected || actualPitch < (size_t)width * 4 || expectedPitch < (size_t)width * 4) {
        return 0;
    }
    const uint32_t mask = 0x00ffffffu;
    for (int y = 0; y < height; ++y) {
        const uint8_t *rowActual = actual + (size_t)y * actualPitch;
        const uint8_t *rowExpected = expected + (size_t)y * expectedPitch;
        for (int x = 0; x < width; ++x) {
            const uint32_t *pa = (const uint32_t *)(rowActual + (size_t)x * 4);
            const uint32_t *pe = (const uint32_t *)(rowExpected + (size_t)x * 4);
            if (((*pa) & mask) != ((*pe) & mask)) {
                return 0;
            }
        }
    }
    return 1;
}

static int
ui_test_getReferencePathForFailure(uint64_t frame,
                                   const char *primaryPngPath,
                                   const uint8_t *expectedPixels,
                                   int expectedWidth,
                                   int expectedHeight,
                                   size_t expectedPitch,
                                   char *outRefPath,
                                   size_t outCap)
{
    if (!outRefPath || outCap == 0) {
        return 0;
    }
    outRefPath[0] = '\0';
    if (!expectedPixels || expectedWidth <= 0 || expectedHeight <= 0 || expectedPitch == 0) {
        return 0;
    }
    if (primaryPngPath && primaryPngPath[0] && ui_test_pathExistsFile(primaryPngPath)) {
        ui_test_copyPath(outRefPath, outCap, primaryPngPath);
        return 1;
    }

    char name[256];
    ui_test_formatArtifactName(name, sizeof(name), frame, "-ref.png");
    char path[PATH_MAX];
    if (!debugger_platform_pathJoin(path, sizeof(path), ui_test_folder, name)) {
        return 0;
    }
    if (!ui_test_writeFramePng(path,
                               expectedPixels,
                               expectedWidth,
                               expectedHeight,
                               (int)expectedPitch)) {
        return 0;
    }
    ui_test_copyPath(outRefPath, outCap, path);
    return 1;
}

static int
ui_test_compareFrame(uint64_t frame, const uint8_t *data, int width, int height, size_t pitch)
{
    char framePngPath[PATH_MAX];
    char rawPath[PATH_MAX];
    if (!ui_test_getFramePath(framePngPath, sizeof(framePngPath), frame)) {
        return 0;
    }
    const uint8_t *expectedPixels = NULL;
    size_t expectedPitch = 0;
    int expectedWidth = 0;
    int expectedHeight = 0;
    int loadedFromRaw = 0;
    if (ui_test_getFrameRawPath(rawPath, sizeof(rawPath), frame)) {
        if (ui_test_loadFrameRaw(rawPath,
                                 &expectedPixels,
                                 &expectedPitch,
                                 &expectedWidth,
                                 &expectedHeight)) {
            loadedFromRaw = 1;
        }
    }
    if (loadedFromRaw) {
        if (expectedWidth == width &&
            expectedHeight == height &&
            ui_test_compareFramePixelsDualPitch(data, pitch, expectedPixels, expectedPitch, width, height)) {
            return 0;
        }

        char refPath[PATH_MAX];
        char diffPath[PATH_MAX];
        char montagePath[PATH_MAX];
        refPath[0] = '\0';
        diffPath[0] = '\0';
        montagePath[0] = '\0';
        (void)ui_test_getReferencePathForFailure(frame,
                                                 framePngPath,
                                                 expectedPixels,
                                                 expectedWidth,
                                                 expectedHeight,
                                                 expectedPitch,
                                                 refPath,
                                                 sizeof(refPath));
        (void)ui_test_writeMismatchImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        if (refPath[0] && diffPath[0]) {
            (void)ui_test_writeDiffScript(frame, refPath, diffPath, montagePath, sizeof(montagePath));
        }
        ui_test_openFailureImage(montagePath, diffPath);
        debug_error("ui-test: mismatch at frame #%llu (%s)",
                    (unsigned long long)frame,
                    montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }

    SDL_Surface *loaded = IMG_Load(framePngPath);
    if (!loaded) {
        ui_test_pendingMissingFrameValid = 1;
        ui_test_pendingMissingFrame = frame;
        return 0;
    }
    SDL_Surface *converted = loaded;
    if (loaded->format->format != SDL_PIXELFORMAT_XRGB8888) {
        converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_XRGB8888, 0);
        SDL_FreeSurface(loaded);
    }
    if (!converted) {
        char diffPath[PATH_MAX];
        char montagePath[PATH_MAX];
        diffPath[0] = '\0';
        montagePath[0] = '\0';
        (void)ui_test_writeMismatchImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        (void)ui_test_writeDiffScript(frame, framePngPath, diffPath, montagePath, sizeof(montagePath));
        ui_test_openFailureImage(montagePath, diffPath);
        debug_error("ui-test: SDL_ConvertSurfaceFormat failed: %s (%s)", SDL_GetError(),
                    montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }
    if (SDL_MUSTLOCK(converted)) {
        if (SDL_LockSurface(converted) != 0) {
            char diffPath[PATH_MAX];
            char montagePath[PATH_MAX];
            diffPath[0] = '\0';
            montagePath[0] = '\0';
            (void)ui_test_writeMismatchImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
            (void)ui_test_writeDiffScript(frame, framePngPath, diffPath, montagePath, sizeof(montagePath));
            ui_test_openFailureImage(montagePath, diffPath);
            debug_error("ui-test: SDL_LockSurface failed: %s (%s)", SDL_GetError(),
                        montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
            SDL_FreeSurface(converted);
            return 1;
        }
    }
    int same = (converted->w == width && converted->h == height) ?
        ui_test_compareFramePixelsDualPitch(data, pitch,
                                            (const uint8_t *)converted->pixels,
                                            (size_t)converted->pitch,
                                            width,
                                            height) : 0;
    if (SDL_MUSTLOCK(converted)) {
        SDL_UnlockSurface(converted);
    }
    SDL_FreeSurface(converted);
    if (!same) {
        char diffPath[PATH_MAX];
        char montagePath[PATH_MAX];
        diffPath[0] = '\0';
        montagePath[0] = '\0';
        (void)ui_test_writeMismatchImage(frame, data, width, height, pitch, diffPath, sizeof(diffPath));
        (void)ui_test_writeDiffScript(frame, framePngPath, diffPath, montagePath, sizeof(montagePath));
        ui_test_openFailureImage(montagePath, diffPath);
        debug_error("ui-test: mismatch at frame #%llu (%s)",
                    (unsigned long long)frame,
                    montagePath[0] ? montagePath : (diffPath[0] ? diffPath : "diff unavailable"));
        return 1;
    }
    return 0;
}

int
ui_test_captureFrame(uint64_t frame)
{
    if (!ui_test_enabled) {
        return 0;
    }
    const uint8_t *data = NULL;
    int width = 0;
    int height = 0;
    size_t pitch = 0;
    if (!libretro_host_getFrame(&data, &width, &height, &pitch)) {
        return 0;
    }
    int different = ui_test_isDifferentToPrev(data, width, height, pitch);
    if (ui_test_mode == UI_TEST_MODE_COMPARE) {
        int r = 0;
        if (different) {
            if (ui_test_pendingMissingFrameValid) {
                debug_error("ui-test: missing reference frame #%llu is not final (next changed frame #%llu)",
                            (unsigned long long)ui_test_pendingMissingFrame,
                            (unsigned long long)frame);
                ui_test_pendingMissingFrameValid = 0;
                ui_test_failed = 1;
                ui_test_exitCode = 1;
                ui_test_updatePrevFrame(data, width, height, pitch);
                return 1;
            }
            r = ui_test_compareFrame(frame, data, width, height, pitch);
            if (r == 1) {
                ui_test_failed = 1;
                ui_test_exitCode = 1;
            }
        }
        ui_test_updatePrevFrame(data, width, height, pitch);
        return r;
    }
    if (different) {
        char path[PATH_MAX];
        if (!ui_test_getFrameRawPath(path, sizeof(path), frame) ||
            !ui_test_writeFrameRaw(path, data, width, height, (int)pitch)) {
            debug_error("ui-test: failed to write frame #%llu", (unsigned long long)frame);
            ui_test_failed = 1;
            ui_test_exitCode = 1;
            return 1;
        }
    }
    ui_test_updatePrevFrame(data, width, height, pitch);
    return 0;
}

int
ui_test_captureWindowFrame(uint64_t frame, SDL_Renderer *renderer)
{
    if (!ui_test_enabled || !renderer) {
        return 0;
    }
    if (ui_test_mode == UI_TEST_MODE_COMPARE) {
        char framePngPath[PATH_MAX];
        char rawPath[PATH_MAX];
        int hasReference = 0;
        if (ui_test_getFrameRawPath(rawPath, sizeof(rawPath), frame) &&
            ui_test_pathExistsFile(rawPath)) {
            hasReference = 1;
        }
        if (!hasReference &&
            ui_test_getFramePath(framePngPath, sizeof(framePngPath), frame) &&
            ui_test_pathExistsFile(framePngPath)) {
            hasReference = 1;
        }
        if (!hasReference) {
            return 0;
        }
    }
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || width <= 0 || height <= 0) {
        return 0;
    }
    size_t pitch = (size_t)width * 4;
    size_t needed = pitch * (size_t)height;
    if (needed == 0) {
        return 0;
    }
    if (!ui_test_captureFrameBuf || ui_test_captureFrameCap < needed) {
        uint8_t *buf = (uint8_t *)realloc(ui_test_captureFrameBuf, needed);
        if (!buf) {
            return 0;
        }
        ui_test_captureFrameBuf = buf;
        ui_test_captureFrameCap = needed;
    }
    SDL_RenderFlush(renderer);
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_XRGB8888,
                             ui_test_captureFrameBuf, (int)pitch) != 0) {
        debug_error("ui-test: SDL_RenderReadPixels failed: %s", SDL_GetError());
        return 0;
    }
    if (ui_test_mode == UI_TEST_MODE_COMPARE) {
        int r = ui_test_compareFrame(frame, ui_test_captureFrameBuf, width, height, pitch);
        if (r == 1) {
            ui_test_failed = 1;
            ui_test_exitCode = 1;
        }
        ui_test_updatePrevFrame(ui_test_captureFrameBuf, width, height, pitch);
        return r;
    }
    int different = ui_test_isDifferentToPrev(ui_test_captureFrameBuf, width, height, pitch);
    if (different) {
        char path[PATH_MAX];
        if (!ui_test_getFrameRawPath(path, sizeof(path), frame) ||
            !ui_test_writeFrameRaw(path, ui_test_captureFrameBuf, width, height, (int)pitch)) {
            debug_error("ui-test: failed to write frame #%llu", (unsigned long long)frame);
            ui_test_failed = 1;
            ui_test_exitCode = 1;
            return 1;
        }
    }
    ui_test_updatePrevFrame(ui_test_captureFrameBuf, width, height, pitch);
    return 0;
}
