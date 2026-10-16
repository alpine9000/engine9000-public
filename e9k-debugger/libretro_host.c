/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>

#include "debug.h"
#include "libretro.h"
#include "libretro_host.h"
#include "debugger.h"
#include "input_record.h"
#include "alloc.h"
#include "machine.h"
#include "smoke_test.h"
#include "state_wrap.h"
#include "libretro_host_internal.h"


#define LIBRETRO_HOST_SAVE_STATE_MAX_BYTES (512u * 1024u * 1024u)

libretro_host_t libretro_host;
static libretro_option_override_t *libretro_optionOverrides = NULL;
static size_t libretro_optionOverrideCount = 0;

static void
libretro_host_setOptionValue(const char *key, const char *value);

static void
libretro_host_applyOptionOverrides(void);

static bool
libretro_host_setOptionsV2(const struct retro_core_options_v2 *opts);

static void
libretro_host_clearMemoryMap(void)
{
    if (libretro_host.memoryDescriptors) {
        for (size_t i = 0; i < libretro_host.memoryDescriptorCount; ++i) {
            free((void *)libretro_host.memoryDescriptors[i].addrspace);
            libretro_host.memoryDescriptors[i].addrspace = NULL;
        }
        free(libretro_host.memoryDescriptors);
        libretro_host.memoryDescriptors = NULL;
    }
    libretro_host.memoryDescriptorCount = 0;
}

static bool
libretro_host_copyMemoryMap(const struct retro_memory_map *map)
{
    struct retro_memory_descriptor *copy = NULL;

    libretro_host_clearMemoryMap();
    if (!map || !map->descriptors || map->num_descriptors == 0) {
        return true;
    }

    copy = calloc(map->num_descriptors, sizeof(*copy));
    if (!copy) {
        return false;
    }

    for (unsigned i = 0; i < map->num_descriptors; ++i) {
        copy[i] = map->descriptors[i];
        if (map->descriptors[i].addrspace) {
            copy[i].addrspace = strdup(map->descriptors[i].addrspace);
            if (!copy[i].addrspace) {
                for (unsigned j = 0; j <= i; ++j) {
                    free((void *)copy[j].addrspace);
                    copy[j].addrspace = NULL;
                }
                free(copy);
                return false;
            }
        }
    }

    libretro_host.memoryDescriptors = copy;
    libretro_host.memoryDescriptorCount = map->num_descriptors;
    return true;
}

static bool
libretro_host_serializeWithRetry(uint8_t **ioBuffer, size_t *ioSize, size_t payloadOffset, size_t initialPayloadSize, size_t *outPayloadSize)
{
    size_t payloadCapacity = 0;
    size_t payloadSize = 0;

    if (outPayloadSize) {
        *outPayloadSize = 0;
    }
    if (!ioBuffer || !ioSize || !libretro_host.serialize) {
        return false;
    }

    payloadCapacity = initialPayloadSize;
    if (payloadCapacity == 0) {
        return false;
    }

    for (;;) {
        size_t totalSize = payloadOffset + payloadCapacity;
        uint8_t *buffer = NULL;

        if (totalSize < payloadCapacity || totalSize > LIBRETRO_HOST_SAVE_STATE_MAX_BYTES) {
            return false;
        }

        if (!*ioBuffer || *ioSize < totalSize) {
            buffer = (uint8_t*)alloc_realloc(*ioBuffer, totalSize);
            if (!buffer) {
                return false;
            }
            *ioBuffer = buffer;
        }

        if (libretro_host.serialize(*ioBuffer + payloadOffset, payloadCapacity)) {
            if (!libretro_host.serializeSize) {
                return false;
            }
            payloadSize = libretro_host.serializeSize();
            if (payloadSize == 0 || payloadSize > payloadCapacity) {
                return false;
            }
            *ioSize = payloadOffset + payloadSize;
            if (outPayloadSize) {
                *outPayloadSize = payloadSize;
            }
            return true;
        }

        if (!libretro_host.serializeSize) {
            return false;
        }
        payloadSize = libretro_host.serializeSize();
        if (payloadSize > payloadCapacity) {
            payloadCapacity = payloadSize;
            continue;
        }
        if (payloadCapacity > LIBRETRO_HOST_SAVE_STATE_MAX_BYTES / 2u) {
            return false;
        }
        payloadCapacity *= 2u;
    }
}

void
libretro_host_setControllerPortDevice(unsigned port, unsigned device)
{
    if (port >= LIBRETRO_HOST_MAX_PORTS) {
        return;
    }
    libretro_host.controllerPortDevice[port] = device & RETRO_DEVICE_MASK;
    if (libretro_host.setControllerPortDevice) {
        libretro_host.setControllerPortDevice(port, device);
    }
}

static void
libretro_host_configureControllerPorts(void)
{
  target->configControllerPorts();
}

static void
libretro_host_clearOptionOverrides(void)
{
    if (!libretro_optionOverrides) {
        libretro_optionOverrideCount = 0;
        return;
    }
    for (size_t i = 0; i < libretro_optionOverrideCount; ++i) {
        free(libretro_optionOverrides[i].key);
        free(libretro_optionOverrides[i].value);
    }
    free(libretro_optionOverrides);
    libretro_optionOverrides = NULL;
    libretro_optionOverrideCount = 0;
}

static void
libretro_host_runFrame(void)
{
    if (libretro_host.running && libretro_host.run) {
        libretro_host.run();
    }
}

static void
libretro_host_clearOptions(void)
{
    if (!libretro_host.options) {
        libretro_host.optionCount = 0;
        return;
    }
    for (size_t i = 0; i < libretro_host.optionCount; ++i) {
        free(libretro_host.options[i].value);
        libretro_host.options[i].value = NULL;
    }
    free(libretro_host.options);
    libretro_host.options = NULL;
    libretro_host.optionCount = 0;
}

static void
libretro_host_clearOptionsV2(void)
{
    libretro_host.optionCatsV2 = NULL;
    libretro_host.optionDefsV2 = NULL;
    libretro_host.optionCatCountV2 = 0;
    libretro_host.optionDefCountV2 = 0;
    libretro_host.coreOptionsV2HasCategories = 0;
    libretro_host.coreOptionsDirty = 0;
    if (libretro_host.optionDisplay) {
        for (size_t i = 0; i < libretro_host.optionDisplayCount; ++i) {
            alloc_free(libretro_host.optionDisplay[i].key);
            libretro_host.optionDisplay[i].key = NULL;
        }
        alloc_free(libretro_host.optionDisplay);
        libretro_host.optionDisplay = NULL;
    }
    libretro_host.optionDisplayCount = 0;
    libretro_host.optionDisplayCap = 0;
    libretro_host.updateDisplayCb = NULL;
}

static libretro_option_display_t *
libretro_host_findOptionDisplay(const char *key)
{
    if (!key || !*key || !libretro_host.optionDisplay) {
        return NULL;
    }
    for (size_t i = 0; i < libretro_host.optionDisplayCount; ++i) {
        libretro_option_display_t *ent = &libretro_host.optionDisplay[i];
        if (ent->key && strcmp(ent->key, key) == 0) {
            return ent;
        }
    }
    return NULL;
}

static void
libretro_host_setOptionDisplayVisible(const char *key, int visible)
{
    if (!key || !*key) {
        return;
    }
    libretro_option_display_t *ent = libretro_host_findOptionDisplay(key);
    if (ent) {
        ent->visible = visible ? 1 : 0;
        return;
    }
    size_t nextCount = libretro_host.optionDisplayCount + 1;
    if (nextCount > libretro_host.optionDisplayCap) {
        size_t nextCap = libretro_host.optionDisplayCap ? libretro_host.optionDisplayCap * 2 : 64;
        if (nextCap < nextCount) {
            nextCap = nextCount;
        }
        libretro_option_display_t *next =
            (libretro_option_display_t*)alloc_realloc(libretro_host.optionDisplay, nextCap * sizeof(*next));
        if (!next) {
            return;
        }
        libretro_host.optionDisplay = next;
        libretro_host.optionDisplayCap = nextCap;
    }
    libretro_option_display_t *add = &libretro_host.optionDisplay[libretro_host.optionDisplayCount++];
    add->key = alloc_strdup(key);
    add->visible = visible ? 1 : 0;
}

static libretro_option_t *
libretro_host_findOption(const char *key)
{
    if (!key) {
        return NULL;
    }
    for (size_t i = 0; i < libretro_host.optionCount; ++i) {
        if (libretro_host.options[i].key && strcmp(libretro_host.options[i].key, key) == 0) {
            return &libretro_host.options[i];
        }
    }
    return NULL;
}

void
libretro_host_setCoreOption(const char *key, const char *value)
{
    if (!key || !*key) {
        return;
    }
    for (size_t i = 0; i < libretro_optionOverrideCount; ++i) {
        if (strcmp(libretro_optionOverrides[i].key, key) == 0) {
            if (!value || !*value) {
                free(libretro_optionOverrides[i].key);
                free(libretro_optionOverrides[i].value);
                for (size_t j = i + 1; j < libretro_optionOverrideCount; ++j) {
                    libretro_optionOverrides[j - 1] = libretro_optionOverrides[j];
                }
                libretro_optionOverrideCount--;
                if (libretro_optionOverrideCount == 0) {
                    free(libretro_optionOverrides);
                    libretro_optionOverrides = NULL;
                }
                libretro_host_setOptionValue(key, NULL);
                return;
            }
            free(libretro_optionOverrides[i].value);
            libretro_optionOverrides[i].value = strdup(value);
            libretro_host_setOptionValue(key, value);
            return;
        }
    }
    if (!value || !*value) {
        libretro_host_setOptionValue(key, NULL);
        return;
    }
    libretro_option_override_t *next = realloc(libretro_optionOverrides,
                                               sizeof(*libretro_optionOverrides) * (libretro_optionOverrideCount + 1));
    if (!next) {
        return;
    }
    libretro_optionOverrides = next;
    libretro_optionOverrides[libretro_optionOverrideCount].key = strdup(key);
    libretro_optionOverrides[libretro_optionOverrideCount].value = strdup(value);
    libretro_optionOverrideCount++;
    libretro_host_setOptionValue(key, value);
}

const char *
libretro_host_getCoreOptionOverrideValue(const char *key)
{
    if (!key || !*key) {
        return NULL;
    }
    for (size_t i = 0; i < libretro_optionOverrideCount; ++i) {
        const char *k = libretro_optionOverrides[i].key;
        const char *v = libretro_optionOverrides[i].value;
        if (!k || !v) {
            continue;
        }
        if (strcmp(k, key) == 0) {
            return v;
        }
    }
    return NULL;
}

bool
libretro_host_hasCoreOptionsV2(void)
{
    return libretro_host.optionDefsV2 && libretro_host.optionDefCountV2 > 0;
}

const struct retro_core_option_v2_category *
libretro_host_getCoreOptionCategories(size_t *out_count)
{
    if (out_count) {
        *out_count = libretro_host.optionCatCountV2;
    }
    return libretro_host.optionCatsV2;
}

const struct retro_core_option_v2_definition *
libretro_host_getCoreOptionDefinitions(size_t *out_count)
{
    if (out_count) {
        *out_count = libretro_host.optionDefCountV2;
    }
    return libretro_host.optionDefsV2;
}

const char *
libretro_host_getCoreOptionValue(const char *key)
{
    libretro_option_t *opt = libretro_host_findOption(key);
    if (!opt) {
        return NULL;
    }
    if (opt->value) {
        return opt->value;
    }
    return opt->default_value;
}

const char *
libretro_host_getCoreOptionDefaultValue(const char *key)
{
    libretro_option_t *opt = libretro_host_findOption(key);
    return opt ? opt->default_value : NULL;
}

int
libretro_host_isCoreOptionVisible(const char *key)
{
    if (!key || !*key) {
        return 1;
    }
    libretro_option_display_t *ent = libretro_host_findOptionDisplay(key);
    if (!ent) {
        return 1;
    }
    return ent->visible ? 1 : 0;
}

void
libretro_host_refreshCoreOptionVisibility(void)
{
    if (libretro_host.updateDisplayCb) {
        (void)libretro_host.updateDisplayCb();
    }
}

static bool
libretro_host_setOptions(const struct retro_core_option_definition *defs)
{
    if (!defs) {
        return false;
    }
    size_t count = 0;
    while (defs[count].key) {
        ++count;
    }
    libretro_host_clearOptions();
    if (!count) {
        return true;
    }
    libretro_host.options = (libretro_option_t *)calloc(count, sizeof(*libretro_host.options));
    if (!libretro_host.options) {
        return false;
    }
    libretro_host.optionCount = count;
    for (size_t i = 0; i < count; ++i) {
        libretro_host.options[i].key = defs[i].key;
        libretro_host.options[i].default_value = defs[i].default_value;
        libretro_host.options[i].value = NULL;
    }
    libretro_host_applyOptionOverrides();
    return true;
}

static bool
libretro_host_setOptionsFromV2Definitions(const struct retro_core_option_v2_definition *defs)
{
    if (!defs) {
        return false;
    }
    size_t count = 0;
    while (defs[count].key) {
        ++count;
    }
    libretro_host_clearOptions();
    if (!count) {
        return true;
    }
    libretro_host.options = (libretro_option_t *)calloc(count, sizeof(*libretro_host.options));
    if (!libretro_host.options) {
        return false;
    }
    libretro_host.optionCount = count;
    for (size_t i = 0; i < count; ++i) {
        libretro_host.options[i].key = defs[i].key;
        libretro_host.options[i].default_value = defs[i].default_value;
        libretro_host.options[i].value = NULL;
    }
    libretro_host_applyOptionOverrides();
    return true;
}

static bool
libretro_host_setOptionsV2(const struct retro_core_options_v2 *opts)
{
    if (!opts || !opts->definitions) {
        return false;
    }
    libretro_host_clearOptionsV2();
    libretro_host.optionDefsV2 = opts->definitions;
    libretro_host.optionDefCountV2 = 0;
    while (libretro_host.optionDefsV2[libretro_host.optionDefCountV2].key) {
        libretro_host.optionDefCountV2++;
    }

    libretro_host.optionCatsV2 = NULL;
    libretro_host.optionCatCountV2 = 0;
    if (opts->categories) {
        libretro_host.optionCatsV2 = opts->categories;
        while (libretro_host.optionCatsV2[libretro_host.optionCatCountV2].key) {
            libretro_host.optionCatCountV2++;
        }
    }

    libretro_host.coreOptionsV2HasCategories = libretro_host.optionCatsV2 ? 1 : 0;

    return libretro_host_setOptionsFromV2Definitions(opts->definitions);
}

static void
libretro_host_setOptionValue(const char *key, const char *value)
{
    if (!key) {
        return;
    }
    libretro_option_t *opt = libretro_host_findOption(key);
    if (!opt) {
        return;
    }
    free(opt->value);
    opt->value = NULL;
    if (value) {
        opt->value = strdup(value);
    }
    libretro_host.coreOptionsDirty = 1;
}

static void
libretro_host_applyOptionOverrides(void)
{
    for (size_t i = 0; i < libretro_optionOverrideCount; ++i) {
        if (!libretro_optionOverrides[i].key || !libretro_optionOverrides[i].value) {
            continue;
        }
        libretro_host_setOptionValue(libretro_optionOverrides[i].key, libretro_optionOverrides[i].value);
    }
}

static bool
libretro_host_mkdir_p(const char *path)
{
    if (!path || !*path) {
        return false;
    }
    char buffer[PATH_MAX];
    strncpy(buffer, path, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    size_t len = strlen(buffer);
    for (size_t i = 1; i < len; ++i) {
        if (buffer[i] == '/') {
            char prev = buffer[i];
            buffer[i] = '\0';
            if (!debugger_platform_makeDir(buffer)) {
                return false;
            }
            buffer[i] = prev;
        }
    }
    if (!debugger_platform_makeDir(buffer)) {
        return false;
    }
    return true;
}

static void
libretro_host_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level == RETRO_LOG_DEBUG) {
        return;
    }
    char buffer[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    if (level == RETRO_LOG_INFO) {
        if (debugger.config.logsEnabled) {
            debug_printf("libretro: %s", buffer);
        }
        return;
    }
    linebuf_pushErr(&debugger.console, buffer);
    fprintf(stderr, "libretro: %s\n", buffer);
}

static void
libretro_host_destroyTexture(void)
{
    if (libretro_host.texture) {
        SDL_DestroyTexture(libretro_host.texture);
        libretro_host.texture = NULL;
    }
    libretro_host.textureWidth = 0;
    libretro_host.textureHeight = 0;
    libretro_host.textureSeq = 0;
}

static void
libretro_host_estimateFpsClearDistinctColorState(void)
{
    if (libretro_host.estimateFpsDistinctColorKeys) {
        free(libretro_host.estimateFpsDistinctColorKeys);
        libretro_host.estimateFpsDistinctColorKeys = NULL;
    }
    if (libretro_host.estimateFpsDistinctColorUsed) {
        free(libretro_host.estimateFpsDistinctColorUsed);
        libretro_host.estimateFpsDistinctColorUsed = NULL;
    }
    libretro_host.estimateFpsDistinctColorCapacity = 0;
    libretro_host.estimateFpsDistinctColorCount = 0;
}

static int
libretro_host_estimateFpsEnsureDistinctColorCapacity(size_t pixelCount)
{
    size_t minCapacity = pixelCount > (SIZE_MAX / 2) ? SIZE_MAX : (pixelCount * 2);
    size_t wantedCapacity = 16;

    while (wantedCapacity < minCapacity && wantedCapacity <= (SIZE_MAX / 2)) {
        wantedCapacity *= 2;
    }
    if (wantedCapacity < minCapacity) {
        wantedCapacity = minCapacity;
    }
    if (wantedCapacity <= libretro_host.estimateFpsDistinctColorCapacity) {
        return 1;
    }

    uint32_t *nextKeys = (uint32_t *)realloc(libretro_host.estimateFpsDistinctColorKeys,
                                             wantedCapacity * sizeof(uint32_t));
    if (!nextKeys) {
        return 0;
    }
    libretro_host.estimateFpsDistinctColorKeys = nextKeys;

    uint8_t *nextUsed = (uint8_t *)realloc(libretro_host.estimateFpsDistinctColorUsed,
                                           wantedCapacity * sizeof(uint8_t));
    if (!nextUsed) {
        return 0;
    }
    libretro_host.estimateFpsDistinctColorUsed = nextUsed;
    libretro_host.estimateFpsDistinctColorCapacity = wantedCapacity;
    return 1;
}

static int
libretro_host_estimateFpsBeginDistinctColorCount(size_t pixelCount)
{
    if (!pixelCount) {
        libretro_host.estimateFpsDistinctColorCount = 0;
        return 0;
    }
    if (!libretro_host_estimateFpsEnsureDistinctColorCapacity(pixelCount)) {
        libretro_host.estimateFpsDistinctColorCount = 0;
        return 0;
    }
    memset(libretro_host.estimateFpsDistinctColorUsed,
           0,
           libretro_host.estimateFpsDistinctColorCapacity * sizeof(uint8_t));
    libretro_host.estimateFpsDistinctColorCount = 0;
    return 1;
}

static void
libretro_host_estimateFpsCountDistinctColor(uint32_t color)
{
    if (!libretro_host.estimateFpsDistinctColorKeys ||
        !libretro_host.estimateFpsDistinctColorUsed ||
        !libretro_host.estimateFpsDistinctColorCapacity) {
        return;
    }

    uint32_t rgb = color & 0x00ffffffu;
    size_t capacity = libretro_host.estimateFpsDistinctColorCapacity;
    size_t idx = ((size_t)rgb * 2654435761u) % capacity;

    while (libretro_host.estimateFpsDistinctColorUsed[idx]) {
        if (libretro_host.estimateFpsDistinctColorKeys[idx] == rgb) {
            return;
        }
        idx++;
        if (idx >= capacity) {
            idx = 0;
        }
    }

    libretro_host.estimateFpsDistinctColorUsed[idx] = 1;
    libretro_host.estimateFpsDistinctColorKeys[idx] = rgb;
    libretro_host.estimateFpsDistinctColorCount++;
}

static void
libretro_host_estimateFpsVoteColor(uint32_t color, uint32_t *candidate, size_t *balance)
{
    if (!candidate || !balance) {
        return;
    }
    if (*balance == 0u) {
        *candidate = color;
        *balance = 1u;
        return;
    }
    if (*candidate == color) {
        (*balance)++;
        return;
    }
    (*balance)--;
}

static int
libretro_host_estimateFpsHasHorizontalRepeatFactor(const uint8_t *frameData,
                                                   unsigned width,
                                                   unsigned height,
                                                   size_t pitch,
                                                   unsigned factor)
{
    if (!frameData || !width || !height || pitch == 0 || factor <= 1u) {
        return 0;
    }
    if ((width % factor) != 0u) {
        return 0;
    }

    uint64_t comparisons = 0u;
    uint64_t matches = 0u;

    for (unsigned y = 0; y < height; ++y) {
        const uint32_t *row = (const uint32_t *)(frameData + ((size_t)y * pitch));
        for (unsigned x = 0; x + factor <= width; x += factor) {
            uint32_t firstColor = row[x];
            for (unsigned offset = 1; offset < factor; ++offset) {
                comparisons++;
                if (row[x + offset] == firstColor) {
                    matches++;
                }
            }
        }
    }

    if (comparisons == 0u) {
        return 0;
    }
    return (matches * 100u) >= (comparisons * 98u) ? 1 : 0;
}

static unsigned
libretro_host_estimateFpsGetHorizontalRepeatFactor(const uint8_t *frameData,
                                                   unsigned width,
                                                   unsigned height,
                                                   size_t pitch)
{
    if (libretro_host_estimateFpsHasHorizontalRepeatFactor(frameData, width, height, pitch, 4u)) {
        return 4u;
    }
    if (libretro_host_estimateFpsHasHorizontalRepeatFactor(frameData, width, height, pitch, 2u)) {
        return 2u;
    }
    return 1u;
}

static int
libretro_host_estimateFpsFindBorderColor(const uint8_t *frameData,
                                         unsigned width,
                                         unsigned height,
                                         size_t pitch,
                                         uint32_t *outColor)
{
    if (!frameData || !width || !height || pitch == 0 || !outColor) {
        return 0;
    }

    uint32_t candidate = 0u;
    size_t balance = 0u;
    size_t total = 0u;

    const uint32_t *topRow = (const uint32_t *)frameData;
    const uint32_t *bottomRow = (const uint32_t *)(frameData + ((size_t)(height - 1u) * pitch));

    for (unsigned x = 0; x < width; ++x) {
        libretro_host_estimateFpsVoteColor(topRow[x], &candidate, &balance);
        total++;
    }

    if (height > 1u) {
        for (unsigned x = 0; x < width; ++x) {
            libretro_host_estimateFpsVoteColor(bottomRow[x], &candidate, &balance);
            total++;
        }
    }

    for (unsigned y = 1; y + 1u < height; ++y) {
        const uint32_t *row = (const uint32_t *)(frameData + ((size_t)y * pitch));
        libretro_host_estimateFpsVoteColor(row[0], &candidate, &balance);
        total++;
        if (width > 1u) {
            libretro_host_estimateFpsVoteColor(row[width - 1u], &candidate, &balance);
            total++;
        }
    }

    if (total == 0u || balance == 0u) {
        return 0;
    }

    size_t matches = 0u;
    for (unsigned x = 0; x < width; ++x) {
        if (topRow[x] == candidate) {
            matches++;
        }
    }
    if (height > 1u) {
        for (unsigned x = 0; x < width; ++x) {
            if (bottomRow[x] == candidate) {
                matches++;
            }
        }
    }
    for (unsigned y = 1; y + 1u < height; ++y) {
        const uint32_t *row = (const uint32_t *)(frameData + ((size_t)y * pitch));
        if (row[0] == candidate) {
            matches++;
        }
        if (width > 1u && row[width - 1u] == candidate) {
            matches++;
        }
    }

    if ((matches * 5u) < (total * 3u)) {
        return 0;
    }

    *outColor = candidate;
    return 1;
}

static void
libretro_host_estimateFpsSetVisibleArea(unsigned width, unsigned height)
{
    libretro_host.estimateFpsVisibleWidth = width;
    libretro_host.estimateFpsVisibleHeight = height;
}

static void
libretro_host_estimateFpsEstimateVisibleArea(const uint8_t *frameData,
                                             unsigned width,
                                             unsigned height,
                                             size_t pitch)
{
    libretro_host_estimateFpsSetVisibleArea(0u, 0u);
    if (!frameData || !width || !height || pitch == 0) {
        return;
    }

    unsigned repeatFactor = libretro_host_estimateFpsGetHorizontalRepeatFactor(frameData,
                                                                                width,
                                                                                height,
                                                                                pitch);
    unsigned fullWidth = width / repeatFactor;
    if (fullWidth == 0u) {
        fullWidth = width;
    }

    uint32_t borderColor = 0u;
    if (!libretro_host_estimateFpsFindBorderColor(frameData, width, height, pitch, &borderColor)) {
        libretro_host_estimateFpsSetVisibleArea(fullWidth, height);
        return;
    }

    unsigned rowThreshold = fullWidth / 32u;
    unsigned colThreshold = height / 32u;
    if (rowThreshold < 1u) {
        rowThreshold = 1u;
    }
    if (colThreshold < 1u) {
        colThreshold = 1u;
    }
    rowThreshold *= repeatFactor;

    int top = -1;
    int bottom = -1;
    for (unsigned y = 0; y < height; ++y) {
        const uint32_t *row = (const uint32_t *)(frameData + ((size_t)y * pitch));
        unsigned activeCount = 0u;
        for (unsigned x = 0; x < width; ++x) {
            if (row[x] != borderColor) {
                activeCount++;
            }
        }
        if (activeCount >= rowThreshold) {
            top = (int)y;
            break;
        }
    }
    if (top < 0) {
        libretro_host_estimateFpsSetVisibleArea(fullWidth, height);
        return;
    }

    for (int y = (int)height - 1; y >= top; --y) {
        const uint32_t *row = (const uint32_t *)(frameData + ((size_t)y * pitch));
        unsigned activeCount = 0u;
        for (unsigned x = 0; x < width; ++x) {
            if (row[x] != borderColor) {
                activeCount++;
            }
        }
        if (activeCount >= rowThreshold) {
            bottom = y;
            break;
        }
    }
    if (bottom < top) {
        libretro_host_estimateFpsSetVisibleArea(fullWidth, height);
        return;
    }

    int left = -1;
    int right = -1;
    for (unsigned x = 0; x < width; ++x) {
        unsigned activeCount = 0u;
        for (int y = top; y <= bottom; ++y) {
            const uint32_t *row = (const uint32_t *)(frameData + ((size_t)y * pitch));
            if (row[x] != borderColor) {
                activeCount++;
            }
        }
        if (activeCount >= colThreshold) {
            left = (int)x;
            break;
        }
    }
    if (left < 0) {
        libretro_host_estimateFpsSetVisibleArea(fullWidth, (unsigned)(bottom - top + 1));
        return;
    }

    for (int x = (int)width - 1; x >= left; --x) {
        unsigned activeCount = 0u;
        for (int y = top; y <= bottom; ++y) {
            const uint32_t *row = (const uint32_t *)(frameData + ((size_t)y * pitch));
            if (row[x] != borderColor) {
                activeCount++;
            }
        }
        if (activeCount >= colThreshold) {
            right = x;
            break;
        }
    }
    if (right < left) {
        libretro_host_estimateFpsSetVisibleArea(fullWidth, (unsigned)(bottom - top + 1));
        return;
    }

    unsigned visibleWidth = (unsigned)(right - left + 1);
    unsigned visibleHeight = (unsigned)(bottom - top + 1);
    if (repeatFactor > 1u) {
        visibleWidth /= repeatFactor;
    }
    if (visibleWidth == 0u) {
        visibleWidth = 1u;
    }
    if (visibleWidth > fullWidth) {
        visibleWidth = fullWidth;
    }
    if (visibleHeight > height) {
        visibleHeight = height;
    }

    libretro_host_estimateFpsSetVisibleArea(visibleWidth, visibleHeight);
}

void
libretro_host_clearEstimateFpsState(void)
{
    if (libretro_host.estimateFpsReferenceFrameData) {
        free(libretro_host.estimateFpsReferenceFrameData);
        libretro_host.estimateFpsReferenceFrameData = NULL;
    }
    libretro_host.estimateFpsReferenceFrameCapacity = 0;
    libretro_host.estimateFpsReferenceFrameSize = 0;
    libretro_host.estimateFpsReferencePitch = 0;
    libretro_host.estimateFpsReferenceWidth = 0;
    libretro_host.estimateFpsReferenceHeight = 0;
    libretro_host.estimateFpsReferenceFrameIndex = 0;
    libretro_host.estimateFpsValue = 0.0;
    libretro_host.estimateFpsVisibleWidth = 0u;
    libretro_host.estimateFpsVisibleHeight = 0u;
    libretro_host_estimateFpsClearDistinctColorState();
}

static int
libretro_host_estimateFpsEnsureReferenceCapacity(size_t needed)
{
    if (needed <= libretro_host.estimateFpsReferenceFrameCapacity) {
        return 1;
    }
    uint8_t *next = (uint8_t *)realloc(libretro_host.estimateFpsReferenceFrameData, needed);
    if (!next) {
        return 0;
    }
    libretro_host.estimateFpsReferenceFrameData = next;
    libretro_host.estimateFpsReferenceFrameCapacity = needed;
    return 1;
}

static int
libretro_host_estimateFpsFramesDiffer(const uint8_t *frameData,
                                      unsigned width,
                                      unsigned height,
                                      size_t pitch)
{
    if (!frameData ||
        !libretro_host.estimateFpsReferenceFrameData ||
        width != (unsigned)libretro_host.estimateFpsReferenceWidth ||
        height != (unsigned)libretro_host.estimateFpsReferenceHeight ||
        pitch != libretro_host.estimateFpsReferencePitch) {
        return 1;
    }
    size_t frameSize = (size_t)height * pitch;
    if (frameSize != libretro_host.estimateFpsReferenceFrameSize) {
        return 1;
    }
    return memcmp(frameData, libretro_host.estimateFpsReferenceFrameData, frameSize) != 0 ? 1 : 0;
}

static void
libretro_host_estimateFpsStoreReferenceFrame(const uint8_t *frameData,
                                             unsigned width,
                                             unsigned height,
                                             size_t pitch,
                                             unsigned frameIndex)
{
    if (!frameData || !width || !height) {
        return;
    }
    size_t frameSize = (size_t)height * pitch;
    if (!libretro_host_estimateFpsEnsureReferenceCapacity(frameSize)) {
        return;
    }
    memcpy(libretro_host.estimateFpsReferenceFrameData, frameData, frameSize);
    libretro_host.estimateFpsReferenceFrameSize = frameSize;
    libretro_host.estimateFpsReferencePitch = pitch;
    libretro_host.estimateFpsReferenceWidth = (int)width;
    libretro_host.estimateFpsReferenceHeight = (int)height;
    libretro_host.estimateFpsReferenceFrameIndex = frameIndex;
}

static void
libretro_host_estimateFpsProcessFrame(const uint8_t *frameData,
                                      unsigned width,
                                      unsigned height,
                                      size_t pitch,
                                      unsigned frameIndex)
{
    if (!libretro_host.estimateFpsEnabled) {
        return;
    }
    if (!frameData || !width || !height || pitch == 0) {
        return;
    }
    libretro_host_estimateFpsEstimateVisibleArea(frameData, width, height, pitch);
    if (!libretro_host.estimateFpsReferenceFrameData ||
        libretro_host.estimateFpsReferenceFrameSize == 0) {
        libretro_host_estimateFpsStoreReferenceFrame(frameData, width, height, pitch, frameIndex);
        return;
    }
    if (!libretro_host_estimateFpsFramesDiffer(frameData, width, height, pitch)) {
        return;
    }
    if (frameIndex > libretro_host.estimateFpsReferenceFrameIndex) {
        unsigned span = frameIndex - libretro_host.estimateFpsReferenceFrameIndex;
        if (span > 0u) {
            double coreFps = libretro_host_getTimingFps();
            if (coreFps > 0.0) {
                libretro_host.estimateFpsValue = coreFps / (double)span;
            }
        }
    }
    libretro_host_estimateFpsStoreReferenceFrame(frameData, width, height, pitch, frameIndex);
}

static void
libretro_host_clearFrame(void)
{
    if (libretro_host.frameData) {
        free(libretro_host.frameData);
        libretro_host.frameData = NULL;
    }
    libretro_host.frameCapacity = 0;
    libretro_host.framePitch = 0;
    libretro_host.frameWidth = 0;
    libretro_host.frameHeight = 0;
    libretro_host.frameSeq = 0;
    libretro_host_clearEstimateFpsState();
}

static void
libretro_host_closeAudio(void)
{
    if (libretro_host.audioDev) {
        SDL_ClearQueuedAudio(libretro_host.audioDev);
        SDL_CloseAudioDevice(libretro_host.audioDev);
        libretro_host.audioDev = 0;
    }
    libretro_host.audioSampleRate = 0;
    libretro_host.audioMaxQueue = 0;
}

static void
libretro_host_openAudio(void)
{
    if (!libretro_host.audioEnabled) {
        libretro_host_closeAudio();
        return;
    }
    libretro_host_closeAudio();
    int rate = (int)libretro_host.avInfo.timing.sample_rate;
    if (rate <= 0) {
        rate = 44100;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    SDL_AudioSpec got;
    SDL_zero(got);
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!dev) {
        fprintf(stderr, "libretro: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    libretro_host.audioDev = dev;
    libretro_host.audioSampleRate = got.freq;
    int ms = debugger.libretro.audioBufferMs;
    if (ms <= 0) {
        ms = 50;
    }
    size_t bytes_per_sec = (size_t)got.freq * (size_t)got.channels * sizeof(int16_t);
    libretro_host.audioMaxQueue = (bytes_per_sec * (size_t)ms) / 1000;
    SDL_PauseAudioDevice(dev, 0);
}

static void
libretro_host_videoCallback(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || !width || !height) {
        return;
    }
    unsigned frameIndex = ++libretro_host.videoFrameCount;
    (void)frameIndex;
    size_t bytesPerPixel = 0;
    if (width > 0) {
        bytesPerPixel = pitch / (size_t)width;
    }
    enum retro_pixel_format convertFormat = libretro_host.pixelFormat;
    if (convertFormat == RETRO_PIXEL_FORMAT_XRGB8888 && bytesPerPixel == 2) {
        convertFormat = RETRO_PIXEL_FORMAT_0RGB1555;
    }
    if (convertFormat == RETRO_PIXEL_FORMAT_RGB565 ||
        convertFormat == RETRO_PIXEL_FORMAT_0RGB1555) {
        size_t needed = (size_t)width * (size_t)height * 4;
        int countDistinctColors = 0;
        if (needed > libretro_host.frameCapacity) {
            uint8_t *next = (uint8_t *)realloc(libretro_host.frameData, needed);
            if (!next) {
                return;
            }
            libretro_host.frameData = next;
            libretro_host.frameCapacity = needed;
        }
        if (libretro_host.estimateFpsEnabled) {
            countDistinctColors = libretro_host_estimateFpsBeginDistinctColorCount((size_t)width * (size_t)height);
        }
        uint32_t *dst = (uint32_t *)libretro_host.frameData;
        if (convertFormat == RETRO_PIXEL_FORMAT_RGB565) {
            for (unsigned y = 0; y < height; ++y) {
                const uint16_t *srcRow = (const uint16_t *)((const uint8_t *)data + (size_t)y * pitch);
                uint32_t *dstRow = dst + (size_t)y * (size_t)width;
                for (unsigned x = 0; x < width; ++x) {
                    uint16_t p = srcRow[x];
                    uint8_t r = (uint8_t)(((p >> 11) & 0x1F) << 3);
                    uint8_t g = (uint8_t)(((p >> 5) & 0x3F) << 2);
                    uint8_t b = (uint8_t)((p & 0x1F) << 3);
                    uint32_t color = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                    dstRow[x] = color;
                    if (countDistinctColors) {
                        libretro_host_estimateFpsCountDistinctColor(color);
                    }
                }
            }
        } else {
            for (unsigned y = 0; y < height; ++y) {
                const uint16_t *srcRow = (const uint16_t *)((const uint8_t *)data + (size_t)y * pitch);
                uint32_t *dstRow = dst + (size_t)y * (size_t)width;
                for (unsigned x = 0; x < width; ++x) {
                    uint16_t p = srcRow[x];
                    uint8_t r = (uint8_t)(((p >> 10) & 0x1F) << 3);
                    uint8_t g = (uint8_t)(((p >> 5) & 0x1F) << 3);
                    uint8_t b = (uint8_t)((p & 0x1F) << 3);
                    uint32_t color = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                    dstRow[x] = color;
                    if (countDistinctColors) {
                        libretro_host_estimateFpsCountDistinctColor(color);
                    }
                }
            }
        }
        libretro_host.framePitch = (size_t)width * 4;
        libretro_host.frameWidth = (int)width;
        libretro_host.frameHeight = (int)height;
        libretro_host.frameSeq++;
        libretro_host_estimateFpsProcessFrame(libretro_host.frameData,
                                              width,
                                              height,
                                              libretro_host.framePitch,
                                              frameIndex);
        return;
    }

    size_t needed = (size_t)height * pitch;
    int countDistinctColors = 0;
    if (needed > libretro_host.frameCapacity) {
        uint8_t *next = (uint8_t *)realloc(libretro_host.frameData, needed);
        if (!next) {
            return;
        }
        libretro_host.frameData = next;
        libretro_host.frameCapacity = needed;
    }
    if (libretro_host.estimateFpsEnabled) {
        countDistinctColors = libretro_host_estimateFpsBeginDistinctColorCount((size_t)width * (size_t)height);
    }
    memcpy(libretro_host.frameData, data, needed);
    if (libretro_host.pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888 && bytesPerPixel == 4) {
        for (unsigned y = 0; y < height; ++y) {
            uint32_t *row = (uint32_t *)(libretro_host.frameData + (size_t)y * pitch);
            for (unsigned x = 0; x < width; ++x) {
                row[x] |= 0xFF000000u;
                if (countDistinctColors) {
                    libretro_host_estimateFpsCountDistinctColor(row[x]);
                }
            }
        }
    }
    libretro_host.framePitch = pitch;
    libretro_host.frameWidth = (int)width;
    libretro_host.frameHeight = (int)height;
    libretro_host.frameSeq++;
    libretro_host_estimateFpsProcessFrame(libretro_host.frameData,
                                          width,
                                          height,
                                          pitch,
                                          frameIndex);
}

static int16_t
libretro_host_clampToInt16(int value);

static void
libretro_host_markSmokeAudioFailed(void)
{
    debugger.smokeTestFailed = 1;
    debugger.smokeTestExitCode = 1;
}

static void
libretro_host_audioSample(int16_t left, int16_t right)
{
    int16_t sample[2] = { left, right };
    if (smoke_test_captureAudio(sample, 1) != 0) {
        libretro_host_markSmokeAudioFailed();
        return;
    }
    if (!libretro_host.audioDev) {
        return;
    }
    int volume = libretro_host.audioVolumePercent;
    if (volume <= 0) {
        return;
    }
    if (volume < 100) {
        int leftScaled = ((int)left * volume) / 100;
        int rightScaled = ((int)right * volume) / 100;
        sample[0] = libretro_host_clampToInt16(leftScaled);
        sample[1] = libretro_host_clampToInt16(rightScaled);
    }
    if (libretro_host.audioMaxQueue > 0) {
        size_t queued = SDL_GetQueuedAudioSize(libretro_host.audioDev);
        if (queued >= libretro_host.audioMaxQueue) {
            SDL_ClearQueuedAudio(libretro_host.audioDev);
            return;
        }
    }
    SDL_QueueAudio(libretro_host.audioDev, sample, sizeof(sample));
}

static size_t
libretro_host_audioSampleBatch(const int16_t *data, size_t frames)
{
    if (smoke_test_captureAudio(data, frames) != 0) {
        libretro_host_markSmokeAudioFailed();
        return frames;
    }
    if (!libretro_host.audioDev || !data || frames == 0) {
        return frames;
    }
    int volume = libretro_host.audioVolumePercent;
    if (volume <= 0) {
        return frames;
    }
    size_t bytes = frames * 2 * sizeof(int16_t);
    if (libretro_host.audioMaxQueue > 0) {
        size_t queued = SDL_GetQueuedAudioSize(libretro_host.audioDev);
        if (queued >= libretro_host.audioMaxQueue) {
            SDL_ClearQueuedAudio(libretro_host.audioDev);
            return frames;
        }
    }
    if (volume >= 100) {
        SDL_QueueAudio(libretro_host.audioDev, data, bytes);
        return frames;
    }
    enum { kChunkSamples = 2048 };
    int16_t scaled[kChunkSamples];
    size_t samples = frames * 2;
    size_t pos = 0;
    while (pos < samples) {
        size_t chunk = samples - pos;
        if (chunk > kChunkSamples) {
            chunk = kChunkSamples;
        }
        for (size_t i = 0; i < chunk; ++i) {
            int v = ((int)data[pos + i] * volume) / 100;
            scaled[i] = libretro_host_clampToInt16(v);
        }
        SDL_QueueAudio(libretro_host.audioDev, scaled, chunk * sizeof(int16_t));
        pos += chunk;
    }
    return frames;
}

static void
libretro_host_inputPoll(void)
{
    for (unsigned port = 0; port < LIBRETRO_HOST_MAX_PORTS; ++port) {
        libretro_host.mouseFrameX[port] = libretro_host.mousePendingX[port];
        libretro_host.mouseFrameY[port] = libretro_host.mousePendingY[port];
        libretro_host.mousePendingX[port] = 0;
        libretro_host.mousePendingY[port] = 0;
    }
    if (libretro_host.autoPressDelayFrames > 0) {
        libretro_host.autoPressDelayFrames--;
        return;
    }
    if (libretro_host.autoPressHoldFrames > 0) {
        uint32_t bit = 1u << RETRO_DEVICE_ID_JOYPAD_START;
        libretro_host.autoInputMask[0] |= bit;
        libretro_host.autoPressHoldFrames--;
        if (libretro_host.autoPressHoldFrames == 0) {
            libretro_host.autoInputMask[0] &= ~bit;
        }
    }
}

static int16_t
libretro_host_clampToInt16(int value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static int16_t
libretro_host_inputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    unsigned baseDevice = device & RETRO_DEVICE_MASK;
    if (baseDevice == RETRO_DEVICE_MOUSE) {
        if (index != 0 || port >= LIBRETRO_HOST_MAX_PORTS) {
            return 0;
        }
        if (id == RETRO_DEVICE_ID_MOUSE_X) {
            return libretro_host_clampToInt16(libretro_host.mouseFrameX[port]);
        }
        if (id == RETRO_DEVICE_ID_MOUSE_Y) {
            return libretro_host_clampToInt16(libretro_host.mouseFrameY[port]);
        }
        if (id <= RETRO_DEVICE_ID_MOUSE_BUTTON_5) {
            return (libretro_host.mouseButtonMask[port] & (1u << id)) ? 1 : 0;
        }
        return 0;
    }
    if (baseDevice == RETRO_DEVICE_KEYBOARD) {
        if (id >= RETROK_LAST) {
            return 0;
        }
        uint8_t down = libretro_host.keyboardState[id];
        return down ? 1 : 0;
    }
    if (baseDevice != RETRO_DEVICE_JOYPAD || index != 0) {
        return 0;
    }
    if (port >= LIBRETRO_HOST_MAX_PORTS || id >= 32) {
        return 0;
    }
    uint32_t mask = libretro_host.inputMask[port] | libretro_host.autoInputMask[port];
    return (mask & (1u << id)) ? 1 : 0;
}

void *
libretro_host_loadSymbol(const char *name)
{
    if (!libretro_host.lib || !name || !*name) {
        return NULL;
    }
    void *symbol = debugger_platform_loadSharedSymbol(libretro_host.lib, name);
    if (!symbol) {
        fprintf(stderr, "libretro: missing symbol %s\n", name);
    }
    return symbol;
}

static bool
libretro_host_environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_ROTATION:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        if (!data) {
            return false;
        }
        {
            const struct retro_message *msg = data;
            if (msg->msg) {
                fprintf(stderr, "libretro message: %s\n", msg->msg);
            }
        }
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (!data || !*libretro_host.systemDir) {
            return false;
        }
        *(const char **)data = libretro_host.systemDir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!data || !*libretro_host.saveDir) {
            return false;
        }
        *(const char **)data = libretro_host.saveDir;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        if (!data) {
            return false;
        }
        {
            struct retro_variable *var = data;
            libretro_option_t *opt = libretro_host_findOption(var->key);
            if (!opt) {
                return false;
            }
            if (opt->value) {
                var->value = opt->value;
            } else {
                var->value = opt->default_value;
            }
            return var->value != NULL;
        }
    case RETRO_ENVIRONMENT_SET_VARIABLE:
        if (!data) {
            return false;
        }
        {
            const struct retro_variable *var = data;
            libretro_host_setOptionValue(var->key, var->value);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        if (!data) {
            return false;
        }
        {
            const struct retro_variable *vars = data;
            for (size_t i = 0; vars[i].key; ++i) {
                libretro_host_setOptionValue(vars[i].key, vars[i].value);
            }
        }
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        if (!data) {
            return false;
        }
        {
            enum retro_pixel_format format = *(enum retro_pixel_format *)data;
            if (format == RETRO_PIXEL_FORMAT_XRGB8888 ||
                format == RETRO_PIXEL_FORMAT_RGB565 ||
                format == RETRO_PIXEL_FORMAT_0RGB1555) {
                libretro_host.pixelFormat = format;
                return true;
            }
        }
        return false;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        if (!data) {
            return false;
        }
        libretro_host.avInfo = *(const struct retro_system_av_info *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        if (!data) {
            return false;
        }
        {
            const struct retro_game_geometry *geom = (const struct retro_game_geometry *)data;
            libretro_host.avInfo.geometry.base_width = geom->base_width;
            libretro_host.avInfo.geometry.base_height = geom->base_height;
            libretro_host.avInfo.geometry.aspect_ratio = geom->aspect_ratio;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!data) {
            return false;
        }
        {
            struct retro_log_callback *log = data;
            log->log = libretro_host_log;
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        libretro_host_setOptions((const struct retro_core_option_definition *)data);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        if (!data) {
            return false;
        }
        {
            const struct retro_core_options_intl *intl = data;
            const struct retro_core_option_definition *defs = intl->local ? intl->local : intl->us;
            libretro_host_setOptions(defs);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        if (data) {
            const struct retro_core_option_display *disp = (const struct retro_core_option_display *)data;
            if (disp && disp->key && *disp->key) {
                libretro_host_setOptionDisplayVisible(disp->key, disp->visible ? 1 : 0);
            }
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        if (data) {
            const struct retro_core_options_update_display_callback *cb =
                (const struct retro_core_options_update_display_callback *)data;
            libretro_host.updateDisplayCb = cb ? cb->callback : NULL;
        }
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        if (!data) {
            return false;
        }
        return libretro_host_copyMemoryMap((const struct retro_memory_map *)data);
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        if (!data) {
            return false;
        }
        {
            const struct retro_keyboard_callback *cb = data;
            libretro_host.keyboardCb = cb ? cb->callback : NULL;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (!data) {
            return false;
        }
        *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (!data) {
            return false;
        }
        *(bool *)data = (libretro_host.coreOptionsDirty != 0);
        libretro_host.coreOptionsDirty = 0;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        if (!data) {
            return false;
        }
        libretro_host_setOptionsV2((const struct retro_core_options_v2 *)data);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        if (!data) {
            return false;
        }
        {
            const struct retro_core_options_v2_intl *intl = (const struct retro_core_options_v2_intl *)data;
            const struct retro_core_options_v2 *opts = (intl && intl->local) ? intl->local : (intl ? intl->us : NULL);
            libretro_host_setOptionsV2(opts);
        }
        return true;
    default:
        return false;
    }
}

static bool
libretro_host_loadRomData(const char *path, void **data, size_t *size)
{
    if (!path || !*path || !data || !size) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "libretro: failed to open rom %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "libretro: failed to seek rom %s: %s\n", path, strerror(errno));
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0) {
        fprintf(stderr, "libretro: failed to tell rom %s: %s\n", path, strerror(errno));
        fclose(file);
        return false;
    }
    if (length == 0) {
        fprintf(stderr, "libretro: rom %s is empty\n", path);
        fclose(file);
        return false;
    }
    if ((size_t)length > SIZE_MAX) {
        fprintf(stderr, "libretro: rom %s is too large\n", path);
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "libretro: failed to rewind rom %s: %s\n", path, strerror(errno));
        fclose(file);
        return false;
    }
    size_t romSize = (size_t)length;
    void *buffer = malloc(romSize);
    if (!buffer) {
        fprintf(stderr, "libretro: out of memory loading rom %s\n", path);
        fclose(file);
        return false;
    }
    size_t read = fread(buffer, 1, romSize, file);
    fclose(file);
    if (read != romSize) {
        fprintf(stderr, "libretro: failed to read rom %s (read %zu of %zu)\n", path, read, romSize);
        free(buffer);
        return false;
    }
    *data = buffer;
    *size = romSize;
    return true;
}

static void
libretro_host_clearState(void)
{
    libretro_host_clearFrame();
    if (libretro_host.stateData) {
        alloc_free(libretro_host.stateData);
        libretro_host.stateData = NULL;
        libretro_host.stateSize = 0;
    }
    libretro_host_clearMemoryMap();
    libretro_host_clearOptions();
    libretro_host_clearOptionOverrides();
    libretro_host_clearOptionsV2();
    memset(&libretro_host, 0, sizeof(libretro_host));
    libretro_host.audioVolumePercent = 100;
}

bool
libretro_host_init(SDL_Renderer *renderer)
{
    (void)renderer;
    libretro_host_clearState();
    libretro_host.pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
    return true;
}

bool
libretro_host_start(const char *corePath, const char *romPath,
                    const char *systemDir, const char *saveDir)
{
    if (!corePath || !*corePath || !romPath || !*romPath || !systemDir || !*systemDir) {
        fprintf(stderr, "libretro: core, rom, or system directory missing\n");
        return false;
    }
    strncpy(libretro_host.corePath, corePath, PATH_MAX - 1);
    libretro_host.corePath[PATH_MAX - 1] = '\0';
    strncpy(libretro_host.romPath, romPath, PATH_MAX - 1);
    libretro_host.romPath[PATH_MAX - 1] = '\0';
    strncpy(libretro_host.systemDir, systemDir, PATH_MAX - 1);
    libretro_host.systemDir[PATH_MAX - 1] = '\0';
    if (saveDir && *saveDir) {
        strncpy(libretro_host.saveDir, saveDir, PATH_MAX - 1);
        libretro_host.saveDir[PATH_MAX - 1] = '\0';
    } else {
        strncpy(libretro_host.saveDir, libretro_host.systemDir, PATH_MAX - 1);
        libretro_host.saveDir[PATH_MAX - 1] = '\0';
    }
    if (!libretro_host_mkdir_p(libretro_host.systemDir) || !libretro_host_mkdir_p(libretro_host.saveDir)) {
        fprintf(stderr, "libretro: failed to create directories\n");
        return false;
    }
    libretro_host.lib = debugger_platform_loadSharedLibrary(libretro_host.corePath);
    if (!libretro_host.lib) {
        fprintf(stderr, "libretro: failed to load %s\n", libretro_host.corePath);
        return false;
    }
    libretro_host.setEnvironment = (retro_set_environment_fn_t)libretro_host_loadSymbol("retro_set_environment");
    libretro_host.setVideoRefresh = (retro_set_video_refresh_fn_t)libretro_host_loadSymbol("retro_set_video_refresh");
    libretro_host.setAudioSample = (retro_set_audio_sample_fn_t)libretro_host_loadSymbol("retro_set_audio_sample");
    libretro_host.setAudioSampleBatch = (retro_set_audio_sample_batch_fn_t)libretro_host_loadSymbol("retro_set_audio_sample_batch");
    libretro_host.setInputPoll = (retro_set_input_poll_fn_t)libretro_host_loadSymbol("retro_set_input_poll");
    libretro_host.setInputState = (retro_set_input_state_fn_t)libretro_host_loadSymbol("retro_set_input_state");
    libretro_host.init = (retro_init_fn_t)libretro_host_loadSymbol("retro_init");
    libretro_host.getSystemInfo = (retro_get_system_info_fn_t)libretro_host_loadSymbol("retro_get_system_info");
    libretro_host.loadGame = (retro_load_game_fn_t)libretro_host_loadSymbol("retro_load_game");
    libretro_host.getSystemAvInfo = (retro_get_system_av_info_fn_t)libretro_host_loadSymbol("retro_get_system_av_info");
    libretro_host.run = (retro_run_fn_t)libretro_host_loadSymbol("retro_run");
    libretro_host.reset = (retro_reset_fn_t)libretro_host_loadSymbol("retro_reset");
    libretro_host.unloadGame = (retro_unload_game_fn_t)libretro_host_loadSymbol("retro_unload_game");
    libretro_host.deinit = (retro_deinit_fn_t)libretro_host_loadSymbol("retro_deinit");
    libretro_host.getMemoryData = (retro_get_memory_data_fn_t)libretro_host_loadSymbol("retro_get_memory_data");
    libretro_host.getMemorySize = (retro_get_memory_size_fn_t)libretro_host_loadSymbol("retro_get_memory_size");
    libretro_host.serializeSize = (retro_serialize_size_fn_t)libretro_host_loadSymbol("retro_serialize_size");
    libretro_host.serialize = (retro_serialize_fn_t)libretro_host_loadSymbol("retro_serialize");
    libretro_host.unserialize = (retro_unserialize_fn_t)libretro_host_loadSymbol("retro_unserialize");
    libretro_host.setControllerPortDevice = (retro_set_controller_port_device_fn_t)libretro_host_loadSymbol("retro_set_controller_port_device");
    libretro_host.debugReadRegs = (e9k_debug_read_regs_fn_t)libretro_host_loadSymbol("e9k_debug_read_regs");
    libretro_host.debugReadProcessors = (e9k_debug_read_processors_fn_t)libretro_host_loadSymbol("e9k_debug_read_processors");
    libretro_host.debugReadProcessorRegs = (e9k_debug_read_processor_regs_fn_t)libretro_host_loadSymbol("e9k_debug_read_processor_regs");
    libretro_host.debugReadProcessorMemory = (e9k_debug_read_processor_memory_fn_t)libretro_host_loadSymbol("e9k_debug_read_processor_memory");
    libretro_host.debugWriteProcessorMemory = (e9k_debug_write_processor_memory_fn_t)libretro_host_loadSymbol("e9k_debug_write_processor_memory");
    libretro_host.debugDisassembleProcessorQuick = (e9k_debug_disassemble_processor_quick_fn_t)libretro_host_loadSymbol("e9k_debug_disassemble_processor_quick");
    libretro_host.debugSuppressProcessorBreakpointAtPc = (e9k_debug_suppress_processor_breakpoint_at_pc_fn_t)libretro_host_loadSymbol("e9k_debug_suppress_processor_breakpoint_at_pc");
    libretro_host.debugStepProcessorInstr = (e9k_debug_step_processor_instr_fn_t)libretro_host_loadSymbol("e9k_debug_step_processor_instr");
    libretro_host.debugPause = (e9k_debug_pause_fn_t)libretro_host_loadSymbol("e9k_debug_pause");
    libretro_host.debugResume = (e9k_debug_resume_fn_t)libretro_host_loadSymbol("e9k_debug_resume");
    libretro_host.debugIsPaused = (e9k_debug_is_paused_fn_t)libretro_host_loadSymbol("e9k_debug_is_paused");
    libretro_host.debugStepInstr = (e9k_debug_step_instr_fn_t)libretro_host_loadSymbol("e9k_debug_step_instr");
    libretro_host.debugStepLine = (e9k_debug_step_line_fn_t)libretro_host_loadSymbol("e9k_debug_step_line");
    libretro_host.debugStepNext = (e9k_debug_step_next_fn_t)libretro_host_loadSymbol("e9k_debug_step_next");
    libretro_host.debugStepOut = (e9k_debug_step_out_fn_t)libretro_host_loadSymbol("e9k_debug_step_out");
    libretro_host.debugAddBreakpoint = (e9k_debug_add_breakpoint_fn_t)libretro_host_loadSymbol("e9k_debug_add_breakpoint");
    libretro_host.debugRemoveBreakpoint = (e9k_debug_remove_breakpoint_fn_t)libretro_host_loadSymbol("e9k_debug_remove_breakpoint");
    libretro_host.debugAddProcessorBreakpoint = (e9k_debug_add_processor_breakpoint_fn_t)libretro_host_loadSymbol("e9k_debug_add_processor_breakpoint");
    libretro_host.debugRemoveProcessorBreakpoint = (e9k_debug_remove_processor_breakpoint_fn_t)libretro_host_loadSymbol("e9k_debug_remove_processor_breakpoint");
    libretro_host.debugAddTempBreakpoint = (e9k_debug_add_temp_breakpoint_fn_t)libretro_host_loadSymbol("e9k_debug_add_temp_breakpoint");
    libretro_host.debugRemoveTempBreakpoint = (e9k_debug_remove_temp_breakpoint_fn_t)libretro_host_loadSymbol("e9k_debug_remove_temp_breakpoint");
    libretro_host.debugResetWatchpoints = (e9k_debug_reset_watchpoints_fn_t)libretro_host_loadSymbol("e9k_debug_reset_watchpoints");
    libretro_host.debugAddWatchpoint = (e9k_debug_add_watchpoint_fn_t)libretro_host_loadSymbol("e9k_debug_add_watchpoint");
    libretro_host.debugRemoveWatchpoint = (e9k_debug_remove_watchpoint_fn_t)libretro_host_loadSymbol("e9k_debug_remove_watchpoint");
    libretro_host.debugReadWatchpoints = (e9k_debug_read_watchpoints_fn_t)libretro_host_loadSymbol("e9k_debug_read_watchpoints");
    libretro_host.debugGetWatchpointEnabledMask = (e9k_debug_get_watchpoint_enabled_mask_fn_t)libretro_host_loadSymbol("e9k_debug_get_watchpoint_enabled_mask");
    libretro_host.debugSetWatchpointEnabledMask = (e9k_debug_set_watchpoint_enabled_mask_fn_t)libretro_host_loadSymbol("e9k_debug_set_watchpoint_enabled_mask");
    libretro_host.debugConsumeWatchbreak = (e9k_debug_consume_watchbreak_fn_t)libretro_host_loadSymbol("e9k_debug_consume_watchbreak");
    libretro_host.debugResetProtects = (e9k_debug_reset_protects_fn_t)libretro_host_loadSymbol("e9k_debug_reset_protects");
    libretro_host.debugAddProtect = (e9k_debug_add_protect_fn_t)libretro_host_loadSymbol("e9k_debug_add_protect");
    libretro_host.debugRemoveProtect = (e9k_debug_remove_protect_fn_t)libretro_host_loadSymbol("e9k_debug_remove_protect");
    libretro_host.debugReadProtects = (e9k_debug_read_protects_fn_t)libretro_host_loadSymbol("e9k_debug_read_protects");
    libretro_host.debugGetProtectEnabledMask = (e9k_debug_get_protect_enabled_mask_fn_t)libretro_host_loadSymbol("e9k_debug_get_protect_enabled_mask");
    libretro_host.debugSetProtectEnabledMask = (e9k_debug_set_protect_enabled_mask_fn_t)libretro_host_loadSymbol("e9k_debug_set_protect_enabled_mask");
    libretro_host.debugReadCallstack = (e9k_debug_read_callstack_fn_t)libretro_host_loadSymbol("e9k_debug_read_callstack");
    libretro_host.debugReadMemory = (e9k_debug_read_memory_fn_t)libretro_host_loadSymbol("e9k_debug_read_memory");
    libretro_host.debugWriteMemory = (e9k_debug_write_memory_fn_t)libretro_host_loadSymbol("e9k_debug_write_memory");
    libretro_host.debugProfilerStart = (e9k_debug_profiler_start_fn_t)libretro_host_loadSymbol("e9k_debug_profiler_start");
    libretro_host.debugProfilerStop = (e9k_debug_profiler_stop_fn_t)libretro_host_loadSymbol("e9k_debug_profiler_stop");
    libretro_host.debugProfilerIsEnabled = (e9k_debug_profiler_is_enabled_fn_t)libretro_host_loadSymbol("e9k_debug_profiler_is_enabled");
    libretro_host.debugProfilerStreamNext = (e9k_debug_profiler_stream_next_fn_t)libretro_host_loadSymbol("e9k_debug_profiler_stream_next");
    libretro_host.debugTextRead = (e9k_debug_text_read_fn_t)libretro_host_loadSymbol("e9k_debug_text_read");
    libretro_host.debugSetSourceLocationResolver = (e9k_debug_set_source_location_resolver_fn_t)libretro_host_loadSymbol("e9k_debug_set_source_location_resolver");
    libretro_host.debugSetDebugOption = (e9k_debug_set_debug_option_fn_t)libretro_host_loadSymbol("e9k_debug_set_debug_option");
    libretro_host.debugNeogeoGetSpriteState = NULL;
    libretro_host.debugNeogeoGetP1Rom = NULL;
    libretro_host.debugNeogeoGetCRom = NULL;
    libretro_host.debugNeogeoGetFixRom = NULL;
    libretro_host.debugNeogeoGetRoms = NULL;
    libretro_host.debugNeogeoGetPaletteState = NULL;
    libretro_host.debugNeogeoGetAudioFrame = NULL;
    libretro_host.debugNeogeoSetSpriteGrayscaleSelection = NULL;
    libretro_host.debugNeogeoSetPaletteGrayscaleMask = NULL;
    libretro_host.debugNeogeoGetPaletteGrayscaleMask = NULL;
    libretro_host.debugNeogeoSetFixLayerMode = NULL;
    libretro_host.debugNeogeoSetAudioVisEnabled = NULL;
    libretro_host.debugNeogeoSetAudioMuteMask = NULL;
    libretro_host.debugNeogeoSetRegisterLogFrameCallback = NULL;
    libretro_host.debugMegadriveGetSpriteState = NULL;
    libretro_host.debugMegadriveGetRoms = NULL;
    libretro_host.debugMegadriveSetPaletteGreyscaleMask = NULL;
    libretro_host.debugMegadriveGetPaletteGreyscaleMask = NULL;
    libretro_host.debugMegadriveGetAudioFrame = NULL;
    libretro_host.debugMegadriveSetAudioVisEnabled = NULL;
    libretro_host.debugMegadriveSetAudioMuteMask = NULL;
    libretro_host.debugMegadriveGetVdpBandwidthFrame = NULL;
    libretro_host.debugMegadriveGetRasterLineCount = NULL;
    libretro_host.debugDisassembleQuick = (e9k_debug_disassemble_quick_fn_t)libretro_host_loadSymbol("e9k_debug_disassemble_quick");
    libretro_host.debugReadKnownPcs = (e9k_debug_read_known_pcs_fn_t)libretro_host_loadSymbol("e9k_debug_read_known_pcs");
    libretro_host.debugResetKnownPcs = (e9k_debug_reset_known_pcs_fn_t)libretro_host_loadSymbol("e9k_debug_reset_known_pcs");
    libretro_host.debugReadCheckpoints = (e9k_debug_read_checkpoints_fn_t)libretro_host_loadSymbol("e9k_debug_read_checkpoints");
    libretro_host.debugResetCheckpoints = (e9k_debug_reset_checkpoints_fn_t)libretro_host_loadSymbol("e9k_debug_reset_checkpoints");
    libretro_host.debugSetCheckpointEnabled = (e9k_debug_set_checkpoint_enabled_fn_t)libretro_host_loadSymbol("e9k_debug_set_checkpoint_enabled");
    libretro_host.debugGetCheckpointEnabled = (e9k_debug_get_checkpoint_enabled_fn_t)libretro_host_loadSymbol("e9k_debug_get_checkpoint_enabled");
    libretro_host.debugReadCounters = (e9k_debug_read_counters_fn_t)libretro_host_loadSymbol("e9k_debug_read_counters");
    libretro_host.debugResetCounters = (e9k_debug_reset_counters_fn_t)libretro_host_loadSymbol("e9k_debug_reset_counters");
    libretro_host.debugReadCycleCount = (e9k_debug_read_cycle_count_fn_t)libretro_host_loadSymbol("e9k_debug_read_cycle_count");
    libretro_host.setVblankCallback = (e9k_debug_set_vblank_callback_fn_t)libretro_host_loadSymbol("e9k_debug_set_vblank_callback");
    if (!libretro_host.setEnvironment || !libretro_host.setVideoRefresh ||
        !libretro_host.setInputPoll || !libretro_host.setInputState ||
        !libretro_host.init || !libretro_host.loadGame || !libretro_host.getSystemAvInfo ||
        !libretro_host.run || !libretro_host.deinit) {
        libretro_host_shutdown();
        return false;
    }
    int needFullpath = 0;
    if (libretro_host.getSystemInfo) {
        struct retro_system_info systemInfo;
        memset(&systemInfo, 0, sizeof(systemInfo));
        libretro_host.getSystemInfo(&systemInfo);
        needFullpath = systemInfo.need_fullpath ? 1 : 0;
    }
    if (!needFullpath) {
        if (!libretro_host_loadRomData(libretro_host.romPath, &libretro_host.romData, &libretro_host.romSize)) {
            libretro_host_shutdown();
            return false;
        }
    }
    libretro_host.setEnvironment(libretro_host_environment);
    libretro_host.setVideoRefresh(libretro_host_videoCallback);
    libretro_host.setAudioSample(libretro_host_audioSample);
    libretro_host.setAudioSampleBatch(libretro_host_audioSampleBatch);
    libretro_host.setInputPoll(libretro_host_inputPoll);
    libretro_host.setInputState(libretro_host_inputState);
    libretro_host_applyOptionOverrides();
    libretro_host.init();
    struct retro_game_info info = {
        .path = libretro_host.romPath,
        .data = libretro_host.romData,
        .size = libretro_host.romSize,
        .meta = NULL,
    };
    if (!libretro_host.loadGame(&info)) {
        fprintf(stderr, "libretro: failed to load rom %s\n", libretro_host.romPath);
        libretro_host_shutdown();
        return false;
    }
    libretro_host_configureControllerPorts();
    if (libretro_host.reset) {
        libretro_host.reset();
    }
    if (debugger.config.neogeo.skipBiosLogo) {
        libretro_host.autoPressDelayFrames = 85;
        libretro_host.autoPressHoldFrames = 5;
    } else {
        libretro_host.autoPressDelayFrames = 0;
        libretro_host.autoPressHoldFrames = 0;
        libretro_host.autoInputMask[0] = 0;
    }
    libretro_host.gameLoaded = true;
    libretro_host.getSystemAvInfo(&libretro_host.avInfo);
    smoke_test_setAudioFormat((int)libretro_host.avInfo.timing.sample_rate, 2);
    libretro_host.audioEnabled = debugger_getAudioEnabled();
    libretro_host_openAudio();
    if (libretro_host.avInfo.geometry.base_width && libretro_host.avInfo.geometry.base_height) {
        libretro_host_destroyTexture();
        libretro_host.textureWidth = libretro_host.avInfo.geometry.base_width;
        libretro_host.textureHeight = libretro_host.avInfo.geometry.base_height;
    }
    libretro_host.running = true;
    return true;
}

void
libretro_host_shutdown(void)
{
    if (libretro_host.gameLoaded && libretro_host.unloadGame) {
        libretro_host.unloadGame();
    }
    if (libretro_host.deinit) {
        libretro_host.deinit();
    }
    if (libretro_host.lib) {
        debugger_platform_closeSharedLibrary(libretro_host.lib);
        libretro_host.lib = NULL;
    }
    libretro_host_destroyTexture();
    libretro_host_clearFrame();
    libretro_host_closeAudio();
    if (libretro_host.romData) {
        free(libretro_host.romData);
        libretro_host.romData = NULL;
        libretro_host.romSize = 0;
    }
    if (libretro_host.stateData) {
        alloc_free(libretro_host.stateData);
        libretro_host.stateData = NULL;
        libretro_host.stateSize = 0;
    }
    libretro_host_clearMemoryMap();
    libretro_host_clearOptions();
    libretro_host.running = false;
    libretro_host.gameLoaded = false;
    memset(&libretro_host, 0, sizeof(libretro_host));
    libretro_host.audioVolumePercent = 100;
}

void
_libretro_host_runOnce(void)
{
  libretro_host_runFrame();
}

SDL_Texture *
libretro_host_getTexture(SDL_Renderer *renderer)
{
    if (!renderer) {
        return NULL;
    }
    SDL_Texture *tex = libretro_host.texture;
    uint64_t seq = 0;
    if (!libretro_host.frameData || libretro_host.frameWidth <= 0 || libretro_host.frameHeight <= 0) {
        return tex;
    }
    seq = libretro_host.frameSeq;
    if (!tex ||
        libretro_host.frameWidth != libretro_host.textureWidth ||
        libretro_host.frameHeight != libretro_host.textureHeight) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
            SDL_TEXTUREACCESS_STREAMING, libretro_host.frameWidth, libretro_host.frameHeight);
        if (!tex) {
            fprintf(stderr, "libretro: SDL_CreateTexture failed: %s\n", SDL_GetError());
            libretro_host.texture = NULL;
            libretro_host.textureWidth = 0;
            libretro_host.textureHeight = 0;
            libretro_host.textureSeq = 0;
            return NULL;
        }
        libretro_host.texture = tex;
        libretro_host.textureWidth = libretro_host.frameWidth;
        libretro_host.textureHeight = libretro_host.frameHeight;
        libretro_host.textureSeq = 0;
    }
    if (seq != libretro_host.textureSeq) {
        SDL_UpdateTexture(tex, NULL, libretro_host.frameData, (int)libretro_host.framePitch);
        libretro_host.textureSeq = seq;
    }
    return libretro_host.texture;
}

bool
libretro_host_getFrame(const uint8_t **out_data, int *out_width, int *out_height, size_t *out_pitch)
{
    if (!out_data || !out_width || !out_height || !out_pitch) {
        return false;
    }
    if (!libretro_host.frameData || libretro_host.frameWidth <= 0 || libretro_host.frameHeight <= 0) {
        return false;
    }
    *out_data = libretro_host.frameData;
    *out_width = libretro_host.frameWidth;
    *out_height = libretro_host.frameHeight;
    *out_pitch = libretro_host.framePitch;
    return true;
}

float
libretro_host_getDisplayAspect(void)
{
    float aspect = libretro_host.avInfo.geometry.aspect_ratio;
    if (aspect > 0.0001f) {
        return aspect;
    }
    unsigned w = libretro_host.avInfo.geometry.base_width;
    unsigned h = libretro_host.avInfo.geometry.base_height;
    if (w > 0 && h > 0) {
        return (float)w / (float)h;
    }
    if (libretro_host.frameWidth > 0 && libretro_host.frameHeight > 0) {
        return (float)libretro_host.frameWidth / (float)libretro_host.frameHeight;
    }
    return 0.0f;
}

double
libretro_host_getTimingFps(void)
{
    double fps = libretro_host.avInfo.timing.fps;
    if (fps > 1e-3) {
        return fps;
    }
    return 60.0;
}

void
libretro_host_setJoypadState(unsigned port, unsigned id, int pressed)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    if (port >= LIBRETRO_HOST_MAX_PORTS || id >= 32) {
        return;
    }
    uint32_t bit = 1u << id;
    if (pressed) {
        libretro_host.inputMask[port] |= bit;
    } else {
        libretro_host.inputMask[port] &= ~bit;
    }
    input_record_recordJoypad(debugger.frameCounter + 1, port, id, pressed);
}

void
libretro_host_clearJoypadState(void)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    memset(libretro_host.inputMask, 0, sizeof(libretro_host.inputMask));
    input_record_recordClear(debugger.frameCounter + 1);
}

void
libretro_host_addMouseMotion(unsigned port, int dx, int dy)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    if (dx == 0 && dy == 0) {
        return;
    }
    input_record_recordCoreMouseMotion(debugger.frameCounter + 1, (int)port, dx, dy);
    if (port == LIBRETRO_HOST_MAX_PORTS) {
        for (unsigned idx = 0; idx < LIBRETRO_HOST_MAX_PORTS; ++idx) {
            libretro_host.mousePendingX[idx] += dx;
            libretro_host.mousePendingY[idx] += dy;
        }
        return;
    }
    if (port >= LIBRETRO_HOST_MAX_PORTS) {
        return;
    }
    libretro_host.mousePendingX[port] += dx;
    libretro_host.mousePendingY[port] += dy;
}

void
libretro_host_setMouseButton(unsigned port, unsigned id, int pressed)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    if (id > RETRO_DEVICE_ID_MOUSE_BUTTON_5) {
        return;
    }
    input_record_recordCoreMouseButton(debugger.frameCounter + 1, (int)port, id, pressed);
    uint32_t bit = 1u << id;
    if (port == LIBRETRO_HOST_MAX_PORTS) {
        for (unsigned idx = 0; idx < LIBRETRO_HOST_MAX_PORTS; ++idx) {
            if (pressed) {
                libretro_host.mouseButtonMask[idx] |= bit;
            } else {
                libretro_host.mouseButtonMask[idx] &= ~bit;
            }
        }
        return;
    }
    if (port >= LIBRETRO_HOST_MAX_PORTS) {
        return;
    }
    if (pressed) {
        libretro_host.mouseButtonMask[port] |= bit;
    } else {
        libretro_host.mouseButtonMask[port] &= ~bit;
    }
}

unsigned
libretro_host_getMousePort(void)
{
    for (unsigned port = 0; port < LIBRETRO_HOST_MAX_PORTS; ++port) {
        if (libretro_host.controllerPortDevice[port] == RETRO_DEVICE_MOUSE) {
            return port;
        }
    }
    return LIBRETRO_HOST_MAX_PORTS;
}

void
libretro_host_sendKeyEvent(unsigned keycode, uint32_t character,
                           uint16_t modifiers, int pressed)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    if (keycode < RETROK_LAST) {
        libretro_host.keyboardState[keycode] = pressed ? 1 : 0;
    }
    if (libretro_host.keyboardCb) {
        libretro_host.keyboardCb(pressed ? true : false, keycode, character, modifiers);
    }
    input_record_recordKey(debugger.frameCounter + 1, keycode, character, modifiers, pressed);
}

bool
libretro_host_isRunning(void)
{
    return libretro_host.running;
}

const void *
libretro_host_getMemory(unsigned id, size_t *size)
{
    if (size) {
        *size = 0;
    }
    if (!libretro_host.gameLoaded || !libretro_host.getMemoryData) {
        return NULL;
    }
    if (size && libretro_host.getMemorySize) {
        *size = libretro_host.getMemorySize(id);
    }
    return libretro_host.getMemoryData(id);
}

bool
libretro_host_readRegs(uint32_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadRegs) {
        return false;
    }
    size_t n = libretro_host.debugReadRegs(out, cap);
    if (out_count) {
        *out_count = n;
    }
    return n > 0;
}

bool
libretro_host_debugReadProcessors(e9k_debug_processor_info_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadProcessors) {
        return false;
    }
    size_t n = libretro_host.debugReadProcessors(out, cap);
    if (out_count) {
        *out_count = n;
    }
    return n > 0;
}

bool
libretro_host_debugReadProcessorRegs(uint32_t processorId, e9k_debug_processor_reg_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadProcessorRegs) {
        return false;
    }
    size_t n = libretro_host.debugReadProcessorRegs(processorId, out, cap);
    if (out_count) {
        *out_count = n;
    }
    return n > 0;
}

bool
libretro_host_debugReadProcessorMemory(uint32_t processorId, uint32_t addr, void *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugReadProcessorMemory) {
        return false;
    }
    return libretro_host.debugReadProcessorMemory(processorId, addr, (uint8_t *)out, cap) == cap;
}

bool
libretro_host_debugWriteProcessorMemory(uint32_t processorId, uint32_t addr, uint32_t value, size_t size)
{
    if (!libretro_host.debugWriteProcessorMemory) {
        return false;
    }
    return libretro_host.debugWriteProcessorMemory(processorId, addr, value, size) ? true : false;
}

bool
libretro_host_debugDisassembleProcessorQuick(uint32_t processorId, uint32_t pc, char *out, size_t cap, size_t *out_len)
{
    if (out_len) {
        *out_len = 0;
    }
    if (out && cap > 0) {
        out[0] = '\0';
    }
    if (!out || cap == 0 || !libretro_host.debugDisassembleProcessorQuick) {
        return false;
    }
    size_t n = libretro_host.debugDisassembleProcessorQuick(processorId, pc, out, cap);
    if (out_len) {
        *out_len = n;
    }
    return n > 0;
}

bool
libretro_host_debugSuppressProcessorBreakpointAtPc(uint32_t processorId)
{
    if (!libretro_host.debugSuppressProcessorBreakpointAtPc) {
        return false;
    }
    return libretro_host.debugSuppressProcessorBreakpointAtPc(processorId) ? true : false;
}

bool
libretro_host_debugStepProcessorInstr(uint32_t processorId)
{
    if (!libretro_host.debugStepProcessorInstr) {
        return false;
    }
    return libretro_host.debugStepProcessorInstr(processorId) ? true : false;
}

bool
libretro_host_debugPause(void)
{
    if (!libretro_host.debugPause) {
        return false;
    }
    libretro_host.debugPause();
    return true;
}

bool
libretro_host_debugResume(void)
{
    if (!libretro_host.debugResume) {
        return false;
    }
    debugger.suppressVblankFrameCounter = 0;
    libretro_host.debugResume();
    return true;
}

bool
libretro_host_debugIsPaused(int *out_paused)
{
    if (out_paused) {
        *out_paused = 0;
    }
    if (!libretro_host.debugIsPaused) {
        return false;
    }
    if (out_paused) {
        *out_paused = libretro_host.debugIsPaused() ? 1 : 0;
    }
    return true;
}

bool
libretro_host_debugStepInstr(void)
{
    if (!libretro_host.debugStepInstr) {
        return false;
    }
    debugger.suppressVblankFrameCounter = 1;
    libretro_host.debugStepInstr();
    return true;
}

bool
libretro_host_debugStepLine(void)
{
    if (!libretro_host.debugStepLine) {
        return false;
    }
    libretro_host.debugStepLine();
    return true;
}

bool
libretro_host_debugStepNext(void)
{
    if (!libretro_host.debugStepNext) {
        return false;
    }
    libretro_host.debugStepNext();
    return true;
}

bool
libretro_host_debugStepOut(void)
{
    if (!libretro_host.debugStepOut) {
        return false;
    }
    libretro_host.debugStepOut();
    return true;
}

bool
libretro_host_debugAddBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugAddBreakpoint) {
        return false;
    }
    libretro_host.debugAddBreakpoint(addr);
    return true;
}

bool
libretro_host_debugRemoveBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugRemoveBreakpoint) {
        return false;
    }
    libretro_host.debugRemoveBreakpoint(addr);
    return true;
}

bool
libretro_host_debugAddProcessorBreakpoint(uint32_t processorId, uint32_t addr)
{
    if (processorId == MACHINE_PROCESSOR_PRIMARY) {
        return libretro_host_debugAddBreakpoint(addr);
    }
    if (!libretro_host.debugAddProcessorBreakpoint) {
        return false;
    }
    libretro_host.debugAddProcessorBreakpoint(processorId, addr);
    return true;
}

bool
libretro_host_debugRemoveProcessorBreakpoint(uint32_t processorId, uint32_t addr)
{
    if (processorId == MACHINE_PROCESSOR_PRIMARY) {
        return libretro_host_debugRemoveBreakpoint(addr);
    }
    if (!libretro_host.debugRemoveProcessorBreakpoint) {
        return false;
    }
    libretro_host.debugRemoveProcessorBreakpoint(processorId, addr);
    return true;
}

bool
libretro_host_debugAddTempBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugAddTempBreakpoint) {
        return false;
    }
    libretro_host.debugAddTempBreakpoint(addr);
    return true;
}

bool
libretro_host_debugRemoveTempBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugRemoveTempBreakpoint) {
        return false;
    }
    libretro_host.debugRemoveTempBreakpoint(addr);
    return true;
}

bool
libretro_host_debugResetWatchpoints(void)
{
    if (!libretro_host.debugResetWatchpoints) {
        return false;
    }
    libretro_host.debugResetWatchpoints();
    return true;
}

bool
libretro_host_debugAddWatchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand,
                                 uint32_t size_operand, uint32_t addr_mask_operand, uint32_t access_source_operand, uint32_t *out_index)
{
    if (out_index) {
        *out_index = 0;
    }
    if (!libretro_host.debugAddWatchpoint) {
        return false;
    }
    int r = libretro_host.debugAddWatchpoint(addr, op_mask, diff_operand, value_operand, old_value_operand, size_operand, addr_mask_operand, access_source_operand);
    if (r < 0) {
        return false;
    }
    if (out_index) {
        *out_index = (uint32_t)r;
    }
    return true;
}

bool
libretro_host_debugRemoveWatchpoint(uint32_t index)
{
    if (!libretro_host.debugRemoveWatchpoint) {
        return false;
    }
    libretro_host.debugRemoveWatchpoint(index);
    return true;
}

bool
libretro_host_debugReadWatchpoints(e9k_debug_watchpoint_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadWatchpoints) {
        return false;
    }
    size_t count = libretro_host.debugReadWatchpoints(out, cap);
    if (out_count) {
        *out_count = count;
    }
    return true;
}

bool
libretro_host_debugGetWatchpointEnabledMask(uint64_t *out_mask)
{
    if (out_mask) {
        *out_mask = 0;
    }
    if (!libretro_host.debugGetWatchpointEnabledMask) {
        return false;
    }
    uint64_t mask = libretro_host.debugGetWatchpointEnabledMask();
    if (out_mask) {
        *out_mask = mask;
    }
    return true;
}

bool
libretro_host_debugSetWatchpointEnabledMask(uint64_t mask)
{
    if (!libretro_host.debugSetWatchpointEnabledMask) {
        return false;
    }
    libretro_host.debugSetWatchpointEnabledMask(mask);
    return true;
}

bool
libretro_host_debugConsumeWatchbreak(e9k_debug_watchbreak_t *out)
{
    if (!out || !libretro_host.debugConsumeWatchbreak) {
        return false;
    }
    return libretro_host.debugConsumeWatchbreak(out) ? true : false;
}

bool
libretro_host_debugResetProtects(void)
{
    if (!libretro_host.debugResetProtects) {
        return false;
    }
    libretro_host.debugResetProtects();
    return true;
}

bool
libretro_host_debugAddProtect(uint32_t addr, uint32_t size_bits, uint32_t mode, uint32_t value, uint32_t *out_index)
{
    if (out_index) {
        *out_index = 0;
    }
    if (!libretro_host.debugAddProtect) {
        return false;
    }
    int r = libretro_host.debugAddProtect(addr, size_bits, mode, value);
    if (r < 0) {
        return false;
    }
    if (out_index) {
        *out_index = (uint32_t)r;
    }
    return true;
}

bool
libretro_host_debugRemoveProtect(uint32_t index)
{
    if (!libretro_host.debugRemoveProtect) {
        return false;
    }
    libretro_host.debugRemoveProtect(index);
    return true;
}

bool
libretro_host_debugReadProtects(e9k_debug_protect_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadProtects) {
        return false;
    }
    size_t count = libretro_host.debugReadProtects(out, cap);
    if (out_count) {
        *out_count = count;
    }
    return true;
}

bool
libretro_host_debugGetProtectEnabledMask(uint64_t *out_mask)
{
    if (out_mask) {
        *out_mask = 0;
    }
    if (!libretro_host.debugGetProtectEnabledMask) {
        return false;
    }
    uint64_t mask = libretro_host.debugGetProtectEnabledMask();
    if (out_mask) {
        *out_mask = mask;
    }
    return true;
}

bool
libretro_host_debugSetProtectEnabledMask(uint64_t mask)
{
    if (!libretro_host.debugSetProtectEnabledMask) {
        return false;
    }
    libretro_host.debugSetProtectEnabledMask(mask);
    return true;
}

bool
libretro_host_debugReadCallstack(uint32_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadCallstack) {
        return false;
    }
    size_t count = libretro_host.debugReadCallstack(out, cap);
    if (out_count) {
        *out_count = count;
    }
    return true;
}

bool
libretro_host_debugReadMemory(uint32_t addr, void *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugReadMemory) {
        return false;
    }
    size_t read = libretro_host.debugReadMemory(addr, (uint8_t *)out, cap);
    return read == cap;
}

size_t
libretro_host_getMemoryMapDescriptors(const struct retro_memory_descriptor **outDescriptors)
{
    if (outDescriptors) {
        *outDescriptors = libretro_host.memoryDescriptors;
    }
    return libretro_host.memoryDescriptorCount;
}

bool
libretro_host_debugWriteMemory(uint32_t addr, uint32_t value, size_t size)
{
    if (size == 0 || size > sizeof(uint32_t) || !libretro_host.debugWriteMemory) {
        return false;
    }
    return libretro_host.debugWriteMemory(addr, value, size) ? true : false;
}

bool
libretro_host_profilerStart(int stream)
{
    if (!libretro_host.debugProfilerStart) {
        return false;
    }
    libretro_host.debugProfilerStart(stream ? 1 : 0);
    return true;
}

bool
libretro_host_profilerStop(void)
{
    if (!libretro_host.debugProfilerStop) {
        return false;
    }
    libretro_host.debugProfilerStop();
    return true;
}

bool
libretro_host_profilerIsEnabled(int *out_enabled)
{
    if (out_enabled) {
        *out_enabled = 0;
    }
    if (!libretro_host.debugProfilerIsEnabled) {
        return false;
    }
    if (out_enabled) {
        *out_enabled = libretro_host.debugProfilerIsEnabled() ? 1 : 0;
    }
    return true;
}

bool
libretro_host_profilerStreamNext(char *out, size_t cap, size_t *out_len)
{
    if (out_len) {
        *out_len = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugProfilerStreamNext) {
        return false;
    }
    size_t len = libretro_host.debugProfilerStreamNext(out, cap);
    if (out_len) {
        *out_len = len;
    }
    return len > 0;
}

size_t
libretro_host_debugTextRead(char *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugTextRead) {
        return 0;
    }
    return libretro_host.debugTextRead(out, cap);
}

size_t
libretro_host_debugReadCheckpoints(e9k_debug_checkpoint_t *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugReadCheckpoints) {
        return 0;
    }
    return libretro_host.debugReadCheckpoints(out, cap);
}

bool
libretro_host_debugResetCheckpoints(void)
{
    if (!libretro_host.debugResetCheckpoints) {
        return false;
    }
    libretro_host.debugResetCheckpoints();
    return true;
}

size_t
libretro_host_debugReadCounters(e9k_debug_counter_t *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugReadCounters) {
        return 0;
    }
    return libretro_host.debugReadCounters(out, cap);
}

bool
libretro_host_debugResetCounters(void)
{
    if (!libretro_host.debugResetCounters) {
        return false;
    }
    libretro_host.debugResetCounters();
    return true;
}

uint64_t
libretro_host_debugReadCycleCount(void)
{
    if (!libretro_host.debugReadCycleCount) {
        return 0;
    }
    return libretro_host.debugReadCycleCount();
}

bool
libretro_host_debugSetCheckpointEnabled(int enabled)
{
    if (!libretro_host.debugSetCheckpointEnabled) {
        return false;
    }
    libretro_host.debugSetCheckpointEnabled(enabled);
    return true;
}

bool
libretro_host_debugGetCheckpointEnabled(int *out_enabled)
{
    if (out_enabled) {
        *out_enabled = 0;
    }
    if (!libretro_host.debugGetCheckpointEnabled) {
        return false;
    }
    if (out_enabled) {
        *out_enabled = libretro_host.debugGetCheckpointEnabled() ? 1 : 0;
    }
    return true;
}

bool
libretro_host_debugDisassembleQuick(uint32_t pc, char *out, size_t cap, size_t *out_len)
{
    if (out_len) {
        *out_len = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugDisassembleQuick) {
        return false;
    }
    size_t len = libretro_host.debugDisassembleQuick(pc, out, cap);
    if (out_len) {
        *out_len = len;
    }
    return len > 0;
}

size_t
libretro_host_debugReadKnownPcs(uint32_t start_addr, uint32_t end_addr, uint32_t *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugReadKnownPcs) {
        return 0;
    }
    return libretro_host.debugReadKnownPcs(start_addr, end_addr, out, cap);
}

bool
libretro_host_getSerializeSize(size_t *out_size)
{
    if (out_size) {
        *out_size = 0;
    }
    if (!libretro_host.serializeSize) {
        return false;
    }
    size_t size = libretro_host.serializeSize();
    if (out_size) {
        *out_size = size;
    }
    return size > 0;
}

bool
libretro_host_serializeTo(void *out, size_t size)
{
    if (!out || size == 0 || !libretro_host.serialize) {
        return false;
    }
    return libretro_host.serialize(out, size) ? true : false;
}

bool
libretro_host_unserializeFrom(const void *data, size_t size)
{
    if (!data || size == 0 || !libretro_host.unserialize) {
        return false;
    }
    return libretro_host.unserialize(data, size) ? true : false;
}

bool
libretro_host_setStateData(const void *data, size_t size)
{
    if (!data || size == 0) {
        return false;
    }
    state_wrap_info_t info;
    if (!state_wrap_parse((const uint8_t *)data, size, &info)) {
        return false;
    }
    uint8_t *buf = (uint8_t*)alloc_realloc(libretro_host.stateData, size);
    if (!buf) {
        return false;
    }
    memcpy(buf, data, size);
    libretro_host.stateData = buf;
    libretro_host.stateSize = size;
    return true;
}

bool
libretro_host_getStateData(const uint8_t **out_data, size_t *out_size)
{
    if (out_data) {
        *out_data = NULL;
    }
    if (out_size) {
        *out_size = 0;
    }
    if (!libretro_host.stateData || libretro_host.stateSize == 0) {
        return false;
    }
    if (out_data) {
        *out_data = (const uint8_t *)libretro_host.stateData;
    }
    if (out_size) {
        *out_size = libretro_host.stateSize;
    }
    return true;
}

bool
libretro_host_resetCore(void)
{
    if (!libretro_host.reset) {
        return false;
    }
    libretro_host.reset();
    if (libretro_host.debugResetKnownPcs) {
        libretro_host.debugResetKnownPcs();
    }
    int reinstallBreakpoints = debugger_onCoreReset();
    const machine_breakpoint_t *bps = NULL;
    int bpCount = 0;
    if (reinstallBreakpoints &&
        machine_getBreakpoints(&debugger.machine, &bps, &bpCount) &&
        bps && bpCount > 0) {
        for (int i = 0; i < bpCount; ++i) {
            if (!bps[i].enabled) {
                continue;
            }
            uint32_t addr = bps[i].processorId == MACHINE_PROCESSOR_PRIMARY
                ? (uint32_t)(bps[i].addr & 0x00ffffffu)
                : (uint32_t)(bps[i].addr & 0x0000ffffu);
            libretro_host_debugAddProcessorBreakpoint(bps[i].processorId, addr);
        }
    }
    if (debugger.config.neogeo.skipBiosLogo) {
        libretro_host.autoPressDelayFrames = 80;
        libretro_host.autoPressHoldFrames = 3;
    } else {
        libretro_host.autoPressDelayFrames = 0;
        libretro_host.autoPressHoldFrames = 0;
        libretro_host.autoInputMask[0] = 0;
    }
    return true;
}

uint64_t
libretro_host_getFrameCount(void)
{
    return libretro_host.frameSeq;
}

const char *
libretro_host_getRomPath(void)
{
    return libretro_host.romPath[0] ? libretro_host.romPath : NULL;
}

const char *
libretro_host_getCorePath(void)
{
    return libretro_host.corePath[0] ? libretro_host.corePath : NULL;
}

const char *
libretro_host_getSaveDir(void)
{
    return libretro_host.saveDir[0] ? libretro_host.saveDir : NULL;
}

const char *
libretro_host_getSystemDir(void)
{
    return libretro_host.systemDir[0] ? libretro_host.systemDir : NULL;
}

bool
libretro_host_setVblankCallback(void (*cb)(void *), void *user)
{
    if (!libretro_host.setVblankCallback) {
        return false;
    }
    libretro_host.setVblankCallback(cb, user);
    return true;
}

bool
libretro_host_setDebugSourceLocationCallback(int (*cb)(uint32_t pc, uint64_t *out_location, void *user), void *user)
{
    if (!libretro_host.debugSetSourceLocationResolver) {
        return false;
    }
    libretro_host.debugSetSourceLocationResolver(cb, user);
    return true;
}

bool
libretro_host_debugSetDebugOption(e9k_debug_option_t option, uint32_t argument, void *user)
{
    if (!libretro_host.debugSetDebugOption) {
        return false;
    }
    libretro_host.debugSetDebugOption(option, argument, user);
    return true;
}

bool
libretro_host_setAudioEnabled(int enabled)
{
    libretro_host.audioEnabled = enabled ? 1 : 0;
    if (libretro_host.gameLoaded) {
        if (libretro_host.audioEnabled) {
            libretro_host_openAudio();
        } else {
            libretro_host_closeAudio();
        }
    }
    return true;
}

void
libretro_host_setAudioVolume(int volumePercent)
{
    if (volumePercent < 0) {
        volumePercent = 0;
    }
    if (volumePercent > 100) {
        volumePercent = 100;
    }
    libretro_host.audioVolumePercent = volumePercent;
}

bool
libretro_host_saveState(size_t *out_size, size_t *out_diff)
{
    size_t headerSize = 0;
    size_t payloadSize = 0;
    size_t size = 0;
    uint8_t *prev = NULL;
    size_t prevSize = 0;

    if (out_size) {
        *out_size = 0;
    }
    if (out_diff) {
        *out_diff = 0;
    }
    if (!libretro_host.serializeSize || !libretro_host.serialize) {
        return false;
    }
    size_t rawSize = libretro_host.serializeSize();
    if (rawSize == 0) {
        return false;
    }

    headerSize = state_wrap_headerSize();
    if (libretro_host.stateData && libretro_host.stateSize > 0) {
        prevSize = libretro_host.stateSize;
        prev = (uint8_t*)alloc_alloc(prevSize);
        if (prev) {
            memcpy(prev, libretro_host.stateData, prevSize);
        }
    }

    if (!libretro_host_serializeWithRetry(&libretro_host.stateData,
                                          &libretro_host.stateSize,
                                          headerSize,
                                          rawSize,
                                          &payloadSize)) {
        alloc_free(prev);
        return false;
    }
    if (!state_wrap_writeHeader(libretro_host.stateData, libretro_host.stateSize, payloadSize, &debugger.machine)) {
        alloc_free(prev);
        return false;
    }
    size = libretro_host.stateSize;
    if (out_size) {
        *out_size = size;
    }
    if (out_diff && prev) {
        size_t cmpSize = size;
        size_t diff = 0;
        if (prevSize != cmpSize) {
            if (prevSize > cmpSize) {
                diff += prevSize - cmpSize;
            } else {
                diff += cmpSize - prevSize;
            }
            if (prevSize < cmpSize) {
                cmpSize = prevSize;
            }
        }
        for (size_t i = 0; i < cmpSize; ++i) {
            if (libretro_host.stateData[i] != prev[i]) {
                diff++;
            }
        }
        *out_diff = diff;
    }
    alloc_free(prev);
    return true;
}

bool
libretro_host_restoreState(size_t *out_size)
{
    if (out_size) {
        *out_size = 0;
    }
    if (!libretro_host.unserialize || !libretro_host.stateData || libretro_host.stateSize == 0) {
        return false;
    }
    state_wrap_info_t info;
    if (!state_wrap_parse((const uint8_t *)libretro_host.stateData, libretro_host.stateSize, &info)) {
        return false;
    }
    debugger_applyStateWrapBases(&info);
    if (!libretro_host.unserialize(info.payload, info.payloadSize)) {
        return false;
    }
    if (out_size) {
        *out_size = libretro_host.stateSize;
    }
    return true;
}
