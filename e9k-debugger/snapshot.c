/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "snapshot.h"
#include "debugger.h"
#include "libretro_host.h"
#include "state_buffer.h"

static const char *
snapshot_basename(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static int
snapshot_pathExistsFile(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    return S_ISREG(sb.st_mode) ? 1 : 0;
}

static int
snapshot_pathExistsDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    return S_ISDIR(sb.st_mode) ? 1 : 0;
}

static const char *
snapshot_snapshotSaveDir(void)
{
    const char *hostSaveDir = libretro_host_getSaveDir();
    if (hostSaveDir && *hostSaveDir) {
        return hostSaveDir;
    }
    if (debugger.libretro.saveDir[0]) {
        return debugger.libretro.saveDir;
    }
    // Match libretro_host_start() behavior: if saveDir is not configured, fall back to systemDir.
    const char *hostSystemDir = libretro_host_getSystemDir();
    if (hostSystemDir && *hostSystemDir) {
        return hostSystemDir;
    }
    if (debugger.libretro.systemDir[0]) {
        return debugger.libretro.systemDir;
    }
    return NULL;
}

static const char *
snapshot_snapshotRomPath(void)
{
    const char *activeRom = libretro_host_getRomPath();
    if (activeRom) {
        return activeRom;
    }
    if (debugger.libretro.romPath[0]) {
        return debugger.libretro.romPath;
    }
    return NULL;
}

static int
snapshot_buildSnapshotPath(char *out, size_t cap)
{
    const char *saveDir = snapshot_snapshotSaveDir();
    const char *romPath = snapshot_snapshotRomPath();
    if (!out || cap == 0 || !saveDir || !romPath) {
        return 0;
    }
    const char *base = snapshot_basename(romPath);
    if (!base || !*base) {
        return 0;
    }
    size_t dirLen = strlen(saveDir);
    int needsSlash = (dirLen > 0 && saveDir[dirLen - 1] != '/' && saveDir[dirLen - 1] != '\\');
    int written = snprintf(out, cap, "%s%s%s.e9k-save", saveDir, needsSlash ? "/" : "", base);
    if (written < 0 || (size_t)written >= cap) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    return 1;
}

static uint64_t
snapshot_hashFNV1a(uint64_t hash, const uint8_t *data, size_t len)
{
    const uint64_t prime = 1099511628211ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= prime;
    }
    return hash;
}

static int
snapshot_computeRomChecksum(uint64_t *outChecksum)
{
    if (!outChecksum) {
        return 0;
    }
    *outChecksum = 0;
    const char *romPath = snapshot_snapshotRomPath();
    if (!romPath || !snapshot_pathExistsFile(romPath)) {
        return 0;
    }
    FILE *f = fopen(romPath, "rb");
    if (!f) {
        return 0;
    }
    uint8_t buf[8192];
    uint64_t hash = 1469598103934665603ull;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        hash = snapshot_hashFNV1a(hash, buf, n);
    }
    fclose(f);
    *outChecksum = hash;
    return 1;
}

static void
snapshot_saveSnapshotOnExit(void)
{
    if (!debugger.hasStateSnapshot) {
        return;
    }
    const char *saveDir = snapshot_snapshotSaveDir();
    if (!saveDir || !snapshot_pathExistsDir(saveDir)) {
        return;
    }
    char path[PATH_MAX];
    if (!snapshot_buildSnapshotPath(path, sizeof(path))) {
        return;
    }
    uint64_t romChecksum = 0;
    if (!snapshot_computeRomChecksum(&romChecksum)) {
        return;
    }
    state_buffer_saveSnapshotFile(path, romChecksum);
}

static void
snapshot_loadSnapshotOnBoot(void)
{
    const char *saveDir = snapshot_snapshotSaveDir();
    if (!saveDir || !snapshot_pathExistsDir(saveDir)) {
        return;
    }
    char path[PATH_MAX];
    if (!snapshot_buildSnapshotPath(path, sizeof(path))) {
        return;
    }
    if (!snapshot_pathExistsFile(path)) {
        return;
    }
    uint64_t romChecksum = 0;
    if (!snapshot_computeRomChecksum(&romChecksum)) {
        return;
    }
    uint64_t savedChecksum = 0;
    if (!state_buffer_loadSnapshotFile(path, &savedChecksum)) {
        return;
    }
    if (savedChecksum && savedChecksum != romChecksum) {
        return;
    }
    uint8_t *stateData = NULL;
    size_t stateSize = 0;
    if (!state_buffer_getSnapshotState(&stateData, &stateSize, NULL)) {
        return;
    }
    if (libretro_host_setStateData(stateData, stateSize)) {
        debugger.hasStateSnapshot = 1;
    }
    alloc_free(stateData);
}

void
snapshot_saveOnExit(void)
{
    snapshot_saveSnapshotOnExit();
}

void
snapshot_loadOnBoot(void)
{
    snapshot_loadSnapshotOnBoot();
}
