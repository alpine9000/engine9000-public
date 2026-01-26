/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "crt.h"
#include "debugger.h"
#include "e9ui.h"
#include "sprite_debug.h"
#include "transition.h"

void
debugger_platform_setDefaults(e9k_path_config_t *config);

static void
config_setConfigValue(char *dest, size_t capacity, const char *value)
{
    if (!dest || capacity == 0) {
        return;
    }
    if (!value || !*value) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, value, capacity - 1);
    dest[capacity - 1] = '\0';
}

static const char *
config_trimValue(char *value)
{
    if (!value) {
        return NULL;
    }
    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
        value[--len] = '\0';
    }
    while (*value == ' ' || *value == '\t') {
        ++value;
    }
    return value;
}

void
config_persistConfig(FILE *f)
{
    if (!f) {
        return;
    }
    if (debugger.config.corePath[0]) {
        fprintf(f, "comp.config.core=%s\n", debugger.config.corePath);
    }
    if (debugger.config.romPath[0]) {
        fprintf(f, "comp.config.rom=%s\n", debugger.config.romPath);
    }
    if (debugger.config.romFolder[0]) {
        fprintf(f, "comp.config.rom_folder=%s\n", debugger.config.romFolder);
    }
    if (debugger.config.elfPath[0]) {
        fprintf(f, "comp.config.elf=%s\n", debugger.config.elfPath);
    }
    if (debugger.config.biosDir[0]) {
        fprintf(f, "comp.config.bios=%s\n", debugger.config.biosDir);
    }
    if (debugger.config.savesDir[0]) {
        fprintf(f, "comp.config.saves=%s\n", debugger.config.savesDir);
    }
    if (debugger.config.sourceDir[0]) {
        fprintf(f, "comp.config.source=%s\n", debugger.config.sourceDir);
    }
    if (debugger.config.systemType[0]) {
        fprintf(f, "comp.config.system_type=%s\n", debugger.config.systemType);
    }
    if (debugger.config.audioBufferMs > 0) {
        fprintf(f, "comp.config.audio_ms=%d\n", debugger.config.audioBufferMs);
    }
    if (!debugger.config.audioEnabled) {
        fprintf(f, "comp.config.audio_enabled=0\n");
    }
    if (debugger.config.skipBiosLogo) {
        fprintf(f, "comp.config.skip_bios=1\n");
    }
    if (!crt_isEnabled()) {
        fprintf(f, "comp.config.crt_enabled=0\n");
    }
    fprintf(f, "comp.config.transition=%s\n", transition_modeName(debugger.transitionMode));
    crt_persistConfig(f);
    sprite_debug_persistConfig(f);
}

void
config_saveConfig(void)
{
    e9ui_saveLayout();
}

void
config_loadConfig(void)
{
    const char *path = debugger_configPath();
    if (!path) {
        return;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        debugger_platform_setDefaults(&debugger.config);
        return;
    }
    char key[256];
    char val[1024];
    while (fscanf(f, "%255[^=]=%1023[^\n]\n", key, val) == 2) {
        if (strncmp(key, "comp.config.", 12) == 0) {
            const char *prop = key + 12;
            const char *value = config_trimValue(val);
            if (strcmp(prop, "core") == 0) {
                config_setConfigValue(debugger.config.corePath, sizeof(debugger.config.corePath), value);
            } else if (strcmp(prop, "rom") == 0) {
                config_setConfigValue(debugger.config.romPath, sizeof(debugger.config.romPath), value);
            } else if (strcmp(prop, "rom_folder") == 0) {
                config_setConfigValue(debugger.config.romFolder, sizeof(debugger.config.romFolder), value);
            } else if (strcmp(prop, "elf") == 0) {
                config_setConfigValue(debugger.config.elfPath, sizeof(debugger.config.elfPath), value);
            } else if (strcmp(prop, "bios") == 0) {
                config_setConfigValue(debugger.config.biosDir, sizeof(debugger.config.biosDir), value);
            } else if (strcmp(prop, "saves") == 0) {
                config_setConfigValue(debugger.config.savesDir, sizeof(debugger.config.savesDir), value);
            } else if (strcmp(prop, "source") == 0) {
                config_setConfigValue(debugger.config.sourceDir, sizeof(debugger.config.sourceDir), value);
            } else if (strcmp(prop, "system_type") == 0) {
                config_setConfigValue(debugger.config.systemType, sizeof(debugger.config.systemType), value);
            } else if (strcmp(prop, "audio_ms") == 0) {
                char *end = NULL;
                long ms = strtol(value, &end, 10);
                if (end && end != value && ms > 0 && ms <= INT_MAX) {
                    debugger.config.audioBufferMs = (int)ms;
                }
            } else if (strcmp(prop, "audio_enabled") == 0) {
                debugger.config.audioEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "skip_bios") == 0) {
                debugger.config.skipBiosLogo = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "crt_enabled") == 0) {
                debugger.config.crtEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "transition") == 0) {
                e9k_transition_mode_t mode = e9k_transition_none;
                if (transition_parseMode(value, &mode)) {
                    debugger.transitionMode = mode;
                }
            }
            continue;
        }
        if (strncmp(key, "comp.crt.", 9) == 0) {
            const char *prop = key + 9;
            const char *value = config_trimValue(val);
            crt_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.sprite_debug.", 18) == 0) {
            const char *prop = key + 18;
            const char *value = config_trimValue(val);
            sprite_debug_loadConfigProperty(prop, value);
            continue;
        }
    }
    fclose(f);
}
