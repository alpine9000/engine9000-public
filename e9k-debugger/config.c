/*
 * Copyright © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "config.h"
#include "crt.h"
#include "debugger.h"
#include "e9ui.h"
#include "sprite_debug.h"
#include "transition.h"

void
debugger_platform_setDefaults(e9k_neogeo_config_t *config);
void
debugger_platform_setDefaultsAmiga(e9k_amiga_config_t *config);

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
    if (debugger.config.amiga.libretro.corePath[0]) {
        fprintf(f, "comp.config.amiga.core=%s\n", debugger.config.amiga.libretro.corePath);
    }
    if (debugger.config.amiga.libretro.romPath[0]) {
        fprintf(f, "comp.config.amiga.rom=%s\n", debugger.config.amiga.libretro.romPath);
    }
    if (debugger.config.amiga.libretro.elfPath[0]) {
        fprintf(f, "comp.config.amiga.elf=%s\n", debugger.config.amiga.libretro.elfPath);
    }
    if (debugger.config.amiga.libretro.toolchainPrefix[0]) {
        fprintf(f, "comp.config.amiga.toolchain_prefix=%s\n", debugger.config.amiga.libretro.toolchainPrefix);
    }
    if (debugger.config.amiga.libretro.systemDir[0]) {
        fprintf(f, "comp.config.amiga.bios=%s\n", debugger.config.amiga.libretro.systemDir);
    }
    if (debugger.config.amiga.libretro.saveDir[0]) {
        fprintf(f, "comp.config.amiga.saves=%s\n", debugger.config.amiga.libretro.saveDir);
    }
    if (debugger.config.amiga.libretro.sourceDir[0]) {
        fprintf(f, "comp.config.amiga.source=%s\n", debugger.config.amiga.libretro.sourceDir);
    }
    if (debugger.config.amiga.libretro.audioBufferMs > 0) {
        fprintf(f, "comp.config.amiga.audio_ms=%d\n", debugger.config.amiga.libretro.audioBufferMs);
    }
    fprintf(f, "comp.config.amiga.audio_enabled=%d\n", debugger.config.amiga.libretro.audioEnabled);
    if (debugger.config.neogeo.libretro.corePath[0]) {
        fprintf(f, "comp.config.neogeo.core=%s\n", debugger.config.neogeo.libretro.corePath);
    }
    if (debugger.config.neogeo.libretro.romPath[0]) {
        fprintf(f, "comp.config.neogeo.rom=%s\n", debugger.config.neogeo.libretro.romPath);
    }
    if (debugger.config.neogeo.romFolder[0]) {
        fprintf(f, "comp.config.neogeo.rom_folder=%s\n", debugger.config.neogeo.romFolder);
    }
    if (debugger.config.neogeo.libretro.elfPath[0]) {
        fprintf(f, "comp.config.neogeo.elf=%s\n", debugger.config.neogeo.libretro.elfPath);
    }
    if (debugger.config.neogeo.libretro.toolchainPrefix[0]) {
        fprintf(f, "comp.config.neogeo.toolchain_prefix=%s\n", debugger.config.neogeo.libretro.toolchainPrefix);
    }
    if (debugger.config.neogeo.libretro.systemDir[0]) {
        fprintf(f, "comp.config.neogeo.bios=%s\n", debugger.config.neogeo.libretro.systemDir);
    }
    if (debugger.config.neogeo.libretro.saveDir[0]) {
        fprintf(f, "comp.config.neogeo.saves=%s\n", debugger.config.neogeo.libretro.saveDir);
    }
    if (debugger.config.neogeo.libretro.sourceDir[0]) {
        fprintf(f, "comp.config.neogeo.source=%s\n", debugger.config.neogeo.libretro.sourceDir);
    }
    if (debugger.config.neogeo.systemType[0]) {
        fprintf(f, "comp.config.neogeo.system_type=%s\n", debugger.config.neogeo.systemType);
    }
    if (debugger.config.neogeo.libretro.audioBufferMs > 0) {
        fprintf(f, "comp.config.neogeo.audio_ms=%d\n", debugger.config.neogeo.libretro.audioBufferMs);
    }
    fprintf(f, "comp.config.neogeo.audio_enabled=%d\n", debugger.config.neogeo.libretro.audioEnabled );
    if (debugger.config.neogeo.skipBiosLogo) {
        fprintf(f, "comp.config.neogeo.skip_bios=1\n");
    }
    if (!debugger.config.crtEnabled) {
        fprintf(f, "comp.config.crt_enabled=0\n");
    }    
    fprintf(f, "comp.config.transition=%s\n", transition_modeName(e9ui->transition.mode));
    fprintf(f, "comp.config.core_system=%d\n", debugger.config.coreSystem);
    crt_persistConfig(f);
    sprite_debug_persistConfig(f);
}

void
config_saveConfig(void)
{
  if (debugger.smokeTestMode != 0) {
    return;
  }
  e9ui_saveLayout(debugger_configPath());
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
        debugger_platform_setDefaults(&debugger.config.neogeo);
        debugger_platform_setDefaultsAmiga(&debugger.config.amiga);
        return;
    }
    char key[256];
    char val[1024];
    while (fscanf(f, "%255[^=]=%1023[^\n]\n", key, val) == 2) {
        if (strncmp(key, "comp.config.", 12) == 0) {
            const char *prop = key + 12;
            const char *value = config_trimValue(val);
            if (strcmp(prop, "amiga.core") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.corePath, sizeof(debugger.config.amiga.libretro.corePath), value);
            } else if (strcmp(prop, "amiga.rom") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.romPath, sizeof(debugger.config.amiga.libretro.romPath), value);
            } else if (strcmp(prop, "amiga.elf") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.elfPath, sizeof(debugger.config.amiga.libretro.elfPath), value);
            } else if (strcmp(prop, "amiga.toolchain_prefix") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.toolchainPrefix, sizeof(debugger.config.amiga.libretro.toolchainPrefix), value);
            } else if (strcmp(prop, "amiga.bios") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.systemDir, sizeof(debugger.config.amiga.libretro.systemDir), value);
            } else if (strcmp(prop, "amiga.saves") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.saveDir, sizeof(debugger.config.amiga.libretro.saveDir), value);
            } else if (strcmp(prop, "amiga.source") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.sourceDir, sizeof(debugger.config.amiga.libretro.sourceDir), value);
            } else if (strcmp(prop, "amiga.audio_ms") == 0) {
                char *end = NULL;
                long ms = strtol(value, &end, 10);
                if (end && end != value && ms > 0 && ms <= INT_MAX) {
                    debugger.config.amiga.libretro.audioBufferMs = (int)ms;
                }
            } else if (strcmp(prop, "amiga.audio_enabled") == 0) {
                debugger.config.amiga.libretro.audioEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "neogeo.core") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.corePath, sizeof(debugger.config.neogeo.libretro.corePath), value);
            } else if (strcmp(prop, "neogeo.rom") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.romPath, sizeof(debugger.config.neogeo.libretro.romPath), value);
            } else if (strcmp(prop, "neogeo.rom_folder") == 0) {
                config_setConfigValue(debugger.config.neogeo.romFolder, sizeof(debugger.config.neogeo.romFolder), value);
            } else if (strcmp(prop, "neogeo.elf") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.elfPath, sizeof(debugger.config.neogeo.libretro.elfPath), value);
            } else if (strcmp(prop, "neogeo.toolchain_prefix") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.toolchainPrefix, sizeof(debugger.config.neogeo.libretro.toolchainPrefix), value);
            } else if (strcmp(prop, "neogeo.bios") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.systemDir, sizeof(debugger.config.neogeo.libretro.systemDir), value);
            } else if (strcmp(prop, "neogeo.saves") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.saveDir, sizeof(debugger.config.neogeo.libretro.saveDir), value);
            } else if (strcmp(prop, "neogeo.source") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.sourceDir, sizeof(debugger.config.neogeo.libretro.sourceDir), value);
            } else if (strcmp(prop, "neogeo.system_type") == 0) {
                config_setConfigValue(debugger.config.neogeo.systemType, sizeof(debugger.config.neogeo.systemType), value);
            } else if (strcmp(prop, "neogeo.audio_ms") == 0) {
                char *end = NULL;
                long ms = strtol(value, &end, 10);
                if (end && end != value && ms > 0 && ms <= INT_MAX) {
                    debugger.config.neogeo.libretro.audioBufferMs = (int)ms;
                }
            } else if (strcmp(prop, "neogeo.audio_enabled") == 0) {
                debugger.config.neogeo.libretro.audioEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "neogeo.skip_bios") == 0) {
                debugger.config.neogeo.skipBiosLogo = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "crt_enabled") == 0) {
                debugger.config.crtEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "core_system") == 0) {
                debugger_setCoreSystem((debugger_system_type_t)atoi(value));
            } else if (strcmp(prop, "transition") == 0) {
                e9k_transition_mode_t mode = e9k_transition_none;
                if (transition_parseMode(value, &mode)) {
                    e9ui->transition.mode = mode;
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
