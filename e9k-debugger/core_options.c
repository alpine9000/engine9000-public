/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <string.h>
#include <ctype.h>

#include "core_options.h"
#include "alloc.h"
#include "amiga_uae_options.h"
#include "config.h"
#include "core_config.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_scroll.h"
#include "libretro_host.h"
#include "neogeo_core_options.h"
#include "settings.h"
#include "system_badge.h"

typedef struct core_options_kv {
    char *key;
    char *value;
} core_options_kv_t;

typedef struct core_options_category_cb {
    struct core_options_modal_state *st;
    const char *categoryKey;
    e9ui_component_t *button;
} core_options_category_cb_t;

typedef struct core_options_option_cb {
    struct core_options_modal_state *st;
    const char *key;
    const char *enabledValue;
    const char *disabledValue;
} core_options_option_cb_t;

typedef struct core_options_modal_state {
    core_options_kv_t *entries;
    size_t entryCount;
    size_t entryCap;

    const struct retro_core_option_v2_category *cats;
    size_t catCount;
    const struct retro_core_option_v2_definition *defs;
    size_t defCount;

    const char *selectedCategoryKey;

    e9ui_component_t *categoryScroll;
    e9ui_component_t *categoryStack;
    int categoryWidthPx;

    e9ui_component_t *optionsScroll;
    e9ui_component_t *optionsStack;
    int optionsWidthPx;

    e9ui_component_t *btnSave;
    e9ui_component_t *btnDefaults;

    core_options_category_cb_t **categoryCallbacks;
    size_t categoryCallbackCount;
    size_t categoryCallbackCap;

    core_options_option_cb_t **optionCallbacks;
    size_t optionCallbackCount;
    size_t optionCallbackCap;

    core_config_options_v2_t probedOptions;
    int probed;
    int targetCoreRunning;
} core_options_modal_state_t;

static void
core_options_optionChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user);

static void
core_options_optionCheckboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

static void
core_options_clearOptionCbs(core_options_modal_state_t *st);

static void
core_options_clearCategoryCbs(core_options_modal_state_t *st);

static e9ui_component_t *
core_options_makeSystemBadge(e9ui_context_t *ctx, debugger_system_type_t coreSystem)
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

static void
core_options_closeModal(void)
{
    if (!e9ui->coreOptionsModal) {
        return;
    }
    e9ui_setHidden(e9ui->coreOptionsModal, 1);
    if (!e9ui->pendingRemove) {
        e9ui->pendingRemove = e9ui->coreOptionsModal;
    }
    e9ui->coreOptionsModal = NULL;
}

void
core_options_cancelModal(void)
{
    core_options_closeModal();
}

static void
core_options_uiClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    (void)user;
    core_options_closeModal();
}

static void
core_options_freeState(core_options_modal_state_t *st)
{
    if (!st) {
        return;
    }

    core_options_clearOptionCbs(st);
    if (st->optionCallbacks) {
        free(st->optionCallbacks);
        st->optionCallbacks = NULL;
    }
    st->optionCallbackCount = 0;
    st->optionCallbackCap = 0;

    core_options_clearCategoryCbs(st);
    if (st->categoryCallbacks) {
        free(st->categoryCallbacks);
        st->categoryCallbacks = NULL;
    }
    st->categoryCallbackCount = 0;
    st->categoryCallbackCap = 0;

    if (st->entries) {
        for (size_t i = 0; i < st->entryCount; ++i) {
            alloc_free(st->entries[i].key);
            alloc_free(st->entries[i].value);
        }
        free(st->entries);
        st->entries = NULL;
    }
    st->entryCount = 0;
    st->entryCap = 0;

    st->categoryScroll = NULL;
    st->categoryStack = NULL;
    st->categoryWidthPx = 0;
    st->optionsScroll = NULL;
    st->optionsStack = NULL;
    st->optionsWidthPx = 0;
    st->btnSave = NULL;
    st->btnDefaults = NULL;

    if (st->probed) {
        core_config_freeCoreOptionsV2(&st->probedOptions);
        st->probed = 0;
    }
    st->targetCoreRunning = 0;
}

static const char *
core_options_findDefaultValue(const core_options_modal_state_t *st, const char *key)
{
    if (!st || !st->defs || !key) {
        return NULL;
    }
    for (size_t i = 0; i < st->defCount; ++i) {
        const struct retro_core_option_v2_definition *def = &st->defs[i];
        if (def->key && strcmp(def->key, key) == 0) {
            return def->default_value;
        }
    }
    return NULL;
}

static const e9k_system_config_t *
core_options_selectConfig(void)
{
    if (e9ui && e9ui->settingsModal) {
        return &debugger.settingsEdit;
    }
    return &debugger.config;
}

static const e9k_libretro_config_t *
core_options_selectLibretroConfig(const e9k_system_config_t *cfg)
{
    if (!cfg) {
        return NULL;
    }
    if (cfg->coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        return &cfg->amiga.libretro;
    }
    return &cfg->neogeo.libretro;
}

static int
core_options_stringsEqual(const char *a, const char *b)
{
    if (!a && !b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return strcmp(a, b) == 0 ? 1 : 0;
}

static core_options_kv_t *
core_options_findEntry(core_options_modal_state_t *st, const char *key)
{
    if (!st || !key) {
        return NULL;
    }
    for (size_t i = 0; i < st->entryCount; ++i) {
        if (st->entries[i].key && strcmp(st->entries[i].key, key) == 0) {
            return &st->entries[i];
        }
    }
    return NULL;
}

static core_options_kv_t *
core_options_getOrAddEntry(core_options_modal_state_t *st, const char *key)
{
    if (!st || !key || !*key) {
        return NULL;
    }
    core_options_kv_t *existing = core_options_findEntry(st, key);
    if (existing) {
        return existing;
    }
    if (st->entryCount >= st->entryCap) {
        size_t nextCap = st->entryCap ? st->entryCap * 2 : 64;
        core_options_kv_t *next = (core_options_kv_t*)realloc(st->entries, nextCap * sizeof(*next));
        if (!next) {
            return NULL;
        }
        st->entries = next;
        st->entryCap = nextCap;
    }
    core_options_kv_t *ent = &st->entries[st->entryCount++];
    memset(ent, 0, sizeof(*ent));
    ent->key = alloc_strdup(key);
    ent->value = alloc_strdup("");
    return ent;
}

static const char *
core_options_getValue(core_options_modal_state_t *st, const char *key)
{
    core_options_kv_t *ent = core_options_findEntry(st, key);
    return ent ? ent->value : NULL;
}

static void
core_options_setValue(core_options_modal_state_t *st, const char *key, const char *value)
{
    core_options_kv_t *ent = core_options_getOrAddEntry(st, key);
    if (!ent) {
        return;
    }
    alloc_free(ent->value);
    ent->value = alloc_strdup(value ? value : "");
}

static void
core_options_trackCategoryCb(core_options_modal_state_t *st, core_options_category_cb_t *cb)
{
    if (!st || !cb) {
        return;
    }
    if (st->categoryCallbackCount >= st->categoryCallbackCap) {
        size_t nextCap = st->categoryCallbackCap ? st->categoryCallbackCap * 2 : 16;
        core_options_category_cb_t **next =
            (core_options_category_cb_t**)realloc(st->categoryCallbacks, nextCap * sizeof(*next));
        if (!next) {
            return;
        }
        st->categoryCallbacks = next;
        st->categoryCallbackCap = nextCap;
    }
    st->categoryCallbacks[st->categoryCallbackCount++] = cb;
}

static void
core_options_trackOptionCb(core_options_modal_state_t *st, core_options_option_cb_t *cb)
{
    if (!st || !cb) {
        return;
    }
    if (st->optionCallbackCount >= st->optionCallbackCap) {
        size_t nextCap = st->optionCallbackCap ? st->optionCallbackCap * 2 : 64;
        core_options_option_cb_t **next =
            (core_options_option_cb_t**)realloc(st->optionCallbacks, nextCap * sizeof(*next));
        if (!next) {
            return;
        }
        st->optionCallbacks = next;
        st->optionCallbackCap = nextCap;
    }
    st->optionCallbacks[st->optionCallbackCount++] = cb;
}

static void
core_options_clearOptionCbs(core_options_modal_state_t *st)
{
    if (!st || !st->optionCallbacks) {
        st->optionCallbackCount = 0;
        return;
    }
    for (size_t i = 0; i < st->optionCallbackCount; ++i) {
        alloc_free(st->optionCallbacks[i]);
    }
    st->optionCallbackCount = 0;
}

static int
core_options_measureContentHeight(e9ui_component_t *container, e9ui_context_t *ctx, int availW)
{
    if (!container || !ctx) {
        return 0;
    }
    int totalH = 0;
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(container, &iter)) {
        return 0;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (!it->child || !it->child->preferredHeight) {
            continue;
        }
        int h = it->child->preferredHeight(it->child, ctx, availW);
        if (h > 0) {
            totalH += h;
        }
    }
    return totalH;
}

static void
core_options_clearCategoryCbs(core_options_modal_state_t *st)
{
    if (!st || !st->categoryCallbacks) {
        st->categoryCallbackCount = 0;
        return;
    }
    for (size_t i = 0; i < st->categoryCallbackCount; ++i) {
        alloc_free(st->categoryCallbacks[i]);
    }
    st->categoryCallbackCount = 0;
}

static void
core_options_updateCategoryButtonThemes(core_options_modal_state_t *st)
{
    if (!st) {
        return;
    }
    for (size_t i = 0; i < st->categoryCallbackCount; ++i) {
        core_options_category_cb_t *cb = st->categoryCallbacks[i];
        e9ui_component_t *btn = cb ? cb->button : NULL;
        if (!cb || !btn) {
            continue;
        }
        int selected = 0;
        if (!st->selectedCategoryKey && !cb->categoryKey) {
            selected = 1;
        } else if (st->selectedCategoryKey && cb->categoryKey &&
                   strcmp(st->selectedCategoryKey, cb->categoryKey) == 0) {
            selected = 1;
        }
        if (selected) {
            e9ui_button_setTheme(btn, e9ui_theme_button_preset_profile_active());
        } else {
            e9ui_button_clearTheme(btn);
        }
    }
}

static int
core_options_hasUncategorizedDefs(const core_options_modal_state_t *st)
{
    if (!st || !st->defs) {
        return 0;
    }
    const e9k_system_config_t *cfg = core_options_selectConfig();
    int isAmiga = (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_AMIGA) ? 1 : 0;
    for (size_t i = 0; i < st->defCount; ++i) {
        const struct retro_core_option_v2_definition *def = &st->defs[i];
        if (st->targetCoreRunning && def && def->key) {
            if (!libretro_host_isCoreOptionVisible(def->key)) {
                continue;
            }
        }
        const char *defCat = def ? def->category_key : NULL;
        if (!defCat || !*defCat) {
            if (isAmiga && def && def->key) {
                if (strcmp(def->key, "puae_video_options_display") == 0 ||
                    strcmp(def->key, "puae_audio_options_display") == 0 ||
                    strcmp(def->key, "puae_mapping_options_display") == 0 ||
                    strcmp(def->key, "puae_model_options_display") == 0) {
                    continue;
                }
            }
            return 1;
        }
    }
    return 0;
}

static int
core_options_categoryHasVisibleDefs(const core_options_modal_state_t *st, const char *categoryKey)
{
    if (!st || !st->defs || !categoryKey || !*categoryKey) {
        return 0;
    }
    for (size_t i = 0; i < st->defCount; ++i) {
        const struct retro_core_option_v2_definition *def = &st->defs[i];
        if (!def || !def->key) {
            continue;
        }
        if (st->targetCoreRunning) {
            if (!libretro_host_isCoreOptionVisible(def->key)) {
                continue;
            }
        }
        const char *defCat = def->category_key;
        if (defCat && strcmp(defCat, categoryKey) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *
core_options_categoryIconAssetForKey(const char *categoryKey)
{
    if (!categoryKey || !*categoryKey) {
        return "assets/icons/settings.png";
    }

    if (strcmp(categoryKey, "system") == 0) {
        return "assets/icons/settings.png";
    }
    if (strcmp(categoryKey, "audio") == 0) {
        return "assets/icons/audio.png";
    }
    if (strcmp(categoryKey, "video") == 0) {
        return "assets/icons/video.png";
    }
    if (strcmp(categoryKey, "media") == 0) {
        return "assets/icons/media.png";
    }
    if (strcmp(categoryKey, "input") == 0) {
        return "assets/icons/game.png";
    }
    if (strcmp(categoryKey, "hotkey") == 0) {
        return "assets/icons/hotkey.png";
    }
    if (strcmp(categoryKey, "retropad") == 0) {
        return "assets/icons/game.png";
    }
    if (strcmp(categoryKey, "osd") == 0) {
        return "assets/icons/osd.png";
    }

    return NULL;
}

static const char *
core_options_labelStripCategoryPath(const char *label)
{
    if (!label || !*label) {
        return label;
    }
    const char *gt = strrchr(label, '>');
    if (!gt) {
        return label;
    }
    const char *p = gt + 1;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (*p) ? p : label;
}

static const char *
core_options_trimSpaces(const char *s)
{
    if (!s) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return s;
}

static int
core_options_equalsIgnoreCase(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (tolower(ca) != tolower(cb)) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static int
core_options_isEnabledDisabledOption(const struct retro_core_option_v2_definition *def,
                                    const char **outEnabledValue,
                                    const char **outDisabledValue)
{
    if (outEnabledValue) {
        *outEnabledValue = NULL;
    }
    if (outDisabledValue) {
        *outDisabledValue = NULL;
    }
    if (!def) {
        return 0;
    }
    const char *v0 = def->values[0].value;
    const char *v1 = def->values[1].value;
    const char *v2 = def->values[2].value;
    if (!v0 || !v1 || v2) {
        return 0;
    }
    const char *t0 = core_options_trimSpaces(v0);
    const char *t1 = core_options_trimSpaces(v1);
    const char *enabledValue = NULL;
    const char *disabledValue = NULL;
    if (core_options_equalsIgnoreCase(t0, "enabled")) {
        enabledValue = v0;
    } else if (core_options_equalsIgnoreCase(t0, "disabled")) {
        disabledValue = v0;
    }
    if (core_options_equalsIgnoreCase(t1, "enabled")) {
        enabledValue = v1;
    } else if (core_options_equalsIgnoreCase(t1, "disabled")) {
        disabledValue = v1;
    }
    if (!enabledValue || !disabledValue) {
        return 0;
    }
    if (outEnabledValue) {
        *outEnabledValue = enabledValue;
    }
    if (outDisabledValue) {
        *outDisabledValue = disabledValue;
    }
    return 1;
}

static void
core_options_buildOptionsForCategory(core_options_modal_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !ctx || !st->optionsStack || !st->defs) {
        return;
    }
    e9ui_child_destroyChildren(st->optionsStack, ctx);
    core_options_clearOptionCbs(st);

    const int labelWidthPx = 340;
    const int totalWidthPx = 900;
    const int rowGapPx = debugger.coreOptionsShowHelp ? 6 : 12;

    for (size_t i = 0; i < st->defCount; ++i) {
        const struct retro_core_option_v2_definition *def = &st->defs[i];
        if (!def->key) {
            continue;
        }
        if (st->targetCoreRunning) {
            if (!libretro_host_isCoreOptionVisible(def->key)) {
                continue;
            }
        }
        const char *defCat = def->category_key;
        int include = 0;
        if (!st->selectedCategoryKey) {
            include = (!defCat || !*defCat) ? 1 : 0;
        } else {
            include = (defCat && strcmp(defCat, st->selectedCategoryKey) == 0) ? 1 : 0;
        }
        if (!include) {
            continue;
        }

        e9ui_select_option_t opts[RETRO_NUM_CORE_OPTION_VALUES_MAX];
        int optCount = 0;
        for (int j = 0; j < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++j) {
            if (!def->values[j].value) {
                break;
            }
            opts[optCount].value = def->values[j].value;
            opts[optCount].label = def->values[j].label;
            optCount++;
        }

        const char *label = (def->desc && *def->desc) ? def->desc : def->key;
        label = core_options_labelStripCategoryPath(label);
        const char *value = core_options_getValue(st, def->key);

        core_options_option_cb_t *cb = (core_options_option_cb_t*)alloc_calloc(1, sizeof(*cb));
        if (cb) {
            cb->st = st;
            cb->key = def->key;
            core_options_trackOptionCb(st, cb);
        }

        const char *enabledValue = NULL;
        const char *disabledValue = NULL;
        if (core_options_isEnabledDisabledOption(def, &enabledValue, &disabledValue)) {
            int selected = 0;
            if (value && enabledValue && strcmp(value, enabledValue) == 0) {
                selected = 1;
            }
            if (cb) {
                cb->enabledValue = enabledValue;
                cb->disabledValue = disabledValue;
            }
            e9ui_component_t *chk = e9ui_labeled_checkbox_make(label, labelWidthPx, totalWidthPx,
                                                              selected, core_options_optionCheckboxChanged, cb);
            if (!chk) {
                continue;
            }
            if (debugger.coreOptionsShowHelp && def->info && *def->info) {
                e9ui_labeled_checkbox_setInfo(chk, def->info);
            }
            e9ui_stack_addFixed(st->optionsStack, chk);
        } else {
            e9ui_component_t *select = e9ui_labeled_select_make(label, labelWidthPx, totalWidthPx,
                                                                opts, optCount, value,
                                                                core_options_optionChanged, cb);
            if (!select) {
                continue;
            }
            if (debugger.coreOptionsShowHelp && def->info && *def->info) {
                e9ui_labeled_select_setInfo(select, def->info);
            }
            {
                const char *defValue = def->default_value;
                e9ui_component_t *textbox = e9ui_labeled_select_getButton(select);
                if (textbox && textbox->name && strcmp(textbox->name, "e9ui_textbox") == 0) {
                    if (value && defValue && strcmp(value, defValue) == 0) {
                        e9ui_textbox_setTextColor(textbox, 1, (SDL_Color){140, 140, 140, 255});
                    } else {
                        e9ui_textbox_setTextColor(textbox, 0, (SDL_Color){0, 0, 0, 0});
                    }
                }
            }
            e9ui_stack_addFixed(st->optionsStack, select);
        }
        e9ui_stack_addFixed(st->optionsStack, e9ui_vspacer_make(rowGapPx));
    }

    e9ui_stack_addFixed(st->optionsStack, e9ui_vspacer_make(72));

    int width = ctx->winW - (st->categoryWidthPx > 0 ? st->categoryWidthPx : 0);
    if (width <= 0) {
        width = ctx->winW;
    }
    int contentH = core_options_measureContentHeight(st->optionsStack, ctx, width);
    if (st->optionsScroll) {
        e9ui_scroll_setContentHeightPx(st->optionsScroll, contentH);
    }
}

static void
core_options_categoryClicked(e9ui_context_t *ctx, void *user)
{
    core_options_category_cb_t *cb = (core_options_category_cb_t*)user;
    if (!cb || !cb->st) {
        return;
    }
    core_options_modal_state_t *st = (core_options_modal_state_t*)cb->st;
    st->selectedCategoryKey = cb->categoryKey;
    core_options_updateCategoryButtonThemes(st);
    core_options_buildOptionsForCategory(st, ctx);
}

static void
core_options_showHelpChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    core_options_modal_state_t *st = (core_options_modal_state_t*)user;
    debugger.coreOptionsShowHelp = selected ? 1 : 0;
    config_saveConfig();
    core_options_buildOptionsForCategory(st, ctx);
}

static void
core_options_optionChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    core_options_option_cb_t *cb = (core_options_option_cb_t*)user;
    if (!cb || !cb->st || !cb->key) {
        return;
    }
    core_options_setValue(cb->st, cb->key, value);
    if (comp) {
        const char *defValue = core_options_findDefaultValue(cb->st, cb->key);
        e9ui_component_t *textbox = e9ui_labeled_select_getButton(comp);
        if (textbox && textbox->name && strcmp(textbox->name, "e9ui_textbox") == 0) {
            if (value && defValue && strcmp(value, defValue) == 0) {
                e9ui_textbox_setTextColor(textbox, 1, (SDL_Color){140, 140, 140, 255});
            } else {
                e9ui_textbox_setTextColor(textbox, 0, (SDL_Color){0, 0, 0, 0});
            }
        }
    }
    if (cb->st->btnSave) {
        e9ui_button_setGlowPulse(cb->st->btnSave, 1);
    }
}

static void
core_options_optionCheckboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    core_options_option_cb_t *cb = (core_options_option_cb_t*)user;
    if (!cb || !cb->st || !cb->key) {
        return;
    }
    const char *value = selected ? cb->enabledValue : cb->disabledValue;
    if (!value) {
        return;
    }
    core_options_setValue(cb->st, cb->key, value);
    if (cb->st->btnSave) {
        e9ui_button_setGlowPulse(cb->st->btnSave, 1);
    }
}

static void
core_options_applyDefaults(core_options_modal_state_t *st)
{
    if (!st || !st->defs) {
        return;
    }
    for (size_t i = 0; i < st->defCount; ++i) {
        const struct retro_core_option_v2_definition *def = &st->defs[i];
        if (!def->key) {
            continue;
        }
        const char *defValue = def->default_value ? def->default_value : "";
        core_options_setValue(st, def->key, defValue);
    }
}

static void
core_options_defaultsClicked(e9ui_context_t *ctx, void *user)
{
    core_options_modal_state_t *st = (core_options_modal_state_t*)user;
    if (!st) {
        return;
    }
    core_options_applyDefaults(st);
    core_options_buildOptionsForCategory(st, ctx);
    if (st->btnSave) {
        e9ui_button_setGlowPulse(st->btnSave, 1);
    }
    e9ui_showTransientMessage("CORE OPTIONS: DEFAULTS");
}

static void
core_options_saveClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    core_options_modal_state_t *st = (core_options_modal_state_t*)user;
    if (!st) {
        return;
    }
    int anyChange = 0;

    const e9k_system_config_t *cfg = core_options_selectConfig();
    if (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        for (size_t i = 0; i < st->entryCount; ++i) {
            const char *key = st->entries[i].key;
            const char *value = st->entries[i].value;
            if (!key || !*key) {
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
            } else {
                if (existing && core_options_stringsEqual(existing, desired)) {
                    continue;
                }
                amiga_uaeSetPuaeOptionValue(key, desired);
                anyChange = 1;
            }
        }
        if (anyChange) {
            settings_markCoreOptionsDirty();
        }
        if (anyChange && e9ui->settingsSaveButton) {
            e9ui_button_setGlowPulse(e9ui->settingsSaveButton, 1);
        }
        settings_refreshSaveLabel();
        e9ui_showTransientMessage(anyChange ? "CORE OPTIONS STAGED" : "CORE OPTIONS: NO CHANGES");
        core_options_closeModal();
        return;
    }

    if (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_NEOGEO && e9ui && e9ui->settingsModal) {
        for (size_t i = 0; i < st->entryCount; ++i) {
            const char *key = st->entries[i].key;
            const char *value = st->entries[i].value;
            if (!key || !*key) {
                continue;
            }
            if (strcmp(key, "geolith_system_type") == 0) {
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
            } else {
                if (existing && core_options_stringsEqual(existing, desired)) {
                    continue;
                }
                neogeo_coreOptionsSetValue(key, desired);
                anyChange = 1;
            }
        }
        if (anyChange) {
            settings_markCoreOptionsDirty();
        }
        if (anyChange && e9ui->settingsSaveButton) {
            e9ui_button_setGlowPulse(e9ui->settingsSaveButton, 1);
        }
        settings_refreshSaveLabel();
        e9ui_showTransientMessage(anyChange ? "CORE OPTIONS STAGED" : "CORE OPTIONS: NO CHANGES");
        core_options_closeModal();
        return;
    }

    for (size_t i = 0; i < st->entryCount; ++i) {
        const char *key = st->entries[i].key;
        const char *value = st->entries[i].value;
        if (!key || !*key) {
            continue;
        }
        if (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
            if (strcmp(key, "geolith_system_type") == 0) {
                continue;
            }
        }
        const char *defValue = core_options_findDefaultValue(st, key);
        const char *desired = NULL;
        if (!defValue || !value || strcmp(defValue, value) != 0) {
            desired = value ? value : "";
        }
        const char *existing = libretro_host_getCoreOptionOverrideValue(key);
        if (!desired) {
            if (!existing) {
                continue;
            }
            libretro_host_setCoreOption(key, NULL);
            anyChange = 1;
        } else {
            if (existing && core_options_stringsEqual(existing, desired)) {
                continue;
            }
            libretro_host_setCoreOption(key, desired);
            anyChange = 1;
        }
    }
    if (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
        const char *romPath = libretro_host_getRomPath();
        const char *saveDir = debugger.libretro.saveDir;
        if (romPath && *romPath && saveDir && *saveDir) {
            neogeo_coreOptionsClear();
            for (size_t i = 0; i < st->entryCount; ++i) {
                const char *key = st->entries[i].key;
                const char *value = st->entries[i].value;
                if (!key || !*key) {
                    continue;
                }
                if (strcmp(key, "geolith_system_type") == 0) {
                    continue;
                }
                const char *defValue = core_options_findDefaultValue(st, key);
                if (defValue && value && strcmp(defValue, value) == 0) {
                    continue;
                }
                neogeo_coreOptionsSetValue(key, value ? value : "");
            }
            neogeo_coreOptionsWriteToFile(saveDir, romPath);
            neogeo_coreOptionsClear();
        }
    }
    if (e9ui && e9ui->settingsModal && anyChange) {
        settings_markCoreOptionsDirty();
        if (e9ui->settingsSaveButton) {
            e9ui_button_setGlowPulse(e9ui->settingsSaveButton, 1);
        }
        settings_refreshSaveLabel();
    }
    if (!anyChange) {
        e9ui_showTransientMessage("CORE OPTIONS: NO CHANGES");
    } else if (st->targetCoreRunning) {
        e9ui_showTransientMessage("CORE OPTIONS UPDATED (restart may be required)");
    } else {
        e9ui_showTransientMessage("CORE OPTIONS SAVED (applies on next core start)");
    }
    core_options_closeModal();
}

static void
core_options_cancelClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    core_options_closeModal();
}

static int
core_options_containerPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !self->children) {
        return 0;
    }
    e9ui_child_iterator it;
    if (!e9ui_child_iterateChildren(self, &it)) {
        return 0;
    }
    if (!e9ui_child_interateNext(&it) || !it.child) {
        return 0;
    }
    return it.child->preferredHeight ? it.child->preferredHeight(it.child, ctx, availW) : 0;
}

static void
core_options_containerLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    if (!self || !self->children) {
        return;
    }
    e9ui_child_iterator it;
    if (!e9ui_child_iterateChildren(self, &it)) {
        return;
    }
    if (!e9ui_child_interateNext(&it) || !it.child) {
        return;
    }
    if (it.child->layout) {
        it.child->layout(it.child, ctx, bounds);
    }
}

static void
core_options_containerRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->children) {
        return;
    }
    e9ui_child_iterator it;
    if (!e9ui_child_iterateChildren(self, &it)) {
        return;
    }
    if (!e9ui_child_interateNext(&it) || !it.child) {
        return;
    }
    if (it.child->render) {
        it.child->render(it.child, ctx);
    }
}

static void
core_options_containerDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    core_options_modal_state_t *st = (core_options_modal_state_t*)self->state;
    core_options_freeState(st);
}

static e9ui_component_t *
core_options_makeBody(core_options_modal_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !ctx) {
        return NULL;
    }

    e9ui_component_t *categoryInner = e9ui_stack_makeVertical();
    e9ui_component_t *categoryScroll = e9ui_scroll_make(categoryInner);
    st->categoryScroll = categoryScroll;
    st->categoryStack = categoryInner;

    e9ui_component_t *optionsInner = e9ui_stack_makeVertical();
    e9ui_component_t *optionsScroll = e9ui_scroll_make(optionsInner);
    st->optionsScroll = optionsScroll;
    st->optionsStack = optionsInner;

    int leftWidth = e9ui_scale_px(ctx, 240);
    st->categoryWidthPx = leftWidth;
    st->optionsWidthPx = ctx->winW - leftWidth;
    e9ui_component_t *cols = e9ui_hstack_make();
    e9ui_hstack_addFixed(cols, categoryScroll, leftWidth);
    e9ui_hstack_addFlex(cols, optionsScroll);

    e9ui_component_t *content = e9ui_box_make(cols);
    if (content) {
        e9ui_box_setPadding(content, 32);
    } else {
        content = cols;
    }

    e9ui_component_t *btnSave = e9ui_button_make("Apply", core_options_saveClicked, st);
    e9ui_component_t *btnDefaults = e9ui_button_make("Defaults", core_options_defaultsClicked, st);
    e9ui_component_t *btnCancel = e9ui_button_make("Cancel", core_options_cancelClicked, st);
    st->btnSave = btnSave;
    st->btnDefaults = btnDefaults;
    if (btnSave) {
        e9ui_button_setTheme(btnSave, e9ui_theme_button_preset_green());
        e9ui_button_setGlowPulse(btnSave, 0);
    }
    if (btnCancel) {
        e9ui_button_setTheme(btnCancel, e9ui_theme_button_preset_red());
        e9ui_button_setGlowPulse(btnCancel, 1);
    }

    e9ui_component_t *footer = e9ui_flow_make();
    e9ui_flow_setPadding(footer, 0);
    e9ui_flow_setSpacing(footer, 8);
    e9ui_flow_setWrap(footer, 0);
    if (btnSave) {
        e9ui_flow_add(footer, btnSave);
    }
    if (btnDefaults) {
        e9ui_flow_add(footer, btnDefaults);
    }    
    if (btnCancel) {
        e9ui_flow_add(footer, btnCancel);
    }

    e9ui_component_t *layout = e9ui_overlay_make(content, footer);
    e9ui_overlay_setAnchor(layout, e9ui_anchor_bottom_right);
    e9ui_overlay_setMargin(layout, 12);

    e9ui_component_t *container = (e9ui_component_t*)alloc_calloc(1, sizeof(*container));
    if (!container) {
        return layout;
    }
    container->name = "core_options_container";
    container->state = st;
    container->preferredHeight = core_options_containerPreferredHeight;
    container->layout = core_options_containerLayout;
    container->render = core_options_containerRender;
    container->dtor = core_options_containerDtor;
    e9ui_child_add(container, layout, 0);
    return container;
}

static void
core_options_buildCategories(core_options_modal_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !ctx || !st->categoryStack) {
        return;
    }

    e9ui_child_destroyChildren(st->categoryStack, ctx);
    core_options_clearCategoryCbs(st);

    const e9k_system_config_t *cfg = core_options_selectConfig();
    debugger_system_type_t coreSystem = cfg ? cfg->coreSystem : DEBUGGER_SYSTEM_NEOGEO;
    e9ui_component_t *badge = core_options_makeSystemBadge(ctx, coreSystem);
    if (badge) {
        e9ui_stack_addFixed(st->categoryStack, badge);
        int gap = e9ui_scale_px(ctx, 12);
        if (gap < 0) {
            gap = 0;
        }
        e9ui_stack_addFixed(st->categoryStack, e9ui_vspacer_make(gap));
    }

    int includeGeneral = 0;
    if (!st->cats || st->catCount == 0) {
        includeGeneral = 1;
    } else if (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
        includeGeneral = 1;
    } else if (core_options_hasUncategorizedDefs(st)) {
        includeGeneral = 1;
    }

    if (includeGeneral) {
        core_options_category_cb_t *generalCb = (core_options_category_cb_t*)alloc_calloc(1, sizeof(*generalCb));
        if (generalCb) {
            generalCb->st = st;
            generalCb->categoryKey = NULL;
            core_options_trackCategoryCb(st, generalCb);
        }
        e9ui_component_t *btnGeneral = e9ui_button_make("General", core_options_categoryClicked, generalCb);
        if (btnGeneral) {
            if (generalCb) {
                generalCb->button = btnGeneral;
            }
            e9ui_button_setLeftJustify(btnGeneral, 16);
            e9ui_button_setIconRightPadding(btnGeneral, 16);
            const char *icon = core_options_categoryIconAssetForKey(NULL);
            if (icon && *icon) {
                e9ui_button_setIconAsset(btnGeneral, icon);
            }
            e9ui_stack_addFixed(st->categoryStack, btnGeneral);
            e9ui_stack_addFixed(st->categoryStack, e9ui_vspacer_make(4));
        }
    }

    if (st->cats && st->catCount > 0) {
        for (size_t i = 0; i < st->catCount; ++i) {
            const struct retro_core_option_v2_category *cat = &st->cats[i];
            if (!cat->key) {
                continue;
            }
            if (!core_options_categoryHasVisibleDefs(st, cat->key)) {
                continue;
            }
            const char *label = (cat->desc && *cat->desc) ? cat->desc : cat->key;
            core_options_category_cb_t *cb = (core_options_category_cb_t*)alloc_calloc(1, sizeof(*cb));
            if (!cb) {
                continue;
            }
            cb->st = st;
            cb->categoryKey = cat->key;
            core_options_trackCategoryCb(st, cb);
            e9ui_component_t *btn = e9ui_button_make(label, core_options_categoryClicked, cb);
            if (!btn) {
                continue;
            }
            cb->button = btn;
            e9ui_button_setLeftJustify(btn, 16);
            e9ui_button_setIconRightPadding(btn, 16);
            const char *icon = core_options_categoryIconAssetForKey(cat->key);
            if (icon && *icon) {
                e9ui_button_setIconAsset(btn, icon);
            }
            if (cat->info && *cat->info) {
                e9ui_setTooltip(btn, cat->info);
            }
            e9ui_stack_addFixed(st->categoryStack, btn);
            e9ui_stack_addFixed(st->categoryStack, e9ui_vspacer_make(4));
        }
    }

    e9ui_component_t *cbShowHelp = e9ui_checkbox_make("Show Help",
                                                      debugger.coreOptionsShowHelp ? 1 : 0,
                                                      core_options_showHelpChanged,
                                                      st);
    if (cbShowHelp) {
        e9ui_checkbox_setLeftMargin(cbShowHelp, 16);
        e9ui_stack_addFixed(st->categoryStack, e9ui_vspacer_make(12));
        e9ui_stack_addFixed(st->categoryStack, cbShowHelp);
        e9ui_stack_addFixed(st->categoryStack, e9ui_vspacer_make(12));
    }

    e9ui_stack_addFixed(st->categoryStack, e9ui_vspacer_make(72));

    if (includeGeneral) {
        st->selectedCategoryKey = NULL;
    } else if (st->cats && st->catCount > 0) {
        st->selectedCategoryKey = NULL;
        for (size_t i = 0; i < st->catCount; ++i) {
            const struct retro_core_option_v2_category *cat = &st->cats[i];
            if (cat && cat->key && *cat->key) {
                st->selectedCategoryKey = cat->key;
                break;
            }
        }
        if (!st->selectedCategoryKey) {
            st->selectedCategoryKey = NULL;
        }
    } else {
        st->selectedCategoryKey = NULL;
    }
    core_options_updateCategoryButtonThemes(st);

    int width = st->categoryWidthPx > 0 ? st->categoryWidthPx : e9ui_scale_px(ctx, 240);
    int contentH = core_options_measureContentHeight(st->categoryStack, ctx, width);
    if (st->categoryScroll) {
        e9ui_scroll_setContentHeightPx(st->categoryScroll, contentH);
    }
}

void
core_options_showModal(e9ui_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (e9ui->coreOptionsModal) {
        return;
    }

    const e9k_system_config_t *cfg = core_options_selectConfig();
    const e9k_libretro_config_t *libcfg = core_options_selectLibretroConfig(cfg);
    const char *corePath = libcfg ? libcfg->corePath : NULL;
    const char *systemDir = libcfg ? libcfg->systemDir : NULL;
    const char *saveDir = libcfg ? libcfg->saveDir : NULL;
    if (!corePath || !*corePath) {
        e9ui_showTransientMessage("CORE OPTIONS: NO CORE SELECTED");
        return;
    }

    const char *runningCorePath = libretro_host_getCorePath();
    int coreRunning = libretro_host_isRunning() ? 1 : 0;
    int targetIsRunning = (coreRunning && runningCorePath && strcmp(runningCorePath, corePath) == 0) ? 1 : 0;

    size_t defCount = 0;
    size_t catCount = 0;
    const struct retro_core_option_v2_definition *defs = NULL;
    const struct retro_core_option_v2_category *cats = NULL;
    core_config_options_v2_t probed = {0};
    int usedProbe = 0;
    if (targetIsRunning && libretro_host_hasCoreOptionsV2()) {
        libretro_host_refreshCoreOptionVisibility();
        defs = libretro_host_getCoreOptionDefinitions(&defCount);
        cats = libretro_host_getCoreOptionCategories(&catCount);
    } else {
        if (!core_config_probeCoreOptionsV2(corePath, systemDir, saveDir, &probed)) {
            e9ui_showTransientMessage("CORE OPTIONS UNAVAILABLE");
            return;
        }
        usedProbe = 1;
        defs = probed.defs;
        cats = probed.cats;
        defCount = probed.defCount;
        catCount = probed.catCount;
    }
    if (!defs || defCount == 0) {
        if (usedProbe) {
            core_config_freeCoreOptionsV2(&probed);
        }
        e9ui_showTransientMessage("CORE OPTIONS UNAVAILABLE");
        return;
    }

    core_options_modal_state_t *st = (core_options_modal_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        if (usedProbe) {
            core_config_freeCoreOptionsV2(&probed);
        }
        return;
    }
    st->defs = defs;
    st->defCount = defCount;
    st->cats = cats;
    st->catCount = catCount;
    st->targetCoreRunning = targetIsRunning ? 1 : 0;
    if (usedProbe) {
        st->probedOptions = probed;
        st->probed = 1;
    }

    for (size_t i = 0; i < defCount; ++i) {
        const struct retro_core_option_v2_definition *def = &defs[i];
        if (!def->key) {
            continue;
        }
        core_options_kv_t *ent = core_options_getOrAddEntry(st, def->key);
        if (!ent) {
            continue;
        }
        const char *initial = NULL;
        if (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_AMIGA) {
            initial = amiga_uaeGetPuaeOptionValue(def->key);
        } else if (cfg && cfg->coreSystem == DEBUGGER_SYSTEM_NEOGEO && e9ui && e9ui->settingsModal) {
            initial = neogeo_coreOptionsGetValue(def->key);
            if (!initial && st->targetCoreRunning) {
                initial = libretro_host_getCoreOptionValue(def->key);
            }
        } else if (st->targetCoreRunning) {
            initial = libretro_host_getCoreOptionValue(def->key);
        } else {
            initial = libretro_host_getCoreOptionOverrideValue(def->key);
        }
        if (!initial) {
            initial = def->default_value ? def->default_value : "";
        }
        alloc_free(ent->value);
        ent->value = alloc_strdup(initial);
    }

    int margin = e9ui_scale_px(ctx, 32);
    int w = ctx->winW - margin * 2;
    int h = ctx->winH - margin * 2;
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }
    e9ui_rect_t rect = { margin, margin, w, h };
    e9ui->coreOptionsModal = e9ui_modal_show(ctx, "Core Options", rect, core_options_uiClosed, NULL);
    if (!e9ui->coreOptionsModal) {
        core_options_freeState(st);
        alloc_free(st);
        return;
    }

    e9ui_component_t *body = core_options_makeBody(st, ctx);
    if (!body) {
        core_options_closeModal();
        core_options_freeState(st);
        alloc_free(st);
        return;
    }
    core_options_buildCategories(st, ctx);
    core_options_buildOptionsForCategory(st, ctx);

    e9ui_modal_setBodyChild(e9ui->coreOptionsModal, body, ctx);
}

void
core_options_uiOpen(e9ui_context_t *ctx, void *user)
{
    (void)user;
    if (e9ui->coreOptionsModal) {
        core_options_cancelModal();
    } else {
        core_options_showModal(ctx);
    }
}
