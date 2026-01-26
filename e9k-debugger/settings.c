/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "settings.h"
#include "alloc.h"
#include "crt.h"
#include "debugger.h"
#include "config.h"
#include "list.h"


typedef struct settings_romselect_state {
    char *romPath;
    char *romFolder;
    e9ui_component_t *romSelect;
    e9ui_component_t *folderSelect;
    int suppress;
} settings_romselect_state_t;

typedef struct settings_systemtype_state {
    e9ui_component_t *aesCheckbox;
    e9ui_component_t *mvsCheckbox;
    char            *systemType;
    int              updating;
} settings_systemtype_state_t;

static int
settings_pathExistsFile(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat statBuffer;
    if (stat(path, &statBuffer) != 0) {
        return 0;
    }
    return S_ISREG(statBuffer.st_mode) ? 1 : 0;
}

static int
settings_pathExistsDir(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat statBuffer;
    if (stat(path, &statBuffer) != 0) {
        return 0;
    }
    return S_ISDIR(statBuffer.st_mode) ? 1 : 0;
}

static void
settings_copyPath(char *dest, size_t capacity, const char *src)
{
    if (!dest || capacity == 0) {
        return;
    }
    if (!src || !*src) {
        dest[0] = '\0';
        return;
    }
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            int written = snprintf(dest, capacity, "%s%s", home, src + 1);
            if (written < 0 || (size_t)written >= capacity) {
                dest[capacity - 1] = '\0';
            }
            return;
        }
    }
    strncpy(dest, src, capacity - 1);
    dest[capacity - 1] = '\0';
}

static void
settings_config_setPath(char *dest, size_t capacity, const char *value)
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

static void
settings_config_setValue(char *dest, size_t capacity, const char *value)
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

static void
settings_copyConfig(e9k_path_config_t *dest, const e9k_path_config_t *src)
{
    if (!dest || !src) {
        return;
    }
    settings_copyPath(dest->corePath, sizeof(dest->corePath), src->corePath);
    settings_copyPath(dest->romPath, sizeof(dest->romPath), src->romPath);
    settings_copyPath(dest->romFolder, sizeof(dest->romFolder), src->romFolder);
    settings_copyPath(dest->elfPath, sizeof(dest->elfPath), src->elfPath);
    settings_copyPath(dest->biosDir, sizeof(dest->biosDir), src->biosDir);
    settings_copyPath(dest->savesDir, sizeof(dest->savesDir), src->savesDir);
    settings_copyPath(dest->sourceDir, sizeof(dest->sourceDir), src->sourceDir);
    settings_config_setValue(dest->systemType, sizeof(dest->systemType), src->systemType);
    dest->audioBufferMs = src->audioBufferMs;
    dest->audioEnabled = src->audioEnabled;
    dest->skipBiosLogo = src->skipBiosLogo;
    dest->crtEnabled = src->crtEnabled;
}

static void
settings_closeModal(void)
{
    if (!debugger.ui.settingsModal) {
        return;
    }
    e9ui_setHidden(debugger.ui.settingsModal, 1);
    if (!debugger.ui.pendingRemove) {
        debugger.ui.pendingRemove = debugger.ui.settingsModal;
    }
    debugger.ui.settingsModal = NULL;
    debugger.ui.settingsSaveButton = NULL;
}

void
settings_cancelModal(void)
{
    if (!debugger.ui.settingsModal) {
        return;
    }
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);
    settings_closeModal();
}

void
settings_updateButton(int settingsOk)
{
    if (!debugger.ui.settingsButton) {
        return;
    }
    if (!settingsOk) {
        e9ui_button_setTheme(debugger.ui.settingsButton, e9ui_theme_button_preset_red());
        e9ui_button_setGlowPulse(debugger.ui.settingsButton, 1);
    } else {
        e9ui_button_clearTheme(debugger.ui.settingsButton);
        e9ui_button_setGlowPulse(debugger.ui.settingsButton, 0);
    }
}

static int
settings_configMissingPathsFor(const e9k_path_config_t *cfg)
{
    if (!cfg) {
        return 1;
    }
    if (!cfg->corePath[0] ||
        (!cfg->romPath[0] && !cfg->romFolder[0]) ||
        !cfg->biosDir[0] ||
        !cfg->savesDir[0] ||
        !settings_pathExistsFile(cfg->corePath) ||
        !settings_pathExistsDir(cfg->biosDir) ||
        !settings_pathExistsDir(cfg->savesDir)) {
        return 1;
    }
    if (cfg->romPath[0] && !settings_pathExistsFile(cfg->romPath)) {
        return 1;
    }
    if (cfg->romFolder[0] && !settings_pathExistsDir(cfg->romFolder)) {
        return 1;
    }
    if (cfg->elfPath[0] && !settings_pathExistsFile(cfg->elfPath)) {
        return 1;
    }
    if (cfg->sourceDir[0] && !settings_pathExistsDir(cfg->sourceDir)) {
        return 1;
    }
    return 0;
}

static int
settings_configMissingPaths(void)
{
    return settings_configMissingPathsFor(&debugger.config);
}

static int
settings_configIsOkFor(const e9k_path_config_t *cfg)
{
    return settings_configMissingPathsFor(cfg) ? 0 : 1;
}

int
settings_configIsOk(void)
{
    return settings_configIsOkFor(&debugger.config);
}

static int
settings_needsRestart(void)
{
    int romChanged = strcmp(debugger.config.romPath, debugger.settingsEdit.romPath) != 0 ||
                     strcmp(debugger.config.romFolder, debugger.settingsEdit.romFolder) != 0;
    int elfChanged = strcmp(debugger.config.elfPath, debugger.settingsEdit.elfPath) != 0;
    int biosChanged = strcmp(debugger.config.biosDir, debugger.settingsEdit.biosDir) != 0;
    int savesChanged = strcmp(debugger.config.savesDir, debugger.settingsEdit.savesDir) != 0;
    int sourceChanged = strcmp(debugger.config.sourceDir, debugger.settingsEdit.sourceDir) != 0;
    int coreChanged = strcmp(debugger.config.corePath, debugger.settingsEdit.corePath) != 0;
    int sysChanged = strcmp(debugger.config.systemType, debugger.settingsEdit.systemType) != 0;
    int audioBefore = debugger.config.audioBufferMs > 0 ? debugger.config.audioBufferMs : 50;
    int audioAfter = debugger.settingsEdit.audioBufferMs > 0 ? debugger.settingsEdit.audioBufferMs : 50;
    int audioChanged = audioBefore != audioAfter;
    int okBefore = settings_configIsOkFor(&debugger.config);
    int okAfter = settings_configIsOkFor(&debugger.settingsEdit);
    int okFixed = (!okBefore && okAfter);
    return romChanged || elfChanged || biosChanged || savesChanged || sourceChanged ||
           coreChanged || sysChanged || audioChanged || okFixed;
}

static void
settings_updateSaveLabel(void)
{
    if (!debugger.ui.settingsSaveButton) {
        return;
    }
    const char *label = settings_needsRestart() ? "Save and Restart" : "Save";
    e9ui_button_setLabel(debugger.ui.settingsSaveButton, label);
}

void
settings_applyToolbarMode(void)
{
    if (!debugger.ui.toolbar || !debugger.ui.settingsButton) {
        return;
    }
    if (!settings_configMissingPaths()) {
        return;
    }
    int childCount = list_count(debugger.ui.toolbar->children);
    if (childCount <= 0) {
        return;
    }
    e9ui_component_t **kids = (e9ui_component_t**)alloc_calloc((size_t)childCount, sizeof(*kids));
    if (!kids) {
        return;
    }
    int childTotal = e9ui_child_enumerateREMOVETHIS(debugger.ui.toolbar, &debugger.ui.ctx, kids, childCount);
    for (int childIndex = 0; childIndex < childTotal; ++childIndex) {
        if (kids[childIndex] && kids[childIndex] != debugger.ui.settingsButton) {
            e9ui_childRemove(debugger.ui.toolbar, kids[childIndex], &debugger.ui.ctx);
        }
    }
    alloc_free(kids);
    debugger.ui.profileButton = NULL;
    debugger.ui.analyseButton = NULL;
    debugger.ui.speedButton = NULL;
    debugger.ui.restartButton = NULL;
    debugger.ui.resetButton = NULL;
}

static int
settings_checkboxGetMargin(const e9ui_context_t *ctx)
{
    int base = debugger.theme.checkbox.margin;
    if (base <= 0) {
        base = E9UI_THEME_CHECKBOX_MARGIN;
    }
    int scaled = e9ui_scale_px(ctx, base);
    return scaled > 0 ? scaled : base;
}

static int
settings_checkboxGetTextGap(const e9ui_context_t *ctx)
{
    int base = debugger.theme.checkbox.textGap;
    if (base <= 0) {
        base = E9UI_THEME_CHECKBOX_TEXT_GAP;
    }
    int scaled = e9ui_scale_px(ctx, base);
    return scaled > 0 ? scaled : base;
}

static int
settings_checkboxMeasureWidth(const char *label, e9ui_context_t *ctx)
{
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int textW = 0;
    int textH = 0;
    if (font && label && *label) {
        TTF_SizeText(font, label, &textW, &textH);
    }
    int lineHeight = font ? TTF_FontHeight(font) : 16;
    int pad = settings_checkboxGetMargin(ctx);
    int height = pad + lineHeight + pad;
    int size = height > 24 ? 24 : (height - 4 > 0 ? height - 4 : 16);
    int gap = settings_checkboxGetTextGap(ctx);
    return size + gap + textW;
}

static void
settings_cancel(void)
{
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);
    settings_closeModal();
}

static void
settings_save(void)
{
    int needsRestart = settings_needsRestart();
    if (debugger.settingsEdit.audioBufferMs <= 0) {
        debugger.settingsEdit.audioBufferMs = 50;
    }
    settings_copyConfig(&debugger.config, &debugger.settingsEdit);
    crt_setEnabled(debugger.config.crtEnabled ? 1 : 0);
    debugger_libretroSetupPaths();
    debugger_applyCoreOptions();
    debugger_refreshElfValid();
    debugger.settingsOk = settings_configIsOk();
    settings_updateButton(debugger.settingsOk);
    settings_applyToolbarMode();
    config_saveConfig();
    if (needsRestart) {
        debugger.restartRequested = 1;
    }
    settings_closeModal();
}

static void
settings_uiClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    (void)user;
    settings_cancel();
}

static void
settings_uiCancel(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    settings_cancel();
}

static void
settings_uiSave(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    settings_save();
}

static void
settings_pathChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    char *dest = (char*)user;
    if (!dest) {
        return;
    }
    settings_config_setPath(dest, PATH_MAX, text);
    settings_updateSaveLabel();
}

static void
settings_updateRomSelectAllowEmpty(settings_romselect_state_t *st)
{
    if (!st) {
        return;
    }
    int hasRom = st->romPath && st->romPath[0];
    int hasFolder = st->romFolder && st->romFolder[0];
    int allowRomEmpty = hasFolder ? 1 : 0;
    int allowFolderEmpty = hasRom ? 1 : 0;
    if (st->romSelect) {
        e9ui_fileSelect_setAllowEmpty(st->romSelect, allowRomEmpty);
    }
    if (st->folderSelect) {
        e9ui_fileSelect_setAllowEmpty(st->folderSelect, allowFolderEmpty);
    }
}

static void
settings_romPathChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    settings_romselect_state_t *st = (settings_romselect_state_t *)user;
    if (!st || st->suppress) {
        return;
    }
    settings_config_setPath(st->romPath, PATH_MAX, text);
    if (text && *text) {
        st->suppress = 1;
        settings_config_setPath(st->romFolder, PATH_MAX, "");
        if (st->folderSelect) {
            e9ui_fileSelect_setText(st->folderSelect, "");
        }
        st->suppress = 0;
    }
    settings_updateRomSelectAllowEmpty(st);
    settings_updateSaveLabel();
}

static void
settings_romFolderChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    settings_romselect_state_t *st = (settings_romselect_state_t *)user;
    if (!st || st->suppress) {
        return;
    }
    settings_config_setPath(st->romFolder, PATH_MAX, text);
    if (text && *text) {
        st->suppress = 1;
        settings_config_setPath(st->romPath, PATH_MAX, "");
        if (st->romSelect) {
            e9ui_fileSelect_setText(st->romSelect, "");
        }
        st->suppress = 0;
    }
    settings_updateRomSelectAllowEmpty(st);
    settings_updateSaveLabel();
}

static void
settings_audioChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    int *dest = (int*)user;
    if (!dest) {
        return;
    }
    if (!text || !*text) {
        *dest = 0;
        settings_updateSaveLabel();
        return;
    }
    char *end = NULL;
    long ms = strtol(text, &end, 10);
    if (!end || end == text) {
        *dest = 0;
        settings_updateSaveLabel();
        return;
    }
    if (ms < 0) {
        ms = 0;
    }
    if (ms > INT_MAX) {
        ms = INT_MAX;
    }
    *dest = (int)ms;
    settings_updateSaveLabel();
}

static void
settings_skipBiosChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
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
settings_funChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;
    if (selected) {
        if (debugger.transitionMode == e9k_transition_none) {
            debugger.transitionMode = e9k_transition_random;
        }
    } else {
        debugger.transitionMode = e9k_transition_none;
    }
    debugger.transitionFullscreenModeSet = 0;
    config_saveConfig();
}

static void
settings_crtChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    int *flag = (int *)user;
    if (!flag) {
        return;
    }
    *flag = selected ? 1 : 0;
    settings_updateSaveLabel();
}

static void
settings_systemTypeSync(settings_systemtype_state_t *st, const char *value, e9ui_context_t *ctx)
{
    if (!st || !st->systemType) {
        return;
    }
    st->updating = 1;
    settings_config_setValue(st->systemType, 16, value);
    int aesSelected = (strcmp(st->systemType, "aes") == 0);
    int mvsSelected = (strcmp(st->systemType, "mvs") == 0);
    if (st->aesCheckbox) {
        e9ui_checkbox_setSelected(st->aesCheckbox, aesSelected, ctx);
    }
    if (st->mvsCheckbox) {
        e9ui_checkbox_setSelected(st->mvsCheckbox, mvsSelected, ctx);
    }
    st->updating = 0;
    settings_updateSaveLabel();
}

static void
settings_systemTypeAesChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    settings_systemtype_state_t *st = (settings_systemtype_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_systemTypeSync(st, "aes", ctx);
    } else if (st->mvsCheckbox && e9ui_checkbox_isSelected(st->mvsCheckbox)) {
        settings_systemTypeSync(st, "mvs", ctx);
    } else {
        settings_systemTypeSync(st, "", ctx);
    }
}

static void
settings_systemTypeMvsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    settings_systemtype_state_t *st = (settings_systemtype_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    if (selected) {
        settings_systemTypeSync(st, "mvs", ctx);
    } else if (st->aesCheckbox && e9ui_checkbox_isSelected(st->aesCheckbox)) {
        settings_systemTypeSync(st, "aes", ctx);
    } else {
        settings_systemTypeSync(st, "", ctx);
    }
}

void
settings_uiOpen(e9ui_context_t *ctx, void *user)
{
    (void)user;
    if (!ctx) {
        return;
    }
    if (debugger.ui.settingsModal) {
        return;
    }
    int margin = e9ui_scale_px(ctx, 32);
    int modalWidth = ctx->winW - margin * 2;
    int modalHeight = ctx->winH - margin * 2;
    if (modalWidth < 1) modalWidth = 1;
    if (modalHeight < 1) modalHeight = 1;
    e9ui_rect_t rect = { margin, margin, modalWidth, modalHeight };
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);
    debugger.ui.settingsModal = e9ui_modal_show(ctx, "Settings", rect, settings_uiClosed, NULL);
    if (debugger.ui.settingsModal) {
        const char *rom_exts[] = { "*.neo" };
        const char *elf_exts[] = { "*.elf" };
        e9ui_component_t *fs_rom = e9ui_fileSelect_make("ROM", 120, 600, "...", rom_exts, 1, E9UI_FILESELECT_FILE);
        e9ui_component_t *fs_rom_folder = e9ui_fileSelect_make("ROM FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        e9ui_component_t *fs_elf = e9ui_fileSelect_make("ELF", 120, 600, "...", elf_exts, 1, E9UI_FILESELECT_FILE);
        e9ui_component_t *fs_bios = e9ui_fileSelect_make("BIOS FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        e9ui_component_t *fs_saves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        e9ui_component_t *fs_source = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        e9ui_component_t *fs_core = e9ui_fileSelect_make("CORE", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FILE);
        e9ui_component_t *cb_skip = e9ui_checkbox_make("SKIP BIOS LOGO",
                                                       debugger.settingsEdit.skipBiosLogo,
                                                       settings_skipBiosChanged,
                                                       &debugger.settingsEdit.skipBiosLogo);
        e9ui_component_t *cb_crt = e9ui_checkbox_make("CRT",
                                                      debugger.settingsEdit.crtEnabled,
                                                      settings_crtChanged,
                                                      &debugger.settingsEdit.crtEnabled);
        int funSelected = (debugger.transitionMode != e9k_transition_none);
        e9ui_component_t *cb_fun = e9ui_checkbox_make("FUN",
                                                      funSelected,
                                                      settings_funChanged,
                                                      NULL);
        settings_systemtype_state_t *sys_state = (settings_systemtype_state_t *)alloc_calloc(1, sizeof(*sys_state));
        int aesSelected = (strcmp(debugger.settingsEdit.systemType, "aes") == 0);
        int mvsSelected = (strcmp(debugger.settingsEdit.systemType, "mvs") == 0);
        e9ui_component_t *cb_aes = e9ui_checkbox_make("AES", aesSelected, settings_systemTypeAesChanged, sys_state);
        e9ui_component_t *cb_mvs = e9ui_checkbox_make("MVS", mvsSelected, settings_systemTypeMvsChanged, sys_state);
        e9ui_component_t *row_system = e9ui_hstack_make();
        e9ui_component_t *row_center = row_system ? e9ui_center_make(row_system) : NULL;
        e9ui_component_t *lt_audio = e9ui_labeled_textbox_make("AUDIO BUFFER MS", 120, 600,
                                                              settings_audioChanged,
                                                              &debugger.settingsEdit.audioBufferMs);
        e9ui_fileSelect_setText(fs_rom, debugger.settingsEdit.romPath);
        e9ui_fileSelect_setText(fs_rom_folder, debugger.settingsEdit.romFolder);
        e9ui_fileSelect_setText(fs_elf, debugger.settingsEdit.elfPath);
        e9ui_fileSelect_setAllowEmpty(fs_elf, 1);
        e9ui_fileSelect_setText(fs_bios, debugger.settingsEdit.biosDir);
        e9ui_fileSelect_setText(fs_saves, debugger.settingsEdit.savesDir);
        e9ui_fileSelect_setText(fs_source, debugger.settingsEdit.sourceDir);
        e9ui_fileSelect_setText(fs_core, debugger.settingsEdit.corePath);
        if (sys_state) {
            sys_state->aesCheckbox = cb_aes;
            sys_state->mvsCheckbox = cb_mvs;
            sys_state->systemType = debugger.settingsEdit.systemType;
        }
        if (row_system && ctx) {
            int gap = e9ui_scale_px(ctx, 12);
            int w_mvs = cb_mvs ? settings_checkboxMeasureWidth("MVS", ctx) : 0;
            int w_aes = cb_aes ? settings_checkboxMeasureWidth("AES", ctx) : 0;
            int w_skip = cb_skip ? settings_checkboxMeasureWidth("SKIP BIOS LOGO", ctx) : 0;
            int w_fun = cb_fun ? settings_checkboxMeasureWidth("FUN", ctx) : 0;
            int w_crt = cb_crt ? settings_checkboxMeasureWidth("CRT", ctx) : 0;
            int totalW = 0;
            if (cb_mvs) {
                e9ui_hstack_addFixed(row_system, cb_mvs, w_mvs);
                totalW += w_mvs;
            }
            if (cb_aes) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(row_system, e9ui_spacer_make(gap), gap);
                    totalW += gap;
                }
                e9ui_hstack_addFixed(row_system, cb_aes, w_aes);
                totalW += w_aes;
            }
            if (cb_skip) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(row_system, e9ui_spacer_make(gap), gap);
                    totalW += gap;
                }
                e9ui_hstack_addFixed(row_system, cb_skip, w_skip);
                totalW += w_skip;
            }
            if (cb_fun) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(row_system, e9ui_spacer_make(gap), gap);
                    totalW += gap;
                }
                e9ui_hstack_addFixed(row_system, cb_fun, w_fun);
                totalW += w_fun;
            }
            if (cb_crt) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(row_system, e9ui_spacer_make(gap), gap);
                    totalW += gap;
                }
                e9ui_hstack_addFixed(row_system, cb_crt, w_crt);
                totalW += w_crt;
            }
            if (row_center) {
                e9ui_center_setSize(row_center, e9ui_unscale_px(ctx, totalW), 0);
            }
        }
        if (lt_audio) {
            char buf[32];
            if (debugger.settingsEdit.audioBufferMs > 0) {
                snprintf(buf, sizeof(buf), "%d", debugger.settingsEdit.audioBufferMs);
                e9ui_labeled_textbox_setText(lt_audio, buf);
            } else {
                e9ui_labeled_textbox_setText(lt_audio, "");
            }
            e9ui_component_t *tb = e9ui_labeled_textbox_getTextbox(lt_audio);
            if (tb) {
                e9ui_textbox_setNumericOnly(tb, 1);
            }
        }
        settings_romselect_state_t *rom_state = (settings_romselect_state_t *)alloc_calloc(1, sizeof(*rom_state));
        if (rom_state) {
            rom_state->romPath = debugger.settingsEdit.romPath;
            rom_state->romFolder = debugger.settingsEdit.romFolder;
            rom_state->romSelect = fs_rom;
            rom_state->folderSelect = fs_rom_folder;
            settings_updateRomSelectAllowEmpty(rom_state);
        }
        e9ui_fileSelect_setOnChange(fs_rom, settings_romPathChanged, rom_state);
        e9ui_fileSelect_setOnChange(fs_rom_folder, settings_romFolderChanged, rom_state);
        e9ui_fileSelect_setOnChange(fs_elf, settings_pathChanged, debugger.settingsEdit.elfPath);
        e9ui_fileSelect_setOnChange(fs_bios, settings_pathChanged, debugger.settingsEdit.biosDir);
        e9ui_fileSelect_setOnChange(fs_saves, settings_pathChanged, debugger.settingsEdit.savesDir);
        e9ui_fileSelect_setOnChange(fs_source, settings_pathChanged, debugger.settingsEdit.sourceDir);
        e9ui_fileSelect_setOnChange(fs_core, settings_pathChanged, debugger.settingsEdit.corePath);
        e9ui_component_t *stack = e9ui_stack_makeVertical();
        e9ui_component_t *gap = e9ui_vspacer_make(12);
        e9ui_stack_addFixed(stack, fs_rom);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fs_rom_folder);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fs_elf);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fs_source);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fs_bios);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fs_saves);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fs_core);
        if (lt_audio) {
            e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
            e9ui_stack_addFixed(stack, lt_audio);
        }
        if (row_center) {
            e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
            e9ui_stack_addFixed(stack, row_center);
        }
        e9ui_component_t *center = e9ui_center_make(stack);
        e9ui_component_t *btn_save = e9ui_button_make("Save", settings_uiSave, NULL);
        e9ui_component_t *btn_cancel = e9ui_button_make("Cancel", settings_uiCancel, NULL);
        debugger.ui.settingsSaveButton = btn_save;
        settings_updateSaveLabel();
        e9ui_component_t *footer = e9ui_flow_make();
        e9ui_flow_setPadding(footer, 0);
        e9ui_flow_setSpacing(footer, 8);
        e9ui_flow_setWrap(footer, 0);
        if (btn_save) {
            e9ui_button_setTheme(btn_save, e9ui_theme_button_preset_green());
            e9ui_button_setGlowPulse(btn_save, 1);
            e9ui_flow_add(footer, btn_save);
        }
        if (btn_cancel) {
            e9ui_button_setTheme(btn_cancel, e9ui_theme_button_preset_red());
            e9ui_button_setGlowPulse(btn_cancel, 1);
            e9ui_flow_add(footer, btn_cancel);
        }
        int contentW = e9ui_scale_px(ctx, 600);
        int h1 = fs_rom->preferredHeight ? fs_rom->preferredHeight(fs_rom, ctx, contentW) : 0;
        int h2 = fs_rom_folder->preferredHeight ? fs_rom_folder->preferredHeight(fs_rom_folder, ctx, contentW) : 0;
        int h3 = fs_elf->preferredHeight ? fs_elf->preferredHeight(fs_elf, ctx, contentW) : 0;
        int h4 = fs_source->preferredHeight ? fs_source->preferredHeight(fs_source, ctx, contentW) : 0;
        int h5 = fs_bios->preferredHeight ? fs_bios->preferredHeight(fs_bios, ctx, contentW) : 0;
        int h6 = fs_saves->preferredHeight ? fs_saves->preferredHeight(fs_saves, ctx, contentW) : 0;
        int h7 = fs_core->preferredHeight ? fs_core->preferredHeight(fs_core, ctx, contentW) : 0;
        int hSys = row_center && row_center->preferredHeight ? row_center->preferredHeight(row_center, ctx, contentW) : 0;
        int h8 = lt_audio && lt_audio->preferredHeight ? lt_audio->preferredHeight(lt_audio, ctx, contentW) : 0;
        int hGap = gap->preferredHeight ? gap->preferredHeight(gap, ctx, contentW) : 0;
        int contentH = h1 + hGap + h2 + hGap + h3 + hGap + h4 + hGap + h5 +
                       hGap + h6 + hGap + h7;
        if (lt_audio) {
            contentH += hGap + h8;
        }
        if (row_center) {
            contentH += hGap + hSys;
        }
        e9ui_center_setSize(center, 640, e9ui_unscale_px(ctx, contentH));
        e9ui_component_t *overlay = e9ui_overlay_make(center, footer);
        e9ui_overlay_setAnchor(overlay, e9ui_anchor_bottom_right);
        e9ui_overlay_setMargin(overlay, 12);
        e9ui_modal_setBodyChild(debugger.ui.settingsModal, overlay, ctx);
    }
}
