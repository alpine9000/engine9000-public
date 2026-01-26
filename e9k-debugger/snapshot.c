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

#include "alloc.h"
#include "breakpoints.h"
#include "debugger.h"
#include "geo9000.h"
#include "json.h"
#include "libretro_host.h"
#include "machine.h"
#include "protect.h"
#include "state_buffer.h"
#include "trainer.h"

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
    if (debugger.settingsEdit.savesDir[0]) {
        return debugger.settingsEdit.savesDir;
    }
    if (debugger.config.savesDir[0]) {
        return debugger.config.savesDir;
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
    if (debugger.config.romPath[0]) {
        return debugger.config.romPath;
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

static int
snapshot_buildDebugJsonPath(char *out, size_t cap)
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
    int written = snprintf(out, cap, "%s%s%s-e9k-debug.json", saveDir, needsSlash ? "/" : "", base);
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
    (void)state_buffer_saveSnapshotFile(path, romChecksum);
}

static void
snapshot_saveDebugStateOnExit(void)
{
    const char *saveDir = snapshot_snapshotSaveDir();
    if (!saveDir || !snapshot_pathExistsDir(saveDir)) {
        return;
    }
    char path[PATH_MAX];
    if (!snapshot_buildDebugJsonPath(path, sizeof(path))) {
        return;
    }
    uint64_t romChecksum = 0;
    if (!snapshot_computeRomChecksum(&romChecksum)) {
        return;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }

    const char *romPath = snapshot_snapshotRomPath();
    const char *base = snapshot_basename(romPath);
    char debugName[PATH_MAX];
    if (base && *base) {
        snprintf(debugName, sizeof(debugName), "%s-e9k-debug.json", base);
    } else {
        snprintf(debugName, sizeof(debugName), "unknown-e9k-debug.json");
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"rom_checksum\": %llu,\n", (unsigned long long)romChecksum);
    fprintf(f, "  \"rom_filename\": \"%s\",\n", debugName);

    const machine_breakpoint_t *bps = NULL;
    int bpCount = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &bpCount);
    fprintf(f, "  \"breakpoints\": [\n");
    for (int i = 0; i < bpCount; ++i) {
        const machine_breakpoint_t *bp = &bps[i];
        fprintf(f, "    {\"addr\": %u, \"enabled\": %s}%s\n",
                (unsigned)(bp->addr & 0x00ffffffu),
                bp->enabled ? "true" : "false",
                (i + 1 < bpCount) ? "," : "");
    }
    fprintf(f, "  ],\n");

    geo_debug_protect_t protects[GEO_PROTECT_COUNT];
    size_t protectCount = 0;
    uint64_t enabledMask = 0;
    libretro_host_debugReadProtects(protects, GEO_PROTECT_COUNT, &protectCount);
    libretro_host_debugGetProtectEnabledMask(&enabledMask);
    fprintf(f, "  \"protects\": [\n");
    size_t validCount = 0;
    for (size_t i = 0; i < protectCount; ++i) {
        if (protects[i].sizeBits != 0) {
            validCount++;
        }
    }
    size_t written = 0;
    for (size_t i = 0; i < protectCount; ++i) {
        const geo_debug_protect_t *p = &protects[i];
        if (p->sizeBits == 0) {
            continue;
        }
        int enabled = ((enabledMask >> i) & 1ull) ? 1 : 0;
        fprintf(f, "    {\"addr\": %u, \"size_bits\": %u, \"mode\": %u, \"value\": %u, \"enabled\": %s}%s\n",
                (unsigned)(p->addr & 0x00ffffffu),
                (unsigned)p->sizeBits,
                (unsigned)p->mode,
                (unsigned)p->value,
                enabled ? "true" : "false",
                (written + 1 < validCount) ? "," : "");
        written++;
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
}

static void *
snapshot_jsonAlloc(void *userData, size_t size)
{
    (void)userData;
    return alloc_alloc(size);
}

static struct json_value_s *
snapshot_jsonObjectFind(struct json_object_s *object, const char *name)
{
    if (!object || !name) {
        return NULL;
    }
    size_t nameLen = strlen(name);
    for (struct json_object_element_s *elem = object->start; elem; elem = elem->next) {
        if (!elem->name || !elem->name->string) {
            continue;
        }
        if (elem->name->string_size == nameLen &&
            strncmp(elem->name->string, name, nameLen) == 0) {
            return elem->value;
        }
    }
    return NULL;
}

static int
snapshot_jsonGetU64(struct json_value_s *value, uint64_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    if (!value || !outValue) {
        return 0;
    }
    struct json_number_s *num = json_value_as_number(value);
    if (!num || !num->number || num->number_size == 0) {
        return 0;
    }
    char stackBuf[64];
    char *buf = stackBuf;
    if (num->number_size + 1 > sizeof(stackBuf)) {
        buf = (char*)alloc_alloc(num->number_size + 1);
        if (!buf) {
            return 0;
        }
    }
    memcpy(buf, num->number, num->number_size);
    buf[num->number_size] = '\0';
    char *end = NULL;
    unsigned long long v = strtoull(buf, &end, 10);
    if (buf != stackBuf) {
        alloc_free(buf);
    }
    if (!end || *end != '\0') {
        return 0;
    }
    *outValue = (uint64_t)v;
    return 1;
}

static int
snapshot_jsonGetU32(struct json_value_s *value, uint32_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    uint64_t v = 0;
    if (!snapshot_jsonGetU64(value, &v)) {
        return 0;
    }
    if (v > 0xffffffffULL) {
        return 0;
    }
    if (outValue) {
        *outValue = (uint32_t)v;
    }
    return 1;
}

static int
snapshot_jsonGetBool(struct json_value_s *value, int *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    if (!value || !outValue) {
        return 0;
    }
    if (json_value_is_true(value)) {
        *outValue = 1;
        return 1;
    }
    if (json_value_is_false(value)) {
        *outValue = 0;
        return 1;
    }
    uint32_t v = 0;
    if (snapshot_jsonGetU32(value, &v)) {
        *outValue = v ? 1 : 0;
        return 1;
    }
    return 0;
}

static void
snapshot_clearBreakpointsCore(void)
{
    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);
    if (bps && count > 0) {
        for (int i = 0; i < count; ++i) {
            uint32_t addr = (uint32_t)(bps[i].addr & 0x00ffffffu);
            libretro_host_debugRemoveBreakpoint(addr);
        }
    }
    machine_clearBreakpoints(&debugger.machine);
}

static void
snapshot_loadDebugStateOnBoot(void)
{
    const char *saveDir = snapshot_snapshotSaveDir();
    if (!saveDir || !snapshot_pathExistsDir(saveDir)) {
        return;
    }
    char path[PATH_MAX];
    if (!snapshot_buildDebugJsonPath(path, sizeof(path))) {
        return;
    }
    if (!snapshot_pathExistsFile(path)) {
        return;
    }
    uint64_t romChecksum = 0;
    if (!snapshot_computeRomChecksum(&romChecksum)) {
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    long fileSize = ftell(f);
    if (fileSize <= 0) {
        fclose(f);
        return;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return;
    }
    size_t bufSize = (size_t)fileSize;
    char *buf = (char*)alloc_alloc(bufSize + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t read = fread(buf, 1, bufSize, f);
    fclose(f);
    if (read != bufSize) {
        alloc_free(buf);
        return;
    }
    buf[bufSize] = '\0';

    struct json_parse_result_s result = {0};
    struct json_value_s *root = json_parse_ex(buf, bufSize, json_parse_flags_default,
                                              snapshot_jsonAlloc, NULL, &result);
    alloc_free(buf);
    if (!root) {
        return;
    }
    struct json_object_s *object = json_value_as_object(root);
    if (!object) {
        alloc_free(root);
        return;
    }
    struct json_value_s *checksumValue = snapshot_jsonObjectFind(object, "rom_checksum");
    uint64_t savedChecksum = 0;
    if (!snapshot_jsonGetU64(checksumValue, &savedChecksum)) {
        alloc_free(root);
        return;
    }
    if (savedChecksum != romChecksum) {
        snapshot_clearBreakpointsCore();
        protect_clear();
        breakpoints_markDirty();
        trainer_markDirty();
        alloc_free(root);
        return;
    }

    snapshot_clearBreakpointsCore();
    protect_clear();

    struct json_value_s *bpsValue = snapshot_jsonObjectFind(object, "breakpoints");
    struct json_array_s *bpsArray = json_value_as_array(bpsValue);
    if (bpsArray) {
        for (struct json_array_element_s *el = bpsArray->start; el; el = el->next) {
            struct json_object_s *bpObj = json_value_as_object(el->value);
            if (!bpObj) {
                continue;
            }
            uint32_t addr = 0;
            int enabled = 0;
            if (!snapshot_jsonGetU32(snapshot_jsonObjectFind(bpObj, "addr"), &addr)) {
                continue;
            }
            snapshot_jsonGetBool(snapshot_jsonObjectFind(bpObj, "enabled"), &enabled);
            machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, enabled);
            if (bp) {
                breakpoints_resolveLocation(bp);
            }
            if (enabled) {
                libretro_host_debugAddBreakpoint(addr & 0x00ffffffu);
            }
        }
    }

    struct json_value_s *protectsValue = snapshot_jsonObjectFind(object, "protects");
    struct json_array_s *protectsArray = json_value_as_array(protectsValue);
    uint64_t enabledMask = 0;
    if (protectsArray) {
        for (struct json_array_element_s *el = protectsArray->start; el; el = el->next) {
            struct json_object_s *pObj = json_value_as_object(el->value);
            if (!pObj) {
                continue;
            }
            uint32_t addr = 0;
            uint32_t sizeBits = 0;
            uint32_t mode = 0;
            uint32_t value = 0;
            int enabled = 0;
            if (!snapshot_jsonGetU32(snapshot_jsonObjectFind(pObj, "addr"), &addr)) {
                continue;
            }
            if (!snapshot_jsonGetU32(snapshot_jsonObjectFind(pObj, "size_bits"), &sizeBits)) {
                continue;
            }
            if (!snapshot_jsonGetU32(snapshot_jsonObjectFind(pObj, "mode"), &mode)) {
                continue;
            }
            snapshot_jsonGetU32(snapshot_jsonObjectFind(pObj, "value"), &value);
            snapshot_jsonGetBool(snapshot_jsonObjectFind(pObj, "enabled"), &enabled);
            uint32_t index = 0;
            if (!libretro_host_debugAddProtect(addr & 0x00ffffffu, sizeBits, mode, value, &index)) {
                continue;
            }
            if (enabled) {
                enabledMask |= (1ull << index);
            }
        }
        libretro_host_debugSetProtectEnabledMask(enabledMask);
    }

    breakpoints_markDirty();
    trainer_markDirty();

    alloc_free(root);
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
    snapshot_saveDebugStateOnExit();
}

void
snapshot_loadOnBoot(void)
{
    snapshot_loadSnapshotOnBoot();
    snapshot_loadDebugStateOnBoot();
}
