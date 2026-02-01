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

#include "rom_config.h"
#include "alloc.h"
#include "breakpoints.h"
#include "debugger.h"
#include "json.h"
#include "libretro_host.h"
#include "machine.h"
#include "protect.h"
#include "trainer.h"

typedef struct rom_config_bp_entry {
    uint32_t addr;
    int enabled;
} rom_config_bp_entry_t;

typedef struct rom_config_protect_entry {
    uint32_t addr;
    uint32_t sizeBits;
    uint32_t mode;
    uint32_t value;
    int enabled;
} rom_config_protect_entry_t;

typedef struct rom_config_data {
    uint64_t romChecksum;
    rom_config_bp_entry_t *breakpoints;
    size_t breakpointCount;
    rom_config_protect_entry_t *protects;
    size_t protectCount;
    char elfPath[PATH_MAX];
    char sourceDir[PATH_MAX];
    char toolchainPrefix[PATH_MAX];
    int hasElf;
    int hasSource;
    int hasToolchain;
} rom_config_data_t;

static char rom_config_activeElfPath[PATH_MAX];
static char rom_config_activeSourceDir[PATH_MAX];
static char rom_config_activeToolchainPrefix[PATH_MAX];
static int rom_config_activeInit = 0;

static const char *
rom_config_basename(const char *path)
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
rom_config_pathExistsFile(const char *path)
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
rom_config_pathExistsDir(const char *path)
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
rom_config_saveDir(const char *fallbackSystemDir)
{
    const char *hostSaveDir = libretro_host_getSaveDir();
    if (hostSaveDir && *hostSaveDir) {
        return hostSaveDir;
    }
    if (debugger.libretro.saveDir[0]) {
        return debugger.libretro.saveDir;
    }
    if (fallbackSystemDir && *fallbackSystemDir) {
        return fallbackSystemDir;
    }
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
rom_config_activeRomPath(void)
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
rom_config_buildJsonPathCore(char *out, size_t cap, const char *saveDir, const char *romPath)
{
    if (!out || cap == 0 || !saveDir || !romPath) {
        return 0;
    }
    const char *base = rom_config_basename(romPath);
    if (!base || !*base) {
        return 0;
    }
    size_t dirLen = strlen(saveDir);
    int needsSlash = (dirLen > 0 && saveDir[dirLen - 1] != '/' && saveDir[dirLen - 1] != '\\');
    int written = snprintf(out, cap, "%s%s%s.json", saveDir, needsSlash ? "/" : "", base);
    if (written < 0 || (size_t)written >= cap) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    return 1;
}

static int
rom_config_buildLegacyJsonPathCore(char *out, size_t cap, const char *saveDir, const char *romPath)
{
    if (!out || cap == 0 || !saveDir || !romPath) {
        return 0;
    }
    const char *base = rom_config_basename(romPath);
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
rom_config_hashFNV1a(uint64_t hash, const uint8_t *data, size_t len)
{
    const uint64_t prime = 1099511628211ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= prime;
    }
    return hash;
}

static int
rom_config_computeRomChecksum(const char *romPath, uint64_t *outChecksum)
{
    if (!outChecksum) {
        return 0;
    }
    *outChecksum = 0;
    if (!romPath || !rom_config_pathExistsFile(romPath)) {
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
        hash = rom_config_hashFNV1a(hash, buf, n);
    }
    fclose(f);
    *outChecksum = hash;
    return 1;
}

static void *
rom_config_jsonAlloc(void *userData, size_t size)
{
    (void)userData;
    return alloc_alloc(size);
}

static struct json_value_s *
rom_config_jsonObjectFind(struct json_object_s *object, const char *name)
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
rom_config_jsonGetU64(struct json_value_s *value, uint64_t *outValue)
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
rom_config_jsonGetU32(struct json_value_s *value, uint32_t *outValue)
{
    if (outValue) {
        *outValue = 0;
    }
    uint64_t v = 0;
    if (!rom_config_jsonGetU64(value, &v)) {
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
rom_config_jsonGetBool(struct json_value_s *value, int *outValue)
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
    if (rom_config_jsonGetU32(value, &v)) {
        *outValue = v ? 1 : 0;
        return 1;
    }
    return 0;
}

static int
rom_config_jsonGetString(struct json_value_s *value, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!value) {
        return 0;
    }
    struct json_string_s *str = json_value_as_string(value);
    if (!str || !str->string) {
        return 0;
    }
    size_t n = str->string_size;
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, str->string, n);
    out[n] = '\0';
    return 1;
}

static void
rom_config_freeData(rom_config_data_t *data)
{
    if (!data) {
        return;
    }
    alloc_free(data->breakpoints);
    alloc_free(data->protects);
    memset(data, 0, sizeof(*data));
}

static int
rom_config_parseFile(const char *path, rom_config_data_t *outData)
{
    if (!outData) {
        return 0;
    }
    memset(outData, 0, sizeof(*outData));
    if (!path || !*path || !rom_config_pathExistsFile(path)) {
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
    long fileSize = ftell(f);
    if (fileSize <= 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    size_t bufSize = (size_t)fileSize;
    char *buf = (char*)alloc_alloc(bufSize + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t read = fread(buf, 1, bufSize, f);
    fclose(f);
    if (read != bufSize) {
        alloc_free(buf);
        return 0;
    }
    buf[bufSize] = '\0';

    struct json_parse_result_s result = {0};
    struct json_value_s *root = json_parse_ex(buf, bufSize, json_parse_flags_default,
                                              rom_config_jsonAlloc, NULL, &result);
    alloc_free(buf);
    if (!root) {
        return 0;
    }
    struct json_object_s *object = json_value_as_object(root);
    if (!object) {
        alloc_free(root);
        return 0;
    }

    uint64_t checksum = 0;
    (void)rom_config_jsonGetU64(rom_config_jsonObjectFind(object, "rom_checksum"), &checksum);
    outData->romChecksum = checksum;

    struct json_object_s *cfgObj = json_value_as_object(rom_config_jsonObjectFind(object, "config"));
    if (cfgObj) {
        if (rom_config_jsonGetString(rom_config_jsonObjectFind(cfgObj, "elf"), outData->elfPath, sizeof(outData->elfPath))) {
            outData->hasElf = 1;
        }
        if (rom_config_jsonGetString(rom_config_jsonObjectFind(cfgObj, "source"), outData->sourceDir, sizeof(outData->sourceDir))) {
            outData->hasSource = 1;
        }
        if (rom_config_jsonGetString(rom_config_jsonObjectFind(cfgObj, "toolchain_prefix"), outData->toolchainPrefix, sizeof(outData->toolchainPrefix))) {
            outData->hasToolchain = 1;
        }
    }

    struct json_array_s *bpsArray = json_value_as_array(rom_config_jsonObjectFind(object, "breakpoints"));
    if (bpsArray) {
        size_t count = 0;
        for (struct json_array_element_s *el = bpsArray->start; el; el = el->next) {
            (void)el;
            count++;
        }
        if (count > 0) {
            outData->breakpoints = (rom_config_bp_entry_t*)alloc_calloc(count, sizeof(*outData->breakpoints));
            if (outData->breakpoints) {
                size_t writeIndex = 0;
                for (struct json_array_element_s *el = bpsArray->start; el; el = el->next) {
                    struct json_object_s *bpObj = json_value_as_object(el->value);
                    if (!bpObj) {
                        continue;
                    }
                    uint32_t addr = 0;
                    int enabled = 0;
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(bpObj, "addr"), &addr)) {
                        continue;
                    }
                    (void)rom_config_jsonGetBool(rom_config_jsonObjectFind(bpObj, "enabled"), &enabled);
                    outData->breakpoints[writeIndex].addr = addr;
                    outData->breakpoints[writeIndex].enabled = enabled ? 1 : 0;
                    writeIndex++;
                }
                outData->breakpointCount = writeIndex;
            }
        }
    }

    struct json_array_s *protectsArray = json_value_as_array(rom_config_jsonObjectFind(object, "protects"));
    if (protectsArray) {
        size_t count = 0;
        for (struct json_array_element_s *el = protectsArray->start; el; el = el->next) {
            (void)el;
            count++;
        }
        if (count > 0) {
            outData->protects = (rom_config_protect_entry_t*)alloc_calloc(count, sizeof(*outData->protects));
            if (outData->protects) {
                size_t writeIndex = 0;
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
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "addr"), &addr)) {
                        continue;
                    }
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "size_bits"), &sizeBits)) {
                        continue;
                    }
                    if (!rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "mode"), &mode)) {
                        continue;
                    }
                    (void)rom_config_jsonGetU32(rom_config_jsonObjectFind(pObj, "value"), &value);
                    (void)rom_config_jsonGetBool(rom_config_jsonObjectFind(pObj, "enabled"), &enabled);
                    outData->protects[writeIndex].addr = addr;
                    outData->protects[writeIndex].sizeBits = sizeBits;
                    outData->protects[writeIndex].mode = mode;
                    outData->protects[writeIndex].value = value;
                    outData->protects[writeIndex].enabled = enabled ? 1 : 0;
                    writeIndex++;
                }
                outData->protectCount = writeIndex;
            }
        }
    }

    alloc_free(root);
    return 1;
}

static void
rom_config_writeJsonFile(const char *path, const char *romPath, const rom_config_data_t *data)
{
    if (!path || !*path || !romPath || !*romPath || !data) {
        return;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }

    const char *base = rom_config_basename(romPath);
    char jsonName[PATH_MAX];
    if (base && *base) {
        snprintf(jsonName, sizeof(jsonName), "%s.json", base);
    } else {
        snprintf(jsonName, sizeof(jsonName), "unknown.json");
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"rom_checksum\": %llu,\n", (unsigned long long)data->romChecksum);
    fprintf(f, "  \"rom_filename\": \"%s\",\n", jsonName);

    fprintf(f, "  \"config\": {\n");
    fprintf(f, "    \"elf\": \"%s\",\n", data->hasElf ? data->elfPath : "");
    fprintf(f, "    \"source\": \"%s\",\n", data->hasSource ? data->sourceDir : "");
    fprintf(f, "    \"toolchain_prefix\": \"%s\"\n", data->hasToolchain ? data->toolchainPrefix : "");
    fprintf(f, "  },\n");

    fprintf(f, "  \"breakpoints\": [\n");
    for (size_t i = 0; i < data->breakpointCount; ++i) {
        const rom_config_bp_entry_t *bp = &data->breakpoints[i];
        fprintf(f, "    {\"addr\": %u, \"enabled\": %s}%s\n",
                (unsigned)(bp->addr & 0x00ffffffu),
                bp->enabled ? "true" : "false",
                (i + 1 < data->breakpointCount) ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"protects\": [\n");
    for (size_t i = 0; i < data->protectCount; ++i) {
        const rom_config_protect_entry_t *p = &data->protects[i];
        fprintf(f, "    {\"addr\": %u, \"size_bits\": %u, \"mode\": %u, \"value\": %u, \"enabled\": %s}%s\n",
                (unsigned)(p->addr & 0x00ffffffu),
                (unsigned)p->sizeBits,
                (unsigned)p->mode,
                (unsigned)p->value,
                p->enabled ? "true" : "false",
                (i + 1 < data->protectCount) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
}

static void
rom_config_setActiveDefaultsFromCurrentSystem(void)
{
    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        strncpy(rom_config_activeElfPath, debugger.config.amiga.libretro.exePath, sizeof(rom_config_activeElfPath) - 1);
        strncpy(rom_config_activeSourceDir, debugger.config.amiga.libretro.sourceDir, sizeof(rom_config_activeSourceDir) - 1);
        strncpy(rom_config_activeToolchainPrefix, debugger.config.amiga.libretro.toolchainPrefix, sizeof(rom_config_activeToolchainPrefix) - 1);
    } else {
        strncpy(rom_config_activeElfPath, debugger.config.neogeo.libretro.exePath, sizeof(rom_config_activeElfPath) - 1);
        strncpy(rom_config_activeSourceDir, debugger.config.neogeo.libretro.sourceDir, sizeof(rom_config_activeSourceDir) - 1);
        strncpy(rom_config_activeToolchainPrefix, debugger.config.neogeo.libretro.toolchainPrefix, sizeof(rom_config_activeToolchainPrefix) - 1);
    }
    rom_config_activeElfPath[sizeof(rom_config_activeElfPath) - 1] = '\0';
    rom_config_activeSourceDir[sizeof(rom_config_activeSourceDir) - 1] = '\0';
    rom_config_activeToolchainPrefix[sizeof(rom_config_activeToolchainPrefix) - 1] = '\0';
    rom_config_activeInit = 1;
}

static void
rom_config_applyActiveSettingsToCurrentSystem(void)
{
    if (!rom_config_activeInit) {
        return;
    }
    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        strncpy(debugger.config.amiga.libretro.exePath, rom_config_activeElfPath, sizeof(debugger.config.amiga.libretro.exePath) - 1);
        strncpy(debugger.config.amiga.libretro.sourceDir, rom_config_activeSourceDir, sizeof(debugger.config.amiga.libretro.sourceDir) - 1);
        strncpy(debugger.config.amiga.libretro.toolchainPrefix, rom_config_activeToolchainPrefix, sizeof(debugger.config.amiga.libretro.toolchainPrefix) - 1);
        debugger.config.amiga.libretro.exePath[sizeof(debugger.config.amiga.libretro.exePath) - 1] = '\0';
        debugger.config.amiga.libretro.sourceDir[sizeof(debugger.config.amiga.libretro.sourceDir) - 1] = '\0';
        debugger.config.amiga.libretro.toolchainPrefix[sizeof(debugger.config.amiga.libretro.toolchainPrefix) - 1] = '\0';
    } else {
        strncpy(debugger.config.neogeo.libretro.exePath, rom_config_activeElfPath, sizeof(debugger.config.neogeo.libretro.exePath) - 1);
        strncpy(debugger.config.neogeo.libretro.sourceDir, rom_config_activeSourceDir, sizeof(debugger.config.neogeo.libretro.sourceDir) - 1);
        strncpy(debugger.config.neogeo.libretro.toolchainPrefix, rom_config_activeToolchainPrefix, sizeof(debugger.config.neogeo.libretro.toolchainPrefix) - 1);
        debugger.config.neogeo.libretro.exePath[sizeof(debugger.config.neogeo.libretro.exePath) - 1] = '\0';
        debugger.config.neogeo.libretro.sourceDir[sizeof(debugger.config.neogeo.libretro.sourceDir) - 1] = '\0';
        debugger.config.neogeo.libretro.toolchainPrefix[sizeof(debugger.config.neogeo.libretro.toolchainPrefix) - 1] = '\0';
    }
    debugger_libretroSelectConfig();
}

static void
rom_config_clearBreakpointsCore(void)
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

void
rom_config_loadSettingsForSelectedRom(void)
{
    const char *romPath = debugger.libretro.romPath[0] ? debugger.libretro.romPath : NULL;
    const char *saveDir = rom_config_saveDir(NULL);
    if (!romPath || !saveDir || !rom_config_pathExistsDir(saveDir)) {
        rom_config_setActiveDefaultsFromCurrentSystem();
        return;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), saveDir, romPath)) {
        rom_config_setActiveDefaultsFromCurrentSystem();
        return;
    }

    char legacyPath[PATH_MAX];
    int haveLegacy = rom_config_buildLegacyJsonPathCore(legacyPath, sizeof(legacyPath), saveDir, romPath);
    const char *pathToRead = NULL;
    if (rom_config_pathExistsFile(jsonPath)) {
        pathToRead = jsonPath;
    } else if (haveLegacy && rom_config_pathExistsFile(legacyPath)) {
        pathToRead = legacyPath;
    }
    if (!pathToRead) {
        rom_config_setActiveDefaultsFromCurrentSystem();
        return;
    }

    rom_config_data_t data;
    if (!rom_config_parseFile(pathToRead, &data)) {
        rom_config_setActiveDefaultsFromCurrentSystem();
        return;
    }

    rom_config_setActiveDefaultsFromCurrentSystem();
    if (data.hasElf) {
        strncpy(rom_config_activeElfPath, data.elfPath, sizeof(rom_config_activeElfPath) - 1);
        rom_config_activeElfPath[sizeof(rom_config_activeElfPath) - 1] = '\0';
    }
    if (data.hasSource) {
        strncpy(rom_config_activeSourceDir, data.sourceDir, sizeof(rom_config_activeSourceDir) - 1);
        rom_config_activeSourceDir[sizeof(rom_config_activeSourceDir) - 1] = '\0';
    }
    if (data.hasToolchain) {
        strncpy(rom_config_activeToolchainPrefix, data.toolchainPrefix, sizeof(rom_config_activeToolchainPrefix) - 1);
        rom_config_activeToolchainPrefix[sizeof(rom_config_activeToolchainPrefix) - 1] = '\0';
    }

    rom_config_applyActiveSettingsToCurrentSystem();
    rom_config_freeData(&data);
}

int
rom_config_loadSettingsForRom(const char *saveDir, const char *romPath,
                              char *outElfPath, size_t elfCap,
                              char *outSourceDir, size_t sourceCap,
                              char *outToolchainPrefix, size_t toolchainCap,
                              int *outHasElf, int *outHasSource, int *outHasToolchain)
{
    if (outHasElf) {
        *outHasElf = 0;
    }
    if (outHasSource) {
        *outHasSource = 0;
    }
    if (outHasToolchain) {
        *outHasToolchain = 0;
    }
    if (outElfPath && elfCap > 0) {
        outElfPath[0] = '\0';
    }
    if (outSourceDir && sourceCap > 0) {
        outSourceDir[0] = '\0';
    }
    if (outToolchainPrefix && toolchainCap > 0) {
        outToolchainPrefix[0] = '\0';
    }
    if (!saveDir || !*saveDir || !romPath || !*romPath || !rom_config_pathExistsDir(saveDir)) {
        return 0;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), saveDir, romPath)) {
        return 0;
    }
    char legacyPath[PATH_MAX];
    int haveLegacy = rom_config_buildLegacyJsonPathCore(legacyPath, sizeof(legacyPath), saveDir, romPath);
    const char *pathToRead = NULL;
    if (rom_config_pathExistsFile(jsonPath)) {
        pathToRead = jsonPath;
    } else if (haveLegacy && rom_config_pathExistsFile(legacyPath)) {
        pathToRead = legacyPath;
    }
    if (!pathToRead) {
        return 0;
    }

    rom_config_data_t data;
    if (!rom_config_parseFile(pathToRead, &data)) {
        return 0;
    }

    if (outHasElf) {
        *outHasElf = data.hasElf ? 1 : 0;
    }
    if (outHasSource) {
        *outHasSource = data.hasSource ? 1 : 0;
    }
    if (outHasToolchain) {
        *outHasToolchain = data.hasToolchain ? 1 : 0;
    }
    if (data.hasElf && outElfPath && elfCap > 0) {
        strncpy(outElfPath, data.elfPath, elfCap - 1);
        outElfPath[elfCap - 1] = '\0';
    }
    if (data.hasSource && outSourceDir && sourceCap > 0) {
        strncpy(outSourceDir, data.sourceDir, sourceCap - 1);
        outSourceDir[sourceCap - 1] = '\0';
    }
    if (data.hasToolchain && outToolchainPrefix && toolchainCap > 0) {
        strncpy(outToolchainPrefix, data.toolchainPrefix, toolchainCap - 1);
        outToolchainPrefix[toolchainCap - 1] = '\0';
    }

    rom_config_freeData(&data);
    return 1;
}

void
rom_config_loadRuntimeStateOnBoot(void)
{
    const char *romPath = rom_config_activeRomPath();
    if (!romPath) {
        return;
    }
    const char *saveDir = rom_config_saveDir(NULL);
    if (!saveDir || !rom_config_pathExistsDir(saveDir)) {
        return;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), saveDir, romPath)) {
        return;
    }
    char legacyPath[PATH_MAX];
    int haveLegacy = rom_config_buildLegacyJsonPathCore(legacyPath, sizeof(legacyPath), saveDir, romPath);
    const char *pathToRead = NULL;
    if (rom_config_pathExistsFile(jsonPath)) {
        pathToRead = jsonPath;
    } else if (haveLegacy && rom_config_pathExistsFile(legacyPath)) {
        pathToRead = legacyPath;
    }
    if (!pathToRead) {
        return;
    }

    rom_config_data_t data;
    if (!rom_config_parseFile(pathToRead, &data)) {
        return;
    }

    uint64_t romChecksum = 0;
    if (!rom_config_computeRomChecksum(romPath, &romChecksum)) {
        rom_config_freeData(&data);
        return;
    }

    if (data.romChecksum != 0 && data.romChecksum != romChecksum) {
        rom_config_clearBreakpointsCore();
        protect_clear();
        breakpoints_markDirty();
        trainer_markDirty();
        rom_config_freeData(&data);
        return;
    }

    rom_config_clearBreakpointsCore();
    protect_clear();

    for (size_t i = 0; i < data.breakpointCount; ++i) {
        const rom_config_bp_entry_t *bp = &data.breakpoints[i];
        machine_breakpoint_t *added = machine_addBreakpoint(&debugger.machine, bp->addr, bp->enabled);
        if (added) {
            breakpoints_resolveLocation(added);
        }
        if (bp->enabled) {
            libretro_host_debugAddBreakpoint(bp->addr & 0x00ffffffu);
        }
    }

    uint64_t enabledMask = 0;
    for (size_t i = 0; i < data.protectCount; ++i) {
        const rom_config_protect_entry_t *p = &data.protects[i];
        uint32_t index = 0;
        if (!libretro_host_debugAddProtect(p->addr & 0x00ffffffu, p->sizeBits, p->mode, p->value, &index)) {
            continue;
        }
        if (p->enabled) {
            enabledMask |= (1ull << index);
        }
    }
    if (data.protectCount > 0) {
        libretro_host_debugSetProtectEnabledMask(enabledMask);
    }

    breakpoints_markDirty();
    trainer_markDirty();

    rom_config_freeData(&data);
}

void
rom_config_saveOnExit(void)
{
    const char *romPath = rom_config_activeRomPath();
    const char *saveDir = rom_config_saveDir(NULL);
    if (!romPath || !saveDir || !rom_config_pathExistsDir(saveDir)) {
        return;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), saveDir, romPath)) {
        return;
    }

    uint64_t romChecksum = 0;
    if (!rom_config_computeRomChecksum(romPath, &romChecksum)) {
        return;
    }

    rom_config_data_t data;
    memset(&data, 0, sizeof(data));
    data.romChecksum = romChecksum;

    if (!rom_config_activeInit) {
        rom_config_setActiveDefaultsFromCurrentSystem();
    }
    if (rom_config_activeInit) {
        strncpy(data.elfPath, rom_config_activeElfPath, sizeof(data.elfPath) - 1);
        strncpy(data.sourceDir, rom_config_activeSourceDir, sizeof(data.sourceDir) - 1);
        strncpy(data.toolchainPrefix, rom_config_activeToolchainPrefix, sizeof(data.toolchainPrefix) - 1);
        data.elfPath[sizeof(data.elfPath) - 1] = '\0';
        data.sourceDir[sizeof(data.sourceDir) - 1] = '\0';
        data.toolchainPrefix[sizeof(data.toolchainPrefix) - 1] = '\0';
        data.hasElf = rom_config_activeElfPath[0] ? 1 : 0;
        data.hasSource = rom_config_activeSourceDir[0] ? 1 : 0;
        data.hasToolchain = rom_config_activeToolchainPrefix[0] ? 1 : 0;
    }

    const machine_breakpoint_t *bps = NULL;
    int bpCount = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &bpCount);
    if (bps && bpCount > 0) {
        data.breakpoints = (rom_config_bp_entry_t*)alloc_calloc((size_t)bpCount, sizeof(*data.breakpoints));
        if (data.breakpoints) {
            data.breakpointCount = (size_t)bpCount;
            for (int i = 0; i < bpCount; ++i) {
                data.breakpoints[i].addr = (uint32_t)(bps[i].addr & 0x00ffffffu);
                data.breakpoints[i].enabled = bps[i].enabled ? 1 : 0;
            }
        }
    }

    geo_debug_protect_t protects[GEO_PROTECT_COUNT];
    size_t protectCount = 0;
    uint64_t enabledMask = 0;
    libretro_host_debugReadProtects(protects, GEO_PROTECT_COUNT, &protectCount);
    libretro_host_debugGetProtectEnabledMask(&enabledMask);
    if (protectCount > 0) {
        data.protects = (rom_config_protect_entry_t*)alloc_calloc(protectCount, sizeof(*data.protects));
        if (data.protects) {
            size_t written = 0;
            for (size_t i = 0; i < protectCount; ++i) {
                const geo_debug_protect_t *p = &protects[i];
                if (p->sizeBits == 0) {
                    continue;
                }
                int enabled = ((enabledMask >> i) & 1ull) ? 1 : 0;
                data.protects[written].addr = (uint32_t)(p->addr & 0x00ffffffu);
                data.protects[written].sizeBits = (uint32_t)p->sizeBits;
                data.protects[written].mode = (uint32_t)p->mode;
                data.protects[written].value = (uint32_t)p->value;
                data.protects[written].enabled = enabled;
                written++;
            }
            data.protectCount = written;
        }
    }

    rom_config_writeJsonFile(jsonPath, romPath, &data);
    rom_config_freeData(&data);
}

void
rom_config_saveSettingsForRom(const char *saveDir, const char *romPath,
                              const char *elfPath, const char *sourceDir,
                              const char *toolchainPrefix)
{
    if (!saveDir || !*saveDir || !romPath || !*romPath || !rom_config_pathExistsDir(saveDir)) {
        return;
    }
    char jsonPath[PATH_MAX];
    if (!rom_config_buildJsonPathCore(jsonPath, sizeof(jsonPath), saveDir, romPath)) {
        return;
    }

    rom_config_data_t data;
    int loaded = rom_config_parseFile(jsonPath, &data);
    if (!loaded) {
        char legacyPath[PATH_MAX];
        if (rom_config_buildLegacyJsonPathCore(legacyPath, sizeof(legacyPath), saveDir, romPath)) {
            loaded = rom_config_parseFile(legacyPath, &data);
        }
    }
    if (!loaded) {
        memset(&data, 0, sizeof(data));
    }

    uint64_t romChecksum = 0;
    if (rom_config_computeRomChecksum(romPath, &romChecksum)) {
        data.romChecksum = romChecksum;
    }

    data.hasElf = 0;
    data.hasSource = 0;
    data.hasToolchain = 0;
    data.elfPath[0] = '\0';
    data.sourceDir[0] = '\0';
    data.toolchainPrefix[0] = '\0';

    if (elfPath && *elfPath) {
        strncpy(data.elfPath, elfPath, sizeof(data.elfPath) - 1);
        data.elfPath[sizeof(data.elfPath) - 1] = '\0';
        data.hasElf = 1;
    }
    if (sourceDir && *sourceDir) {
        strncpy(data.sourceDir, sourceDir, sizeof(data.sourceDir) - 1);
        data.sourceDir[sizeof(data.sourceDir) - 1] = '\0';
        data.hasSource = 1;
    }
    if (toolchainPrefix && *toolchainPrefix) {
        strncpy(data.toolchainPrefix, toolchainPrefix, sizeof(data.toolchainPrefix) - 1);
        data.toolchainPrefix[sizeof(data.toolchainPrefix) - 1] = '\0';
        data.hasToolchain = 1;
    }

    const char *activeRom = rom_config_activeRomPath();
    if (activeRom && strcmp(activeRom, romPath) == 0) {
        strncpy(rom_config_activeElfPath, data.elfPath, sizeof(rom_config_activeElfPath) - 1);
        strncpy(rom_config_activeSourceDir, data.sourceDir, sizeof(rom_config_activeSourceDir) - 1);
        strncpy(rom_config_activeToolchainPrefix, data.toolchainPrefix, sizeof(rom_config_activeToolchainPrefix) - 1);
        rom_config_activeElfPath[sizeof(rom_config_activeElfPath) - 1] = '\0';
        rom_config_activeSourceDir[sizeof(rom_config_activeSourceDir) - 1] = '\0';
        rom_config_activeToolchainPrefix[sizeof(rom_config_activeToolchainPrefix) - 1] = '\0';
        rom_config_activeInit = 1;
    }

    rom_config_writeJsonFile(jsonPath, romPath, &data);
    rom_config_freeData(&data);
}
