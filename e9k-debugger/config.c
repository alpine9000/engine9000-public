/*
 * Copyright © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "config.h"
#include "amiga_blit_info.h"
#include "amiga_memview.h"
#include "crt.h"
#include "amiga_custom.h"
#include "amiga_custom_log.h"
#include "amiga_custom_ui.h"
#include "debugger.h"
#include "e9ui.h"
#include "hex_byte_color.h"
#include "hex_convert.h"
#include "hotkeys.h"
#include "neogeo_audio_vis.h"
#include "neogeo_memview.h"
#include "neogeo_palette_debug.h"
#include "neogeo_register_log.h"
#include "neogeo_sprite_debug.h"
#include "neogeo_sprite_list.h"
#include "profile_checkpoints.h"
#include "mega_audio_vis.h"
#include "mega_vdp.h"
#include "mega_memview.h"
#include "mega_palette_debug.h"
#include "mega_sprite_debug.h"
#include "shader_ui.h"
#include "settings.h"
#include "transition.h"
#include "ui_test.h"

static const char config_uiTestSessionConfigName[] = ".e9k-debugger.cfg";


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

static const char *
config_trimKey(char *key)
{
    if (!key) {
        return NULL;
    }
    while (*key == ' ' || *key == '\t') {
        ++key;
    }
    size_t len = strlen(key);
    while (len > 0 && (key[len - 1] == ' ' || key[len - 1] == '\t' || key[len - 1] == '\n' || key[len - 1] == '\r')) {
        key[--len] = '\0';
    }
    return key;
}

void
config_persistConfig(FILE *f)
{
    if (!f) {
        return;
    }
    if (debugger.config.amiga.libretro.romPath[0]) {
        fprintf(f, "comp.config.amiga.rom=%s\n", debugger.config.amiga.libretro.romPath);
    }
    if (debugger.config.amiga.libretro.exePath[0]) {
        fprintf(f, "comp.config.amiga.elf=%s\n", debugger.config.amiga.libretro.exePath);
    }
    fprintf(f, "comp.config.amiga.toolchain_prefix=%s\n", debugger.config.amiga.libretro.toolchainPrefix);
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
    if (debugger.config.neogeo.libretro.romPath[0]) {
        fprintf(f, "comp.config.neogeo.rom=%s\n", debugger.config.neogeo.libretro.romPath);
    }
    if (debugger.config.neogeo.romFolder[0]) {
        fprintf(f, "comp.config.neogeo.rom_folder=%s\n", debugger.config.neogeo.romFolder);
    }
    if (debugger.config.neogeo.libretro.exePath[0]) {
        fprintf(f, "comp.config.neogeo.elf=%s\n", debugger.config.neogeo.libretro.exePath);
    }
    fprintf(f, "comp.config.neogeo.toolchain_prefix=%s\n", debugger.config.neogeo.libretro.toolchainPrefix);
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
    if (debugger.config.neogeo.cdSystemType[0]) {
        fprintf(f, "comp.config.neogeo.cd_system_type=%s\n", debugger.config.neogeo.cdSystemType);
    }
    if (debugger.config.neogeo.libretro.audioBufferMs > 0) {
        fprintf(f, "comp.config.neogeo.audio_ms=%d\n", debugger.config.neogeo.libretro.audioBufferMs);
    }
    fprintf(f, "comp.config.neogeo.audio_enabled=%d\n", debugger.config.neogeo.libretro.audioEnabled );
    if (debugger.config.neogeo.skipBiosLogo) {
        fprintf(f, "comp.config.neogeo.skip_bios=1\n");
    }
    if (debugger.config.megadrive.libretro.romPath[0]) {
        fprintf(f, "comp.config.megadrive.rom=%s\n", debugger.config.megadrive.libretro.romPath);
    }
    if (debugger.config.megadrive.romFolder[0]) {
        fprintf(f, "comp.config.megadrive.rom_folder=%s\n", debugger.config.megadrive.romFolder);
    }
    if (debugger.config.megadrive.libretro.exePath[0]) {
        fprintf(f, "comp.config.megadrive.elf=%s\n", debugger.config.megadrive.libretro.exePath);
    }
    fprintf(f, "comp.config.megadrive.toolchain_prefix=%s\n", debugger.config.megadrive.libretro.toolchainPrefix);
    if (debugger.config.megadrive.libretro.systemDir[0]) {
        fprintf(f, "comp.config.megadrive.bios=%s\n", debugger.config.megadrive.libretro.systemDir);
    }
    if (debugger.config.megadrive.libretro.saveDir[0]) {
        fprintf(f, "comp.config.megadrive.saves=%s\n", debugger.config.megadrive.libretro.saveDir);
    }
    if (debugger.config.megadrive.libretro.sourceDir[0]) {
        fprintf(f, "comp.config.megadrive.source=%s\n", debugger.config.megadrive.libretro.sourceDir);
    }
    if (debugger.config.megadrive.libretro.audioBufferMs > 0) {
        fprintf(f, "comp.config.megadrive.audio_ms=%d\n", debugger.config.megadrive.libretro.audioBufferMs);
    }
    fprintf(f, "comp.config.megadrive.audio_enabled=%d\n", debugger.config.megadrive.libretro.audioEnabled);
    fprintf(f, "comp.config.crt_enabled=%d\n", debugger.config.crtEnabled ? 1 : 0);
    fprintf(f, "comp.config.logs_enabled=%d\n", debugger.config.logsEnabled ? 1 : 0);
    fprintf(f, "comp.config.record_enabled=%d\n", debugger.config.recordEnabled ? 1 : 0);
    if (!debugger.config.logosEnabled) {
        fprintf(f, "comp.config.logos_enabled=0\n");
    }
    if (debugger.preferredControllerGuid[0]) {
        fprintf(f, "comp.config.controller_guid=%s\n", debugger.preferredControllerGuid);
    }
    if (!debugger.coreOptionsShowHelp) {
        fprintf(f, "comp.config.core_options_show_help=0\n");
    }
    if (!hex_byte_color_isEnabled()) {
        fprintf(f, "comp.config.hex_byte_colors=0\n");
    }
    fprintf(f, "comp.config.transition=%s\n", transition_modeName(e9ui->transition.mode));
    fprintf(f, "comp.config.core_system=%d\n", target->coreIndex);
    hotkeys_persistConfig(f);
    crt_persistConfig(f);
    neogeo_register_log_persistConfig(f);
    neogeo_sprite_debug_persistConfig(f);
    neogeo_sprite_list_persistConfig(f);
    neogeo_audio_vis_persistConfig(f);
    neogeo_palette_debug_persistConfig(f);
    neogeo_memview_persistConfig(f);
    mega_audio_vis_persistConfig(f);
    mega_vdp_persistConfig(f);
    mega_memview_persistConfig(f);
    mega_palette_debug_persistConfig(f);
    mega_sprite_debug_persistConfig(f);
    amiga_custom_ui_persistConfig(f);
    amiga_custom_log_persistConfig(f);
    amiga_custom_persistConfig(f);
    amiga_memview_persistConfig(f);
    amiga_blit_info_persistConfig(f);
    profile_checkpoints_persistConfig(f);
    shader_ui_persistConfig(f);
    hex_convert_persistConfig(f);
    settings_persistConfig(f);
}

void
config_saveConfig(void)
{
    if (debugger.smokeTestMode != 0) {
        return;
    }
    const char *path = debugger_configPath();
    if (ui_test_getMode() != UI_TEST_MODE_NONE) {
        const char *tempPath = debugger_configTempPath();
        if (tempPath && *tempPath) {
            path = tempPath;
        }
    }
    if (!path || !*path) {
        return;
    }
    e9ui_saveLayout(path);
}

static void
config_loadConfigFile(const char *path)
{
    if (!path || !*path) {
        return;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }

    char line[1280];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *key = config_trimKey(line);
        const char *val = eq + 1;
        const char *value = config_trimValue((char *)val);
        if (!key) {
            continue;
        }

        if (strncmp(key, "comp.config.", 12) == 0) {
            const char *prop = key + 12;
            if (strcmp(prop, "amiga.rom") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.romPath, sizeof(debugger.config.amiga.libretro.romPath), value);
            } else if (strcmp(prop, "amiga.elf") == 0) {
                config_setConfigValue(debugger.config.amiga.libretro.exePath, sizeof(debugger.config.amiga.libretro.exePath), value);
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
            } else if (strcmp(prop, "neogeo.rom") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.romPath, sizeof(debugger.config.neogeo.libretro.romPath), value);
            } else if (strcmp(prop, "neogeo.rom_folder") == 0) {
                config_setConfigValue(debugger.config.neogeo.romFolder, sizeof(debugger.config.neogeo.romFolder), value);
            } else if (strcmp(prop, "neogeo.elf") == 0) {
                config_setConfigValue(debugger.config.neogeo.libretro.exePath, sizeof(debugger.config.neogeo.libretro.exePath), value);
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
            } else if (strcmp(prop, "neogeo.cd_system_type") == 0) {
                config_setConfigValue(debugger.config.neogeo.cdSystemType, sizeof(debugger.config.neogeo.cdSystemType), value);
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
            } else if (strcmp(prop, "megadrive.rom") == 0) {
                config_setConfigValue(debugger.config.megadrive.libretro.romPath, sizeof(debugger.config.megadrive.libretro.romPath), value);
            } else if (strcmp(prop, "megadrive.rom_folder") == 0) {
                config_setConfigValue(debugger.config.megadrive.romFolder, sizeof(debugger.config.megadrive.romFolder), value);
            } else if (strcmp(prop, "megadrive.elf") == 0) {
                config_setConfigValue(debugger.config.megadrive.libretro.exePath, sizeof(debugger.config.megadrive.libretro.exePath), value);
            } else if (strcmp(prop, "megadrive.toolchain_prefix") == 0) {
                config_setConfigValue(debugger.config.megadrive.libretro.toolchainPrefix, sizeof(debugger.config.megadrive.libretro.toolchainPrefix), value);
            } else if (strcmp(prop, "megadrive.bios") == 0) {
                config_setConfigValue(debugger.config.megadrive.libretro.systemDir, sizeof(debugger.config.megadrive.libretro.systemDir), value);
            } else if (strcmp(prop, "megadrive.saves") == 0) {
                config_setConfigValue(debugger.config.megadrive.libretro.saveDir, sizeof(debugger.config.megadrive.libretro.saveDir), value);
            } else if (strcmp(prop, "megadrive.source") == 0) {
                config_setConfigValue(debugger.config.megadrive.libretro.sourceDir, sizeof(debugger.config.megadrive.libretro.sourceDir), value);
            } else if (strcmp(prop, "megadrive.audio_ms") == 0) {
                char *end = NULL;
                long ms = strtol(value, &end, 10);
                if (end && end != value && ms > 0 && ms <= INT_MAX) {
                    debugger.config.megadrive.libretro.audioBufferMs = (int)ms;
                }
            } else if (strcmp(prop, "megadrive.audio_enabled") == 0) {
                debugger.config.megadrive.libretro.audioEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "crt_enabled") == 0) {
                debugger.config.crtEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "logs_enabled") == 0) {
                debugger.config.logsEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "record_enabled") == 0) {
                debugger.config.recordEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "logos_enabled") == 0) {
                debugger.config.logosEnabled = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "controller_guid") == 0) {
                config_setConfigValue(debugger.preferredControllerGuid,
                                      sizeof(debugger.preferredControllerGuid),
                                      value);
            } else if (strcmp(prop, "core_options_show_help") == 0) {
                debugger.coreOptionsShowHelp = atoi(value) ? 1 : 0;
            } else if (strcmp(prop, "hex_byte_colors") == 0) {
                hex_byte_color_setEnabled(atoi(value) ? 1 : 0);
            } else if (strcmp(prop, "core_system") == 0) {
                int coreSystem = atoi(value);
                if (!target_getByIndex(coreSystem)) {
                    coreSystem = target_firstEnabledIndex();
                }
                if (coreSystem >= 0) {
                    target_setTargetIndex(coreSystem);
                }
            } else if (strcmp(prop, "transition") == 0) {
                e9k_transition_mode_t mode = e9k_transition_none;
                if (transition_parseMode(value, &mode)) {
                    e9ui->transition.mode = mode;
                }
            } else if (strncmp(prop, "hotkey.", 7) == 0) {
                hotkeys_loadConfigProperty(prop + 7, value);
            }
            continue;
        }
        if (strncmp(key, "comp.crt.", 9) == 0) {
            const char *prop = key + 9;
            crt_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.register_log.", 18) == 0) {
            const char *prop = key + 18;
            neogeo_register_log_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.sprite_debug.", 18) == 0) {
            const char *prop = key + 18;
            neogeo_sprite_debug_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.sprite_list.", 17) == 0) {
            const char *prop = key + 17;
            neogeo_sprite_list_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.neogeo_audio_vis.", 22) == 0) {
            const char *prop = key + 22;
            neogeo_audio_vis_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.neogeo_palette_debug.", 26) == 0) {
            const char *prop = key + 26;
            neogeo_palette_debug_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.neogeo_memview.", 20) == 0) {
            const char *prop = key + 20;
            neogeo_memview_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.mega_memview.", 18) == 0) {
            const char *prop = key + 18;
            mega_memview_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.mega_audio_vis.", 20) == 0) {
            const char *prop = key + 20;
            mega_audio_vis_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.mega_vdp.", 14) == 0) {
            const char *prop = key + 14;
            mega_vdp_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.mega_palette_debug.", 24) == 0) {
            const char *prop = key + 24;
            mega_palette_debug_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.mega_sprite_debug.", 23) == 0) {
            const char *prop = key + 23;
            mega_sprite_debug_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.custom_ui.", 15) == 0) {
            const char *prop = key + 15;
            amiga_custom_ui_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.custom_log.", 16) == 0) {
            const char *prop = key + 16;
            amiga_custom_log_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.custom_amiga.", 18) == 0) {
            const char *prop = key + 18;
            amiga_custom_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.amiga_memview.", 19) == 0) {
            const char *prop = key + 19;
            amiga_memview_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.amiga_blit_info.", 21) == 0) {
            const char *prop = key + 21;
            amiga_blit_info_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.shader_ui.", 15) == 0) {
            const char *prop = key + 15;
            shader_ui_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.profile_checkpoints.", 25) == 0) {
            const char *prop = key + 25;
            profile_checkpoints_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.hex_convert.", 17) == 0) {
            const char *prop = key + 17;
            hex_convert_loadConfigProperty(prop, value);
            continue;
        }
        if (strncmp(key, "comp.settings.", 14) == 0) {
            const char *prop = key + 14;
            settings_loadConfigProperty(prop, value);
            continue;
        }
    }
    fclose(f);
}

void
config_loadConfig(void)
{
    target_setConfigDefaults();
    hotkeys_resetConfigOverrides();

    if (ui_test_getMode() != UI_TEST_MODE_NONE) {
        if (debugger_getLoadTestTempConfig()) {
            const char *tempPath = debugger_configTempPath();
            if (tempPath && *tempPath) {
                config_loadConfigFile(tempPath);
                return;
            }
        }

        char uiTestPath[1024];
        uiTestPath[0] = '\0';
        const char *folder = ui_test_getFolder();
        if (folder && *folder &&
            debugger_platform_pathJoin(uiTestPath, sizeof(uiTestPath), folder, config_uiTestSessionConfigName)) {
            config_loadConfigFile(uiTestPath);
        } else {
            config_loadConfigFile(debugger_configPath());
        }
        return;
    }

    config_loadConfigFile(debugger_configPath());
}
