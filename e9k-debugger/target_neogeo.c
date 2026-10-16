/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"
#include "target.h"
#include "emu_geo.h"
#include "rom_config.h"
#include "debugger_input_bindings.h"
#include "neogeo_audio_vis.h"
#if E9K_ENABLE_AMIGA
#include "amiga_uae_options.h"
#endif
#include "neogeo_core_options.h"
#include "core_options.h"
#include "libretro_host.h"
#include "romset.h"
#include "system_badge.h"
#include "alloc.h"
#include "file.h"
#include "strutil.h"
#include <strings.h>

#define TARGET_NEOGEO_Z80_PROCESSOR_ID 1u

typedef struct target_neogeo_systemtype_state {
    e9ui_component_t *aesCheckbox;
    e9ui_component_t *mvsCheckbox;
    e9ui_component_t *uniCheckbox;
    e9ui_component_t *cdCheckbox;
    char *systemType;
    char *cdSystemType;
    int updating;
} target_neogeo_systemtype_state_t;

typedef struct target_neogeo_romfolder_cache {
    char folder[PATH_MAX];
    char saveDir[PATH_MAX];
    char systemDir[PATH_MAX];
    char neoPath[PATH_MAX];
    int valid;
} target_neogeo_romfolder_cache_t;

static target_neogeo_romfolder_cache_t target_neogeo_romFolderCache;
static char target_neogeo_activeRomMemorySpace[16];
static char target_neogeo_activeRomMemoryAddress[32];
static char target_neogeo_activeRomMemorySearch[128];

static const char *
target_neogeo_defaultCorePath(void);

static const char *
target_neogeo_memorySpaceOptionKey(void)
{
    return "e9k_debugger_neogeo_memory_space";
}

static const char *
target_neogeo_memoryAddressOptionKey(void)
{
    return "e9k_debugger_neogeo_memory_address";
}

static const char *
target_neogeo_memorySearchOptionKey(void)
{
    return "e9k_debugger_neogeo_memory_search";
}

static int
target_neogeo_romPathIsZip(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && strcasecmp(dot + 1, "zip") == 0;
}

static int
target_neogeo_romFolderCacheMatches(const char *folder, const char *saveDir, const char *systemDir)
{
    return target_neogeo_romFolderCache.valid &&
           strcmp(target_neogeo_romFolderCache.folder, folder ? folder : "") == 0 &&
           strcmp(target_neogeo_romFolderCache.saveDir, saveDir ? saveDir : "") == 0 &&
           strcmp(target_neogeo_romFolderCache.systemDir, systemDir ? systemDir : "") == 0 &&
           target_neogeo_romFolderCache.neoPath[0] != '\0';
}

static void
target_neogeo_romFolderCacheStore(const char *folder, const char *saveDir, const char *systemDir, const char *neoPath)
{
    strutil_strlcpy(target_neogeo_romFolderCache.folder,
                    sizeof(target_neogeo_romFolderCache.folder),
                    folder ? folder : "");
    strutil_strlcpy(target_neogeo_romFolderCache.saveDir,
                    sizeof(target_neogeo_romFolderCache.saveDir),
                    saveDir ? saveDir : "");
    strutil_strlcpy(target_neogeo_romFolderCache.systemDir,
                    sizeof(target_neogeo_romFolderCache.systemDir),
                    systemDir ? systemDir : "");
    strutil_strlcpy(target_neogeo_romFolderCache.neoPath,
                    sizeof(target_neogeo_romFolderCache.neoPath),
                    neoPath ? neoPath : "");
    target_neogeo_romFolderCache.valid = target_neogeo_romFolderCache.neoPath[0] != '\0';
}

static void
target_neogeo_setConfigDefaults(e9k_system_config_t *config)
{
    if (!config) {
        return;
    }
    snprintf(config->neogeo.libretro.systemDir, sizeof(config->neogeo.libretro.systemDir), "./system");
    snprintf(config->neogeo.libretro.saveDir, sizeof(config->neogeo.libretro.saveDir), "./saves");
    config->neogeo.libretro.sourceDir[0] = '\0';
    snprintf(config->neogeo.libretro.toolchainPrefix, sizeof(config->neogeo.libretro.toolchainPrefix), "m68k-neogeo-elf");
    config->neogeo.libretro.audioBufferMs = 250;
    config->neogeo.skipBiosLogo = 0;
    strncpy(config->neogeo.systemType, "aes", sizeof(config->neogeo.systemType) - 1);
    config->neogeo.systemType[sizeof(config->neogeo.systemType) - 1] = '\0';
    config->neogeo.cdSystemType[0] = '\0';
    config->neogeo.libretro.exePath[0] = '\0';
}

static void
target_neogeo_settingsClearOptions(void)
{
    neogeo_coreOptionsClear();
}

static void
target_neogeo_systemTypeSync(target_neogeo_systemtype_state_t *st, const char *value, e9ui_context_t *ctx)
{
    if (!st || !st->systemType) {
        return;
    }
    st->updating = 1;
    settings_config_setValue(st->systemType, 16, value);
    int aesSelected = (strcmp(st->systemType, "aes") == 0);
    int mvsSelected = (strcmp(st->systemType, "mvs") == 0);
    int uniSelected = (strcmp(st->systemType, "uni") == 0);
    int cdSelected = (st->cdSystemType && st->cdSystemType[0]);
    if (st->aesCheckbox) {
        e9ui_checkbox_setSelected(st->aesCheckbox, cdSelected ? 0 : aesSelected, ctx);
    }
    if (st->mvsCheckbox) {
        e9ui_checkbox_setSelected(st->mvsCheckbox, cdSelected ? 0 : mvsSelected, ctx);
    }
    if (st->uniCheckbox) {
        e9ui_checkbox_setSelected(st->uniCheckbox, cdSelected ? 0 : uniSelected, ctx);
    }
    if (st->cdCheckbox) {
        e9ui_checkbox_setSelected(st->cdCheckbox, cdSelected, ctx);
    }
    st->updating = 0;
    settings_updateSaveLabel();
}

static void
target_neogeo_systemTypeAesChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    target_neogeo_systemtype_state_t *st = (target_neogeo_systemtype_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_config_setValue(st->cdSystemType, 16, "");
        target_neogeo_systemTypeSync(st, "aes", ctx);
    } else if (st->mvsCheckbox && e9ui_checkbox_isSelected(st->mvsCheckbox)) {
        target_neogeo_systemTypeSync(st, "mvs", ctx);
    } else if (st->uniCheckbox && e9ui_checkbox_isSelected(st->uniCheckbox)) {
        target_neogeo_systemTypeSync(st, "uni", ctx);
    } else {
        target_neogeo_systemTypeSync(st, "", ctx);
    }
}

static void
target_neogeo_systemTypeMvsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    target_neogeo_systemtype_state_t *st = (target_neogeo_systemtype_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_config_setValue(st->cdSystemType, 16, "");
        target_neogeo_systemTypeSync(st, "mvs", ctx);
    } else if (st->aesCheckbox && e9ui_checkbox_isSelected(st->aesCheckbox)) {
        target_neogeo_systemTypeSync(st, "aes", ctx);
    } else if (st->uniCheckbox && e9ui_checkbox_isSelected(st->uniCheckbox)) {
        target_neogeo_systemTypeSync(st, "uni", ctx);
    } else {
        target_neogeo_systemTypeSync(st, "", ctx);
    }
}

static void
target_neogeo_systemTypeUniChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    target_neogeo_systemtype_state_t *st = (target_neogeo_systemtype_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_config_setValue(st->cdSystemType, 16, "");
        target_neogeo_systemTypeSync(st, "uni", ctx);
    } else if (st->mvsCheckbox && e9ui_checkbox_isSelected(st->mvsCheckbox)) {
        target_neogeo_systemTypeSync(st, "mvs", ctx);
    } else if (st->aesCheckbox && e9ui_checkbox_isSelected(st->aesCheckbox)) {
        target_neogeo_systemTypeSync(st, "aes", ctx);
    } else {
        target_neogeo_systemTypeSync(st, "", ctx);
    }
}

static void
target_neogeo_systemTypeCdChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    target_neogeo_systemtype_state_t *st = (target_neogeo_systemtype_state_t *)user;
    if (!st || st->updating || !st->cdSystemType) {
        return;
    }
    if (selected) {
        settings_config_setValue(st->cdSystemType, 16, "cdz");
        target_neogeo_systemTypeSync(st, "", ctx);
    } else {
        settings_config_setValue(st->cdSystemType, 16, "");
        target_neogeo_systemTypeSync(st, st->systemType && st->systemType[0] ? st->systemType : "aes", ctx);
    }
}

static void
target_neogeo_skipBiosChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    int *dest = (int*)user;
    if (!dest) {
        return;
    }
    *dest = selected ? 1 : 0;
    settings_updateSaveLabel();
}

static void
target_neogeo_settingsBuildModal(e9ui_context_t *ctx, target_settings_modal_t *out)
{
    if (!out || !ctx) {
        return;
    }
    out->body = NULL;
    out->footerWarning = NULL;

    const char *romExts[] = { "*.neo", "*.zip", "*.cue", "*.chd" };
    const char *elfExts[] = { "*.elf", "*.txt" };

    settings_romselect_state_t *romState = (settings_romselect_state_t *)alloc_calloc(1, sizeof(*romState));
    if (romState) {
        romState->romPath = debugger.settingsEdit.neogeo.libretro.romPath;
        romState->romFolder = debugger.settingsEdit.neogeo.romFolder;
    }

    e9ui_component_t *fsRom = e9ui_fileSelect_make("ROM", 120, 600, "...", romExts, (int)countof(romExts), E9UI_FILESELECT_FILE);
    e9ui_component_t *fsRomFolder = e9ui_fileSelect_make("ROM FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsElf = e9ui_fileSelect_make("ELF", 120, 600, "...", elfExts, (int)countof(elfExts), E9UI_FILESELECT_FILE);
    e9ui_component_t *fsBios = e9ui_fileSelect_make("BIOS FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsSaves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
    e9ui_component_t *fsSource = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);

    if (fsRom) {
        e9ui_fileSelect_setText(fsRom, debugger.settingsEdit.neogeo.libretro.romPath);
        e9ui_fileSelect_setOnChange(fsRom, settings_romPathChanged, romState);
    }
    if (fsRomFolder) {
        e9ui_fileSelect_setText(fsRomFolder, debugger.settingsEdit.neogeo.romFolder);
        e9ui_fileSelect_setOnChange(fsRomFolder, settings_romFolderChanged, romState);
    }
    if (fsElf) {
        e9ui_fileSelect_setAllowEmpty(fsElf, 1);
        e9ui_fileSelect_setText(fsElf, debugger.settingsEdit.neogeo.libretro.exePath);
        e9ui_fileSelect_setOnChange(fsElf, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.exePath);
    }
    if (fsBios) {
        e9ui_fileSelect_setText(fsBios, debugger.settingsEdit.neogeo.libretro.systemDir);
        e9ui_fileSelect_setOnChange(fsBios, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.systemDir);
    }
    if (fsSaves) {
        e9ui_fileSelect_setText(fsSaves, debugger.settingsEdit.neogeo.libretro.saveDir);
        e9ui_fileSelect_setOnChange(fsSaves, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.saveDir);
    }
    if (fsSource) {
        e9ui_fileSelect_setAllowEmpty(fsSource, 1);
        e9ui_fileSelect_setText(fsSource, debugger.settingsEdit.neogeo.libretro.sourceDir);
        e9ui_fileSelect_setOnChange(fsSource, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.sourceDir);
    }
    e9ui_component_t *ltToolchain = e9ui_labeled_textbox_make("TOOLCHAIN PREFIX",
                                                              120,
                                                              600,
                                                              settings_toolchainPrefixChanged,
                                                              debugger.settingsEdit.neogeo.libretro.toolchainPrefix);
    if (ltToolchain) {
        e9ui_labeled_textbox_setText(ltToolchain, debugger.settingsEdit.neogeo.libretro.toolchainPrefix);
    }

    e9ui_component_t *ltAudio = e9ui_labeled_textbox_make("AUDIO BUFFER MS",
                                                          120,
                                                          600,
                                                          settings_audioChanged,
                                                          &debugger.settingsEdit.neogeo.libretro.audioBufferMs);
    if (ltAudio) {
        char buf[32];
        int audioValue = debugger.settingsEdit.neogeo.libretro.audioBufferMs;
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

    if (romState) {
        romState->romSelect = fsRom;
        romState->folderSelect = fsRomFolder;
        romState->elfSelect = fsElf;
        romState->sourceSelect = fsSource;
        romState->toolchainSelect = ltToolchain;
        settings_romSelectUpdateAllowEmpty(romState);
        settings_romSelectRefreshRecents(romState);
    }

    // System row: [MVS] [AES] [UNIBIOS] [CD] [SKIP BIOS LOGO]
    e9ui_component_t *rowSystem = e9ui_flow_make();
    if (rowSystem) {
        e9ui_flow_setWrap(rowSystem, 0);
        e9ui_flow_setSpacing(rowSystem, 12);
        target_neogeo_systemtype_state_t *sys = (target_neogeo_systemtype_state_t *)alloc_calloc(1, sizeof(*sys));
        if (sys) {
            sys->systemType = debugger.settingsEdit.neogeo.systemType;
            sys->cdSystemType = debugger.settingsEdit.neogeo.cdSystemType;
        }
        int cdSelected = debugger.settingsEdit.neogeo.cdSystemType[0] ? 1 : 0;
        int aesSelected = !cdSelected && (strcmp(debugger.settingsEdit.neogeo.systemType, "aes") == 0);
        int mvsSelected = !cdSelected && (strcmp(debugger.settingsEdit.neogeo.systemType, "mvs") == 0);
        int uniSelected = !cdSelected && (strcmp(debugger.settingsEdit.neogeo.systemType, "uni") == 0);
        e9ui_component_t *cbMvs = e9ui_checkbox_make("MVS", mvsSelected, target_neogeo_systemTypeMvsChanged, sys);
        e9ui_component_t *cbAes = e9ui_checkbox_make("AES", aesSelected, target_neogeo_systemTypeAesChanged, sys);
        e9ui_component_t *cbUni = e9ui_checkbox_make("UNIBIOS", uniSelected, target_neogeo_systemTypeUniChanged, sys);
        e9ui_component_t *cbCd = e9ui_checkbox_make("CD", cdSelected, target_neogeo_systemTypeCdChanged, sys);
        e9ui_component_t *cbSkip = e9ui_checkbox_make("SKIP BIOS LOGO",
                                                      debugger.settingsEdit.neogeo.skipBiosLogo,
                                                      target_neogeo_skipBiosChanged,
                                                      &debugger.settingsEdit.neogeo.skipBiosLogo);
        if (sys) {
            sys->aesCheckbox = cbAes;
            sys->mvsCheckbox = cbMvs;
            sys->uniCheckbox = cbUni;
            sys->cdCheckbox = cbCd;
        }
        if (cbMvs) {
            e9ui_flow_add(rowSystem, cbMvs);
        }
        if (cbAes) {
            e9ui_flow_add(rowSystem, cbAes);
        }
        if (cbUni) {
            e9ui_flow_add(rowSystem, cbUni);
        }
        if (cbCd) {
            e9ui_flow_add(rowSystem, cbCd);
        }
        if (cbSkip) {
            e9ui_flow_add(rowSystem, cbSkip);
        }
    }

    e9ui_component_t *body = e9ui_stack_makeVertical();
    if (body) {
        int first = 1;
        if (fsRom) {
            e9ui_stack_addFixed(body, fsRom);
            first = 0;
        }
        if (fsRomFolder) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsRomFolder);
            first = 0;
        }
        if (fsElf) {
            if (!first) {
                e9ui_stack_addFixed(body, e9ui_vspacer_make(12));
            }
            e9ui_stack_addFixed(body, fsElf);
            first = 0;
        }
        if (ltToolchain) {
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
    out->footerWarning = rowSystem;
}

static void
target_neogeo_applyActiveSettingsToCurrentSystem(void)
{
    strutil_strlcpy(debugger.config.neogeo.libretro.exePath,
                      sizeof(debugger.config.neogeo.libretro.exePath),
                      rom_config_activeElfPath);
    strutil_strlcpy(debugger.config.neogeo.libretro.sourceDir,
                      sizeof(debugger.config.neogeo.libretro.sourceDir),
                      rom_config_activeSourceDir);
    strutil_strlcpy(debugger.config.neogeo.libretro.toolchainPrefix,
                      sizeof(debugger.config.neogeo.libretro.toolchainPrefix),
                      rom_config_activeToolchainPrefix);
}


static void
target_neogeo_setActiveDefaultsFromCurrentSystem(void)
{
    strutil_strlcpy(rom_config_activeElfPath, sizeof(rom_config_activeElfPath),
                      debugger.config.neogeo.libretro.exePath);
    strutil_strlcpy(rom_config_activeSourceDir, sizeof(rom_config_activeSourceDir),
                      debugger.config.neogeo.libretro.sourceDir);
    strutil_strlcpy(rom_config_activeToolchainPrefix, sizeof(rom_config_activeToolchainPrefix),
                      debugger.config.neogeo.libretro.toolchainPrefix);
}




static int
target_neogeo_configMissingPaths(const e9k_neogeo_config_t *cfg)
{
    if (!cfg) {
        return 1;
    }
    const char *corePath = target_neogeo_defaultCorePath();
    if (!corePath || !*corePath ||
        (!cfg->libretro.romPath[0] && !cfg->romFolder[0]) ||
        !cfg->libretro.systemDir[0] ||
        !cfg->libretro.saveDir[0] ||
        !settings_pathExistsFile(corePath) ||
        !settings_pathExistsDir(cfg->libretro.systemDir) ||
        !settings_pathExistsDir(cfg->libretro.saveDir)) {
        return 1;
    }
    if (cfg->libretro.romPath[0] && !settings_pathExistsFile(cfg->libretro.romPath)) {
        return 1;
    }
    if (cfg->romFolder[0] && !settings_pathExistsDir(cfg->romFolder)) {
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
target_neogeo_configIsOk(void)
{
  return target_neogeo_configMissingPaths(&debugger.config.neogeo) ? 0 : 1;
}


static int
target_negeo_restartNeededForNeogeo(const e9k_neogeo_config_t *before, const e9k_neogeo_config_t *after)
{
    if (!before || !after) {
        return 1;
    }
    int romChanged = strcmp(before->libretro.romPath, after->libretro.romPath) != 0 ||
                     strcmp(before->romFolder, after->romFolder) != 0;
    int elfChanged = strcmp(before->libretro.exePath, after->libretro.exePath) != 0;
    int toolchainChanged = strcmp(before->libretro.toolchainPrefix, after->libretro.toolchainPrefix) != 0;
    int biosChanged = strcmp(before->libretro.systemDir, after->libretro.systemDir) != 0;
    int savesChanged = strcmp(before->libretro.saveDir, after->libretro.saveDir) != 0;
    int sourceChanged = strcmp(before->libretro.sourceDir, after->libretro.sourceDir) != 0;
    int sysChanged = strcmp(before->systemType, after->systemType) != 0;
    int cdSysChanged = strcmp(before->cdSystemType, after->cdSystemType) != 0;
    int audioBefore = settings_audioBufferNormalized(before->libretro.audioBufferMs);
    int audioAfter = settings_audioBufferNormalized(after->libretro.audioBufferMs);
    int audioChanged = audioBefore != audioAfter;
    return romChanged || elfChanged || toolchainChanged || biosChanged || savesChanged || sourceChanged || sysChanged || cdSysChanged || audioChanged;
}


static int
target_neogeo_settings_configIsOkFor(const e9k_neogeo_config_t *cfg)
{
    return target_neogeo_configMissingPaths(cfg) ? 0 : 1;
}


static int
target_neogeo_needsRestart(void)
{
    int configChanged = target_negeo_restartNeededForNeogeo(&debugger.config.neogeo, &debugger.settingsEdit.neogeo);
    if (settings_coreOptionsDirty) {
      configChanged = 1;
    }
    int okBefore = target_neogeo_settings_configIsOkFor(&debugger.config.neogeo);
    int okAfter = target_neogeo_settings_configIsOkFor(&debugger.settingsEdit.neogeo);

    return configChanged ||  (!okBefore && okAfter);    
}


static int
target_neogeo_settingsSaveButtonDisabled(void)
{
  return 0;
}


static int
target_neogeo_effectiveRomPath(const e9k_neogeo_config_t *cfg, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!cfg) {
        return 0;
    }
    if (cfg->libretro.romPath[0]) {
        if (target_neogeo_romPathIsZip(cfg->libretro.romPath)) {
            return romset_buildNeoOutputPath(cfg->libretro.romPath,
                                             1,
                                             cfg->libretro.saveDir,
                                             cfg->libretro.systemDir,
                                             out,
                                             cap);
        }
        strutil_strlcpy(out, cap, cfg->libretro.romPath);
        return 1;
    }
    if (!cfg->romFolder[0]) {
        return 0;
    }
    return romset_buildNeoOutputPath(cfg->romFolder,
                                     0,
                                     cfg->libretro.saveDir,
                                     cfg->libretro.systemDir,
                                     out,
                                     cap);
}


static void
target_neogeo_validateSettings(void)
{
  if (debugger.settingsEdit.neogeo.libretro.audioBufferMs <= 0) {
    debugger.settingsEdit.neogeo.libretro.audioBufferMs = 50;
  }
  char romPath[PATH_MAX];
  if (target_neogeo_effectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
    if (!neogeo_coreOptionsWriteToFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath)) {
      e9ui_showTransientMessage("CORE OPTIONS SAVE FAILED");
      return;
    }
  }
  neogeo_coreOptionsClear();
  const char *saveDir = debugger.settingsEdit.neogeo.libretro.saveDir[0] ?
    debugger.settingsEdit.neogeo.libretro.saveDir : debugger.settingsEdit.neogeo.libretro.systemDir;

  if (target_neogeo_effectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
    rom_config_saveSettingsForRom(saveDir, romPath, target_neogeo(),
				  debugger.settingsEdit.neogeo.libretro.exePath,
				  debugger.settingsEdit.neogeo.libretro.sourceDir,
				  debugger.settingsEdit.neogeo.libretro.toolchainPrefix);
  }
  
}


static void
target_neogeo_settingsDefault(void)
{
    char romPath[PATH_MAX];
    char romFolder[PATH_MAX];
    char elfPath[PATH_MAX];
    settings_copyPath(romPath, sizeof(romPath), debugger.settingsEdit.neogeo.libretro.romPath);
    settings_copyPath(romFolder, sizeof(romFolder), debugger.settingsEdit.neogeo.romFolder);
    settings_copyPath(elfPath, sizeof(elfPath), debugger.settingsEdit.neogeo.libretro.exePath);
    int audioEnabled = debugger.settingsEdit.neogeo.libretro.audioEnabled;
    target_neogeo_setConfigDefaults(&debugger.settingsEdit);
    debugger.settingsEdit.neogeo.libretro.audioEnabled = audioEnabled;
    settings_copyPath(debugger.settingsEdit.neogeo.libretro.romPath, sizeof(debugger.settingsEdit.neogeo.libretro.romPath), romPath);
    settings_copyPath(debugger.settingsEdit.neogeo.romFolder, sizeof(debugger.settingsEdit.neogeo.romFolder), romFolder);
    settings_copyPath(debugger.settingsEdit.neogeo.libretro.exePath, sizeof(debugger.settingsEdit.neogeo.libretro.exePath), elfPath);
}


static void
target_neogeo_applyRomConfigForSelection(settings_romselect_state_t *st, const char** saveDirP, const char** romPathP)
{
    (void)st;
    *saveDirP = debugger.settingsEdit.neogeo.libretro.saveDir[0] ?
    debugger.settingsEdit.neogeo.libretro.saveDir : debugger.settingsEdit.neogeo.libretro.systemDir;
    static char romPathBuf[PATH_MAX];
    if (target_neogeo_effectiveRomPath(&debugger.settingsEdit.neogeo, romPathBuf, sizeof(romPathBuf))) {
        *romPathP = romPathBuf;
    }
}


static void
target_neoge_settingsSetConfigPaths(int hasElf, const char* elfPath, int hasSource, const char* sourceDir, int hasToolchain, const char*toolchainPrefix)
{
  settings_config_setPath(debugger.settingsEdit.neogeo.libretro.exePath, PATH_MAX, hasElf ? elfPath : "");
  settings_config_setPath(debugger.settingsEdit.neogeo.libretro.sourceDir, PATH_MAX, hasSource ? sourceDir : "");
  settings_config_setValue(debugger.settingsEdit.neogeo.libretro.toolchainPrefix, PATH_MAX, hasToolchain ? toolchainPrefix : "");
}


static const char *
target_neogeo_defaultCorePath(void)
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
  snprintf(relPath, sizeof(relPath), "system/geo9000.%s", ext);
  if (file_getAssetPath(relPath, corePath, sizeof(corePath))) {
    return corePath;
  }
  snprintf(fallbackPath, sizeof(fallbackPath), "./system/geo9000.%s", ext);
  return fallbackPath;
}


static void
target_neogeo_settingsRomPathChanged(settings_romselect_state_t* st)
{
  (void)st;
  char romPath[PATH_MAX];
  if (target_neogeo_effectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
    neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
  } else {
    neogeo_coreOptionsClear();
  }
}


static void
target_neogeo_settingsFolderChanged(void)
{
    char romPath[PATH_MAX];
    if (target_neogeo_effectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
        neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
    } else {
        neogeo_coreOptionsClear();
    }
}


static void
target_neogeo_settingsCoreChanged(void)
{
#if E9K_ENABLE_AMIGA
    amiga_uaeClearPuaeOptions();
#endif
    char romPath[PATH_MAX];
    if (target_neogeo_effectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
        neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
    } else {
        neogeo_coreOptionsClear();
    }
}


static void
target_neogeo_settingsLoadOptions(e9k_system_config_t * st)
{
  (void)st;
  char romPath[PATH_MAX];
  if (target_neogeo_effectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
    neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
  }
}


const e9k_libretro_config_t*
target_neogeo_selectLibretroConfig(const e9k_system_config_t *cfg)
{
  return &cfg->neogeo.libretro;
}


static int
target_neogeo_coreOptionsHasGeneral(const core_options_modal_state_t *st)
{
  (void)st;
  return 1;
}


static void
target_neogeo_coreOptionsSaveClicked(e9ui_context_t *ctx,core_options_modal_state_t *st)
{
  (void)ctx;
  (void)st;
  int anyChange = 0;
  int anyRomConfigBindingChange = 0;
  int anyCoreOptionChange = 0;
  
  for (size_t i = 0; i < st->entryCount; ++i) {
    const char *key = st->entries[i].key;
    const char *value = st->entries[i].value;
    if (!key || !*key) {
      continue;
    }
    if (strcmp(key, "geolith_system_type") == 0) {
      continue;
    }
    if (strcmp(key, "geolith_cd_system_type") == 0) {
      continue;
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
    const char *existing = neogeo_coreOptionsGetValue(key);
    if (!desired) {
      if (!existing) {
	continue;
      }
      neogeo_coreOptionsSetValue(key, NULL);
      anyChange = 1;
      anyCoreOptionChange = 1;
    } else {
      if (existing && core_options_stringsEqual(existing, desired)) {
	continue;
      }
      neogeo_coreOptionsSetValue(key, desired);
      anyChange = 1;
      anyCoreOptionChange = 1;
    }
  }
  if (anyChange) {
    settings_markCoreOptionsDirtyWithRestart(anyCoreOptionChange ? 1 : 0);
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
target_neogeo_coreOptionGetValue(const char* key)
{
  if (debugger_input_bindings_isOptionKey(key)) {
    return rom_config_getActiveInputBindingValue(key);
  }
  return  neogeo_coreOptionsGetValue(key);
}


static e9k_libretro_config_t *
target_neogeo_getLibretroCliConfig(void)
{
  return &debugger.cliConfig.neogeo.libretro;
}

static void
target_neogeo_onCoreStarted(void)
{
}


static void
target_neogeo_onVblank(void)
{
  e9k_debug_sprite_state_t spriteState;
  if (libretro_host_neogeo_getSpriteState(&spriteState) && spriteState.vram && spriteState.vram_words) {
    emu_geo_setSpriteState(&spriteState, 1);
  } else {
    emu_geo_setSpriteState(NULL, 0);
  }
  if (neogeo_audio_vis_isOpen()) {
    e9k_debug_audio_frame_t audioFrame;
    if (libretro_host_neogeo_getAudioFrame(&audioFrame)) {
      emu_geo_setAudioFrame(&audioFrame, 1);
    } else {
      emu_geo_setAudioFrame(NULL, 0);
    }
  } else {
    emu_geo_setAudioFrame(NULL, 0);
  }
}



static void
target_neogeo_libretroSelectConfig(void)
{
    debugger.libretro.audioBufferMs = debugger.config.neogeo.libretro.audioBufferMs;
    debugger.libretro.audioEnabled = debugger.config.neogeo.libretro.audioEnabled;
    debugger_copyPath(debugger.libretro.sourceDir, sizeof(debugger.libretro.sourceDir), debugger.config.neogeo.libretro.sourceDir);
    debugger_copyPath(debugger.libretro.exePath, sizeof(debugger.libretro.exePath), debugger.config.neogeo.libretro.exePath);
    debugger_copyPath(debugger.libretro.toolchainPrefix, sizeof(debugger.libretro.toolchainPrefix), debugger.config.neogeo.libretro.toolchainPrefix);
    debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), debugger.config.neogeo.libretro.romPath);
    debugger_copyPath(debugger.libretro.systemDir, sizeof(debugger.libretro.systemDir), debugger.config.neogeo.libretro.systemDir);
    debugger_copyPath(debugger.libretro.saveDir, sizeof(debugger.libretro.saveDir), debugger.config.neogeo.libretro.saveDir);
    const char *romInputPath = debugger.config.neogeo.romFolder[0] ? debugger.config.neogeo.romFolder : NULL;
    int romInputIsZip = 0;
    if (!romInputPath && target_neogeo_romPathIsZip(debugger.config.neogeo.libretro.romPath)) {
        romInputPath = debugger.config.neogeo.libretro.romPath;
        romInputIsZip = 1;
    }
    if (romInputPath) {
        if (target_neogeo_romFolderCacheMatches(romInputPath,
                                                debugger.config.neogeo.libretro.saveDir,
                                                debugger.config.neogeo.libretro.systemDir)) {
            debugger_copyPath(debugger.libretro.romPath,
                              sizeof(debugger.libretro.romPath),
                              target_neogeo_romFolderCache.neoPath);
        } else {
            char neoPath[PATH_MAX];
            int built = romInputIsZip ?
                romset_buildNeoFromZip(romInputPath, neoPath, sizeof(neoPath)) :
                romset_buildNeoFromFolder(romInputPath, neoPath, sizeof(neoPath));
            if (built) {
                target_neogeo_romFolderCacheStore(romInputPath,
                                                  debugger.config.neogeo.libretro.saveDir,
                                                  debugger.config.neogeo.libretro.systemDir,
                                                  neoPath);
                debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), neoPath);
            } else {
                target_neogeo_romFolderCache.valid = 0;
                debugger.libretro.romPath[0] = '\0';
            }
        }
    } else {
        target_neogeo_romFolderCache.valid = 0;
    }
}


static void
target_neogeo_pickElfToolchainPaths(const char** rawElf, const char** toolchainPrefix)
{
  *rawElf = debugger.config.neogeo.libretro.exePath;
  *toolchainPrefix = debugger.config.neogeo.libretro.toolchainPrefix;
}



static void
target_neogeo_applyCoreOptions(void)
{
  const char *romPath = debugger.libretro.romPath[0] ? debugger.libretro.romPath : debugger.config.neogeo.libretro.romPath;
  const char *saveDir = debugger.libretro.saveDir[0] ? debugger.libretro.saveDir : debugger.config.neogeo.libretro.saveDir;
  if (romPath && *romPath && saveDir && *saveDir) {
    neogeo_coreOptionsApplyFileToHost(saveDir, romPath);
  }
}


static void
target_neogeo_validateAPI(void)
{
  emu_geo_setSpriteState(NULL, 0);
  emu_geo_setAudioFrame(NULL, 0);
  libretro_host_megadrive_unbindApis();
  libretro_host_neogeo_bindApis();
}


static int
target_neogeo_audioEnabled(void)
{
  return debugger.config.neogeo.libretro.audioEnabled;
}


static void
target_neogeo_audioEnable(int enabled)
{
  debugger.config.neogeo.libretro.audioEnabled = enabled;
}



static SDL_Texture *
target_neogeo_getBadgeTexture(SDL_Renderer *renderer, target_iface_t* t, int* outW, int* outH)
{
  if (t->badge && t->badgeRenderer != renderer) {
    SDL_DestroyTexture(t->badge);
    t->badge = NULL;
  }
  t->badgeRenderer = renderer;
  if (!t->badge) {
    t->badge = system_badge_loadTexture(renderer, "assets/neogeo.png", &t->badgeW, &t->badgeH);
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
target_neogeo_configControllerPorts(void)
{  

}

static int
target_neogeo_memoryGetLimits(uint32_t *outMinAddr, uint32_t *outMaxAddr)
{
    if (outMinAddr) {
        *outMinAddr = 0x00000000u;
    }
    if (outMaxAddr) {
        *outMaxAddr = 0x00ffffffu;
    }
    return 1;
}

static int
target_neogeo_memoryGetSpaces(target_memory_space_t *outSpaces, size_t cap, size_t *outCount)
{
    if (outCount) {
        *outCount = 2;
    }
    if (!outSpaces || cap == 0) {
        return 1;
    }

    outSpaces[0] = (target_memory_space_t){
        .value = "68k",
        .label = "68K",
        .minAddr = 0x00000000u,
        .maxAddr = 0x00ffffffu,
        .addressDigits = 6,
        .processorMemory = 0,
        .processorId = 0
    };
    if (cap > 1) {
        outSpaces[1] = (target_memory_space_t){
            .value = "z80",
            .label = "Z80",
            .minAddr = 0x0000u,
            .maxAddr = 0xffffu,
            .addressDigits = 4,
            .processorMemory = 1,
            .processorId = TARGET_NEOGEO_Z80_PROCESSOR_ID
        };
    }
    return 1;
}

static int
target_neogeo_memoryTrackGetRanges(target_memory_range_t *outRanges, size_t cap, size_t *outCount)
{
    if (outCount) {
        *outCount = 1;
    }
    if (!outRanges || cap == 0) {
        return 1;
    }
    outRanges[0].baseAddr = 0x00100000u;
    outRanges[0].size = 0x00010000u;
    return 1;
}

static int
target_neogeo_registersReadExtra(const char **outTitle, e9k_debug_processor_reg_t *outRegs, size_t cap, size_t *outCount)
{
    size_t count = 0;

    if (outTitle) {
        *outTitle = "Z80";
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!outRegs || cap == 0) {
        return 1;
    }
    if (!libretro_host_debugReadProcessorRegs(TARGET_NEOGEO_Z80_PROCESSOR_ID, outRegs, cap, &count)) {
        return 0;
    }
    if (outCount) {
        *outCount = count;
    }
    return 1;
}

static  int
target_neogeo_controllerMapButton(SDL_GameControllerButton button, unsigned *outId)
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

static size_t
target_neogeo_romConfigCustomOptionCount(void)
{
    return 3;
}

static const char *
target_neogeo_romConfigCustomOptionKeyAt(size_t index)
{
    if (index == 0) {
        return target_neogeo_memorySpaceOptionKey();
    }
    if (index == 1) {
        return target_neogeo_memoryAddressOptionKey();
    }
    if (index == 2) {
        return target_neogeo_memorySearchOptionKey();
    }
    return NULL;
}

static const char *
target_neogeo_romConfigGetActiveCustomOptionValue(const char *key)
{
    if (!key) {
        return NULL;
    }
    if (strcmp(key, target_neogeo_memorySpaceOptionKey()) == 0) {
        return target_neogeo_activeRomMemorySpace[0] ? target_neogeo_activeRomMemorySpace : NULL;
    }
    if (strcmp(key, target_neogeo_memoryAddressOptionKey()) == 0) {
        return target_neogeo_activeRomMemoryAddress[0] ? target_neogeo_activeRomMemoryAddress : NULL;
    }
    if (strcmp(key, target_neogeo_memorySearchOptionKey()) == 0) {
        return target_neogeo_activeRomMemorySearch[0] ? target_neogeo_activeRomMemorySearch : NULL;
    }
    return NULL;
}

static void
target_neogeo_romConfigSetActiveCustomOptionValue(const char *key, const char *value)
{
    if (!key) {
        return;
    }
    if (strcmp(key, target_neogeo_memorySpaceOptionKey()) == 0) {
        strutil_strlcpy(target_neogeo_activeRomMemorySpace, sizeof(target_neogeo_activeRomMemorySpace), value ? value : "");
        return;
    }
    if (strcmp(key, target_neogeo_memoryAddressOptionKey()) == 0) {
        strutil_strlcpy(target_neogeo_activeRomMemoryAddress, sizeof(target_neogeo_activeRomMemoryAddress), value ? value : "");
        return;
    }
    if (strcmp(key, target_neogeo_memorySearchOptionKey()) == 0) {
        strutil_strlcpy(target_neogeo_activeRomMemorySearch, sizeof(target_neogeo_activeRomMemorySearch), value ? value : "");
    }
}

static void
target_neogeo_romConfigClearActiveCustomOptions(void)
{
    target_neogeo_activeRomMemorySpace[0] = '\0';
    target_neogeo_activeRomMemoryAddress[0] = '\0';
    target_neogeo_activeRomMemorySearch[0] = '\0';
}

static int
target_neogeo_memoryPanelOptions(target_memory_panel_options_t *outOptions)
{
    if (!outOptions) {
        return 0;
    }
    outOptions->spaceKey = target_neogeo_memorySpaceOptionKey();
    outOptions->addressKey = target_neogeo_memoryAddressOptionKey();
    outOptions->searchKey = target_neogeo_memorySearchOptionKey();
    return 1;
}

static target_iface_t _target_neogeo = {
    .name = "NEO GEO",
    .dasm = &dasm_geo_iface,
    .emu = &emu_geo_iface,
    .setConfigDefaults = target_neogeo_setConfigDefaults,
    .setActiveDefaultsFromCurrentSystem = target_neogeo_setActiveDefaultsFromCurrentSystem,
    .applyActiveSettingsToCurrentSystem = target_neogeo_applyActiveSettingsToCurrentSystem,
    .configIsOk = target_neogeo_configIsOk,
    .needsRestart = target_neogeo_needsRestart,
    .settingsSaveButtonDisabled = target_neogeo_settingsSaveButtonDisabled,
    .validateSettings = target_neogeo_validateSettings,
    .settingsDefaults = target_neogeo_settingsDefault,
    .applyRomConfigForSelection = target_neogeo_applyRomConfigForSelection,
    .settingsSetConfigPaths = target_neoge_settingsSetConfigPaths,
    .defaultCorePath = target_neogeo_defaultCorePath,
    .settingsRomPathChanged = target_neogeo_settingsRomPathChanged,
    .settingsRomFolderChanged = target_neogeo_settingsFolderChanged,
    .settingsCoreChanged = target_neogeo_settingsCoreChanged,
    .settingsClearOptions = target_neogeo_settingsClearOptions,
    .settingsLoadOptions = target_neogeo_settingsLoadOptions,
    .settingsBuildModal = target_neogeo_settingsBuildModal,
    .selectLibretroConfig = target_neogeo_selectLibretroConfig,
    .coreOptionsHasGeneral = target_neogeo_coreOptionsHasGeneral,
    .coreOptionsSaveClicked = target_neogeo_coreOptionsSaveClicked,
    .coreOptionGetValue = target_neogeo_coreOptionGetValue,
    .getLibretroCliConfig = target_neogeo_getLibretroCliConfig,
    .onCoreStarted = target_neogeo_onCoreStarted,
    .onVblank = target_neogeo_onVblank,
    .coreIndex = TARGET_NEOGEO,
    .libretroSelectConfig = target_neogeo_libretroSelectConfig,
    .pickElfToolchainPaths = target_neogeo_pickElfToolchainPaths,
    .applyCoreOptions = target_neogeo_applyCoreOptions,
    .validateAPI = target_neogeo_validateAPI,
    .audioEnabled = target_neogeo_audioEnabled,
    .audioEnable = target_neogeo_audioEnable,
    .mousePort = -1,
    .memoryGetLimits = target_neogeo_memoryGetLimits,
    .memoryGetSpaces = target_neogeo_memoryGetSpaces,
    .memoryTrackGetRanges = target_neogeo_memoryTrackGetRanges,
    .memoryPanelOptions = target_neogeo_memoryPanelOptions,
    .registersReadExtra = target_neogeo_registersReadExtra,
    .getBadgeTexture = target_neogeo_getBadgeTexture,
    .configControllerPorts = target_neogeo_configControllerPorts,
    .controllerMapButton = target_neogeo_controllerMapButton,
    .romConfigCustomOptionCount = target_neogeo_romConfigCustomOptionCount,
    .romConfigCustomOptionKeyAt = target_neogeo_romConfigCustomOptionKeyAt,
    .romConfigGetActiveCustomOptionValue = target_neogeo_romConfigGetActiveCustomOptionValue,
    .romConfigSetActiveCustomOptionValue = target_neogeo_romConfigSetActiveCustomOptionValue,
    .romConfigClearActiveCustomOptions = target_neogeo_romConfigClearActiveCustomOptions,

};

target_iface_t*
target_neogeo(void)
{
  return &_target_neogeo;
}
  
