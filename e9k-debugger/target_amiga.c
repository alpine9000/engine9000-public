/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "debugger.h"
#include "target.h"
#include "emu_ami.h"
#include "emu.h"
#include "rom_config.h"
#include "debugger_input_bindings.h"
#include "amiga_uae_options.h"
#include "neogeo_core_options.h"
#include "core_options.h"
#include "libretro_host.h"
#include "profile.h"
#include "smoke_test.h"
#include "system_badge.h"
#include "ui_test.h"
#include "alloc.h"
#include "file.h"
#include "strutil.h"

#ifndef RETRO_DEVICE_PUAE_JOYPAD
#define RETRO_DEVICE_PUAE_JOYPAD RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#endif

#ifndef RETRO_DEVICE_PUAE_CD32PAD
#define RETRO_DEVICE_PUAE_CD32PAD RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#endif

typedef struct target_amiga_romselect_extra {
    e9ui_component_t *df0Select;
    e9ui_component_t *df1Select;
    e9ui_component_t *dh0FolderSelect;
    e9ui_component_t *dh0HdfSelect;
    e9ui_component_t *dh1FolderSelect;
    e9ui_component_t *dh1HdfSelect;
    struct target_amiga_toolchain_preset_state *toolchainPresetState;
    int updatingMedia;
} target_amiga_romselect_extra_t;

typedef struct target_amiga_toolchain_preset_state {
    e9ui_component_t *toolchainTextbox;
    e9ui_component_t *vasmCheckbox;
    e9ui_component_t *gccCheckbox;
    int updating;
} target_amiga_toolchain_preset_state_t;

#if defined(_WIN32)
static const char target_amiga_toolchainPrefixVasm[] = ".\\system\\v-hunk-";
#else
static const char target_amiga_toolchainPrefixVasm[] = "./system/v-hunk-";
#endif
static const char target_amiga_toolchainPrefixGcc[] = "m68k-amigaos-";

static void
target_amiga_toolchainPresetSync(target_amiga_toolchain_preset_state_t *st, e9ui_context_t *ctx, const char *prefix);

static const char *
target_amiga_defaultCorePath(void);

static void
target_amiga_configControllerPorts(void);

static const char *
target_amiga_mouseCaptureOptionKey(void)
{
    return "e9k_debugger_amiga_mouse_capture";
}

static const char *
target_amiga_memorySpaceOptionKey(void)
{
    return "e9k_debugger_amiga_memory_space";
}

static const char *
target_amiga_memoryAddressOptionKey(void)
{
    return "e9k_debugger_amiga_memory_address";
}

static const char *
target_amiga_memorySearchOptionKey(void)
{
    return "e9k_debugger_amiga_memory_search";
}

static const char *
target_amiga_portDeviceOptionKeyAt(size_t index)
{
    static const char *keys[2] = {
        "e9k_debugger_amiga_port1_device",
        "e9k_debugger_amiga_port2_device"
    };
    if (index >= 2) {
        return NULL;
    }
    return keys[index];
}

#define TARGET_AMIGA_FLOPPY_RECENTS_MAX 8
#define TARGET_AMIGA_ROM_CONFIG_FIXED_OPTION_COUNT 6
static const char target_amiga_floppyRecentClearLabel[] = "<CLEAR RECENTS>";
static const char target_amiga_floppyRecentClearValue[] = "__e9k_amiga_clear_floppy_recents__";

static const char *
target_amiga_df0RecentOptionKeyAt(size_t index)
{
    static const char *keys[TARGET_AMIGA_FLOPPY_RECENTS_MAX] = {
        "e9k_debugger_amiga_df0_recent_0",
        "e9k_debugger_amiga_df0_recent_1",
        "e9k_debugger_amiga_df0_recent_2",
        "e9k_debugger_amiga_df0_recent_3",
        "e9k_debugger_amiga_df0_recent_4",
        "e9k_debugger_amiga_df0_recent_5",
        "e9k_debugger_amiga_df0_recent_6",
        "e9k_debugger_amiga_df0_recent_7"
    };
    if (index >= TARGET_AMIGA_FLOPPY_RECENTS_MAX) {
        return NULL;
    }
    return keys[index];
}

static const char *
target_amiga_df1RecentOptionKeyAt(size_t index)
{
    static const char *keys[TARGET_AMIGA_FLOPPY_RECENTS_MAX] = {
        "e9k_debugger_amiga_df1_recent_0",
        "e9k_debugger_amiga_df1_recent_1",
        "e9k_debugger_amiga_df1_recent_2",
        "e9k_debugger_amiga_df1_recent_3",
        "e9k_debugger_amiga_df1_recent_4",
        "e9k_debugger_amiga_df1_recent_5",
        "e9k_debugger_amiga_df1_recent_6",
        "e9k_debugger_amiga_df1_recent_7"
    };
    if (index >= TARGET_AMIGA_FLOPPY_RECENTS_MAX) {
        return NULL;
    }
    return keys[index];
}

static const char *
target_amiga_normalizeMouseCaptureOverrideValue(const char *value)
{
    if (!value || !*value) {
        return NULL;
    }
    if (strcmp(value, "enabled") == 0 || strcmp(value, "Enabled") == 0) {
        return "enabled";
    }
    if (strcmp(value, "disabled") == 0 || strcmp(value, "Disabled") == 0) {
        return "disabled";
    }
    return NULL;
}

static char target_amiga_activeRomMouseCaptureOverride[16];
static int target_amiga_hasActiveRomMouseCaptureOverride = 0;
static char target_amiga_activeRomPortDevice[2][16];
static char target_amiga_activeRomMemorySpace[16];
static char target_amiga_activeRomMemoryAddress[32];
static char target_amiga_activeRomMemorySearch[128];
static char target_amiga_activeRomFloppyRecents[2][TARGET_AMIGA_FLOPPY_RECENTS_MAX][PATH_MAX];
static int target_amiga_settingsMediaInitiallyEmpty = 0;
static int target_amiga_settingsMediaNeedsRestart = 0;

static int
target_amiga_pathHasValue(const char *path)
{
    return (path && *path) ? 1 : 0;
}

static int
target_amiga_anyMediaConfigured(void)
{
    const char *df0 = amiga_uaeGetFloppyPath(0);
    const char *df1 = amiga_uaeGetFloppyPath(1);
    const char *dh0Folder = amiga_uaeGetHardDriveFolderPathForUnit(0);
    const char *dh0Hdf = amiga_uaeGetHardDriveHdfPathForUnit(0);
    const char *dh1Folder = amiga_uaeGetHardDriveFolderPathForUnit(1);
    const char *dh1Hdf = amiga_uaeGetHardDriveHdfPathForUnit(1);
    return target_amiga_pathHasValue(df0) ||
        target_amiga_pathHasValue(df1) ||
        target_amiga_pathHasValue(dh0Folder) ||
        target_amiga_pathHasValue(dh0Hdf) ||
        target_amiga_pathHasValue(dh1Folder) ||
        target_amiga_pathHasValue(dh1Hdf);
}

static void
target_amiga_captureSettingsMediaBaseline(void)
{
    target_amiga_settingsMediaInitiallyEmpty = target_amiga_anyMediaConfigured() ? 0 : 1;
    target_amiga_settingsMediaNeedsRestart = 0;
}

static void
target_amiga_updateSettingsMediaRestartRequirement(void)
{
    target_amiga_settingsMediaNeedsRestart =
        (target_amiga_settingsMediaInitiallyEmpty && target_amiga_anyMediaConfigured()) ? 1 : 0;
    settings_updateSaveLabel();
}

static const char *
target_amiga_getActiveMouseCaptureOverride(void)
{
    if (!target_amiga_hasActiveRomMouseCaptureOverride) {
        return NULL;
    }
    return target_amiga_activeRomMouseCaptureOverride[0] ? target_amiga_activeRomMouseCaptureOverride : NULL;
}

static void
target_amiga_setActiveMouseCaptureOverride(const char *value)
{
    const char *normalized = target_amiga_normalizeMouseCaptureOverrideValue(value);
    if (!normalized) {
        if (!value || !*value) {
            target_amiga_activeRomMouseCaptureOverride[0] = '\0';
            target_amiga_hasActiveRomMouseCaptureOverride = 0;
        }
        return;
    }
    strncpy(target_amiga_activeRomMouseCaptureOverride, normalized, sizeof(target_amiga_activeRomMouseCaptureOverride) - 1);
    target_amiga_activeRomMouseCaptureOverride[sizeof(target_amiga_activeRomMouseCaptureOverride) - 1] = '\0';
    target_amiga_hasActiveRomMouseCaptureOverride = 1;
}

static int
target_amiga_parseFloppyRecentOptionKey(const char *key, int *outDrive, size_t *outIndex)
{
    if (outDrive) {
        *outDrive = -1;
    }
    if (outIndex) {
        *outIndex = 0;
    }
    if (!key) {
        return 0;
    }

    const char *prefix = NULL;
    int drive = -1;
    if (strncmp(key, "e9k_debugger_amiga_df0_recent_", 29) == 0) {
        prefix = "e9k_debugger_amiga_df0_recent_";
        drive = 0;
    } else if (strncmp(key, "e9k_debugger_amiga_df1_recent_", 29) == 0) {
        prefix = "e9k_debugger_amiga_df1_recent_";
        drive = 1;
    } else {
        return 0;
    }

    const char *indexText = key + strlen(prefix);
    if (!*indexText) {
        return 0;
    }
    for (const char *p = indexText; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }

    int index = atoi(indexText);
    if (index < 0 || index >= TARGET_AMIGA_FLOPPY_RECENTS_MAX) {
        return 0;
    }
    if (outDrive) {
        *outDrive = drive;
    }
    if (outIndex) {
        *outIndex = (size_t)index;
    }
    return 1;
}

static void
target_amiga_clearActiveFloppyRecents(void)
{
    memset(target_amiga_activeRomFloppyRecents, 0, sizeof(target_amiga_activeRomFloppyRecents));
}

static const char *
target_amiga_getActiveFloppyRecentValue(int drive, size_t index)
{
    if (drive < 0 || drive > 1 || index >= TARGET_AMIGA_FLOPPY_RECENTS_MAX) {
        return NULL;
    }
    if (!target_amiga_activeRomFloppyRecents[drive][index][0]) {
        return NULL;
    }
    return target_amiga_activeRomFloppyRecents[drive][index];
}

static void
target_amiga_setActiveFloppyRecentValue(int drive, size_t index, const char *value)
{
    if (drive < 0 || drive > 1 || index >= TARGET_AMIGA_FLOPPY_RECENTS_MAX) {
        return;
    }
    if (!value || !*value) {
        target_amiga_activeRomFloppyRecents[drive][index][0] = '\0';
        return;
    }
    strutil_strlcpy(target_amiga_activeRomFloppyRecents[drive][index],
                    sizeof(target_amiga_activeRomFloppyRecents[drive][index]),
                    value);
}

static void
target_amiga_addActiveFloppyRecent(int drive, const char *path)
{
    if (drive < 0 || drive > 1 || !path || !*path) {
        return;
    }

    int existingIndex = -1;
    for (int i = 0; i < TARGET_AMIGA_FLOPPY_RECENTS_MAX; ++i) {
        const char *entry = target_amiga_activeRomFloppyRecents[drive][i];
        if (!entry[0]) {
            continue;
        }
        if (strcmp(entry, path) == 0) {
            existingIndex = i;
            break;
        }
    }

    if (existingIndex == 0) {
        return;
    }

    if (existingIndex > 0) {
        char keep[PATH_MAX];
        strutil_strlcpy(keep, sizeof(keep), target_amiga_activeRomFloppyRecents[drive][existingIndex]);
        for (int i = existingIndex; i > 0; --i) {
            strutil_strlcpy(target_amiga_activeRomFloppyRecents[drive][i],
                            sizeof(target_amiga_activeRomFloppyRecents[drive][i]),
                            target_amiga_activeRomFloppyRecents[drive][i - 1]);
        }
        strutil_strlcpy(target_amiga_activeRomFloppyRecents[drive][0],
                        sizeof(target_amiga_activeRomFloppyRecents[drive][0]),
                        keep);
        return;
    }

    for (int i = TARGET_AMIGA_FLOPPY_RECENTS_MAX - 1; i > 0; --i) {
        strutil_strlcpy(target_amiga_activeRomFloppyRecents[drive][i],
                        sizeof(target_amiga_activeRomFloppyRecents[drive][i]),
                        target_amiga_activeRomFloppyRecents[drive][i - 1]);
    }
    strutil_strlcpy(target_amiga_activeRomFloppyRecents[drive][0],
                    sizeof(target_amiga_activeRomFloppyRecents[drive][0]),
                    path);
}

static void
target_amiga_refreshFloppyRecentsDropdown(e9ui_component_t *fileSelect, int drive)
{
    if (!fileSelect || drive < 0 || drive > 1) {
        return;
    }
    e9ui_textbox_option_t options[TARGET_AMIGA_FLOPPY_RECENTS_MAX + 1];
    int optionCount = 0;
    for (int i = 0; i < TARGET_AMIGA_FLOPPY_RECENTS_MAX; ++i) {
        const char *value = target_amiga_activeRomFloppyRecents[drive][i];
        if (!value[0]) {
            continue;
        }
        options[optionCount].value = value;
        options[optionCount].label = value;
        optionCount++;
    }
    options[optionCount].value = target_amiga_floppyRecentClearValue;
    options[optionCount].label = target_amiga_floppyRecentClearLabel;
    optionCount++;
    e9ui_fileSelect_setOptions(fileSelect, optionCount > 0 ? options : NULL, optionCount);
}

static void
target_amiga_captureFloppyRecentsFromUae(void)
{
    const char *df0 = amiga_uaeGetFloppyPath(0);
    const char *df1 = amiga_uaeGetFloppyPath(1);
    target_amiga_addActiveFloppyRecent(0, df0);
    target_amiga_addActiveFloppyRecent(1, df1);
}

static size_t
target_amiga_romConfigCustomOptionCount(void)
{
    return TARGET_AMIGA_ROM_CONFIG_FIXED_OPTION_COUNT + (TARGET_AMIGA_FLOPPY_RECENTS_MAX * 2);
}

static const char *
target_amiga_romConfigCustomOptionKeyAt(size_t index)
{
    if (index == 0) {
        return target_amiga_mouseCaptureOptionKey();
    }
    if (index == 1) {
        return target_amiga_portDeviceOptionKeyAt(0);
    }
    if (index == 2) {
        return target_amiga_portDeviceOptionKeyAt(1);
    }
    if (index == 3) {
        return target_amiga_memorySpaceOptionKey();
    }
    if (index == 4) {
        return target_amiga_memoryAddressOptionKey();
    }
    if (index == 5) {
        return target_amiga_memorySearchOptionKey();
    }
    index -= TARGET_AMIGA_ROM_CONFIG_FIXED_OPTION_COUNT;
    if (index < TARGET_AMIGA_FLOPPY_RECENTS_MAX) {
        return target_amiga_df0RecentOptionKeyAt(index);
    }
    index -= TARGET_AMIGA_FLOPPY_RECENTS_MAX;
    if (index < TARGET_AMIGA_FLOPPY_RECENTS_MAX) {
        return target_amiga_df1RecentOptionKeyAt(index);
    }
    return NULL;
}

static const char *
target_amiga_romConfigGetActiveCustomOptionValue(const char *key)
{
    if (!key) {
        return NULL;
    }
    if (strcmp(key, target_amiga_mouseCaptureOptionKey()) == 0) {
        return target_amiga_getActiveMouseCaptureOverride();
    }
    if (strcmp(key, target_amiga_memorySpaceOptionKey()) == 0) {
        return target_amiga_activeRomMemorySpace[0] ? target_amiga_activeRomMemorySpace : NULL;
    }
    if (strcmp(key, target_amiga_memoryAddressOptionKey()) == 0) {
        return target_amiga_activeRomMemoryAddress[0] ? target_amiga_activeRomMemoryAddress : NULL;
    }
    if (strcmp(key, target_amiga_memorySearchOptionKey()) == 0) {
        return target_amiga_activeRomMemorySearch[0] ? target_amiga_activeRomMemorySearch : NULL;
    }
    for (size_t i = 0; i < 2; ++i) {
        const char *portKey = target_amiga_portDeviceOptionKeyAt(i);
        if (portKey && strcmp(key, portKey) == 0) {
            return target_amiga_activeRomPortDevice[i][0] ? target_amiga_activeRomPortDevice[i] : NULL;
        }
    }
    int drive = -1;
    size_t index = 0;
    if (target_amiga_parseFloppyRecentOptionKey(key, &drive, &index)) {
        return target_amiga_getActiveFloppyRecentValue(drive, index);
    }
    return NULL;
}

static void
target_amiga_romConfigSetActiveCustomOptionValue(const char *key, const char *value)
{
    if (!key) {
        return;
    }
    if (strcmp(key, target_amiga_mouseCaptureOptionKey()) == 0) {
        target_amiga_setActiveMouseCaptureOverride(value);
        return;
    }
    if (strcmp(key, target_amiga_memorySpaceOptionKey()) == 0) {
        strutil_strlcpy(target_amiga_activeRomMemorySpace, sizeof(target_amiga_activeRomMemorySpace), value ? value : "");
        return;
    }
    if (strcmp(key, target_amiga_memoryAddressOptionKey()) == 0) {
        strutil_strlcpy(target_amiga_activeRomMemoryAddress, sizeof(target_amiga_activeRomMemoryAddress), value ? value : "");
        return;
    }
    if (strcmp(key, target_amiga_memorySearchOptionKey()) == 0) {
        strutil_strlcpy(target_amiga_activeRomMemorySearch, sizeof(target_amiga_activeRomMemorySearch), value ? value : "");
        return;
    }
    for (size_t i = 0; i < 2; ++i) {
        const char *portKey = target_amiga_portDeviceOptionKeyAt(i);
        if (portKey && strcmp(key, portKey) == 0) {
            const char *src = (value && *value) ? value : "";
            strncpy(target_amiga_activeRomPortDevice[i], src, sizeof(target_amiga_activeRomPortDevice[i]) - 1);
            target_amiga_activeRomPortDevice[i][sizeof(target_amiga_activeRomPortDevice[i]) - 1] = '\0';
            return;
        }
    }
    int drive = -1;
    size_t index = 0;
    if (target_amiga_parseFloppyRecentOptionKey(key, &drive, &index)) {
        target_amiga_setActiveFloppyRecentValue(drive, index, value);
    }
}

static void
target_amiga_romConfigClearActiveCustomOptions(void)
{
    target_amiga_setActiveMouseCaptureOverride(NULL);
    memset(target_amiga_activeRomPortDevice, 0, sizeof(target_amiga_activeRomPortDevice));
    target_amiga_activeRomMemorySpace[0] = '\0';
    target_amiga_activeRomMemoryAddress[0] = '\0';
    target_amiga_activeRomMemorySearch[0] = '\0';
    target_amiga_clearActiveFloppyRecents();
}

static int
target_amiga_memoryPanelOptions(target_memory_panel_options_t *outOptions)
{
    if (!outOptions) {
        return 0;
    }
    outOptions->spaceKey = target_amiga_memorySpaceOptionKey();
    outOptions->addressKey = target_amiga_memoryAddressOptionKey();
    outOptions->searchKey = target_amiga_memorySearchOptionKey();
    return 1;
}

static int
target_amiga_coreOptionsHideOptionKey(const char *key)
{
    if (!key) {
        return 0;
    }
    return strcmp(key, "puae_joyport") == 0 ? 1 : 0;
}

static int
target_amiga_coreOptionsIsSyntheticOptionKey(const char *key)
{
    if (!key) {
        return 0;
    }
    if (strcmp(key, target_amiga_mouseCaptureOptionKey()) == 0) {
        return 1;
    }
    for (size_t i = 0; i < 2; ++i) {
        const char *portKey = target_amiga_portDeviceOptionKeyAt(i);
        if (portKey && strcmp(key, portKey) == 0) {
            return 1;
        }
    }
    return 0;
}

static size_t
target_amiga_coreOptionsSyntheticDefCount(void)
{
    return 3;
}

static const struct retro_core_option_v2_definition *
target_amiga_coreOptionsSyntheticDefAt(size_t index)
{
    static const struct retro_core_option_v2_definition defs[] = {
        {
            .key = "e9k_debugger_amiga_mouse_capture",
            .desc = "Capture Mouse on Click",
            .default_value = "disabled",
            .values = {
                { "enabled", "Enabled" },
                { "disabled", "Disabled" },
                { NULL, NULL }
            }
        },
        {
            .key = "e9k_debugger_amiga_port1_device",
            .desc = "Port 1 Device",
            .default_value = "automatic",
            .values = {
                { "automatic", "Automatic" },
                { "joystick", "Joystick" },
                { "cd32pad", "CD32 Pad" },
                { "mouse", "Mouse" },
                { NULL, NULL }
            }
        },
        {
            .key = "e9k_debugger_amiga_port2_device",
            .desc = "Port 2 Device",
            .default_value = "automatic",
            .values = {
                { "automatic", "Automatic" },
                { "joystick", "Joystick" },
                { "cd32pad", "CD32 Pad" },
                { "mouse", "Mouse" },
                { NULL, NULL }
            }
        }
    };
    if (index >= (sizeof(defs) / sizeof(defs[0]))) {
        return NULL;
    }
    return &defs[index];
}

static size_t
target_amiga_coreOptionsMapDebuggerInputSpecIndex(size_t specIndex)
{
    if (specIndex == 4) {
        return 5;
    }
    if (specIndex == 5) {
        return 4;
    }
    if (specIndex == 16) {
        return 17;
    }
    if (specIndex == 17) {
        return 16;
    }
    return specIndex;
}

static const char *
target_amiga_coreOptionsDebuggerInputLabel(const char *optionKey, const char *defaultLabel)
{
    if (!optionKey || !*optionKey) {
        return defaultLabel;
    }
    if (strcmp(optionKey, "e9k_debugger_input_button_a") == 0) {
        return "Button 2";
    }
    if (strcmp(optionKey, "e9k_debugger_input_button_b") == 0) {
        return "Button 1";
    }
    if (strcmp(optionKey, "e9k_debugger_input_p2_button_a") == 0) {
        return "P2 Button 2";
    }
    if (strcmp(optionKey, "e9k_debugger_input_p2_button_b") == 0) {
        return "P2 Button 1";
    }
    return defaultLabel;
}

static void
target_amiga_setConfigDefaults(e9k_system_config_t *config)
{
    if (!config) {
        return;
    }
    snprintf(config->amiga.libretro.systemDir, sizeof(config->amiga.libretro.systemDir), "./system");
    snprintf(config->amiga.libretro.saveDir, sizeof(config->amiga.libretro.saveDir), "./saves");
    config->amiga.libretro.sourceDir[0] = '\0';
    snprintf(config->amiga.libretro.toolchainPrefix, sizeof(config->amiga.libretro.toolchainPrefix), "m68k-amigaos-");
    config->amiga.libretro.audioBufferMs = 250;
    config->amiga.libretro.exePath[0] = '\0';
}

static void
target_amiga_refreshFloppyRecentsDropdowns(target_amiga_romselect_extra_t *extra)
{
    if (!extra) {
        return;
    }
    if (extra->df0Select) {
        target_amiga_refreshFloppyRecentsDropdown(extra->df0Select, 0);
    }
    if (extra->df1Select) {
        target_amiga_refreshFloppyRecentsDropdown(extra->df1Select, 1);
    }
}


static void
target_amiga_applyActiveSettingsToCurrentSystem(void)
{
    strutil_strlcpy(debugger.config.amiga.libretro.exePath,
                      sizeof(debugger.config.amiga.libretro.exePath),
                      rom_config_activeElfPath);
    strutil_strlcpy(debugger.config.amiga.libretro.sourceDir,
                      sizeof(debugger.config.amiga.libretro.sourceDir),
                      rom_config_activeSourceDir);
    strutil_strlcpy(debugger.config.amiga.libretro.toolchainPrefix,
                      sizeof(debugger.config.amiga.libretro.toolchainPrefix),
                      rom_config_activeToolchainPrefix);
}

static void
target_amiga_setActiveDefaultsFromCurrentSystem(void)
{
    strutil_strlcpy(rom_config_activeElfPath, sizeof(rom_config_activeElfPath),
                      debugger.config.amiga.libretro.exePath);
    strutil_strlcpy(rom_config_activeSourceDir, sizeof(rom_config_activeSourceDir),
                      debugger.config.amiga.libretro.sourceDir);
    strutil_strlcpy(rom_config_activeToolchainPrefix, sizeof(rom_config_activeToolchainPrefix),
                      debugger.config.amiga.libretro.toolchainPrefix);
}

static int
target_amiga_configMissingPaths(const e9k_amiga_config_t *cfg)
{
    if (!cfg) {
        return 1;
    }
    const char *corePath = target_amiga_defaultCorePath();
    if (!corePath || !*corePath ||
        !cfg->libretro.romPath[0] ||
        !cfg->libretro.systemDir[0] ||
        !cfg->libretro.saveDir[0] ||
        !settings_pathExistsFile(corePath) ||
        !settings_pathHasUaeExtension(cfg->libretro.romPath) ||
        !settings_pathExistsFile(cfg->libretro.romPath) ||
        !settings_pathExistsDir(cfg->libretro.systemDir) ||
        !settings_pathExistsDir(cfg->libretro.saveDir)) {
        return 1;
    }
    if (cfg->libretro.exePath[0] && !settings_pathExistsFile(cfg->libretro.exePath)) {
        return 1;
    }
    if (cfg->libretro.sourceDir[0] && !settings_pathExistsDir(cfg->libretro.sourceDir)) {
        return 1;
    }
    return 0;
}

static int
target_amiga_configIsOk(void)
{
    return target_amiga_configMissingPaths(&debugger.config.amiga) ? 0 : 1;
}

static int
target_amiga_configIsOkForAmiga(const e9k_amiga_config_t *cfg)
{
    return target_amiga_configMissingPaths(cfg) ? 0 : 1;
}

static int
target_amiga_restartNeededForAmiga(const e9k_amiga_config_t *before, const e9k_amiga_config_t *after)
{
    if (!before || !after) {
        return 1;
    }
    int romChanged = strcmp(before->libretro.romPath, after->libretro.romPath) != 0;
    int elfChanged = strcmp(before->libretro.exePath, after->libretro.exePath) != 0;
    int toolchainChanged = strcmp(before->libretro.toolchainPrefix, after->libretro.toolchainPrefix) != 0;
    int biosChanged = strcmp(before->libretro.systemDir, after->libretro.systemDir) != 0;
    int savesChanged = strcmp(before->libretro.saveDir, after->libretro.saveDir) != 0;
    int sourceChanged = strcmp(before->libretro.sourceDir, after->libretro.sourceDir) != 0;
    int audioBefore = settings_audioBufferNormalized(before->libretro.audioBufferMs);
    int audioAfter = settings_audioBufferNormalized(after->libretro.audioBufferMs);
    int audioChanged = audioBefore != audioAfter;
    return romChanged || elfChanged || toolchainChanged || biosChanged || savesChanged || sourceChanged || audioChanged;
}

static int
target_amiga_needsRestart(void)
{
    int  configChanged = target_amiga_restartNeededForAmiga(&debugger.config.amiga, &debugger.settingsEdit.amiga);
    if (amiga_uaeHasRestartRequiredDirty()) {
      configChanged = 1;
    }
    if (settings_coreOptionsNeedsRestart()) {
      if (target == target_amiga() && libretro_host_isRunning()) {
        configChanged = 1;
      }
    }
    int okBefore = target_amiga_configIsOkForAmiga(&debugger.config.amiga);
    int okAfter = target_amiga_configIsOkForAmiga(&debugger.settingsEdit.amiga);

    return configChanged || target_amiga_settingsMediaNeedsRestart || (!okBefore && okAfter);
}

static int
target_amiga_settingsSaveButtonDisabled(void)
{
  const char *uaePath = debugger.settingsEdit.amiga.libretro.romPath;
  return (!uaePath || !uaePath[0] || !settings_pathHasUaeExtension(uaePath)) ? 1 : 0;
}

static void
target_amiga_validateSettings(void)
{
    if (debugger.settingsEdit.amiga.libretro.audioBufferMs <= 0) {
        debugger.settingsEdit.amiga.libretro.audioBufferMs = 50;
    }
    const char *uaePath = debugger.settingsEdit.amiga.libretro.romPath;
    if (uaePath && *uaePath) {
        if (!settings_pathHasUaeExtension(uaePath)) {
            e9ui_showTransientMessage("UAE CONFIG MUST END WITH .uae");
            return;
        }
        if (!amiga_uaeWriteUaeOptionsToFile(uaePath)) {
            e9ui_showTransientMessage("UAE SAVE FAILED");
            return;
        }
    }
    target_amiga_captureFloppyRecentsFromUae();
    const char *saveDir = debugger.settingsEdit.amiga.libretro.saveDir[0] ?
        debugger.settingsEdit.amiga.libretro.saveDir : debugger.settingsEdit.amiga.libretro.systemDir;
    const char *romPath = debugger.settingsEdit.amiga.libretro.romPath;
    rom_config_saveSettingsForRom(saveDir, romPath, target_amiga(),
                                  debugger.settingsEdit.amiga.libretro.exePath,
                                  debugger.settingsEdit.amiga.libretro.sourceDir,
                                  debugger.settingsEdit.amiga.libretro.toolchainPrefix);
}

static void
target_amiga_settingsDefault(void)
{  
    char uaePath[PATH_MAX];
    char elfPath[PATH_MAX];
    settings_copyPath(uaePath, sizeof(uaePath), debugger.settingsEdit.amiga.libretro.romPath);
    settings_copyPath(elfPath, sizeof(elfPath), debugger.settingsEdit.amiga.libretro.exePath);
    int audioEnabled = debugger.settingsEdit.amiga.libretro.audioEnabled;
    target_amiga_setConfigDefaults(&debugger.settingsEdit);
    debugger.settingsEdit.amiga.libretro.audioEnabled = audioEnabled;
    settings_copyPath(debugger.settingsEdit.amiga.libretro.romPath, sizeof(debugger.settingsEdit.amiga.libretro.romPath), uaePath);
    settings_copyPath(debugger.settingsEdit.amiga.libretro.exePath, sizeof(debugger.settingsEdit.amiga.libretro.exePath), elfPath);
    amiga_uaeClearPuaeOptions();
    if (debugger.settingsEdit.amiga.libretro.romPath[0]) {
      amiga_uaeLoadUaeOptions(debugger.settingsEdit.amiga.libretro.romPath);
    }
}

static void
target_amiga_applyRomConfigForSelection(settings_romselect_state_t *st, const char** saveDirP, const char** romPathP)
{
  (void)st;
    *saveDirP = debugger.settingsEdit.amiga.libretro.saveDir[0] ?
    debugger.settingsEdit.amiga.libretro.saveDir : debugger.settingsEdit.amiga.libretro.systemDir;
    *romPathP = debugger.settingsEdit.amiga.libretro.romPath;

}

static void
target_amiga_settingsSetConfigPaths(int hasElf, const char *elfPath, int hasSource, const char *sourceDir, int hasToolchain, const char *toolchainPrefix)
{
    settings_config_setPath(debugger.settingsEdit.amiga.libretro.exePath, PATH_MAX, hasElf ? elfPath : "");
    settings_config_setPath(debugger.settingsEdit.amiga.libretro.sourceDir, PATH_MAX, hasSource ? sourceDir : "");
    settings_config_setValue(debugger.settingsEdit.amiga.libretro.toolchainPrefix, PATH_MAX, hasToolchain ? toolchainPrefix : "");
}


static const char *
target_amiga_defaultCorePath(void)
{
  static char corePath[PATH_MAX];
  static char fallbackPath[PATH_MAX];
  char relPath[PATH_MAX];
#if defined(_WIN32)
  const char *ext = "dll";
#elif defined(__APPLE__)
  const char *ext = "dylib";
#else
  const char *ext = "so";
#endif
  snprintf(relPath, sizeof(relPath), "system/ami9000.%s", ext);
  if (file_getAssetPath(relPath, corePath, sizeof(corePath))) {
    return corePath;
  }
  snprintf(fallbackPath, sizeof(fallbackPath), "./system/ami9000.%s", ext);
  return fallbackPath;
  
}

static void
target_amiga_settingsRomPathChanged(settings_romselect_state_t* st)  
{
  amiga_uaeLoadUaeOptions(st ? st->romPath : NULL);
  target_amiga_captureSettingsMediaBaseline();
  target_amiga_romselect_extra_t *extra = st ? (target_amiga_romselect_extra_t *)st->targetUser : NULL;
  target_amiga_refreshFloppyRecentsDropdowns(extra);
  if (extra && extra->df0Select) {
    const char *df0 = amiga_uaeGetFloppyPath(0);
    e9ui_fileSelect_setText(extra->df0Select, df0 ? df0 : "");
  }
  if (extra && extra->df1Select) {
    const char *df1 = amiga_uaeGetFloppyPath(1);
    e9ui_fileSelect_setText(extra->df1Select, df1 ? df1 : "");
  }
  if (extra && extra->dh0FolderSelect) {
    const char *dh0Folder = amiga_uaeGetHardDriveFolderPathForUnit(0);
    e9ui_fileSelect_setText(extra->dh0FolderSelect, dh0Folder ? dh0Folder : "");
  }
  if (extra && extra->dh0HdfSelect) {
    const char *dh0Hdf = amiga_uaeGetHardDriveHdfPathForUnit(0);
    e9ui_fileSelect_setText(extra->dh0HdfSelect, dh0Hdf ? dh0Hdf : "");
  }
  if (extra && extra->dh1FolderSelect) {
    const char *dh1Folder = amiga_uaeGetHardDriveFolderPathForUnit(1);
    e9ui_fileSelect_setText(extra->dh1FolderSelect, dh1Folder ? dh1Folder : "");
  }
  if (extra && extra->dh1HdfSelect) {
    const char *dh1Hdf = amiga_uaeGetHardDriveHdfPathForUnit(1);
    e9ui_fileSelect_setText(extra->dh1HdfSelect, dh1Hdf ? dh1Hdf : "");
  }
  if (extra && extra->toolchainPresetState) {
    target_amiga_toolchainPresetSync(extra->toolchainPresetState,
                                     NULL,
                                     debugger.settingsEdit.amiga.libretro.toolchainPrefix);
  }
  settings_updateSaveLabel();
}

static void
target_amiga_settingsFolderChanged(void)
{

}

static void
target_amiga_settingsCoreChanged(void)
{
    amiga_uaeLoadUaeOptions(debugger.settingsEdit.amiga.libretro.romPath);
    neogeo_coreOptionsClear();
}

static void
target_amiga_settingsLoadOptions(e9k_system_config_t * st)
{
  (void)st;
  amiga_uaeLoadUaeOptions(debugger.settingsEdit.amiga.libretro.romPath);
  target_amiga_captureSettingsMediaBaseline();
}


const e9k_libretro_config_t*
target_amiga_selectLibretroConfig(const e9k_system_config_t *cfg)
{
  return &cfg->amiga.libretro;
}


static int
target_amiga_coreOptionsHasGeneral(const core_options_modal_state_t *st)
{
  if (!st || !st->defs) {
    return 0;
  }
  for (size_t i = 0; i < st->defCount; ++i) {
    const struct retro_core_option_v2_definition *def = &st->defs[i];
    if (st->targetCoreRunning && def && def->key) {
      if (!libretro_host_isCoreOptionVisible(def->key)) {
	continue;
      }
    }
    const char *defCat = def ? def->category_key : NULL;
    if (!defCat || !*defCat) {
      if (def && def->key) {
	if (strcmp(def->key, "puae_video_options_display") == 0 ||
	    strcmp(def->key, "puae_audio_options_display") == 0 ||
	    strcmp(def->key, "puae_mapping_options_display") == 0 ||
	    strcmp(def->key, "puae_model_options_display") == 0) {
	  return 1;
	}
      }
    }
  }

  return 0;  
}

static int
target_amiga_coreOptionNeedsRestart(const char *key)
{
  static const char *saveOnlyKeys[] = {
    "puae_video_options_display",
    "puae_audio_options_display",
    "puae_mapping_options_display",
    "puae_model_options_display"
  };
  if (!key || !*key) {
    return 1;
  }
  for (size_t i = 0; i < sizeof(saveOnlyKeys) / sizeof(saveOnlyKeys[0]); ++i) {
    if (strcmp(key, saveOnlyKeys[i]) == 0) {
      return 0;
    }
  }
  if (strcmp(key, target_amiga_mouseCaptureOptionKey()) == 0) {
    return 0;
  }
  return 1;
}

static void
target_amiga_coreOptionsSaveClicked(e9ui_context_t *ctx,core_options_modal_state_t *st)
{
  (void)ctx;
  int anyChange = 0;
  int anyRestartChange = 0;
  int anyRomConfigBindingChange = 0;
  
    for (size_t i = 0; i < st->entryCount; ++i) {
        const char *key = st->entries[i].key;
        const char *value = st->entries[i].value;
        if (!key || !*key) {
            continue;
        }
    if (strcmp(key, target_amiga_mouseCaptureOptionKey()) == 0) {
      if (!rom_config_activeInit && (!e9ui || !e9ui->settingsModal)) {
        rom_config_syncActiveFromCurrentSystem();
      }
      const char *existingOverride = target_amiga_getActiveMouseCaptureOverride();
      const char *rawDesiredOverride = (value && *value) ? value : NULL;
      const char *desiredOverride = target_amiga_normalizeMouseCaptureOverrideValue(rawDesiredOverride);
      if (rawDesiredOverride && !desiredOverride) {
        continue;
      }
      if (desiredOverride) {
        const char *defValue = core_options_findDefaultValue(st, key);
        const char *defOverride = target_amiga_normalizeMouseCaptureOverrideValue(defValue);
        if (defOverride && strcmp(defOverride, desiredOverride) == 0) {
          desiredOverride = NULL;
        }
      }
      if ((existingOverride && !desiredOverride) ||
          (!existingOverride && desiredOverride) ||
          (existingOverride && desiredOverride && !core_options_stringsEqual(existingOverride, desiredOverride))) {
        target_amiga_setActiveMouseCaptureOverride(desiredOverride);
        if (desiredOverride && strcmp(desiredOverride, "disabled") == 0) {
          (void)emu_mouseCaptureRelease(ctx);
        }
        anyChange = 1;
        anyRomConfigBindingChange = 1;
      }
      continue;
    }
    for (size_t portIndex = 0; portIndex < 2; ++portIndex) {
      const char *portKey = target_amiga_portDeviceOptionKeyAt(portIndex);
      if (portKey && strcmp(key, portKey) == 0) {
        const char *existingPortDevice = target_amiga_activeRomPortDevice[portIndex][0] ? target_amiga_activeRomPortDevice[portIndex] : NULL;
        const char *desiredPortDevice = (value && *value) ? value : NULL;
        if (desiredPortDevice) {
          const char *defValue = core_options_findDefaultValue(st, key);
          if (defValue && strcmp(defValue, desiredPortDevice) == 0) {
            desiredPortDevice = NULL;
          }
        }
        if ((existingPortDevice && !desiredPortDevice) ||
            (!existingPortDevice && desiredPortDevice) ||
            (existingPortDevice && desiredPortDevice && !core_options_stringsEqual(existingPortDevice, desiredPortDevice))) {
          target_amiga_romConfigSetActiveCustomOptionValue(key, desiredPortDevice);
          anyChange = 1;
          anyRestartChange = 1;
          anyRomConfigBindingChange = 1;
        }
        goto next_entry;
      }
    }
    if (debugger_input_bindings_isOptionKey(key)) {
      const char *existingBinding = rom_config_getActiveInputBindingValue(key);
      const char *desiredBinding = (value && *value) ? value : NULL;
      if (desiredBinding) {
        const char *defValue = core_options_findDefaultValue(st, key);
        if (defValue && strcmp(defValue, desiredBinding) == 0) {
          desiredBinding = NULL;
        }
      }
      if (desiredBinding) {
        if (!existingBinding || !core_options_stringsEqual(existingBinding, desiredBinding)) {
          rom_config_setActiveInputBindingValue(key, desiredBinding);
          anyChange = 1;
          anyRomConfigBindingChange = 1;
        }
      } else if (existingBinding) {
        rom_config_setActiveInputBindingValue(key, NULL);
        anyChange = 1;
        anyRomConfigBindingChange = 1;
      }
      continue;
    }
    const char *defValue = core_options_findDefaultValue(st, key);
    const char *desired = NULL;
    if (!defValue || !value || strcmp(defValue, value) != 0) {
      desired = value ? value : "";
    }
    const char *existing = amiga_uaeGetPuaeOptionValue(key);
    if (!desired) {
      if (!existing) {
	continue;
      }
      amiga_uaeSetPuaeOptionValue(key, NULL);
      anyChange = 1;
      if (target_amiga_coreOptionNeedsRestart(key)) {
        anyRestartChange = 1;
      }
    } else {
      if (existing && core_options_stringsEqual(existing, desired)) {
	continue;
      }
      amiga_uaeSetPuaeOptionValue(key, desired);
      anyChange = 1;
      if (target_amiga_coreOptionNeedsRestart(key)) {
        anyRestartChange = 1;
      }
    }
next_entry:
    ;
  }
  if (anyChange) {
    settings_markCoreOptionsDirtyWithRestart(anyRestartChange);
  }
  if (anyRomConfigBindingChange && (!e9ui || !e9ui->settingsModal)) {
    rom_config_saveCurrentRomSettings();
  }
  settings_refreshSaveLabel();
  if (anyChange) {
    e9ui_showTransientMessage((!e9ui || !e9ui->settingsModal) ? "CORE OPTIONS APPLIED" : "CORE OPTIONS STAGED");
  } else {
    e9ui_showTransientMessage("CORE OPTIONS: NO CHANGES");
  }
  core_options_closeModal();
}


const char*
target_amiga_coreOptionGetValue(const char* key)
{
  const char *value = NULL;
  if (strcmp(key, target_amiga_mouseCaptureOptionKey()) == 0) {
    const char *overrideValue = target_amiga_getActiveMouseCaptureOverride();
    if (overrideValue && *overrideValue) {
      value = overrideValue;
    } else {
      const struct retro_core_option_v2_definition *def = target_amiga_coreOptionsSyntheticDefAt(0);
      value = def ? def->default_value : NULL;
    }
  } else if (strcmp(key, target_amiga_portDeviceOptionKeyAt(0)) == 0) {
    value = target_amiga_activeRomPortDevice[0][0] ? target_amiga_activeRomPortDevice[0] : "automatic";
  } else if (strcmp(key, target_amiga_portDeviceOptionKeyAt(1)) == 0) {
    value = target_amiga_activeRomPortDevice[1][0] ? target_amiga_activeRomPortDevice[1] : "automatic";
  } else if (debugger_input_bindings_isOptionKey(key)) {
    value = rom_config_getActiveInputBindingValue(key);
  } else {
    value = amiga_uaeGetPuaeOptionValue(key);
  }
  return value;
}


static e9k_libretro_config_t *
target_amiga_getLibretroCliConfig(void)
{
  return &debugger.cliConfig.amiga.libretro;
}

static void
target_amiga_onCoreStarted(void)
{
    if (ui_test_isEnabled()) {
        (void)libretro_host_amiga_setDeterministic(1);
    }
}

static void
target_amiga_onDebugExitFromCore(void)
{
    profile_analyseOnExitIfRunning();
    debugger.exitRequested = 1;
}

static void
target_amiga_onDebugSmokeStartFromCore(void)
{
    smoke_test_startFromFrame(debugger.frameCounter);
}

static void
target_amiga_onDebugProfileStartFromCore(void)
{
    profile_startFromDebugWrite();
}


static void target_amiga_onVblank(void) {}

static void
target_amiga_libretroSelectConfig(void)
{
    debugger.libretro.audioBufferMs = debugger.config.amiga.libretro.audioBufferMs;
    debugger.libretro.audioEnabled = debugger.config.amiga.libretro.audioEnabled;
    debugger_copyPath(debugger.libretro.sourceDir, sizeof(debugger.libretro.sourceDir), debugger.config.amiga.libretro.sourceDir);
    debugger_copyPath(debugger.libretro.exePath, sizeof(debugger.libretro.exePath), debugger.config.amiga.libretro.exePath);
    debugger_copyPath(debugger.libretro.toolchainPrefix, sizeof(debugger.libretro.toolchainPrefix), debugger.config.amiga.libretro.toolchainPrefix);
    debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), debugger.config.amiga.libretro.romPath);
    debugger_copyPath(debugger.libretro.systemDir, sizeof(debugger.libretro.systemDir), debugger.config.amiga.libretro.systemDir);
    debugger_copyPath(debugger.libretro.saveDir, sizeof(debugger.libretro.saveDir), debugger.config.amiga.libretro.saveDir);
}

static void
target_amiga_pickElfToolchainPaths(const char** rawElf, const char** toolchainPrefix)
{
  *rawElf = debugger.config.amiga.libretro.exePath;
  *toolchainPrefix = debugger.config.amiga.libretro.toolchainPrefix;
}


static void
target_amiga_applyCoreOptions(void)
{
  target_amiga_configControllerPorts();
  const char *uaePath = debugger.libretro.romPath[0] ? debugger.libretro.romPath : debugger.config.amiga.libretro.romPath;
  if (uaePath && *uaePath) {
    amiga_uaeApplyPuaeOptionsToHost(uaePath);
  }
  libretro_host_setCoreOption("puae_joyport", "joystick");
}

static void
target_amiga_validateAPI(void)
{
  libretro_host_megadrive_unbindApis();
  libretro_host_neogeo_unbindApis();
  if (!libretro_host_amiga_setDebugBaseCallback(debugger_onSetDebugBaseFromCore)) {
    debug_error("debug_base: core does not expose e9k_debug_set_debug_base_callback");
  }
  if (!libretro_host_amiga_setDebugBaseStackCallback(debugger_onPushDebugBaseFromCore)) {
    debug_error("debug_base_stack: core does not expose e9k_debug_set_debug_base_stack_callback");
  }
  if (!libretro_host_amiga_setDebugBreakpointCallback(debugger_onAddBreakpointFromCore)) {
    debug_error("breakpoint: core does not expose e9k_debug_set_debug_breakpoint_callback");
  }
  if (!libretro_host_amiga_setDebugExitCallback(target_amiga_onDebugExitFromCore)) {
    debug_error("exit: core does not expose e9k_debug_set_debug_exit_callback");
  }
  if (!libretro_host_amiga_setDebugSmokeStartCallback(target_amiga_onDebugSmokeStartFromCore)) {
    debug_error("smoke_start: core does not expose e9k_debug_set_debug_smoke_start_callback");
  }
  if (!libretro_host_amiga_setDebugProfileStartCallback(target_amiga_onDebugProfileStartFromCore)) {
    debug_error("profile_start: core does not expose e9k_debug_set_debug_profile_start_callback");
  }
  if (!libretro_host_amiga_setDebugArgs(debugger.debugArgs, (size_t)debugger.debugArgCount)) {
    debug_error("debug_args: core does not expose e9k_debug_set_debug_args");
  }
  int *debugDma = NULL;
  if (libretro_host_amiga_getDmaAddr(&debugDma)) {
    debugger.amigaDebug.debugDma = debugDma;
  }
  int *debugCopper = NULL;
  if (libretro_host_amiga_getCopperAddr(&debugCopper)) {
    debugger.amigaDebug.debugCopper = debugCopper;
  }
}


static int
target_amiga_audioEnabled(void)
{
  return debugger.config.amiga.libretro.audioEnabled;
}

static void
target_amiga_audioEnable(int enabled)
{
  debugger.config.amiga.libretro.audioEnabled = enabled;
}

static SDL_Texture *
target_amiga_getBadgeTexture(SDL_Renderer *renderer, target_iface_t* t, int* outW, int* outH)
{
  if (t->badge && t->badgeRenderer != renderer) {
    SDL_DestroyTexture(t->badge);
    t->badge = NULL;
  }
  t->badgeRenderer = renderer;
  if (!t->badge) {
    t->badge = system_badge_loadTexture(renderer, "assets/amiga.png", &t->badgeW, &t->badgeH);
  }

  if (t->badge) {
    if (outW) {
      *outW = t->badgeW;
    }
    if (outH) {
      *outH = t->badgeH;
    }
    return t->badge;
  }
  
  return t->badge;
}

static void
target_amiga_configControllerPorts(void)
{
  for (unsigned port = 0; port < 2; ++port) {
    const char *value = target_amiga_activeRomPortDevice[port][0] ? target_amiga_activeRomPortDevice[port] : "automatic";
    unsigned device = RETRO_DEVICE_JOYPAD;
    if (strcmp(value, "joystick") == 0) {
      device = RETRO_DEVICE_PUAE_JOYPAD;
    } else if (strcmp(value, "cd32pad") == 0) {
      device = RETRO_DEVICE_PUAE_CD32PAD;
    } else if (strcmp(value, "mouse") == 0) {
      device = RETRO_DEVICE_NONE;
    }
    libretro_host_setControllerPortDevice(port, device);
  }
}


static  int
target_amiga_controllerMapButton(SDL_GameControllerButton button, unsigned *outId)
{
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: *outId = RETRO_DEVICE_ID_JOYPAD_B; return 1;
  case SDL_CONTROLLER_BUTTON_B: *outId = RETRO_DEVICE_ID_JOYPAD_A; return 1;
  case SDL_CONTROLLER_BUTTON_X: *outId = RETRO_DEVICE_ID_JOYPAD_Y; return 1;
  case SDL_CONTROLLER_BUTTON_Y: *outId = RETRO_DEVICE_ID_JOYPAD_X; return 1;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: *outId = RETRO_DEVICE_ID_JOYPAD_L; return 1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: *outId = RETRO_DEVICE_ID_JOYPAD_R; return 1;
  case SDL_CONTROLLER_BUTTON_START: *outId = RETRO_DEVICE_ID_JOYPAD_START; return 1;
  case SDL_CONTROLLER_BUTTON_BACK: *outId = RETRO_DEVICE_ID_JOYPAD_SELECT; return 1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: *outId = RETRO_DEVICE_ID_JOYPAD_UP; return 1;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: *outId = RETRO_DEVICE_ID_JOYPAD_DOWN; return 1;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: *outId = RETRO_DEVICE_ID_JOYPAD_LEFT; return 1;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: *outId = RETRO_DEVICE_ID_JOYPAD_RIGHT; return 1;
  default:
    break;
  }
  return 0;
}

static void
target_amiga_settingsFloppyChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    int drive = (int)(intptr_t)user;
    if (comp) {
        const char *selectedValue = e9ui_fileSelect_getSelectedValue(comp);
        if (selectedValue &&
            strcmp(selectedValue, target_amiga_floppyRecentClearValue) == 0 &&
            text &&
            (strcmp(text, target_amiga_floppyRecentClearLabel) == 0 ||
             strcmp(text, target_amiga_floppyRecentClearValue) == 0)) {
            const char *currentPath = amiga_uaeGetFloppyPath(drive);
            if (drive >= 0 && drive <= 1) {
                for (int i = 0; i < TARGET_AMIGA_FLOPPY_RECENTS_MAX; ++i) {
                    target_amiga_setActiveFloppyRecentValue(drive, (size_t)i, NULL);
                }
                target_amiga_refreshFloppyRecentsDropdown(comp, drive);
            }
            e9ui_fileSelect_setText(comp, currentPath ? currentPath : "");
            return;
        }
    }
    const char *path = text ? text : "";
    amiga_uaeSetFloppyPath(drive, path);
    if (target == target_amiga() && libretro_host_isRunning()) {
        if (libretro_host_amiga_setFloppyPath(drive, path)) {
            amiga_uaeClearFloppyDirty(drive);
        }
    }
    target_amiga_updateSettingsMediaRestartRequirement();
}

static void
target_amiga_settingsHardDriveFolderChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    target_amiga_romselect_extra_t *extra = (target_amiga_romselect_extra_t *)user;
    if (extra && extra->updatingMedia) {
        return;
    }
    int unit = (extra && comp == extra->dh1FolderSelect) ? 1 : 0;
    e9ui_component_t *hdfSelect = unit == 1 && extra ? extra->dh1HdfSelect : (extra ? extra->dh0HdfSelect : NULL);
    amiga_uaeSetHardDriveFolderPathForUnit(unit, text ? text : "");
    if (text && text[0] && hdfSelect) {
        extra->updatingMedia = 1;
        amiga_uaeSetHardDriveHdfPathForUnit(unit, "");
        e9ui_fileSelect_setText(hdfSelect, "");
        extra->updatingMedia = 0;
    }
    target_amiga_updateSettingsMediaRestartRequirement();
}

static void
target_amiga_settingsHardDriveHdfChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    target_amiga_romselect_extra_t *extra = (target_amiga_romselect_extra_t *)user;
    if (extra && extra->updatingMedia) {
        return;
    }
    int unit = (extra && comp == extra->dh1HdfSelect) ? 1 : 0;
    e9ui_component_t *folderSelect = unit == 1 && extra ? extra->dh1FolderSelect : (extra ? extra->dh0FolderSelect : NULL);
    amiga_uaeSetHardDriveHdfPathForUnit(unit, text ? text : "");
    if (text && text[0] && folderSelect) {
        extra->updatingMedia = 1;
        amiga_uaeSetHardDriveFolderPathForUnit(unit, "");
        e9ui_fileSelect_setText(folderSelect, "");
        extra->updatingMedia = 0;
    }
    target_amiga_updateSettingsMediaRestartRequirement();
}

static int
target_amiga_isVasmToolchainPrefix(const char *prefix)
{
    return (prefix && strcmp(prefix, target_amiga_toolchainPrefixVasm) == 0) ? 1 : 0;
}

static int
target_amiga_isGccToolchainPrefix(const char *prefix)
{
    return (prefix && strcmp(prefix, target_amiga_toolchainPrefixGcc) == 0) ? 1 : 0;
}

static void
target_amiga_toolchainPresetSync(target_amiga_toolchain_preset_state_t *st, e9ui_context_t *ctx, const char *prefix)
{
    if (!st) {
        return;
    }
    int selectVasm = target_amiga_isVasmToolchainPrefix(prefix);
    int selectGcc = target_amiga_isGccToolchainPrefix(prefix);
    if (!selectVasm && !selectGcc) {
        selectGcc = 1;
    }
    st->updating = 1;
    if (st->vasmCheckbox) {
        e9ui_checkbox_setSelected(st->vasmCheckbox, selectVasm ? 1 : 0, ctx);
    }
    if (st->gccCheckbox) {
        e9ui_checkbox_setSelected(st->gccCheckbox, selectGcc ? 1 : 0, ctx);
    }
    st->updating = 0;
}

static void
target_amiga_toolchainPrefixChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    target_amiga_toolchain_preset_state_t *st = (target_amiga_toolchain_preset_state_t *)user;
    settings_toolchainPrefixChanged(ctx, comp, text, debugger.settingsEdit.amiga.libretro.toolchainPrefix);
    target_amiga_toolchainPresetSync(st, ctx, text);
}

static void
target_amiga_toolchainPresetApply(target_amiga_toolchain_preset_state_t *st, e9ui_context_t *ctx, const char *prefix)
{
    if (!st || !st->toolchainTextbox || !prefix) {
        return;
    }
    e9ui_labeled_textbox_setText(st->toolchainTextbox, prefix);
    settings_toolchainPrefixChanged(ctx,
                                    st->toolchainTextbox,
                                    prefix,
                                    debugger.settingsEdit.amiga.libretro.toolchainPrefix);
}

static void
target_amiga_toolchainPresetChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    target_amiga_toolchain_preset_state_t *st = (target_amiga_toolchain_preset_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    st->updating = 1;
    if (self == st->vasmCheckbox) {
        if (selected) {
            if (st->gccCheckbox) {
                e9ui_checkbox_setSelected(st->gccCheckbox, 0, ctx);
            }
            target_amiga_toolchainPresetApply(st, ctx, target_amiga_toolchainPrefixVasm);
        } else if (st->vasmCheckbox) {
            e9ui_checkbox_setSelected(st->vasmCheckbox, 1, ctx);
        }
    } else if (self == st->gccCheckbox) {
        if (selected) {
            if (st->vasmCheckbox) {
                e9ui_checkbox_setSelected(st->vasmCheckbox, 0, ctx);
            }
            target_amiga_toolchainPresetApply(st, ctx, target_amiga_toolchainPrefixGcc);
        } else if (st->gccCheckbox) {
            e9ui_checkbox_setSelected(st->gccCheckbox, 1, ctx);
        }
    }
    st->updating = 0;
}

static void
target_amiga_settingsBuildModal(e9ui_context_t *ctx, target_settings_modal_t *out)
{
    if (!out || !ctx) {
        return;
    }
    out->body = NULL;
    out->footerWarning = NULL;

    const char *romExts[] = { "*.uae" };
    const char *floppyExts[] = { "*.adf", "*.adz", "*.fdi", "*.dms", "*.ipf", "*.raw", ".7z" };
    const char *hdfExts[] = { "*.hdf", "*.hdz" };
    const char *elfExts[] = { "*.elf", "*.txt" };

    settings_romselect_state_t *romState = (settings_romselect_state_t *)alloc_calloc(1, sizeof(*romState));
    if (romState) {
        romState->romPath = debugger.settingsEdit.amiga.libretro.romPath;
        romState->romFolder = NULL;
    }
    target_amiga_romselect_extra_t *extra = (target_amiga_romselect_extra_t *)alloc_calloc(1, sizeof(*extra));
    settings_syncActiveRomConfigForSelection();

    e9ui_component_t *fsRom = e9ui_fileSelect_make("UAE CONFIG", 120, 600, "...", romExts, 1, E9UI_FILESELECT_FILE);
    if (fsRom) {
        e9ui_fileSelect_enableNewButton(fsRom, "NEW");
        e9ui_fileSelect_setValidate(fsRom, settings_validateUaeConfig, NULL);
        e9ui_fileSelect_setText(fsRom, debugger.settingsEdit.amiga.libretro.romPath);
        e9ui_fileSelect_setOnChange(fsRom, settings_romPathChanged, romState);
    }

    e9ui_component_t *fsDf0 = e9ui_fileSelect_make("DF0", 120, 600, "...", floppyExts, 6, E9UI_FILESELECT_FILE);
    e9ui_component_t *fsDf1 = e9ui_fileSelect_make("DF1", 120, 600, "...", floppyExts, 6, E9UI_FILESELECT_FILE);
    e9ui_component_t *fsDh0Folder = e9ui_fileSelect_make("DH0 FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsDh0Hdf = e9ui_fileSelect_make("DH0 HDF", 120, 600, "...", hdfExts, 2, E9UI_FILESELECT_FILE);
    e9ui_component_t *fsDh1Folder = e9ui_fileSelect_make("DH1 FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsDh1Hdf = e9ui_fileSelect_make("DH1 HDF", 120, 600, "...", hdfExts, 2, E9UI_FILESELECT_FILE);

    if (fsDf0) {
        const char *df0 = amiga_uaeGetFloppyPath(0);
        e9ui_fileSelect_setAllowEmpty(fsDf0, 1);
        e9ui_fileSelect_setText(fsDf0, df0 ? df0 : "");
        e9ui_fileSelect_setOnChange(fsDf0, target_amiga_settingsFloppyChanged, (void *)(intptr_t)0);
        target_amiga_refreshFloppyRecentsDropdown(fsDf0, 0);
    }
    if (fsDf1) {
        const char *df1 = amiga_uaeGetFloppyPath(1);
        e9ui_fileSelect_setAllowEmpty(fsDf1, 1);
        e9ui_fileSelect_setText(fsDf1, df1 ? df1 : "");
        e9ui_fileSelect_setOnChange(fsDf1, target_amiga_settingsFloppyChanged, (void *)(intptr_t)1);
        target_amiga_refreshFloppyRecentsDropdown(fsDf1, 1);
    }
    if (fsDh0Folder) {
        const char *dh0Folder = amiga_uaeGetHardDriveFolderPathForUnit(0);
        e9ui_fileSelect_setAllowEmpty(fsDh0Folder, 1);
        e9ui_fileSelect_setText(fsDh0Folder, dh0Folder ? dh0Folder : "");
        e9ui_fileSelect_setOnChange(fsDh0Folder, target_amiga_settingsHardDriveFolderChanged, extra);
    }
    if (fsDh0Hdf) {
        const char *dh0Hdf = amiga_uaeGetHardDriveHdfPathForUnit(0);
        e9ui_fileSelect_setAllowEmpty(fsDh0Hdf, 1);
        e9ui_fileSelect_setText(fsDh0Hdf, dh0Hdf ? dh0Hdf : "");
        e9ui_fileSelect_setOnChange(fsDh0Hdf, target_amiga_settingsHardDriveHdfChanged, extra);
    }
    if (fsDh1Folder) {
        const char *dh1Folder = amiga_uaeGetHardDriveFolderPathForUnit(1);
        e9ui_fileSelect_setAllowEmpty(fsDh1Folder, 1);
        e9ui_fileSelect_setText(fsDh1Folder, dh1Folder ? dh1Folder : "");
        e9ui_fileSelect_setOnChange(fsDh1Folder, target_amiga_settingsHardDriveFolderChanged, extra);
    }
    if (fsDh1Hdf) {
        const char *dh1Hdf = amiga_uaeGetHardDriveHdfPathForUnit(1);
        e9ui_fileSelect_setAllowEmpty(fsDh1Hdf, 1);
        e9ui_fileSelect_setText(fsDh1Hdf, dh1Hdf ? dh1Hdf : "");
        e9ui_fileSelect_setOnChange(fsDh1Hdf, target_amiga_settingsHardDriveHdfChanged, extra);
    }

    e9ui_component_t *fsElf = e9ui_fileSelect_make("EXE", 120, 600, "...", elfExts, (int)countof(elfExts), E9UI_FILESELECT_FILE);
    if (fsElf) {
        e9ui_fileSelect_setAllowEmpty(fsElf, 1);
        e9ui_fileSelect_setText(fsElf, debugger.settingsEdit.amiga.libretro.exePath);
        e9ui_fileSelect_setOnChange(fsElf, settings_pathChanged, debugger.settingsEdit.amiga.libretro.exePath);
    }

    target_amiga_toolchain_preset_state_t *toolchainPresetState =
        (target_amiga_toolchain_preset_state_t *)alloc_calloc(1, sizeof(*toolchainPresetState));
    e9ui_component_t *ltToolchain = e9ui_labeled_textbox_make("TOOLCHAIN PREFIX",
                                                              120,
                                                              0,
                                                              target_amiga_toolchainPrefixChanged,
                                                              toolchainPresetState);
    if (ltToolchain) {
        e9ui_labeled_textbox_setText(ltToolchain, debugger.settingsEdit.amiga.libretro.toolchainPrefix);
        if (toolchainPresetState) {
            toolchainPresetState->toolchainTextbox = ltToolchain;
        }
    }

    e9ui_component_t *toolchainVasmCheckbox = NULL;
    e9ui_component_t *toolchainGccCheckbox = NULL;
    e9ui_component_t *toolchainRow = NULL;
    e9ui_component_t *toolchainRowCenter = NULL;
    if (toolchainPresetState) {
        toolchainVasmCheckbox = e9ui_checkbox_make("vasm", 0, target_amiga_toolchainPresetChanged, toolchainPresetState);
        toolchainGccCheckbox = e9ui_checkbox_make("gcc", 1, target_amiga_toolchainPresetChanged, toolchainPresetState);
        toolchainPresetState->vasmCheckbox = toolchainVasmCheckbox;
        toolchainPresetState->gccCheckbox = toolchainGccCheckbox;
        target_amiga_toolchainPresetSync(toolchainPresetState, ctx, debugger.settingsEdit.amiga.libretro.toolchainPrefix);
    }

    if (ltToolchain && toolchainVasmCheckbox && toolchainGccCheckbox) {
        int vasmWidth = 0;
        int gccWidth = 0;
        e9ui_checkbox_measure(toolchainVasmCheckbox, ctx, &vasmWidth, NULL);
        e9ui_checkbox_measure(toolchainGccCheckbox, ctx, &gccWidth, NULL);
        int gap = e9ui_scale_px(ctx, 10);
        if (gap <= 0) {
            gap = 10;
        }
        toolchainRow = e9ui_hstack_make();
        if (toolchainRow) {
            e9ui_hstack_addFlex(toolchainRow, ltToolchain);
            e9ui_hstack_addFixed(toolchainRow, e9ui_spacer_make(gap), gap);
            e9ui_hstack_addFixed(toolchainRow, toolchainVasmCheckbox, vasmWidth);
            e9ui_hstack_addFixed(toolchainRow, e9ui_spacer_make(gap), gap);
            e9ui_hstack_addFixed(toolchainRow, toolchainGccCheckbox, gccWidth);
        }
        if (toolchainRow) {
            toolchainRowCenter = e9ui_center_make(toolchainRow);
            if (toolchainRowCenter) {
                e9ui_center_setSize(toolchainRowCenter, 600, 0);
            }
        }
    }

    e9ui_component_t *fsBios = e9ui_fileSelect_make("KICKSTART FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsSaves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsSource = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);

    if (fsBios) {
        e9ui_fileSelect_setText(fsBios, debugger.settingsEdit.amiga.libretro.systemDir);
        e9ui_fileSelect_setOnChange(fsBios, settings_pathChanged, debugger.settingsEdit.amiga.libretro.systemDir);
    }
    if (fsSaves) {
        e9ui_fileSelect_setText(fsSaves, debugger.settingsEdit.amiga.libretro.saveDir);
        e9ui_fileSelect_setOnChange(fsSaves, settings_pathChanged, debugger.settingsEdit.amiga.libretro.saveDir);
    }
    if (fsSource) {
        e9ui_fileSelect_setAllowEmpty(fsSource, 1);
        e9ui_fileSelect_setText(fsSource, debugger.settingsEdit.amiga.libretro.sourceDir);
        e9ui_fileSelect_setOnChange(fsSource, settings_pathChanged, debugger.settingsEdit.amiga.libretro.sourceDir);
    }
    e9ui_component_t *ltAudio = e9ui_labeled_textbox_make("AUDIO BUFFER MS",
                                                          120,
                                                          600,
                                                          settings_audioChanged,
                                                          &debugger.settingsEdit.amiga.libretro.audioBufferMs);
    if (ltAudio) {
        char buf[32];
        int audioValue = debugger.settingsEdit.amiga.libretro.audioBufferMs;
        if (audioValue > 0) {
            snprintf(buf, sizeof(buf), "%d", audioValue);
            e9ui_labeled_textbox_setText(ltAudio, buf);
        } else {
            e9ui_labeled_textbox_setText(ltAudio, "");
        }
        e9ui_component_t *tbComp = e9ui_labeled_textbox_getTextbox(ltAudio);
        if (tbComp) {
            e9ui_textbox_setNumericOnly(tbComp, 1);
        }
    }

    if (extra) {
        extra->df0Select = fsDf0;
        extra->df1Select = fsDf1;
        extra->dh0FolderSelect = fsDh0Folder;
        extra->dh0HdfSelect = fsDh0Hdf;
        extra->dh1FolderSelect = fsDh1Folder;
        extra->dh1HdfSelect = fsDh1Hdf;
        extra->toolchainPresetState = toolchainPresetState;
        if (romState) {
            romState->targetUser = extra;
        }
    }

    if (romState) {
        romState->romSelect = fsRom;
        romState->elfSelect = fsElf;
        romState->sourceSelect = fsSource;
        romState->toolchainSelect = ltToolchain;
        settings_romSelectUpdateAllowEmpty(romState);
        settings_romSelectRefreshRecents(romState);
    }

    e9ui_component_t *body = e9ui_stack_makeVertical();
    if (body) {
        int first = 1;
        if (fsRom) {
            e9ui_stack_addFixed(body, fsRom);
            first = 0;
        }
        if (fsDf0) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsDf0);
            first = 0;
        }
        if (fsDf1) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsDf1);
            first = 0;
        }
        if (fsDh0Folder) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsDh0Folder);
            first = 0;
        }
        if (fsDh0Hdf) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsDh0Hdf);
            first = 0;
        }
        if (fsDh1Folder) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsDh1Folder);
            first = 0;
        }
        if (fsDh1Hdf) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsDh1Hdf);
            first = 0;
        }
        if (fsElf) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsElf);
            first = 0;
        }
        if (toolchainRowCenter) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, toolchainRowCenter);
            first = 0;
        } else if (ltToolchain) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, ltToolchain);
            first = 0;
        }
        if (fsSource) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsSource);
            first = 0;
        }
        if (fsBios) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsBios);
            first = 0;
        }
        if (fsSaves) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsSaves);
            first = 0;
        }
        if (ltAudio) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, ltAudio);
            first = 0;
        }
    }
    out->body = body;
    out->footerWarning = settings_uaeExtensionWarning_make();
}

static void
target_amiga_settingsClearOptions(void)
{
    amiga_uaeClearPuaeOptions();
}

static int
target_amiga_memoryGetLimits(uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    (void)outMinAddr;
    (void)outMaxAddr;
    return 0;
}

static int
target_amiga_memoryGetSpaces(target_memory_space_t *outSpaces, size_t cap, size_t *outCount)
{
    if (outCount) {
        *outCount = 1;
    }
    if (!outSpaces || cap == 0) {
        return 1;
    }
    outSpaces[0] = (target_memory_space_t){
        .value = "main",
        .label = "Main",
        .minAddr = 0x00000000u,
        .maxAddr = 0x00ffffffu,
        .addressDigits = 6,
        .processorMemory = 0,
        .processorId = 0
    };
    return 1;
}

static int
target_amiga_memoryTrackGetRanges(target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    const struct retro_memory_descriptor *descriptors = NULL;
    size_t descriptorCount = libretro_host_getMemoryMapDescriptors(&descriptors);
    size_t write = 0;

    if (descriptorCount > 0 && descriptors) {
        for (size_t i = 0; i < descriptorCount; ++i) {
            const struct retro_memory_descriptor *desc = &descriptors[i];
            uint64_t start = 0;
            uint64_t len = 0;

            if (!(desc->flags & RETRO_MEMDESC_SYSTEM_RAM) ||
                !desc->ptr ||
                desc->len == 0 ||
                desc->select != 0 ||
                desc->disconnect != 0) {
                continue;
            }

            start = (uint64_t)desc->start;
            len = (uint64_t)desc->len;
            if (start > UINT32_MAX || len > UINT32_MAX || (start + len - 1u) > UINT32_MAX) {
                continue;
            }

            if (outRanges && write < cap) {
                outRanges[write].baseAddr = (uint32_t)start;
                outRanges[write].size = (uint32_t)len;
            }
            ++write;
        }
    }

    if (write > 0) {
        if (outCount) {
            *outCount = write;
        }
        return 1;
    }

    if (outCount) {
        *outCount = 2;
    }
    if (!outRanges || cap < 2) {
        return 1;
    }
    outRanges[0].baseAddr = 0x00C00000u;
    outRanges[0].size = 0x00080000u;
    outRanges[1].baseAddr = 0x00000000u;
    outRanges[1].size = 0x00080000u;
    return 1;
}

static int
target_amiga_registersReadExtra(const char **outTitle, e9k_debug_processor_reg_t *outRegs, size_t cap, size_t *outCount)
{
    (void)outRegs;
    (void)cap;
    if (outTitle) {
        *outTitle = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    return 1;
}

static void
target_amiga_appendText(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!out || cap == 0 || !pos || !text) {
        return;
    }
    while (*text && *pos + 1 < cap) {
        out[*pos] = *text;
        *pos += 1;
        text++;
    }
    out[*pos] = '\0';
}

static void
target_amiga_formatStatusBar(char *out, size_t cap)
{
    e9k_debug_counter_t counters[E9K_COUNTER_COUNT];
    size_t bytes = 0;
    size_t count = 0;
    size_t pos = 0;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';

    bytes = libretro_host_debugReadCounters(counters, sizeof(counters));
    count = bytes / sizeof(counters[0]);
    if (count > E9K_COUNTER_COUNT) {
        count = E9K_COUNTER_COUNT;
    }
    for (size_t i = 0; i < count; ++i) {
        char value[32];
        if (counters[i].name[0] == '\0' || counters[i].count == 0) {
            continue;
        }
        snprintf(value, sizeof(value), "%llu", (unsigned long long)counters[i].current);
        target_amiga_appendText(out, cap, &pos, " ");
        target_amiga_appendText(out, cap, &pos, counters[i].name);
        target_amiga_appendText(out, cap, &pos, ": ");
        target_amiga_appendText(out, cap, &pos, value);
    }
}

static target_iface_t _target_amiga = {
    .name = "AMIGA",
    .dasm = &dasm_ami_iface,
    .emu = &emu_ami_iface,
    .setConfigDefaults = target_amiga_setConfigDefaults,
    .setActiveDefaultsFromCurrentSystem = target_amiga_setActiveDefaultsFromCurrentSystem,
    .applyActiveSettingsToCurrentSystem = target_amiga_applyActiveSettingsToCurrentSystem,
    .configIsOk = target_amiga_configIsOk,
    .needsRestart = target_amiga_needsRestart,
    .settingsSaveButtonDisabled = target_amiga_settingsSaveButtonDisabled,
    .validateSettings = target_amiga_validateSettings,
    .settingsDefaults = target_amiga_settingsDefault,
    .applyRomConfigForSelection = target_amiga_applyRomConfigForSelection,
    .settingsSetConfigPaths = target_amiga_settingsSetConfigPaths,
    .defaultCorePath = target_amiga_defaultCorePath,
    .settingsRomPathChanged = target_amiga_settingsRomPathChanged,
    .settingsRomFolderChanged = target_amiga_settingsFolderChanged,
    .settingsCoreChanged = target_amiga_settingsCoreChanged,
    .settingsClearOptions = target_amiga_settingsClearOptions,
    .settingsLoadOptions = target_amiga_settingsLoadOptions,
    .settingsBuildModal = target_amiga_settingsBuildModal,
    .selectLibretroConfig = target_amiga_selectLibretroConfig,
    .coreOptionsHasGeneral = target_amiga_coreOptionsHasGeneral,
    .coreOptionsSaveClicked = target_amiga_coreOptionsSaveClicked,
    .coreOptionGetValue = target_amiga_coreOptionGetValue,
    .getLibretroCliConfig = target_amiga_getLibretroCliConfig,
    .onCoreStarted = target_amiga_onCoreStarted,
    .onVblank = target_amiga_onVblank,
    .coreIndex = TARGET_AMIGA,
    .libretroSelectConfig = target_amiga_libretroSelectConfig,
    .pickElfToolchainPaths = target_amiga_pickElfToolchainPaths,
    .applyCoreOptions = target_amiga_applyCoreOptions,
    .validateAPI = target_amiga_validateAPI,
    .audioEnabled = target_amiga_audioEnabled,
    .audioEnable = target_amiga_audioEnable,
    .mousePort = LIBRETRO_HOST_MAX_PORTS,
    .memoryGetLimits = target_amiga_memoryGetLimits,
    .memoryGetSpaces = target_amiga_memoryGetSpaces,
    .memoryTrackGetRanges = target_amiga_memoryTrackGetRanges,
    .memoryPanelOptions = target_amiga_memoryPanelOptions,
    .registersReadExtra = target_amiga_registersReadExtra,
    .getBadgeTexture = target_amiga_getBadgeTexture,
    .configControllerPorts = target_amiga_configControllerPorts,
    .controllerMapButton = target_amiga_controllerMapButton,
    .romConfigCustomOptionCount = target_amiga_romConfigCustomOptionCount,
    .romConfigCustomOptionKeyAt = target_amiga_romConfigCustomOptionKeyAt,
    .romConfigGetActiveCustomOptionValue = target_amiga_romConfigGetActiveCustomOptionValue,
    .romConfigSetActiveCustomOptionValue = target_amiga_romConfigSetActiveCustomOptionValue,
    .romConfigClearActiveCustomOptions = target_amiga_romConfigClearActiveCustomOptions,
    .coreOptionsHideOptionKey = target_amiga_coreOptionsHideOptionKey,
    .coreOptionsIsSyntheticOptionKey = target_amiga_coreOptionsIsSyntheticOptionKey,
    .coreOptionsSyntheticDefCount = target_amiga_coreOptionsSyntheticDefCount,
    .coreOptionsSyntheticDefAt = target_amiga_coreOptionsSyntheticDefAt,
    .coreOptionsMapDebuggerInputSpecIndex = target_amiga_coreOptionsMapDebuggerInputSpecIndex,
    .coreOptionsDebuggerInputLabel = target_amiga_coreOptionsDebuggerInputLabel,
    .formatStatusBar = target_amiga_formatStatusBar,
  };

target_iface_t *target_amiga(void) { return &_target_amiga; }  
