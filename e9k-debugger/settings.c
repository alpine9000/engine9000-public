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
#include <stdint.h>
#include <sys/stat.h>

#include "settings.h"
#include "alloc.h"
#include "crt.h"
#include "core_options.h"
#include "debugger.h"
#include "config.h"
#include "list.h"
#include "amiga_uae_options.h"
#include "neogeo_core_options.h"
#include "system_badge.h"

void
debugger_platform_setDefaults(e9k_neogeo_config_t *config);

void
debugger_platform_setDefaultsAmiga(e9k_amiga_config_t *config);

static int
settings_neogeoEffectiveRomPath(const e9k_neogeo_config_t *cfg, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!cfg) {
        return 0;
    }
    if (cfg->libretro.romPath[0]) {
        snprintf(out, cap, "%s", cfg->libretro.romPath);
        out[cap - 1] = '\0';
        return 1;
    }
    if (!cfg->romFolder[0]) {
        return 0;
    }
    const char *base = cfg->libretro.saveDir[0] ? cfg->libretro.saveDir : cfg->libretro.systemDir;
    if (!base || !*base) {
        return 0;
    }
    char sep = '/';
    if (strchr(base, '\\')) {
        sep = '\\';
    }
    int needsSep = 1;
    size_t len = strlen(base);
    if (len > 0 && (base[len - 1] == '/' || base[len - 1] == '\\')) {
        needsSep = 0;
    }
    int written = 0;
    if (needsSep) {
        written = snprintf(out, cap, "%s%c%s", base, sep, "e9k-romfolder.neo");
    } else {
        written = snprintf(out, cap, "%s%s", base, "e9k-romfolder.neo");
    }
    if (written < 0 || (size_t)written >= cap) {
        out[cap - 1] = '\0';
        return 0;
    }
    return 1;
}


typedef struct settings_romselect_state {
    char *romPath;
    char *romFolder;
    char *corePath;
    e9ui_component_t *romSelect;
    e9ui_component_t *folderSelect;
    e9ui_component_t *coreSelect;
    e9ui_component_t *df0Select;
    e9ui_component_t *df1Select;
    e9ui_component_t *hd0Select;
    int suppress;
} settings_romselect_state_t;

typedef struct settings_systemtype_state {
    e9ui_component_t *aesCheckbox;
    e9ui_component_t *mvsCheckbox;
    char            *systemType;
    int              updating;
} settings_systemtype_state_t;

typedef struct settings_coresystem_state {
    e9ui_component_t       *neogeoCheckbox;
    e9ui_component_t       *amigaCheckbox;
    e9ui_component_t       *coreSelectNeogeo;
    e9ui_component_t       *coreSelectAmiga;
    debugger_system_type_t *coreSystem;
    char                  *corePathNeogeo;
    char                  *corePathAmiga;
    int                    updating;
    int                    allowRebuild;
} settings_coresystem_state_t;

typedef struct settings_toolchainprefix_state {
    char                  *prefix;
    debugger_system_type_t system;
} settings_toolchainprefix_state_t;

static void
settings_rebuildModalBody(e9ui_context_t *ctx);

static e9ui_component_t *
settings_makeSystemBadge(e9ui_context_t *ctx, debugger_system_type_t coreSystem);

static int settings_pendingRebuild = 0;
static int settings_coreOptionsDirty = 0;

void
settings_markCoreOptionsDirty(void)
{
    settings_coreOptionsDirty = 1;
}

void
settings_clearCoreOptionsDirty(void)
{
    settings_coreOptionsDirty = 0;
}

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

static int
settings_pathHasUaeExtension(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    size_t len = strlen(path);
    if (len < 4) {
        return 0;
    }
    const char *ext = path + len - 4;
    if (ext[0] != '.') {
        return 0;
    }
    char a = ext[1];
    char b = ext[2];
    char c = ext[3];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return (a == 'u' && b == 'a' && c == 'e') ? 1 : 0;
}

static int
settings_validateUaeConfig(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    (void)user;
    return settings_pathHasUaeExtension(text) ? 1 : 0;
}

//static
void
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
settings_copyConfig(e9k_system_config_t *dest, const e9k_system_config_t *src)
{
    if (!dest || !src) {
        return;
    }
    memcpy(dest, src, sizeof(e9k_system_config_t));
}

static void
settings_closeModal(void)
{
    if (!e9ui->settingsModal) {
        return;
    }
    settings_clearCoreOptionsDirty();
    settings_pendingRebuild = 0;
    e9ui_setHidden(e9ui->settingsModal, 1);
    if (!e9ui->pendingRemove) {
        e9ui->pendingRemove = e9ui->settingsModal;
    }
    e9ui->settingsModal = NULL;
    e9ui->settingsSaveButton = NULL;
}

void
settings_cancelModal(void)
{
    if (!e9ui->settingsModal) {
        return;
    }
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);
    amiga_uaeClearPuaeOptions();
    neogeo_coreOptionsClear();
    settings_clearCoreOptionsDirty();
    settings_closeModal();
}

void
settings_updateButton(int settingsOk)
{
    if (!e9ui->settingsButton) {
        return;
    }
    if (!settingsOk) {
        e9ui_button_setTheme(e9ui->settingsButton, e9ui_theme_button_preset_red());
        e9ui_button_setGlowPulse(e9ui->settingsButton, 1);
    } else {
        e9ui_button_clearTheme(e9ui->settingsButton);
        e9ui_button_setGlowPulse(e9ui->settingsButton, 0);
    }
}

static int
settings_configMissingPathsFor(const e9k_neogeo_config_t *cfg)
{
    if (!cfg) {
        return 1;
    }
    if (!cfg->libretro.corePath[0] ||
        (!cfg->libretro.romPath[0] && !cfg->romFolder[0]) ||
        !cfg->libretro.systemDir[0] ||
        !cfg->libretro.saveDir[0] ||
        !settings_pathExistsFile(cfg->libretro.corePath) ||
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
    if (cfg->libretro.elfPath[0] && !settings_pathExistsFile(cfg->libretro.elfPath)) {
        return 1;
    }
    if (cfg->libretro.sourceDir[0] && !settings_pathExistsDir(cfg->libretro.sourceDir)) {
        return 1;
    }
    return 0;
}

static int
settings_configMissingPathsForAmiga(const e9k_amiga_config_t *cfg)
{
    if (!cfg) {
        return 1;
    }
    if (!cfg->libretro.corePath[0] ||
        !cfg->libretro.romPath[0] ||
        !cfg->libretro.systemDir[0] ||
        !cfg->libretro.saveDir[0] ||
        !settings_pathExistsFile(cfg->libretro.corePath) ||
        !settings_pathHasUaeExtension(cfg->libretro.romPath) ||
        !settings_pathExistsFile(cfg->libretro.romPath) ||
        !settings_pathExistsDir(cfg->libretro.systemDir) ||
        !settings_pathExistsDir(cfg->libretro.saveDir)) {
        return 1;
    }
    if (cfg->libretro.elfPath[0] && !settings_pathExistsFile(cfg->libretro.elfPath)) {
        return 1;
    }
    if (cfg->libretro.sourceDir[0] && !settings_pathExistsDir(cfg->libretro.sourceDir)) {
        return 1;
    }
    return 0;
}

static int
settings_configIsOkFor(const e9k_neogeo_config_t *cfg)
{
    return settings_configMissingPathsFor(cfg) ? 0 : 1;
}

static int
settings_configIsOkForAmiga(const e9k_amiga_config_t *cfg)
{
    return settings_configMissingPathsForAmiga(cfg) ? 0 : 1;
}

static int
settings_configMissingPathsForSystem(const e9k_system_config_t *cfg)
{
    if (!cfg) {
        return 1;
    }
    if (cfg->coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        return settings_configMissingPathsForAmiga(&cfg->amiga);
    }
    return settings_configMissingPathsFor(&cfg->neogeo);
}

static int
settings_configMissingPaths(void)
{
    return settings_configMissingPathsForSystem(&debugger.config);
}

int
settings_configIsOk(void)
{
    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        return settings_configIsOkForAmiga(&debugger.config.amiga);
    }
    return settings_configIsOkFor(&debugger.config.neogeo);
}

static int
settings_audioBufferNormalized(int value)
{
    return value > 0 ? value : 50;
}

static int
settings_restartNeededForNeogeo(const e9k_neogeo_config_t *before, const e9k_neogeo_config_t *after)
{
    if (!before || !after) {
        return 1;
    }
    int romChanged = strcmp(before->libretro.romPath, after->libretro.romPath) != 0 ||
                     strcmp(before->romFolder, after->romFolder) != 0;
    int elfChanged = strcmp(before->libretro.elfPath, after->libretro.elfPath) != 0;
    int toolchainChanged = strcmp(before->libretro.toolchainPrefix, after->libretro.toolchainPrefix) != 0;
    int biosChanged = strcmp(before->libretro.systemDir, after->libretro.systemDir) != 0;
    int savesChanged = strcmp(before->libretro.saveDir, after->libretro.saveDir) != 0;
    int sourceChanged = strcmp(before->libretro.sourceDir, after->libretro.sourceDir) != 0;
    int coreChanged = strcmp(before->libretro.corePath, after->libretro.corePath) != 0;
    int sysChanged = strcmp(before->systemType, after->systemType) != 0;
    int audioBefore = settings_audioBufferNormalized(before->libretro.audioBufferMs);
    int audioAfter = settings_audioBufferNormalized(after->libretro.audioBufferMs);
    int audioChanged = audioBefore != audioAfter;
    return romChanged || elfChanged || toolchainChanged || biosChanged || savesChanged || sourceChanged || coreChanged || sysChanged || audioChanged;
}

static int
settings_restartNeededForAmiga(const e9k_amiga_config_t *before, const e9k_amiga_config_t *after)
{
    if (!before || !after) {
        return 1;
    }
    int romChanged = strcmp(before->libretro.romPath, after->libretro.romPath) != 0;
    int elfChanged = strcmp(before->libretro.elfPath, after->libretro.elfPath) != 0;
    int toolchainChanged = strcmp(before->libretro.toolchainPrefix, after->libretro.toolchainPrefix) != 0;
    int biosChanged = strcmp(before->libretro.systemDir, after->libretro.systemDir) != 0;
    int savesChanged = strcmp(before->libretro.saveDir, after->libretro.saveDir) != 0;
    int sourceChanged = strcmp(before->libretro.sourceDir, after->libretro.sourceDir) != 0;
    int coreChanged = strcmp(before->libretro.corePath, after->libretro.corePath) != 0;
    int audioBefore = settings_audioBufferNormalized(before->libretro.audioBufferMs);
    int audioAfter = settings_audioBufferNormalized(after->libretro.audioBufferMs);
    int audioChanged = audioBefore != audioAfter;
    return romChanged || elfChanged || toolchainChanged || biosChanged || savesChanged || sourceChanged || coreChanged || audioChanged;
}

static int
settings_needsRestart(void)
{
    int coreSystemChanged = (debugger.config.coreSystem != debugger.settingsEdit.coreSystem);
    debugger_system_type_t selected = debugger.settingsEdit.coreSystem;
    int configChanged = 0;
    int okBefore = 0;
    int okAfter = 0;
    if (selected == DEBUGGER_SYSTEM_AMIGA) {
        configChanged = settings_restartNeededForAmiga(&debugger.config.amiga, &debugger.settingsEdit.amiga);
        if (amiga_uaeUaeOptionsDirty()) {
            configChanged = 1;
        }
        if (settings_coreOptionsDirty) {
            configChanged = 1;
        }
        okBefore = settings_configIsOkForAmiga(&debugger.config.amiga);
        okAfter = settings_configIsOkForAmiga(&debugger.settingsEdit.amiga);
    } else {
        configChanged = settings_restartNeededForNeogeo(&debugger.config.neogeo, &debugger.settingsEdit.neogeo);
        if (settings_coreOptionsDirty) {
            configChanged = 1;
        }
        okBefore = settings_configIsOkFor(&debugger.config.neogeo);
        okAfter = settings_configIsOkFor(&debugger.settingsEdit.neogeo);
    }
    int okFixed = (!okBefore && okAfter);
    return coreSystemChanged || configChanged || okFixed;
}

static void
settings_updateSaveLabel(void)
{
    if (!e9ui->settingsSaveButton) {
        return;
    }
    const char *label = settings_needsRestart() ? "Save and Restart" : "Save";
    e9ui_button_setLabel(e9ui->settingsSaveButton, label);

    if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        const char *uaePath = debugger.settingsEdit.amiga.libretro.romPath;
        e9ui->settingsSaveButton->disabled = (!uaePath || !uaePath[0] || !settings_pathHasUaeExtension(uaePath)) ? 1 : 0;
    } else {
        e9ui->settingsSaveButton->disabled = 0;
    }
}

static int
settings_shouldShowUaeExtensionWarning(void)
{
    if (debugger.settingsEdit.coreSystem != DEBUGGER_SYSTEM_AMIGA) {
        return 0;
    }
    const char *uaePath = debugger.settingsEdit.amiga.libretro.romPath;
    if (!uaePath || !uaePath[0]) {
        return 0;
    }
    if (settings_pathHasUaeExtension(uaePath)) {
        return 0;
    }
    return 1;
}

typedef struct settings_uae_extension_warning_state {
    SDL_Color color;
} settings_uae_extension_warning_state_t;

static int
settings_uaeExtensionWarning_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    if (!settings_shouldShowUaeExtensionWarning()) {
        return 0;
    }
    TTF_Font *font = ctx ? ctx->font : NULL;
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int pad = ctx ? e9ui_scale_px(ctx, 4) : 4;
    return lh + pad * 2;
}

static void
settings_uaeExtensionWarning_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
settings_uaeExtensionWarning_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    if (!settings_shouldShowUaeExtensionWarning()) {
        return;
    }
    settings_uae_extension_warning_state_t *st = (settings_uae_extension_warning_state_t*)self->state;
    if (!st) {
        return;
    }
    TTF_Font *font = ctx->font;
    if (!font) {
        return;
    }
    const char *msg = "UAE CONFIG filename must end with .uae";
    int tw = 0;
    int th = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, msg, st->color, &tw, &th);
    if (!tex) {
        return;
    }
    int x = self->bounds.x + self->bounds.w - tw;
    int y = self->bounds.y + (self->bounds.h - th) / 2;
    SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
}

static void
settings_uaeExtensionWarning_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

static e9ui_component_t *
settings_uaeExtensionWarning_make(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    settings_uae_extension_warning_state_t *st = (settings_uae_extension_warning_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    st->color = (SDL_Color){255, 80, 80, 255};
    c->name = "settings_uae_extension_warning";
    c->state = st;
    c->preferredHeight = settings_uaeExtensionWarning_preferredHeight;
    c->layout = settings_uaeExtensionWarning_layout;
    c->render = settings_uaeExtensionWarning_render;
    c->dtor = settings_uaeExtensionWarning_dtor;
    return c;
}

void
settings_refreshSaveLabel(void)
{
    settings_updateSaveLabel();
}

void
settings_applyToolbarMode(void)
{
    if (!e9ui->toolbar || !e9ui->settingsButton) {
        return;
    }
    if (!settings_configMissingPaths()) {
        return;
    }
    int childCount = list_count(e9ui->toolbar->children);
    if (childCount <= 0) {
        return;
    }
    e9ui_component_t **kids = (e9ui_component_t**)alloc_calloc((size_t)childCount, sizeof(*kids));
    if (!kids) {
        return;
    }
    int childTotal = e9ui_child_enumerateREMOVETHIS(e9ui->toolbar, &e9ui->ctx, kids, childCount);
    for (int childIndex = 0; childIndex < childTotal; ++childIndex) {
        if (kids[childIndex] && kids[childIndex] != e9ui->settingsButton) {
            e9ui_childRemove(e9ui->toolbar, kids[childIndex], &e9ui->ctx);
        }
    }
    alloc_free(kids);
    e9ui->profileButton = NULL;
    e9ui->analyseButton = NULL;
    e9ui->speedButton = NULL;
    e9ui->restartButton = NULL;
    e9ui->resetButton = NULL;
}

static int
settings_checkboxGetMargin(const e9ui_context_t *ctx)
{
    int base = e9ui->theme.checkbox.margin;
    if (base <= 0) {
        base = E9UI_THEME_CHECKBOX_MARGIN;
    }
    int scaled = e9ui_scale_px(ctx, base);
    return scaled > 0 ? scaled : base;
}

static int
settings_checkboxGetTextGap(const e9ui_context_t *ctx)
{
    int base = e9ui->theme.checkbox.textGap;
    if (base <= 0) {
        base = E9UI_THEME_CHECKBOX_TEXT_GAP;
    }
    int scaled = e9ui_scale_px(ctx, base);
    return scaled > 0 ? scaled : base;
}

static int
settings_checkboxMeasureWidth(const char *label, e9ui_context_t *ctx)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
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
    amiga_uaeClearPuaeOptions();
    neogeo_coreOptionsClear();
    settings_clearCoreOptionsDirty();
    settings_closeModal();
}

static void
settings_save(void)
{
    int needsRestart = settings_needsRestart();
    if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        if (debugger.settingsEdit.amiga.libretro.audioBufferMs <= 0) {
            debugger.settingsEdit.amiga.libretro.audioBufferMs = 50;
        }
    } else {
        if (debugger.settingsEdit.neogeo.libretro.audioBufferMs <= 0) {
            debugger.settingsEdit.neogeo.libretro.audioBufferMs = 50;
        }
    }

    if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
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
        amiga_uaeClearPuaeOptions();
    } else if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
        char romPath[PATH_MAX];
        if (settings_neogeoEffectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
            if (!neogeo_coreOptionsWriteToFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath)) {
                e9ui_showTransientMessage("CORE OPTIONS SAVE FAILED");
                return;
            }
        }
        neogeo_coreOptionsClear();
    }

    settings_copyConfig(&debugger.config, &debugger.settingsEdit);
    debugger_setCoreSystem(debugger.config.coreSystem);
    crt_setEnabled(debugger.config.crtEnabled ? 1 : 0);
    debugger_libretroSelectConfig();
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
settings_uiDefaults(e9ui_context_t *ctx, void *user)
{
    (void)user;
    if (!ctx || !e9ui->settingsModal) {
        return;
    }
    debugger_system_type_t system = debugger.settingsEdit.coreSystem;
    if (system == DEBUGGER_SYSTEM_AMIGA) {
        char uaePath[PATH_MAX];
        char elfPath[PATH_MAX];
        settings_copyPath(uaePath, sizeof(uaePath), debugger.settingsEdit.amiga.libretro.romPath);
        settings_copyPath(elfPath, sizeof(elfPath), debugger.settingsEdit.amiga.libretro.elfPath);
        int audioEnabled = debugger.settingsEdit.amiga.libretro.audioEnabled;
        debugger_platform_setDefaultsAmiga(&debugger.settingsEdit.amiga);
        debugger.settingsEdit.amiga.libretro.audioEnabled = audioEnabled;
        settings_copyPath(debugger.settingsEdit.amiga.libretro.romPath, sizeof(debugger.settingsEdit.amiga.libretro.romPath), uaePath);
        settings_copyPath(debugger.settingsEdit.amiga.libretro.elfPath, sizeof(debugger.settingsEdit.amiga.libretro.elfPath), elfPath);
        amiga_uaeClearPuaeOptions();
        if (debugger.settingsEdit.amiga.libretro.romPath[0]) {
            amiga_uaeLoadUaeOptions(debugger.settingsEdit.amiga.libretro.romPath);
        }
    } else {
        char romPath[PATH_MAX];
        char romFolder[PATH_MAX];
        char elfPath[PATH_MAX];
        settings_copyPath(romPath, sizeof(romPath), debugger.settingsEdit.neogeo.libretro.romPath);
        settings_copyPath(romFolder, sizeof(romFolder), debugger.settingsEdit.neogeo.romFolder);
        settings_copyPath(elfPath, sizeof(elfPath), debugger.settingsEdit.neogeo.libretro.elfPath);
        int audioEnabled = debugger.settingsEdit.neogeo.libretro.audioEnabled;
        debugger_platform_setDefaults(&debugger.settingsEdit.neogeo);
        debugger.settingsEdit.neogeo.libretro.audioEnabled = audioEnabled;
        settings_copyPath(debugger.settingsEdit.neogeo.libretro.romPath, sizeof(debugger.settingsEdit.neogeo.libretro.romPath), romPath);
        settings_copyPath(debugger.settingsEdit.neogeo.romFolder, sizeof(debugger.settingsEdit.neogeo.romFolder), romFolder);
        settings_copyPath(debugger.settingsEdit.neogeo.libretro.elfPath, sizeof(debugger.settingsEdit.neogeo.libretro.elfPath), elfPath);
    }
    settings_clearCoreOptionsDirty();
    neogeo_coreOptionsClear();
    settings_pendingRebuild = 1;
    e9ui_showTransientMessage("DEFAULTS RESTORED");
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

static const char *
settings_defaultCorePathForSystem(debugger_system_type_t system)
{
    switch (system) {
    case DEBUGGER_SYSTEM_AMIGA:
        return "./system/puae_libretro.dylib";
    case DEBUGGER_SYSTEM_NEOGEO:
    case DEBUGGER_SYSTEM_MEGADRIVE:
    default:
        return "./system/geolith_libretro.dylib";
    }
}

static void
settings_toolchainPrefixChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    settings_toolchainprefix_state_t *st = (settings_toolchainprefix_state_t *)user;
    if (!st || !st->prefix) {
        return;
    }
    settings_config_setValue(st->prefix, PATH_MAX, text ? text : "");
    settings_updateSaveLabel();
}

static int
settings_isDefaultCorePath(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    return strcmp(path, "./system/puae_libretro.dylib") == 0 ||
           strcmp(path, "./system/geolith_libretro.dylib") == 0;
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
        const char *defaultCore = settings_defaultCorePathForSystem(debugger.settingsEdit.coreSystem);
        if (defaultCore && st->corePath && (!st->corePath[0] || settings_isDefaultCorePath(st->corePath))) {
            settings_config_setPath(st->corePath, PATH_MAX, defaultCore);
            if (st->coreSelect) {
                e9ui_fileSelect_setText(st->coreSelect, defaultCore);
            }
        }
        st->suppress = 1;
        settings_config_setPath(st->romFolder, PATH_MAX, "");
        if (st->folderSelect) {
            e9ui_fileSelect_setText(st->folderSelect, "");
        }
        st->suppress = 0;
    }
    settings_updateRomSelectAllowEmpty(st);
    settings_updateSaveLabel();

    if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        amiga_uaeLoadUaeOptions(st->romPath);
        if (st->df0Select) {
            const char *df0 = amiga_uaeGetFloppyPath(0);
            e9ui_fileSelect_setText(st->df0Select, df0 ? df0 : "");
        }
        if (st->df1Select) {
            const char *df1 = amiga_uaeGetFloppyPath(1);
            e9ui_fileSelect_setText(st->df1Select, df1 ? df1 : "");
        }
        if (st->hd0Select) {
            const char *hd0 = amiga_uaeGetHardDriveFolderPath();
            e9ui_fileSelect_setText(st->hd0Select, hd0 ? hd0 : "");
        }
        settings_updateSaveLabel();
    } else if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
        char romPath[PATH_MAX];
        if (settings_neogeoEffectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
            neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
        } else {
            neogeo_coreOptionsClear();
        }
    }
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
    if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
        char romPath[PATH_MAX];
        if (settings_neogeoEffectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
            neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
        } else {
            neogeo_coreOptionsClear();
        }
    }
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
settings_amigaFloppyChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    int drive = (int)(intptr_t)user;
    amiga_uaeSetFloppyPath(drive, text ? text : "");
    settings_updateSaveLabel();
}

static void
settings_amigaHardDriveFolderChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    (void)user;
    amiga_uaeSetHardDriveFolderPath(text ? text : "");
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
        if (e9ui->transition.mode == e9k_transition_none) {
            e9ui->transition.mode = e9k_transition_random;
        }
    } else {
        e9ui->transition.mode = e9k_transition_none;
    }
    e9ui->transition.fullscreenModeSet = 0;
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

static void
settings_coreSystemSync(settings_coresystem_state_t *st, debugger_system_type_t system, e9ui_context_t *ctx)
{
    if (!st || !st->coreSystem) {
        return;
    }
    st->updating = 1;
    debugger_system_type_t current = *st->coreSystem;
    debugger_system_type_t normalized = (system == DEBUGGER_SYSTEM_AMIGA) ? DEBUGGER_SYSTEM_AMIGA : DEBUGGER_SYSTEM_NEOGEO;
    int systemChanged = (current != normalized);
    *st->coreSystem = normalized;

    if (normalized == DEBUGGER_SYSTEM_AMIGA) {
        amiga_uaeLoadUaeOptions(debugger.settingsEdit.amiga.libretro.romPath);
        neogeo_coreOptionsClear();
    } else {
        amiga_uaeClearPuaeOptions();
        char romPath[PATH_MAX];
        if (settings_neogeoEffectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
            neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
        } else {
            neogeo_coreOptionsClear();
        }
    }

    int amigaSelected = (normalized == DEBUGGER_SYSTEM_AMIGA);
    int neogeoSelected = !amigaSelected;
    const char *defaultCore = settings_defaultCorePathForSystem(normalized);
    char *corePath = (normalized == DEBUGGER_SYSTEM_AMIGA) ? st->corePathAmiga : st->corePathNeogeo;
    if (defaultCore && corePath && (!corePath[0] || settings_isDefaultCorePath(corePath))) {
        settings_config_setPath(corePath, PATH_MAX, defaultCore);
    }
    if (st->allowRebuild && systemChanged) {
        st->updating = 0;
        settings_pendingRebuild = 1;
        return;
    }
    if (st->neogeoCheckbox) {
        e9ui_checkbox_setSelected(st->neogeoCheckbox, neogeoSelected, ctx);
    }
    if (st->amigaCheckbox) {
        e9ui_checkbox_setSelected(st->amigaCheckbox, amigaSelected, ctx);
    }
    if (defaultCore && corePath && (!corePath[0] || settings_isDefaultCorePath(corePath))) {
        e9ui_component_t *coreSelect = (normalized == DEBUGGER_SYSTEM_AMIGA) ? st->coreSelectAmiga : st->coreSelectNeogeo;
        if (coreSelect) {
            e9ui_fileSelect_setText(coreSelect, defaultCore);
        }
    }
    st->updating = 0;
    settings_updateSaveLabel();
}

static void
settings_coreSystemNeoGeoChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    settings_coresystem_state_t *st = (settings_coresystem_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    settings_coreSystemSync(st, selected ? DEBUGGER_SYSTEM_NEOGEO : DEBUGGER_SYSTEM_AMIGA, ctx);
}

static void
settings_coreSystemAmigaChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    settings_coresystem_state_t *st = (settings_coresystem_state_t *)user;
    if (!st || st->updating) {
        return;
    }
    settings_coreSystemSync(st, selected ? DEBUGGER_SYSTEM_AMIGA : DEBUGGER_SYSTEM_NEOGEO, ctx);
}

static int
settings_measureContentHeight(e9ui_context_t *ctx, int isAmiga)
{
    if (!ctx) {
        return 0;
    }
    const char *romExtsNeogeo[] = { "*.neo" };
    const char *romExtsAmiga[] = { "*.uae" };
    const char *floppyExtsAmiga[] = { "*.adf", "*.adz", "*.fdi", "*.dms", "*.ipf", "*.raw" };
    const char *elfExts[] = { "*.elf" };
    e9ui_component_t *fsRom = NULL;
    e9ui_component_t *fsDf0 = NULL;
    e9ui_component_t *fsDf1 = NULL;
    e9ui_component_t *fsHd0 = NULL;
    e9ui_component_t *fsRomFolder = NULL;
    e9ui_component_t *fsElf = NULL;
    e9ui_component_t *fsBios = NULL;
    e9ui_component_t *fsSaves = NULL;
    e9ui_component_t *fsSource = NULL;
    e9ui_component_t *fsCore = NULL;
    e9ui_component_t *ltToolchain = NULL;
    e9ui_component_t *ltAudio = NULL;
    e9ui_component_t *rowSystemCenter = NULL;
    e9ui_component_t *rowCoreCenter = NULL;
    e9ui_component_t *rowGlobalCenter = NULL;
    e9ui_component_t *gap = e9ui_vspacer_make(12);

    e9ui_component_t *cbNeogeo = e9ui_checkbox_make("NEO GEO", 1, NULL, NULL);
    e9ui_component_t *cbAmiga = e9ui_checkbox_make("AMIGA", 0, NULL, NULL);
    e9ui_component_t *rowCore = e9ui_hstack_make();
    rowCoreCenter = rowCore ? e9ui_center_make(rowCore) : NULL;
    e9ui_component_t *btnCoreOptions = e9ui_button_make("Core Options", NULL, NULL);

    e9ui_component_t *cbFun = e9ui_checkbox_make("FUN", 0, NULL, NULL);
    e9ui_component_t *cbCrt = e9ui_checkbox_make("CRT", 0, NULL, NULL);
    e9ui_component_t *rowGlobal = e9ui_hstack_make();
    rowGlobalCenter = rowGlobal ? e9ui_center_make(rowGlobal) : NULL;

    if (isAmiga) {
        fsRom = e9ui_fileSelect_make("UAE CONFIG", 120, 600, "...", romExtsAmiga, 1, E9UI_FILESELECT_FILE);
        e9ui_fileSelect_enableNewButton(fsRom, "NEW");
        e9ui_fileSelect_setValidate(fsRom, settings_validateUaeConfig, NULL);
        fsDf0 = e9ui_fileSelect_make("DF0", 120, 600, "...", floppyExtsAmiga, 6, E9UI_FILESELECT_FILE);
        fsDf1 = e9ui_fileSelect_make("DF1", 120, 600, "...", floppyExtsAmiga, 6, E9UI_FILESELECT_FILE);
        fsHd0 = e9ui_fileSelect_make("HD0 FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        e9ui_fileSelect_setAllowEmpty(fsDf0, 1);
        e9ui_fileSelect_setAllowEmpty(fsDf1, 1);
        e9ui_fileSelect_setAllowEmpty(fsHd0, 1);
        fsElf = e9ui_fileSelect_make("ELF", 120, 600, "...", elfExts, 1, E9UI_FILESELECT_FILE);
        ltToolchain = e9ui_labeled_textbox_make("TOOLCHAIN PREFIX", 120, 600, NULL, NULL);
        fsBios = e9ui_fileSelect_make("KICKSTART FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSaves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSource = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsCore = e9ui_fileSelect_make("CORE", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FILE);
        ltAudio = e9ui_labeled_textbox_make("AUDIO BUFFER MS", 120, 600, NULL, NULL);
    } else {
        fsRom = e9ui_fileSelect_make("ROM", 120, 600, "...", romExtsNeogeo, 1, E9UI_FILESELECT_FILE);
        fsRomFolder = e9ui_fileSelect_make("ROM FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsElf = e9ui_fileSelect_make("ELF", 120, 600, "...", elfExts, 1, E9UI_FILESELECT_FILE);
        ltToolchain = e9ui_labeled_textbox_make("TOOLCHAIN PREFIX", 120, 600, NULL, NULL);
        fsBios = e9ui_fileSelect_make("BIOS FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSaves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSource = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsCore = e9ui_fileSelect_make("CORE", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FILE);
        e9ui_component_t *cbAes = e9ui_checkbox_make("AES", 1, NULL, NULL);
        e9ui_component_t *cbMvs = e9ui_checkbox_make("MVS", 0, NULL, NULL);
        e9ui_component_t *cbSkip = e9ui_checkbox_make("SKIP BIOS LOGO", 0, NULL, NULL);
        e9ui_component_t *rowSystem = e9ui_hstack_make();
        rowSystemCenter = rowSystem ? e9ui_center_make(rowSystem) : NULL;
        ltAudio = e9ui_labeled_textbox_make("AUDIO BUFFER MS", 120, 600, NULL, NULL);
        if (rowSystem && ctx) {
            int gapPx = e9ui_scale_px(ctx, 12);
            int wMvs = cbMvs ? settings_checkboxMeasureWidth("MVS", ctx) : 0;
            int wAes = cbAes ? settings_checkboxMeasureWidth("AES", ctx) : 0;
            int wSkip = cbSkip ? settings_checkboxMeasureWidth("SKIP BIOS LOGO", ctx) : 0;
            int totalW = 0;
            if (cbMvs) {
                e9ui_hstack_addFixed(rowSystem, cbMvs, wMvs);
                totalW += wMvs;
            }
            if (cbAes) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(rowSystem, e9ui_spacer_make(gapPx), gapPx);
                    totalW += gapPx;
                }
                e9ui_hstack_addFixed(rowSystem, cbAes, wAes);
                totalW += wAes;
            }
            if (cbSkip) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(rowSystem, e9ui_spacer_make(gapPx), gapPx);
                    totalW += gapPx;
                }
                e9ui_hstack_addFixed(rowSystem, cbSkip, wSkip);
                totalW += wSkip;
            }
            if (rowSystemCenter) {
                e9ui_center_setSize(rowSystemCenter, e9ui_unscale_px(ctx, totalW), 0);
            }
        }
    }

    if (rowCore && ctx) {
        int gapPx = e9ui_scale_px(ctx, 12);
        int wNeogeo = cbNeogeo ? settings_checkboxMeasureWidth("NEO GEO", ctx) : 0;
        int wAmiga = cbAmiga ? settings_checkboxMeasureWidth("AMIGA", ctx) : 0;
        int wCoreOptions = 0;
        int hCoreOptions = 0;
        if (btnCoreOptions) {
            e9ui_button_measure(btnCoreOptions, ctx, &wCoreOptions, &hCoreOptions);
            (void)hCoreOptions;
        }
        int totalW = 0;
        if (cbNeogeo) {
            e9ui_hstack_addFixed(rowCore, cbNeogeo, wNeogeo);
            totalW += wNeogeo;
        }
        if (cbAmiga) {
            if (totalW > 0) {
                e9ui_hstack_addFixed(rowCore, e9ui_spacer_make(gapPx), gapPx);
                totalW += gapPx;
            }
            e9ui_hstack_addFixed(rowCore, cbAmiga, wAmiga);
            totalW += wAmiga;
        }
        if (btnCoreOptions && wCoreOptions > 0) {
            if (totalW > 0) {
                e9ui_hstack_addFixed(rowCore, e9ui_spacer_make(gapPx), gapPx);
                totalW += gapPx;
            }
            e9ui_hstack_addFixed(rowCore, btnCoreOptions, wCoreOptions);
            totalW += wCoreOptions;
        }
        if (rowCoreCenter) {
            e9ui_center_setSize(rowCoreCenter, e9ui_unscale_px(ctx, totalW), 0);
        }
    }

    if (rowGlobal && ctx) {
        int gapPx = e9ui_scale_px(ctx, 12);
        int wFun = cbFun ? settings_checkboxMeasureWidth("FUN", ctx) : 0;
        int wCrt = cbCrt ? settings_checkboxMeasureWidth("CRT", ctx) : 0;
        int totalW = 0;
        if (cbFun) {
            e9ui_hstack_addFixed(rowGlobal, cbFun, wFun);
            totalW += wFun;
        }
        if (cbCrt) {
            if (totalW > 0) {
                e9ui_hstack_addFixed(rowGlobal, e9ui_spacer_make(gapPx), gapPx);
                totalW += gapPx;
            }
            e9ui_hstack_addFixed(rowGlobal, cbCrt, wCrt);
            totalW += wCrt;
        }
        if (rowGlobalCenter) {
            e9ui_center_setSize(rowGlobalCenter, e9ui_unscale_px(ctx, totalW), 0);
        }
    }

    debugger_system_type_t coreSystem = isAmiga ? DEBUGGER_SYSTEM_AMIGA : DEBUGGER_SYSTEM_NEOGEO;
    e9ui_component_t *badge = settings_makeSystemBadge(ctx, coreSystem);
    e9ui_component_t *rowHeader = NULL;
    if (badge && rowCoreCenter) {
        rowHeader = e9ui_hstack_make();
        if (rowHeader) {
            int badgeWPx = e9ui_scale_px(ctx, 139);
            int gapPx = e9ui_scale_px(ctx, 12);
            e9ui_hstack_addFixed(rowHeader, badge, badgeWPx);
            e9ui_hstack_addFixed(rowHeader, e9ui_spacer_make(gapPx), gapPx);
            e9ui_hstack_addFlex(rowHeader, rowCoreCenter);
        } else {
            e9ui_childDestroy(badge, ctx);
            badge = NULL;
        }
    }

    int contentW = e9ui_scale_px(ctx, 600);
    int hGap = gap && gap->preferredHeight ? gap->preferredHeight(gap, ctx, contentW) : 0;
    e9ui_component_t *coreRow = rowHeader ? rowHeader : (rowCoreCenter ? rowCoreCenter : badge);
    int hCoreRow = coreRow && coreRow->preferredHeight ? coreRow->preferredHeight(coreRow, ctx, contentW) : 0;
    int hRom = fsRom && fsRom->preferredHeight ? fsRom->preferredHeight(fsRom, ctx, contentW) : 0;
    int hDf0 = fsDf0 && fsDf0->preferredHeight ? fsDf0->preferredHeight(fsDf0, ctx, contentW) : 0;
    int hDf1 = fsDf1 && fsDf1->preferredHeight ? fsDf1->preferredHeight(fsDf1, ctx, contentW) : 0;
    int hHd0 = fsHd0 && fsHd0->preferredHeight ? fsHd0->preferredHeight(fsHd0, ctx, contentW) : 0;
    int hRomFolder = fsRomFolder && fsRomFolder->preferredHeight ? fsRomFolder->preferredHeight(fsRomFolder, ctx, contentW) : 0;
    int hElf = fsElf && fsElf->preferredHeight ? fsElf->preferredHeight(fsElf, ctx, contentW) : 0;
    int hToolchain = ltToolchain && ltToolchain->preferredHeight ? ltToolchain->preferredHeight(ltToolchain, ctx, contentW) : 0;
    int hSource = fsSource && fsSource->preferredHeight ? fsSource->preferredHeight(fsSource, ctx, contentW) : 0;
    int hBios = fsBios && fsBios->preferredHeight ? fsBios->preferredHeight(fsBios, ctx, contentW) : 0;
    int hSaves = fsSaves && fsSaves->preferredHeight ? fsSaves->preferredHeight(fsSaves, ctx, contentW) : 0;
    int hCoreFs = fsCore && fsCore->preferredHeight ? fsCore->preferredHeight(fsCore, ctx, contentW) : 0;
    int hAudio = ltAudio && ltAudio->preferredHeight ? ltAudio->preferredHeight(ltAudio, ctx, contentW) : 0;
    int hSys = rowSystemCenter && rowSystemCenter->preferredHeight ? rowSystemCenter->preferredHeight(rowSystemCenter, ctx, contentW) : 0;
    int hGlobal = rowGlobalCenter && rowGlobalCenter->preferredHeight ? rowGlobalCenter->preferredHeight(rowGlobalCenter, ctx, contentW) : 0;
    int contentH = 0;
    if (coreRow) {
        contentH += hCoreRow + hGap;
    }
    contentH += hRom;
    if (fsDf0) {
        contentH += hGap + hDf0;
    }
    if (fsDf1) {
        contentH += hGap + hDf1;
    }
    if (fsHd0) {
        contentH += hGap + hHd0;
    }
    if (fsRomFolder) {
        contentH += hGap + hRomFolder;
    }
    if (fsElf) {
        contentH += hGap + hElf;
    }
    if (ltToolchain) {
        contentH += hGap + hToolchain;
    }
    if (fsSource) {
        contentH += hGap + hSource;
    }
    if (fsBios) {
        contentH += hGap + hBios;
    }
    if (fsSaves) {
        contentH += hGap + hSaves;
    }
    if (fsCore) {
        contentH += hGap + hCoreFs;
    }
    if (ltAudio) {
        contentH += hGap + hAudio;
    }
    if (rowSystemCenter) {
        contentH += hGap + hSys;
    }
    if (rowGlobalCenter) {
        contentH += hGap + hGlobal;
    }

    if (rowHeader) {
        e9ui_childDestroy(rowHeader, ctx);
        rowCoreCenter = NULL;
        badge = NULL;
    } else if (rowCoreCenter) {
        e9ui_childDestroy(rowCoreCenter, ctx);
    }
    if (badge) {
        e9ui_childDestroy(badge, ctx);
    }
    if (rowSystemCenter) {
        e9ui_childDestroy(rowSystemCenter, ctx);
    }
    if (rowGlobalCenter) {
        e9ui_childDestroy(rowGlobalCenter, ctx);
    }
    if (fsRom) {
        e9ui_childDestroy(fsRom, ctx);
    }
    if (fsDf0) {
        e9ui_childDestroy(fsDf0, ctx);
    }
    if (fsDf1) {
        e9ui_childDestroy(fsDf1, ctx);
    }
    if (fsHd0) {
        e9ui_childDestroy(fsHd0, ctx);
    }
    if (fsRomFolder) {
        e9ui_childDestroy(fsRomFolder, ctx);
    }
    if (fsElf) {
        e9ui_childDestroy(fsElf, ctx);
    }
    if (fsBios) {
        e9ui_childDestroy(fsBios, ctx);
    }
    if (fsSaves) {
        e9ui_childDestroy(fsSaves, ctx);
    }
    if (fsSource) {
        e9ui_childDestroy(fsSource, ctx);
    }
    if (fsCore) {
        e9ui_childDestroy(fsCore, ctx);
    }
    if (ltToolchain) {
        e9ui_childDestroy(ltToolchain, ctx);
    }
    if (ltAudio) {
        e9ui_childDestroy(ltAudio, ctx);
    }
    if (gap) {
        e9ui_childDestroy(gap, ctx);
    }

    return contentH;
}

static e9ui_component_t *
settings_makeSystemBadge(e9ui_context_t *ctx, debugger_system_type_t coreSystem)
{
    if (!ctx || !ctx->renderer) {
        return NULL;
    }
    int w = 0;
    int h = 0;
    SDL_Texture *tex = system_badge_getTexture(ctx->renderer, coreSystem, &w, &h);
    if (!tex) {
        return NULL;
    }
    e9ui_component_t *img = e9ui_image_makeFromTexture(tex, w, h);
    if (!img) {
        return NULL;
    }
    e9ui_component_t *box = e9ui_box_make(img);
    if (!box) {
        return img;
    }
    e9ui_box_setWidth(box, e9ui_dim_fixed, 139);
    e9ui_box_setHeight(box, e9ui_dim_fixed, 48);
    return box;
}

static e9ui_component_t *
settings_buildModalBody(e9ui_context_t *ctx)
{
    if (!ctx) {
        return NULL;
    }
    int isAmiga = (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_AMIGA);
    const char *romExtsNeogeo[] = { "*.neo" };
    const char *romExtsAmiga[] = { "*.uae" };
    const char *floppyExtsAmiga[] = { "*.adf", "*.adz", "*.fdi", "*.dms", "*.ipf", "*.raw" };
    const char *elfExts[] = { "*.elf" };
    e9ui_component_t *fsRom = NULL;
    e9ui_component_t *fsDf0 = NULL;
    e9ui_component_t *fsDf1 = NULL;
    e9ui_component_t *fsHd0 = NULL;
    e9ui_component_t *fsRomFolder = NULL;
    e9ui_component_t *fsElf = NULL;
    e9ui_component_t *fsBios = NULL;
    e9ui_component_t *fsSaves = NULL;
    e9ui_component_t *fsSource = NULL;
    e9ui_component_t *fsCore = NULL;
    e9ui_component_t *ltToolchain = NULL;
    e9ui_component_t *ltAudio = NULL;
    e9ui_component_t *rowSystemCenter = NULL;
    e9ui_component_t *rowGlobalCenter = NULL;
    settings_systemtype_state_t *sysState = NULL;
    settings_romselect_state_t *romState = NULL;

    settings_coresystem_state_t *coreState = (settings_coresystem_state_t *)alloc_calloc(1, sizeof(*coreState));
    int amigaSelected = isAmiga ? 1 : 0;
    int neogeoSelected = !amigaSelected;
    e9ui_component_t *cbNeogeo = e9ui_checkbox_make("NEO GEO", neogeoSelected, settings_coreSystemNeoGeoChanged, coreState);
    e9ui_component_t *cbAmiga = e9ui_checkbox_make("AMIGA", amigaSelected, settings_coreSystemAmigaChanged, coreState);
    e9ui_component_t *rowCore = e9ui_hstack_make();
    e9ui_component_t *rowCoreCenter = rowCore ? e9ui_center_make(rowCore) : NULL;
    e9ui_component_t *btnCoreOptionsTop = e9ui_button_make("Core Options", core_options_uiOpen, NULL);
    e9ui_setTooltip(btnCoreOptionsTop, "Libretro core options");
    e9ui_component_t *badge = settings_makeSystemBadge(ctx, debugger.settingsEdit.coreSystem);
    e9ui_component_t *rowHeader = NULL;
    if (badge && rowCoreCenter) {
        rowHeader = e9ui_hstack_make();
        if (rowHeader) {
            int badgeWPx = e9ui_scale_px(ctx, 139);
            int gapPx = e9ui_scale_px(ctx, 12);
            e9ui_hstack_addFixed(rowHeader, badge, badgeWPx);
            e9ui_hstack_addFixed(rowHeader, e9ui_spacer_make(gapPx), gapPx);
            e9ui_hstack_addFlex(rowHeader, rowCoreCenter);
        } else {
            e9ui_childDestroy(badge, ctx);
            badge = NULL;
        }
    }

    int funSelected = (e9ui->transition.mode != e9k_transition_none);
    e9ui_component_t *cbFun = e9ui_checkbox_make("FUN", funSelected, settings_funChanged, NULL);
    e9ui_component_t *cbCrt = e9ui_checkbox_make("CRT",
                                                 debugger.settingsEdit.crtEnabled,
                                                 settings_crtChanged,
                                                 &debugger.settingsEdit.crtEnabled);
    e9ui_component_t *rowGlobal = e9ui_hstack_make();
    rowGlobalCenter = rowGlobal ? e9ui_center_make(rowGlobal) : NULL;

    if (isAmiga) {
        fsRom = e9ui_fileSelect_make("UAE CONFIG", 120, 600, "...", romExtsAmiga, 1, E9UI_FILESELECT_FILE);
        e9ui_fileSelect_enableNewButton(fsRom, "NEW");
        e9ui_fileSelect_setValidate(fsRom, settings_validateUaeConfig, NULL);
        fsDf0 = e9ui_fileSelect_make("DF0", 120, 600, "...", floppyExtsAmiga, 6, E9UI_FILESELECT_FILE);
        fsDf1 = e9ui_fileSelect_make("DF1", 120, 600, "...", floppyExtsAmiga, 6, E9UI_FILESELECT_FILE);
        fsHd0 = e9ui_fileSelect_make("HD0 FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        e9ui_fileSelect_setAllowEmpty(fsDf0, 1);
        e9ui_fileSelect_setAllowEmpty(fsDf1, 1);
        e9ui_fileSelect_setAllowEmpty(fsHd0, 1);
        fsElf = e9ui_fileSelect_make("ELF", 120, 600, "...", elfExts, 1, E9UI_FILESELECT_FILE);
        settings_toolchainprefix_state_t *tc = (settings_toolchainprefix_state_t *)alloc_calloc(1, sizeof(*tc));
        if (tc) {
            tc->prefix = debugger.settingsEdit.amiga.libretro.toolchainPrefix;
            tc->system = DEBUGGER_SYSTEM_AMIGA;
        }
        ltToolchain = e9ui_labeled_textbox_make("TOOLCHAIN PREFIX", 120, 600, settings_toolchainPrefixChanged, tc);
        fsBios = e9ui_fileSelect_make("KICKSTART FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSaves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSource = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsCore = e9ui_fileSelect_make("CORE", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FILE);
        ltAudio = e9ui_labeled_textbox_make("AUDIO BUFFER MS", 120, 600,
                                            settings_audioChanged,
                                            &debugger.settingsEdit.amiga.libretro.audioBufferMs);

        e9ui_fileSelect_setText(fsRom, debugger.settingsEdit.amiga.libretro.romPath);
        if (fsDf0) {
            const char *df0 = amiga_uaeGetFloppyPath(0);
            e9ui_fileSelect_setText(fsDf0, df0 ? df0 : "");
        }
        if (fsDf1) {
            const char *df1 = amiga_uaeGetFloppyPath(1);
            e9ui_fileSelect_setText(fsDf1, df1 ? df1 : "");
        }
        if (fsHd0) {
            const char *hd0 = amiga_uaeGetHardDriveFolderPath();
            e9ui_fileSelect_setText(fsHd0, hd0 ? hd0 : "");
        }
        e9ui_fileSelect_setText(fsElf, debugger.settingsEdit.amiga.libretro.elfPath);
        e9ui_fileSelect_setAllowEmpty(fsElf, 1);
        if (ltToolchain) {
            e9ui_labeled_textbox_setText(ltToolchain, debugger.settingsEdit.amiga.libretro.toolchainPrefix);
        }
        e9ui_fileSelect_setText(fsBios, debugger.settingsEdit.amiga.libretro.systemDir);
        e9ui_fileSelect_setText(fsSaves, debugger.settingsEdit.amiga.libretro.saveDir);
        e9ui_fileSelect_setText(fsSource, debugger.settingsEdit.amiga.libretro.sourceDir);
        e9ui_fileSelect_setText(fsCore, debugger.settingsEdit.amiga.libretro.corePath);
    } else {
        fsRom = e9ui_fileSelect_make("ROM", 120, 600, "...", romExtsNeogeo, 1, E9UI_FILESELECT_FILE);
        fsRomFolder = e9ui_fileSelect_make("ROM FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsElf = e9ui_fileSelect_make("ELF", 120, 600, "...", elfExts, 1, E9UI_FILESELECT_FILE);
        settings_toolchainprefix_state_t *tc = (settings_toolchainprefix_state_t *)alloc_calloc(1, sizeof(*tc));
        if (tc) {
            tc->prefix = debugger.settingsEdit.neogeo.libretro.toolchainPrefix;
            tc->system = DEBUGGER_SYSTEM_NEOGEO;
        }
        ltToolchain = e9ui_labeled_textbox_make("TOOLCHAIN PREFIX", 120, 600, settings_toolchainPrefixChanged, tc);
        fsBios = e9ui_fileSelect_make("BIOS FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSaves = e9ui_fileSelect_make("SAVES FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsSource = e9ui_fileSelect_make("SOURCE FOLDER", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FOLDER);
        fsCore = e9ui_fileSelect_make("CORE", 120, 600, "...", NULL, 0, E9UI_FILESELECT_FILE);
        e9ui_component_t *cbSkip = e9ui_checkbox_make("SKIP BIOS LOGO",
                                                      debugger.settingsEdit.neogeo.skipBiosLogo,
                                                      settings_skipBiosChanged,
                                                      &debugger.settingsEdit.neogeo.skipBiosLogo);
        sysState = (settings_systemtype_state_t *)alloc_calloc(1, sizeof(*sysState));
        int aesSelected = (strcmp(debugger.settingsEdit.neogeo.systemType, "aes") == 0);
        int mvsSelected = (strcmp(debugger.settingsEdit.neogeo.systemType, "mvs") == 0);
        e9ui_component_t *cbAes = e9ui_checkbox_make("AES", aesSelected, settings_systemTypeAesChanged, sysState);
        e9ui_component_t *cbMvs = e9ui_checkbox_make("MVS", mvsSelected, settings_systemTypeMvsChanged, sysState);
        e9ui_component_t *rowSystem = e9ui_hstack_make();
        rowSystemCenter = rowSystem ? e9ui_center_make(rowSystem) : NULL;
        ltAudio = e9ui_labeled_textbox_make("AUDIO BUFFER MS", 120, 600,
                                            settings_audioChanged,
                                            &debugger.settingsEdit.neogeo.libretro.audioBufferMs);
        e9ui_fileSelect_setText(fsRom, debugger.settingsEdit.neogeo.libretro.romPath);
        e9ui_fileSelect_setText(fsRomFolder, debugger.settingsEdit.neogeo.romFolder);
        e9ui_fileSelect_setText(fsElf, debugger.settingsEdit.neogeo.libretro.elfPath);
        e9ui_fileSelect_setAllowEmpty(fsElf, 1);
        if (ltToolchain) {
            e9ui_labeled_textbox_setText(ltToolchain, debugger.settingsEdit.neogeo.libretro.toolchainPrefix);
        }
        e9ui_fileSelect_setText(fsBios, debugger.settingsEdit.neogeo.libretro.systemDir);
        e9ui_fileSelect_setText(fsSaves, debugger.settingsEdit.neogeo.libretro.saveDir);
        e9ui_fileSelect_setText(fsSource, debugger.settingsEdit.neogeo.libretro.sourceDir);
        e9ui_fileSelect_setText(fsCore, debugger.settingsEdit.neogeo.libretro.corePath);
        if (sysState) {
            sysState->aesCheckbox = cbAes;
            sysState->mvsCheckbox = cbMvs;
            sysState->systemType = debugger.settingsEdit.neogeo.systemType;
        }
        if (rowSystem && ctx) {
            int gap = e9ui_scale_px(ctx, 12);
            int wMvs = cbMvs ? settings_checkboxMeasureWidth("MVS", ctx) : 0;
            int wAes = cbAes ? settings_checkboxMeasureWidth("AES", ctx) : 0;
            int wSkip = cbSkip ? settings_checkboxMeasureWidth("SKIP BIOS LOGO", ctx) : 0;
            int totalW = 0;
            if (cbMvs) {
                e9ui_hstack_addFixed(rowSystem, cbMvs, wMvs);
                totalW += wMvs;
            }
            if (cbAes) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(rowSystem, e9ui_spacer_make(gap), gap);
                    totalW += gap;
                }
                e9ui_hstack_addFixed(rowSystem, cbAes, wAes);
                totalW += wAes;
            }
            if (cbSkip) {
                if (totalW > 0) {
                    e9ui_hstack_addFixed(rowSystem, e9ui_spacer_make(gap), gap);
                    totalW += gap;
                }
                e9ui_hstack_addFixed(rowSystem, cbSkip, wSkip);
                totalW += wSkip;
            }
            if (rowSystemCenter) {
                e9ui_center_setSize(rowSystemCenter, e9ui_unscale_px(ctx, totalW), 0);
            }
        }
    }

    if (coreState) {
        coreState->neogeoCheckbox = cbNeogeo;
        coreState->amigaCheckbox = cbAmiga;
        coreState->coreSystem = &debugger.settingsEdit.coreSystem;
        coreState->corePathNeogeo = debugger.settingsEdit.neogeo.libretro.corePath;
        coreState->corePathAmiga = debugger.settingsEdit.amiga.libretro.corePath;
        if (isAmiga) {
            coreState->coreSelectAmiga = fsCore;
        } else {
            coreState->coreSelectNeogeo = fsCore;
        }
        coreState->allowRebuild = 0;
        settings_coreSystemSync(coreState, debugger.settingsEdit.coreSystem, ctx);
        coreState->allowRebuild = 1;
    }

    if (rowCore && ctx) {
        int gap = e9ui_scale_px(ctx, 12);
        int wNeogeo = cbNeogeo ? settings_checkboxMeasureWidth("NEO GEO", ctx) : 0;
        int wAmiga = cbAmiga ? settings_checkboxMeasureWidth("AMIGA", ctx) : 0;
        int wCoreOptions = 0;
        int hCoreOptions = 0;
        if (btnCoreOptionsTop) {
            e9ui_button_measure(btnCoreOptionsTop, ctx, &wCoreOptions, &hCoreOptions);
            (void)hCoreOptions;
        }
        int totalW = 0;
        if (cbNeogeo) {
            e9ui_hstack_addFixed(rowCore, cbNeogeo, wNeogeo);
            totalW += wNeogeo;
        }
        if (cbAmiga) {
            if (totalW > 0) {
                e9ui_hstack_addFixed(rowCore, e9ui_spacer_make(gap), gap);
                totalW += gap;
            }
            e9ui_hstack_addFixed(rowCore, cbAmiga, wAmiga);
            totalW += wAmiga;
        }
        if (btnCoreOptionsTop && wCoreOptions > 0) {
            if (totalW > 0) {
                e9ui_hstack_addFixed(rowCore, e9ui_spacer_make(gap), gap);
                totalW += gap;
            }
            e9ui_hstack_addFixed(rowCore, btnCoreOptionsTop, wCoreOptions);
            totalW += wCoreOptions;
        }
        if (rowCoreCenter) {
            e9ui_center_setSize(rowCoreCenter, e9ui_unscale_px(ctx, totalW), 0);
        }
    }
    if (rowGlobal && ctx) {
        int gap = e9ui_scale_px(ctx, 12);
        int wFun = cbFun ? settings_checkboxMeasureWidth("FUN", ctx) : 0;
        int wCrt = cbCrt ? settings_checkboxMeasureWidth("CRT", ctx) : 0;
        int totalW = 0;
        if (cbFun) {
            e9ui_hstack_addFixed(rowGlobal, cbFun, wFun);
            totalW += wFun;
        }
        if (cbCrt) {
            if (totalW > 0) {
                e9ui_hstack_addFixed(rowGlobal, e9ui_spacer_make(gap), gap);
                totalW += gap;
            }
            e9ui_hstack_addFixed(rowGlobal, cbCrt, wCrt);
            totalW += wCrt;
        }
        if (rowGlobalCenter) {
            e9ui_center_setSize(rowGlobalCenter, e9ui_unscale_px(ctx, totalW), 0);
        }
    }
    if (ltAudio) {
        char buf[32];
        int audioValue = isAmiga ? debugger.settingsEdit.amiga.libretro.audioBufferMs : debugger.settingsEdit.neogeo.libretro.audioBufferMs;
        if (audioValue > 0) {
            snprintf(buf, sizeof(buf), "%d", audioValue);
            e9ui_labeled_textbox_setText(ltAudio, buf);
        } else {
            e9ui_labeled_textbox_setText(ltAudio, "");
        }
        e9ui_component_t *tb = e9ui_labeled_textbox_getTextbox(ltAudio);
        if (tb) {
            e9ui_textbox_setNumericOnly(tb, 1);
        }
    }
    romState = (settings_romselect_state_t *)alloc_calloc(1, sizeof(*romState));
    if (romState) {
        if (isAmiga) {
            romState->romPath = debugger.settingsEdit.amiga.libretro.romPath;
            romState->romFolder = NULL;
            romState->corePath = debugger.settingsEdit.amiga.libretro.corePath;
        } else {
            romState->romPath = debugger.settingsEdit.neogeo.libretro.romPath;
            romState->romFolder = debugger.settingsEdit.neogeo.romFolder;
            romState->corePath = debugger.settingsEdit.neogeo.libretro.corePath;
        }
        romState->romSelect = fsRom;
        romState->folderSelect = fsRomFolder;
        romState->coreSelect = fsCore;
        romState->df0Select = fsDf0;
        romState->df1Select = fsDf1;
        romState->hd0Select = fsHd0;
        settings_updateRomSelectAllowEmpty(romState);
    }
    if (fsRom) {
        e9ui_fileSelect_setOnChange(fsRom, settings_romPathChanged, romState);
    }
    if (fsDf0) {
        e9ui_fileSelect_setOnChange(fsDf0, settings_amigaFloppyChanged, (void *)(intptr_t)0);
    }
    if (fsDf1) {
        e9ui_fileSelect_setOnChange(fsDf1, settings_amigaFloppyChanged, (void *)(intptr_t)1);
    }
    if (fsHd0) {
        e9ui_fileSelect_setOnChange(fsHd0, settings_amigaHardDriveFolderChanged, NULL);
    }
    if (fsRomFolder) {
        e9ui_fileSelect_setOnChange(fsRomFolder, settings_romFolderChanged, romState);
    }
    if (fsElf) {
        if (isAmiga) {
            e9ui_fileSelect_setOnChange(fsElf, settings_pathChanged, debugger.settingsEdit.amiga.libretro.elfPath);
        } else {
            e9ui_fileSelect_setOnChange(fsElf, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.elfPath);
        }
    }
    if (fsBios) {
        if (isAmiga) {
            e9ui_fileSelect_setOnChange(fsBios, settings_pathChanged, debugger.settingsEdit.amiga.libretro.systemDir);
        } else {
            e9ui_fileSelect_setOnChange(fsBios, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.systemDir);
        }
    }
    if (fsSaves) {
        if (isAmiga) {
            e9ui_fileSelect_setOnChange(fsSaves, settings_pathChanged, debugger.settingsEdit.amiga.libretro.saveDir);
        } else {
            e9ui_fileSelect_setOnChange(fsSaves, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.saveDir);
        }
    }
    if (fsSource) {
        if (isAmiga) {
            e9ui_fileSelect_setOnChange(fsSource, settings_pathChanged, debugger.settingsEdit.amiga.libretro.sourceDir);
        } else {
            e9ui_fileSelect_setOnChange(fsSource, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.sourceDir);
        }
    }
    if (fsCore) {
        if (isAmiga) {
            e9ui_fileSelect_setOnChange(fsCore, settings_pathChanged, debugger.settingsEdit.amiga.libretro.corePath);
        } else {
            e9ui_fileSelect_setOnChange(fsCore, settings_pathChanged, debugger.settingsEdit.neogeo.libretro.corePath);
        }
    }

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_component_t *gap = e9ui_vspacer_make(12);
    if (rowHeader) {
        e9ui_stack_addFixed(stack, rowHeader);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
    } else if (rowCoreCenter) {
        e9ui_stack_addFixed(stack, rowCoreCenter);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
    } else if (badge) {
        e9ui_stack_addFixed(stack, badge);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
    }
    if (fsRom) {
        e9ui_stack_addFixed(stack, fsRom);
    }
    if (fsDf0) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsDf0);
    }
    if (fsDf1) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsDf1);
    }
    if (fsHd0) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsHd0);
    }
    if (fsRomFolder) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsRomFolder);
    }
    if (fsElf) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsElf);
    }
    if (ltToolchain) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, ltToolchain);
    }
    if (fsSource) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsSource);
    }
    if (fsBios) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsBios);
    }
    if (fsSaves) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsSaves);
    }
    if (fsCore) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, fsCore);
    }
    if (ltAudio) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, ltAudio);
    }
    if (rowSystemCenter) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, rowSystemCenter);
    }
    if (rowGlobalCenter) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(stack, rowGlobalCenter);
    }
    e9ui_component_t *center = e9ui_center_make(stack);
    e9ui_component_t *btnDefaults = e9ui_button_make("Defaults", settings_uiDefaults, NULL);
    e9ui_component_t *btnSave = e9ui_button_make("Save", settings_uiSave, NULL);
    e9ui_component_t *btnCancel = e9ui_button_make("Cancel", settings_uiCancel, NULL);
    e9ui->settingsSaveButton = btnSave;
    settings_updateSaveLabel();
    e9ui_component_t *buttons = e9ui_flow_make();
    e9ui_flow_setPadding(buttons, 0);
    e9ui_flow_setSpacing(buttons, 8);
    e9ui_flow_setWrap(buttons, 0);
    if (btnSave) {
        e9ui_button_setTheme(btnSave, e9ui_theme_button_preset_green());
        e9ui_button_setGlowPulse(btnSave, 1);
        e9ui_flow_add(buttons, btnSave);
    }
    if (btnDefaults) {
        e9ui_flow_add(buttons, btnDefaults);
    }    
    if (btnCancel) {
        e9ui_button_setTheme(btnCancel, e9ui_theme_button_preset_red());
        e9ui_button_setGlowPulse(btnCancel, 1);
        e9ui_flow_add(buttons, btnCancel);
    }
    e9ui_component_t *warning = settings_uaeExtensionWarning_make();
    e9ui_component_t *footer = e9ui_stack_makeVertical();
    if (warning) {
        e9ui_stack_addFixed(footer, warning);
    }
    if (buttons) {
        e9ui_stack_addFixed(footer, buttons);
    }
    int contentW = e9ui_scale_px(ctx, 600);
    int hGap = gap->preferredHeight ? gap->preferredHeight(gap, ctx, contentW) : 0;
    e9ui_component_t *coreRow = rowHeader ? rowHeader : (rowCoreCenter ? rowCoreCenter : badge);
    int hCoreRow = coreRow && coreRow->preferredHeight ? coreRow->preferredHeight(coreRow, ctx, contentW) : 0;
    int hRom = fsRom && fsRom->preferredHeight ? fsRom->preferredHeight(fsRom, ctx, contentW) : 0;
    int hDf0 = fsDf0 && fsDf0->preferredHeight ? fsDf0->preferredHeight(fsDf0, ctx, contentW) : 0;
    int hDf1 = fsDf1 && fsDf1->preferredHeight ? fsDf1->preferredHeight(fsDf1, ctx, contentW) : 0;
    int hHd0 = fsHd0 && fsHd0->preferredHeight ? fsHd0->preferredHeight(fsHd0, ctx, contentW) : 0;
    int hRomFolder = fsRomFolder && fsRomFolder->preferredHeight ? fsRomFolder->preferredHeight(fsRomFolder, ctx, contentW) : 0;
    int hElf = fsElf && fsElf->preferredHeight ? fsElf->preferredHeight(fsElf, ctx, contentW) : 0;
    int hToolchain = ltToolchain && ltToolchain->preferredHeight ? ltToolchain->preferredHeight(ltToolchain, ctx, contentW) : 0;
    int hSource = fsSource && fsSource->preferredHeight ? fsSource->preferredHeight(fsSource, ctx, contentW) : 0;
    int hBios = fsBios && fsBios->preferredHeight ? fsBios->preferredHeight(fsBios, ctx, contentW) : 0;
    int hSaves = fsSaves && fsSaves->preferredHeight ? fsSaves->preferredHeight(fsSaves, ctx, contentW) : 0;
    int hCoreFs = fsCore && fsCore->preferredHeight ? fsCore->preferredHeight(fsCore, ctx, contentW) : 0;
    int hAudio = ltAudio && ltAudio->preferredHeight ? ltAudio->preferredHeight(ltAudio, ctx, contentW) : 0;
    int hSys = rowSystemCenter && rowSystemCenter->preferredHeight ? rowSystemCenter->preferredHeight(rowSystemCenter, ctx, contentW) : 0;
    int hGlobal = rowGlobalCenter && rowGlobalCenter->preferredHeight ? rowGlobalCenter->preferredHeight(rowGlobalCenter, ctx, contentW) : 0;
    int contentH = 0;
    if (coreRow) {
        contentH += hCoreRow + hGap;
    }
    contentH += hRom;
    if (fsDf0) {
        contentH += hGap + hDf0;
    }
    if (fsDf1) {
        contentH += hGap + hDf1;
    }
    if (fsHd0) {
        contentH += hGap + hHd0;
    }
    if (fsRomFolder) {
        contentH += hGap + hRomFolder;
    }
    if (fsElf) {
        contentH += hGap + hElf;
    }
    if (ltToolchain) {
        contentH += hGap + hToolchain;
    }
    if (fsSource) {
        contentH += hGap + hSource;
    }
    if (fsBios) {
        contentH += hGap + hBios;
    }
    if (fsSaves) {
        contentH += hGap + hSaves;
    }
    if (fsCore) {
        contentH += hGap + hCoreFs;
    }
    if (ltAudio) {
        contentH += hGap + hAudio;
    }
    if (rowSystemCenter) {
        contentH += hGap + hSys;
    }
    if (rowGlobalCenter) {
        contentH += hGap + hGlobal;
    }
    int otherHeight = settings_measureContentHeight(ctx, isAmiga ? 0 : 1);
    int targetHeight = contentH > otherHeight ? contentH : otherHeight;
    e9ui_center_setSize(center, 640, e9ui_unscale_px(ctx, targetHeight));
    e9ui_component_t *overlay = e9ui_overlay_make(center, footer);
    e9ui_overlay_setAnchor(overlay, e9ui_anchor_bottom_right);
    e9ui_overlay_setMargin(overlay, 12);
    return overlay;
}

static void
settings_rebuildModalBody(e9ui_context_t *ctx)
{
    if (!e9ui->settingsModal || !ctx) {
        return;
    }
    e9ui_component_t *overlay = settings_buildModalBody(ctx);
    if (overlay) {
        e9ui_modal_setBodyChild(e9ui->settingsModal, overlay, ctx);
    }
}

void
settings_pollRebuild(e9ui_context_t *ctx)
{
    if (!settings_pendingRebuild) {
        return;
    }
    settings_pendingRebuild = 0;
    if (!e9ui->settingsModal || !ctx) {
        return;
    }
    if (e9ui->pendingRemove == e9ui->settingsModal) {
        return;
    }
    settings_rebuildModalBody(ctx);
}

void
settings_uiOpen(e9ui_context_t *ctx, void *user)
{
    (void)user;
    if (!ctx) {
        return;
    }
    if (e9ui->settingsModal) {
        return;
    }
    settings_clearCoreOptionsDirty();
    int margin = e9ui_scale_px(ctx, 32);
    int modalWidth = ctx->winW - margin * 2;
    int modalHeight = ctx->winH - margin * 2;
    if (modalWidth < 1) modalWidth = 1;
    if (modalHeight < 1) modalHeight = 1;
    e9ui_rect_t rect = { margin, margin, modalWidth, modalHeight };
    settings_copyConfig(&debugger.settingsEdit, &debugger.config);
    amiga_uaeClearPuaeOptions();
    neogeo_coreOptionsClear();
    if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        amiga_uaeLoadUaeOptions(debugger.settingsEdit.amiga.libretro.romPath);
    } else if (debugger.settingsEdit.coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
        char romPath[PATH_MAX];
        if (settings_neogeoEffectiveRomPath(&debugger.settingsEdit.neogeo, romPath, sizeof(romPath))) {
            neogeo_coreOptionsLoadFromFile(debugger.settingsEdit.neogeo.libretro.saveDir, romPath);
        }
    }
    e9ui->settingsModal = e9ui_modal_show(ctx, "Settings", rect, settings_uiClosed, NULL);
    if (e9ui->settingsModal) {
        e9ui_component_t *overlay = settings_buildModalBody(ctx);
        if (overlay) {
            e9ui_modal_setBodyChild(e9ui->settingsModal, overlay, ctx);
        }
    }
}
