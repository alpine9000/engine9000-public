/*
 * COPYRIGHT (C) 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "hotkeys.h"
#include "e9ui.h"
#include "e9ui_checkbox.h"
#include "e9ui_scroll.h"
#include "e9ui_text.h"
#include "e9ui_text_select.h"
#include "e9ui_textbox.h"
#include "e9ui_window.h"
#include "help.h"
#include "core_options.h"
#include "settings.h"
#include "debugger.h"
#include "emu.h"
#include "ui.h"
#include "prompt.h"
#include "input_record.h"
#include "profile_checkpoints.h"
#include "config.h"
#include "source_pane.h"
#include "hex_byte_color.h"
#include "hex_convert.h"
#include "strutil.h"

static int hotkeys_enabled = 1;
static e9ui_component_t *hotkeys_configModal = NULL;
static int hotkeys_textboxShortcutMatcherInstalled = 0;

typedef struct hotkeys_config_spec
{
    const char *id;
    const char *label;
    SDL_Keycode defaultKey;
    SDL_Keymod defaultMods;
} hotkeys_config_spec_t;

typedef struct hotkeys_config_override
{
    int set;
    SDL_Keycode key;
    SDL_Keymod mods;
} hotkeys_config_override_t;

typedef struct hotkeys_config_modal_state hotkeys_config_modal_state_t;

typedef struct hotkeys_config_entry
{
    hotkeys_config_modal_state_t *st;
    size_t specIndex;
    SDL_Keycode key;
    SDL_Keymod mods;
    e9ui_component_t *button;
    e9ui_component_t *btnUnbind;
    int conflict;
} hotkeys_config_entry_t;

struct hotkeys_config_modal_state
{
    e9ui_component_t *container;
    e9ui_component_t *scroll;
    e9ui_component_t *stack;
    e9ui_component_t *controllerSelect;
    e9ui_component_t *btnSave;
    e9ui_component_t *btnDefaults;
    e9ui_component_t *keyCaptureReturnFocus;
    hotkeys_config_entry_t *entries;
    size_t entryCount;
    e9ui_select_option_t *controllerOptions;
    char **controllerOptionValues;
    char **controllerOptionLabels;
    int controllerOptionCount;
    char pendingControllerGuid[E9UI_GAMEPAD_GUID_CAP];
    hotkeys_config_entry_t *capturingEntry;
    SDL_Keymod pendingMods;
    SDL_Keycode pendingModifierKey;
    Uint32 pendingModsExpireMs;
    int hasConflicts;
};

typedef struct hotkeys_action_registration
{
    int registryId;
    size_t specIndex;
} hotkeys_action_registration_t;

typedef enum hotkeys_config_section {
    hotkeys_config_section_global = 0,
    hotkeys_config_section_execution,
    hotkeys_config_section_checkpoints
} hotkeys_config_section_t;

static const hotkeys_config_spec_t hotkeys_configSpecs[] = {
    { "help", "Help", SDLK_F1, 0 },
    { "screenshot", "Screenshot", SDLK_F2, 0 },
    { "cycle_core_restart", "Cycle Core + Restart", SDLK_F3, 0 },
    { "rolling_save_toggle", "Rolling Save Pause/Resume", SDLK_F4, 0 },
    { "warp", "Warp", SDLK_F5, 0 },
    { "audio_toggle", "Audio Toggle", SDLK_F6, 0 },
    { "save_state", "Save State", SDLK_F7, 0 },
    { "restore_state", "Restore State", SDLK_F8, KMOD_SHIFT },
    { "restart", "Restart", SDLK_F8, 0 },
    { "reset_core", "Reset Core", SDLK_F9, 0 },
    { "hotkeys_toggle", "Hotkeys On/Off", SDLK_F11, 0 },
    { "settings", "Settings", SDLK_F12, 0 },
    { "hex_convert", "Hex Converter", SDLK_i, KMOD_CTRL },
    { "hex_convert_inline", "In-place Hex Convert", SDLK_h, KMOD_CTRL },
    { "fullscreen", "Fullscreen", SDLK_RETURN, KMOD_ALT },
    { "mouse_release", "Capture/Release Mouse", SDLK_LALT, (SDL_Keymod)(KMOD_CTRL | KMOD_ALT) },
    { "prompt_focus", "Prompt Focus", SDLK_TAB, 0 },
    { "continue", "Continue", SDLK_c, KMOD_ALT },
    { "pause", "Pause", SDLK_p, KMOD_ALT },
    { "step", "Step", SDLK_s, KMOD_ALT },
    { "next", "Next", SDLK_n, KMOD_ALT },
    { "step_inst", "Step Inst", SDLK_i, KMOD_ALT },
    { "debug_processor_toggle", "Debugger CPU Toggle", SDLK_z, KMOD_ALT },
    { "breakpoint_add_current", "Breakpoint Add Current", SDLK_d, KMOD_ALT },
    { "frame_back", "Frame Step Back", SDLK_b, KMOD_ALT },
    { "frame_step", "Frame Step", SDLK_f, KMOD_ALT },
    { "frame_continue", "Frame Continue", SDLK_g, KMOD_ALT },
    { "checkpoint_prev", "Checkpoint Toggle", SDLK_COMMA, 0 },
    { "checkpoint_reset", "Checkpoint Reset", SDLK_PERIOD, 0 },
    { "checkpoint_next", "Checkpoint Dump", SDLK_SLASH, 0 }
};

static hotkeys_config_override_t hotkeys_configOverrides[
    sizeof(hotkeys_configSpecs) / sizeof(hotkeys_configSpecs[0])
] = {0};
static hotkeys_action_registration_t *hotkeys_actionRegistrations = NULL;
static size_t hotkeys_actionRegistrationCount = 0;
static size_t hotkeys_actionRegistrationCap = 0;

static e9ui_component_t *
hotkeys_findTopModal(void);

static void
hotkeys_getConfigBindingAt(size_t index, SDL_Keycode *key, SDL_Keymod *mods);

static int
hotkeys_trackedActionAllowed(size_t specIndex, e9ui_context_t *ctx, const SDL_KeyboardEvent *kev);

static SDL_Keymod
hotkeys_normalizeMods(SDL_Keymod mods)
{
    SDL_Keymod out = 0;
    if (mods & KMOD_CTRL) {
        out = (SDL_Keymod)(out | KMOD_CTRL);
    }
    if (mods & KMOD_SHIFT) {
        out = (SDL_Keymod)(out | KMOD_SHIFT);
    }
    if (mods & KMOD_ALT) {
        out = (SDL_Keymod)(out | KMOD_ALT);
    }
    if (mods & KMOD_GUI) {
        out = (SDL_Keymod)(out | KMOD_GUI);
    }
    return out;
}

static size_t
hotkeys_configSpecCount(void)
{
    return sizeof(hotkeys_configSpecs) / sizeof(hotkeys_configSpecs[0]);
}

static int
hotkeys_checkboxMeasureWidth(e9ui_component_t *checkbox, e9ui_context_t *ctx)
{
    int width = 0;
    int height = 0;

    if (!checkbox || !ctx) {
        return 0;
    }
    e9ui_checkbox_measure(checkbox, ctx, &width, &height);
    return width > 0 ? width : 0;
}

static void
hotkeys_debuggerOptionFunChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
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
hotkeys_debuggerOptionLogsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;

    debugger.settingsEdit.logsEnabled = selected ? 1 : 0;
    settings_updateSaveLabel();
}

static void
hotkeys_debuggerOptionCrtChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;

    debugger.settingsEdit.crtEnabled = selected ? 1 : 0;
    settings_updateSaveLabel();
}

static void
hotkeys_debuggerOptionRecordChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;

    debugger.settingsEdit.recordEnabled = selected ? 1 : 0;
    settings_updateSaveLabel();
}

static void
hotkeys_debuggerOptionLogosChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;

    debugger.settingsEdit.logosEnabled = selected ? 1 : 0;
    settings_markCoreOptionsDirtyWithRestart(1);
    settings_updateSaveLabel();
}

static void
hotkeys_debuggerOptionHexByteColorsChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    (void)user;

    hex_byte_color_setEnabled(selected ? 1 : 0);
    config_saveConfig();
}

static int
hotkeys_findConfigSpecIndex(const char *id)
{
    if (!id || !id[0]) {
        return -1;
    }
    size_t count = hotkeys_configSpecCount();
    for (size_t i = 0; i < count; ++i) {
        if (hotkeys_configSpecs[i].id && strcmp(hotkeys_configSpecs[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void
hotkeys_bindingToMaskValue(SDL_Keymod mods, SDL_Keymod *outMask, SDL_Keymod *outValue)
{
    SDL_Keymod normalized = hotkeys_normalizeMods(mods);
    SDL_Keymod mask = (SDL_Keymod)(KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI);
    if (outMask) {
        *outMask = mask;
    }
    if (outValue) {
        *outValue = normalized;
    }
}

static int
hotkeys_getConfigBindingById(const char *actionId, SDL_Keycode *key, SDL_Keymod *mods)
{
    int index = hotkeys_findConfigSpecIndex(actionId);
    if (index < 0) {
        return 0;
    }
    hotkeys_getConfigBindingAt((size_t)index, key, mods);
    return 1;
}

static int
hotkeys_textboxShortcutMatches(const SDL_KeyboardEvent *kev, const char *shortcutId, void *user)
{
    (void)user;
    if (!kev || !shortcutId) {
        return 0;
    }
    return hotkeys_eventMatchesAction(kev, shortcutId);
}

static void
hotkeys_installTextboxShortcutMatcher(void)
{
    if (hotkeys_textboxShortcutMatcherInstalled) {
        return;
    }
    e9ui_textbox_setShortcutMatcher(hotkeys_textboxShortcutMatches, NULL);
    hotkeys_textboxShortcutMatcherInstalled = 1;
}

static void
hotkeys_getConfigBindingAt(size_t index, SDL_Keycode *key, SDL_Keymod *mods)
{
    SDL_Keycode outKey = SDLK_UNKNOWN;
    SDL_Keymod outMods = 0;
    if (index < hotkeys_configSpecCount()) {
        if (hotkeys_configOverrides[index].set) {
            outKey = hotkeys_configOverrides[index].key;
            outMods = hotkeys_configOverrides[index].mods;
        } else {
            outKey = hotkeys_configSpecs[index].defaultKey;
            outMods = hotkeys_configSpecs[index].defaultMods;
        }
    }
    if (key) {
        *key = outKey;
    }
    if (mods) {
        *mods = outMods;
    }
}

static void
hotkeys_trackActionRegistration(int registryId, size_t specIndex)
{
    if (registryId <= 0 || specIndex >= hotkeys_configSpecCount()) {
        return;
    }
    for (size_t i = 0; i < hotkeys_actionRegistrationCount; ++i) {
        if (hotkeys_actionRegistrations[i].registryId == registryId) {
            hotkeys_actionRegistrations[i].specIndex = specIndex;
            return;
        }
    }
    if (hotkeys_actionRegistrationCount >= hotkeys_actionRegistrationCap) {
        size_t nextCap = hotkeys_actionRegistrationCap ? hotkeys_actionRegistrationCap * 2 : 32;
        hotkeys_action_registration_t *next = (hotkeys_action_registration_t *)alloc_realloc(
            hotkeys_actionRegistrations, nextCap * sizeof(*next));
        if (!next) {
            return;
        }
        hotkeys_actionRegistrations = next;
        hotkeys_actionRegistrationCap = nextCap;
    }
    hotkeys_actionRegistrations[hotkeys_actionRegistrationCount].registryId = registryId;
    hotkeys_actionRegistrations[hotkeys_actionRegistrationCount].specIndex = specIndex;
    hotkeys_actionRegistrationCount++;
}

static void
hotkeys_untrackActionRegistration(int registryId)
{
    if (registryId <= 0) {
        return;
    }
    for (size_t i = 0; i < hotkeys_actionRegistrationCount; ++i) {
        if (hotkeys_actionRegistrations[i].registryId == registryId) {
            hotkeys_actionRegistrations[i] = hotkeys_actionRegistrations[hotkeys_actionRegistrationCount - 1];
            hotkeys_actionRegistrationCount--;
            return;
        }
    }
}

static int
hotkeys_findTrackedSpecIndexForRegistryId(int registryId, size_t *outSpecIndex)
{
    if (registryId <= 0) {
        return 0;
    }
    for (size_t i = 0; i < hotkeys_actionRegistrationCount; ++i) {
        if (hotkeys_actionRegistrations[i].registryId == registryId) {
            if (outSpecIndex) {
                *outSpecIndex = hotkeys_actionRegistrations[i].specIndex;
            }
            return 1;
        }
    }
    return 0;
}

static void
hotkeys_applyBindingToRegistryEntry(int registryId, size_t specIndex)
{
    if (!e9ui || registryId <= 0 || specIndex >= hotkeys_configSpecCount()) {
        return;
    }
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mods = 0;
    hotkeys_getConfigBindingAt(specIndex, &key, &mods);
    SDL_Keymod mask = 0;
    SDL_Keymod value = 0;
    hotkeys_bindingToMaskValue(mods, &mask, &value);

    e9k_hotkey_registry_t *hk = &e9ui->hotkeys;
    for (int i = 0; i < hk->count; ++i) {
        if (hk->entries[i].id != registryId) {
            continue;
        }
        hk->entries[i].key = (int)key;
        hk->entries[i].mask = (int)mask;
        hk->entries[i].value = (int)value;
        return;
    }
}

static void
hotkeys_refreshActionRegistrations(void)
{
    for (size_t i = 0; i < hotkeys_actionRegistrationCount; ++i) {
        hotkeys_applyBindingToRegistryEntry(hotkeys_actionRegistrations[i].registryId,
                                            hotkeys_actionRegistrations[i].specIndex);
    }
}

static int
hotkeys_eventMatchesBinding(const SDL_KeyboardEvent *kev, SDL_Keycode wantKey, SDL_Keymod wantMods)
{
    if (!kev) {
        return 0;
    }
    if (kev->keysym.sym != wantKey) {
        return 0;
    }
    SDL_Keymod evMods = hotkeys_normalizeMods(kev->keysym.mod);
    return evMods == hotkeys_normalizeMods(wantMods) ? 1 : 0;
}

int
hotkeys_eventMatchesAction(const SDL_KeyboardEvent *kev, const char *actionId)
{
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mods = 0;
    if (!hotkeys_getConfigBindingById(actionId, &key, &mods)) {
        return 0;
    }
    if (key == SDLK_UNKNOWN) {
        return 0;
    }
    return hotkeys_eventMatchesBinding(kev, key, mods);
}

static int
hotkeys_eventMatchesRegisteredHotkey(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
    if (!ctx || !kev || !e9ui) {
        return 0;
    }
    e9k_hotkey_registry_t *hk = &e9ui->hotkeys;
    SDL_Keycode key = kev->keysym.sym;
    SDL_Keymod mods = hotkeys_normalizeMods(kev->keysym.mod);

    for (int i = 0; i < hk->count; i++) {
        e9k_hotkey_entry_t *entry = &hk->entries[i];
        if (!entry->active) {
            continue;
        }
        if ((SDL_Keycode)entry->key != key) {
            continue;
        }
        if (entry->mask == 0 && entry->value == 0) {
            SDL_Keymod disallowMods = (SDL_Keymod)(mods & (KMOD_CTRL | KMOD_ALT));
            if (disallowMods != 0) {
                continue;
            }
        }
        if ((mods & (SDL_Keymod)entry->mask) != (SDL_Keymod)entry->value) {
            continue;
        }
        size_t trackedSpecIndex = 0;
        if (hotkeys_findTrackedSpecIndexForRegistryId(entry->id, &trackedSpecIndex)) {
            if (!hotkeys_trackedActionAllowed(trackedSpecIndex, ctx, kev)) {
                continue;
            }
        }
        return 1;
    }
    return 0;
}

static int
hotkeys_promptFocusActionAllowed(e9ui_context_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    if (hotkeys_findTopModal()) {
        return 0;
    }
    e9ui_component_t *focus = e9ui_getFocus(ctx);
    if (focus && focus->name && strcmp(focus->name, "emu") == 0) {
        return 0;
    }
    if (prompt_isFocused(ctx, e9ui->prompt)) {
        return 0;
    }
    if (focus && focus->name && strcmp(focus->name, "e9ui_textbox") == 0) {
        if (e9ui_textbox_getCompletionMode(focus) != e9ui_textbox_completion_none) {
            return 0;
        }
    }
    return 1;
}

static int
hotkeys_trackedActionAllowed(size_t specIndex, e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
    (void)kev;
    if (specIndex >= hotkeys_configSpecCount()) {
        return 1;
    }
    const char *id = hotkeys_configSpecs[specIndex].id;
    if (!id) {
        return 1;
    }
    if (strcmp(id, "prompt_focus") == 0) {
        return hotkeys_promptFocusActionAllowed(ctx);
    }
    return 1;
}

static void
hotkeys_setConfigBindingAt(size_t index, SDL_Keycode key, SDL_Keymod mods, int hasOverride)
{
    if (index >= hotkeys_configSpecCount()) {
        return;
    }
    if (!hasOverride) {
        hotkeys_configOverrides[index].set = 0;
        hotkeys_configOverrides[index].key = SDLK_UNKNOWN;
        hotkeys_configOverrides[index].mods = 0;
        return;
    }
    hotkeys_configOverrides[index].set = 1;
    hotkeys_configOverrides[index].key = key;
    hotkeys_configOverrides[index].mods = hotkeys_normalizeMods(mods);
}

static void
hotkeys_trimToken(char **token)
{
    if (!token || !*token) {
        return;
    }
    char *s = *token;
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
    *token = s;
}

static void
hotkeys_lowerString(char *s)
{
    if (!s) {
        return;
    }
    for (; *s; ++s) {
        *s = (char)tolower((unsigned char)*s);
    }
}

static void
hotkeys_applyDisplayTokenCase(char *s)
{
    if (!s) {
        return;
    }
    char *tokenStart = s;
    while (*tokenStart) {
        char *tokenEnd = tokenStart;
        while (*tokenEnd && *tokenEnd != '+') {
            ++tokenEnd;
        }

        size_t tokenLen = (size_t)(tokenEnd - tokenStart);
        int isSingleAlpha = (tokenLen == 1 && isalpha((unsigned char)tokenStart[0])) ? 1 : 0;
        for (char *p = tokenStart; p < tokenEnd; ++p) {
            if (!isalpha((unsigned char)*p)) {
                continue;
            }
            if (isSingleAlpha) {
                *p = (char)tolower((unsigned char)*p);
            } else {
                *p = (char)toupper((unsigned char)*p);
            }
        }

        if (*tokenEnd == '+') {
            tokenStart = tokenEnd + 1;
        } else {
            break;
        }
    }
}

static void
hotkeys_applyPlatformDisplayNames(char *s, size_t cap)
{
    if (!s || cap == 0) {
        return;
    }
#if defined(__APPLE__)
    char out[128];
    out[0] = '\0';
    char *tokenStart = s;
    while (*tokenStart) {
        char *tokenEnd = tokenStart;
        while (*tokenEnd && *tokenEnd != '+') {
            ++tokenEnd;
        }
        char token[64];
        size_t tokenLen = (size_t)(tokenEnd - tokenStart);
        if (tokenLen >= sizeof(token)) {
            tokenLen = sizeof(token) - 1;
        }
        memcpy(token, tokenStart, tokenLen);
        token[tokenLen] = '\0';
        const char *use = token;
        if (strcmp(token, "Alt") == 0 ||
            strcmp(token, "ALT") == 0 ||
            strcmp(token, "Left Alt") == 0 ||
            strcmp(token, "LEFT ALT") == 0 ||
            strcmp(token, "Right Alt") == 0 ||
            strcmp(token, "RIGHT ALT") == 0 ||
            strcmp(token, "LALT") == 0 ||
            strcmp(token, "RALT") == 0) {
            use = "OPTION";
        }
        size_t outLen = strlen(out);
        size_t useLen = strlen(use);
        size_t need = outLen + useLen + (outLen ? 1u : 0u) + 1u;
        if (need > sizeof(out)) {
            return;
        }
        if (outLen) {
            out[outLen++] = '+';
            out[outLen] = '\0';
        }
        memcpy(out + outLen, use, useLen + 1);
        if (*tokenEnd == '+') {
            tokenStart = tokenEnd + 1;
        } else {
            break;
        }
    }
    snprintf(s, cap, "%s", out);
#else
    (void)s;
    (void)cap;
#endif
}

static int
hotkeys_parseBindingString(const char *value, SDL_Keycode *outKey, SDL_Keymod *outMods)
{
    if (!value || !value[0]) {
        return 0;
    }

    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s", value);
    tmp[sizeof(tmp) - 1] = '\0';

    SDL_Keymod mods = 0;
    SDL_Keycode key = SDLK_UNKNOWN;
    char *save = NULL;
    char *token = strtok_r(tmp, "+", &save);
    while (token) {
        hotkeys_trimToken(&token);
        hotkeys_lowerString(token);
        if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
            mods = (SDL_Keymod)(mods | KMOD_CTRL);
        } else if (strcmp(token, "shift") == 0) {
            mods = (SDL_Keymod)(mods | KMOD_SHIFT);
        } else if (strcmp(token, "alt") == 0 || strcmp(token, "option") == 0) {
            mods = (SDL_Keymod)(mods | KMOD_ALT);
        } else if (strcmp(token, "cmd") == 0 || strcmp(token, "gui") == 0 || strcmp(token, "command") == 0) {
            mods = (SDL_Keymod)(mods | KMOD_GUI);
        } else if (strcmp(token, "none") == 0 || strcmp(token, "unbound") == 0) {
            key = SDLK_UNKNOWN;
        } else if (strncmp(token, "sdlk:", 5) == 0) {
            char *end = NULL;
            long v = strtol(token + 5, &end, 10);
            if (end && end != token + 5 && *end == '\0') {
                key = (SDL_Keycode)v;
            }
        } else {
            SDL_Keycode parsed = SDL_GetKeyFromName(token);
            if (parsed == SDLK_UNKNOWN && strcmp(token, "comma") == 0) {
                parsed = SDLK_COMMA;
            } else if (parsed == SDLK_UNKNOWN && strcmp(token, "period") == 0) {
                parsed = SDLK_PERIOD;
            } else if (parsed == SDLK_UNKNOWN && strcmp(token, "slash") == 0) {
                parsed = SDLK_SLASH;
            }
            key = parsed;
        }
        token = strtok_r(NULL, "+", &save);
    }

    if (key == SDLK_UNKNOWN) {
        if (outKey) {
            *outKey = SDLK_UNKNOWN;
        }
        if (outMods) {
            *outMods = hotkeys_normalizeMods(mods);
        }
        return 1;
    }
    if (outKey) {
        *outKey = key;
    }
    if (outMods) {
        *outMods = hotkeys_normalizeMods(mods);
    }
    return 1;
}

static int
hotkeys_appendBindingToken(char *out, size_t cap, const char *token)
{
    if (!out || cap == 0 || !token || !token[0]) {
        return 0;
    }
    size_t len = strlen(out);
    size_t tokenLen = strlen(token);
    size_t need = len + tokenLen + (len ? 1u : 0u) + 1u;
    if (need > cap) {
        return 0;
    }
    if (len) {
        out[len++] = '+';
        out[len] = '\0';
    }
    memcpy(out + len, token, tokenLen + 1);
    return 1;
}

static int
hotkeys_buildBindingString(SDL_Keycode key, SDL_Keymod mods, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (key == SDLK_UNKNOWN) {
        return 1;
    }

    mods = hotkeys_normalizeMods(mods);
    if (mods & KMOD_CTRL) {
        if (!hotkeys_appendBindingToken(out, cap, "ctrl")) {
            return 0;
        }
    }
    if (mods & KMOD_SHIFT) {
        if (!hotkeys_appendBindingToken(out, cap, "shift")) {
            return 0;
        }
    }
    if (mods & KMOD_ALT) {
        if (!hotkeys_appendBindingToken(out, cap, "alt")) {
            return 0;
        }
    }
    if (mods & KMOD_GUI) {
        if (!hotkeys_appendBindingToken(out, cap, "cmd")) {
            return 0;
        }
    }

    char keyName[64];
    const char *rawName = SDL_GetKeyName(key);
    if (!rawName || !rawName[0]) {
        snprintf(keyName, sizeof(keyName), "sdlk:%d", (int)key);
    } else {
        snprintf(keyName, sizeof(keyName), "%s", rawName);
        keyName[sizeof(keyName) - 1] = '\0';
        hotkeys_lowerString(keyName);
    }
    return hotkeys_appendBindingToken(out, cap, keyName);
}

static void
hotkeys_formatModsDisplay(SDL_Keymod mods, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    mods = hotkeys_normalizeMods(mods);
    if (mods & KMOD_CTRL) {
        (void)hotkeys_appendBindingToken(out, cap, "ctrl");
    }
    if (mods & KMOD_SHIFT) {
        (void)hotkeys_appendBindingToken(out, cap, "shift");
    }
    if (mods & KMOD_ALT) {
        (void)hotkeys_appendBindingToken(out, cap, "alt");
    }
    if (mods & KMOD_GUI) {
        (void)hotkeys_appendBindingToken(out, cap, "cmd");
    }
    hotkeys_applyDisplayTokenCase(out);
    hotkeys_applyPlatformDisplayNames(out, cap);
}

static void
hotkeys_drawInvalidOutline(const e9ui_component_t *comp, e9ui_context_t *ctx)
{
    if (!comp || !ctx || !ctx->renderer) {
        return;
    }
    SDL_Color base = (SDL_Color){140, 28, 28, 235};
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    int blur = e9ui_scale_px(ctx, 5);
    if (blur < 1) {
        blur = 1;
    }
    for (int i = blur; i >= 1; --i) {
        int alpha = base.a / (i + 2) + 20;
        if (alpha > 255) {
            alpha = 255;
        }
        SDL_SetRenderDrawColor(ctx->renderer, base.r, base.g, base.b, (Uint8)alpha);
        SDL_Rect r = { comp->bounds.x - i, comp->bounds.y - i,
                       comp->bounds.w + i * 2, comp->bounds.h + i * 2 };
        SDL_RenderDrawRect(ctx->renderer, &r);
    }
    SDL_SetRenderDrawColor(ctx->renderer, base.r, base.g, base.b, 255);
    SDL_Rect r0 = { comp->bounds.x - 1, comp->bounds.y - 1,
                    comp->bounds.w + 2, comp->bounds.h + 2 };
    SDL_Rect r1 = { comp->bounds.x - 2, comp->bounds.y - 2,
                    comp->bounds.w + 4, comp->bounds.h + 4 };
    SDL_RenderDrawRect(ctx->renderer, &r0);
    SDL_RenderDrawRect(ctx->renderer, &r1);
}

static void
hotkeys_configUpdateConflictUi(hotkeys_config_modal_state_t *st)
{
    if (!st || !st->btnSave) {
        return;
    }
    e9ui_setHidden(st->btnSave, st->hasConflicts ? 1 : 0);
}

static void
hotkeys_configRecomputeConflicts(hotkeys_config_modal_state_t *st)
{
    if (!st || !st->entries) {
        return;
    }
    st->hasConflicts = 0;
    for (size_t i = 0; i < st->entryCount; ++i) {
        st->entries[i].conflict = 0;
    }
    for (size_t i = 0; i < st->entryCount; ++i) {
        hotkeys_config_entry_t *a = &st->entries[i];
        if (a->key == SDLK_UNKNOWN) {
            continue;
        }
        SDL_Keymod aMods = hotkeys_normalizeMods(a->mods);
        for (size_t j = i + 1; j < st->entryCount; ++j) {
            hotkeys_config_entry_t *b = &st->entries[j];
            if (b->key == SDLK_UNKNOWN) {
                continue;
            }
            if (a->key != b->key) {
                continue;
            }
            if (aMods != hotkeys_normalizeMods(b->mods)) {
                continue;
            }
            a->conflict = 1;
            b->conflict = 1;
            st->hasConflicts = 1;
        }
    }
    for (size_t i = 0; i < st->entryCount; ++i) {
        if (!st->entries[i].button) {
            continue;
        }
        e9ui_button_setGlowPulse(st->entries[i].button, st->entries[i].conflict ? 1 : 0);
    }
    hotkeys_configUpdateConflictUi(st);
}

static void
hotkeys_configSetSavePending(hotkeys_config_modal_state_t *st)
{
    if (st && st->btnSave) {
        e9ui_button_setGlowPulse(st->btnSave, 1);
    }
}

static void
hotkeys_configFreeControllerOptions(hotkeys_config_modal_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->controllerOptionValues) {
        for (int i = 0; i < st->controllerOptionCount; ++i) {
            alloc_free(st->controllerOptionValues[i]);
        }
        alloc_free(st->controllerOptionValues);
        st->controllerOptionValues = NULL;
    }
    if (st->controllerOptionLabels) {
        for (int i = 0; i < st->controllerOptionCount; ++i) {
            alloc_free(st->controllerOptionLabels[i]);
        }
        alloc_free(st->controllerOptionLabels);
        st->controllerOptionLabels = NULL;
    }
    if (st->controllerOptions) {
        alloc_free(st->controllerOptions);
        st->controllerOptions = NULL;
    }
    st->controllerOptionCount = 0;
}

static int
hotkeys_configSetControllerOption(hotkeys_config_modal_state_t *st,
                                  int index,
                                  const char *value,
                                  const char *label)
{
    if (!st || !st->controllerOptions || !st->controllerOptionValues || !st->controllerOptionLabels) {
        return 0;
    }
    if (index < 0 || index >= st->controllerOptionCount) {
        return 0;
    }
    st->controllerOptionValues[index] = alloc_strdup(value ? value : "");
    st->controllerOptionLabels[index] = alloc_strdup(label ? label : "");
    if (!st->controllerOptionValues[index] || !st->controllerOptionLabels[index]) {
        return 0;
    }
    st->controllerOptions[index].value = st->controllerOptionValues[index];
    st->controllerOptions[index].label = st->controllerOptionLabels[index];
    return 1;
}

static void
hotkeys_configRebuildControllerOptions(hotkeys_config_modal_state_t *st)
{
    if (!st) {
        return;
    }

    hotkeys_configFreeControllerOptions(st);

    size_t availableCount = e9ui_gamepadReadAvailable(NULL, 0);
    size_t totalCount = 1 + availableCount;
    int selectedConnected = 0;
    e9ui_gamepad_info_t *pads = NULL;
    if (availableCount > 0) {
        pads = (e9ui_gamepad_info_t *)alloc_calloc(availableCount, sizeof(*pads));
        if (pads) {
            size_t readCount = e9ui_gamepadReadAvailable(pads, availableCount);
            availableCount = readCount;
        } else {
            availableCount = 0;
        }
    }
    if (st->pendingControllerGuid[0]) {
        for (size_t i = 0; i < availableCount; ++i) {
            if (strcmp(pads[i].guid, st->pendingControllerGuid) == 0) {
                selectedConnected = 1;
                break;
            }
        }
        if (!selectedConnected) {
            totalCount++;
        }
    }

    st->controllerOptions = (e9ui_select_option_t *)alloc_calloc(totalCount, sizeof(*st->controllerOptions));
    st->controllerOptionValues = (char **)alloc_calloc(totalCount, sizeof(*st->controllerOptionValues));
    st->controllerOptionLabels = (char **)alloc_calloc(totalCount, sizeof(*st->controllerOptionLabels));
    if (!st->controllerOptions || !st->controllerOptionValues || !st->controllerOptionLabels) {
        if (pads) {
            alloc_free(pads);
        }
        hotkeys_configFreeControllerOptions(st);
        return;
    }
    st->controllerOptionCount = (int)totalCount;

    int optionIndex = 0;
    if (!hotkeys_configSetControllerOption(st, optionIndex++, "auto", "Auto (First Connected)")) {
        if (pads) {
            alloc_free(pads);
        }
        hotkeys_configFreeControllerOptions(st);
        return;
    }

    if (availableCount > 0 && pads) {
        for (size_t i = 0; i < availableCount && optionIndex < st->controllerOptionCount; ++i) {
            if (!hotkeys_configSetControllerOption(st,
                                                   optionIndex++,
                                                   pads[i].guid,
                                                   pads[i].name[0] ? pads[i].name : pads[i].guid)) {
                alloc_free(pads);
                hotkeys_configFreeControllerOptions(st);
                return;
            }
        }
    }

    if (st->pendingControllerGuid[0] && !selectedConnected && optionIndex < st->controllerOptionCount) {
        char label[160];
        strutil_join3Trunc(label, sizeof(label), "Disconnected (", st->pendingControllerGuid, ")");
        if (!hotkeys_configSetControllerOption(st, optionIndex++, st->pendingControllerGuid, label)) {
            if (pads) {
                alloc_free(pads);
            }
            hotkeys_configFreeControllerOptions(st);
            return;
        }
    }

    if (st->controllerSelect) {
        const char *selectedValue = st->pendingControllerGuid[0] ? st->pendingControllerGuid : "auto";
        e9ui_labeled_select_setOptions(st->controllerSelect,
                                       st->controllerOptions,
                                       st->controllerOptionCount,
                                       selectedValue);
    }
    if (pads) {
        alloc_free(pads);
    }
}

static void
hotkeys_configControllerChanged(e9ui_context_t *ctx,
                                e9ui_component_t *comp,
                                const char *value,
                                void *user)
{
    (void)ctx;
    (void)comp;
    hotkeys_config_modal_state_t *st = (hotkeys_config_modal_state_t *)user;
    if (!st) {
        return;
    }
    const char *src = (value && strcmp(value, "auto") != 0) ? value : "";
    strutil_strlcpy(st->pendingControllerGuid, sizeof(st->pendingControllerGuid), src);
    hotkeys_configSetSavePending(st);
}

static int
hotkeys_isModifierKey(SDL_Keycode key, SDL_Keymod *outMod)
{
    SDL_Keymod mod = 0;
    switch (key) {
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        mod = KMOD_CTRL;
        break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        mod = KMOD_SHIFT;
        break;
    case SDLK_LALT:
    case SDLK_RALT:
        mod = KMOD_ALT;
        break;
    case SDLK_LGUI:
    case SDLK_RGUI:
        mod = KMOD_GUI;
        break;
    default:
        return 0;
    }
    if (outMod) {
        *outMod = mod;
    }
    return 1;
}

static void
hotkeys_buildBindingDisplayString(SDL_Keycode key, SDL_Keymod mods, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (key == SDLK_UNKNOWN) {
        return;
    }

    SDL_Keymod modifierKeyMod = 0;
    SDL_Keymod normalizedMods = hotkeys_normalizeMods(mods);
    if (hotkeys_isModifierKey(key, &modifierKeyMod)) {
        SDL_Keymod nonRedundantMods = hotkeys_normalizeMods((SDL_Keymod)(normalizedMods & ~modifierKeyMod));
        if (nonRedundantMods == 0 && modifierKeyMod != 0) {
            const char *rawName = SDL_GetKeyName(key);
            if (rawName && rawName[0]) {
                char keyName[64];
                snprintf(keyName, sizeof(keyName), "%s", rawName);
                keyName[sizeof(keyName) - 1] = '\0';
                hotkeys_lowerString(keyName);
                hotkeys_applyDisplayTokenCase(keyName);
                hotkeys_applyPlatformDisplayNames(keyName, sizeof(keyName));
                snprintf(out, cap, "%s", keyName);
            } else if (modifierKeyMod == KMOD_SHIFT) {
                snprintf(out, cap, "shift");
            } else if (modifierKeyMod == KMOD_CTRL) {
                snprintf(out, cap, "ctrl");
            } else if (modifierKeyMod == KMOD_ALT) {
                snprintf(out, cap, "alt");
            } else if (modifierKeyMod == KMOD_GUI) {
                snprintf(out, cap, "cmd");
            } else {
                snprintf(out, cap, "modifier");
            }
            hotkeys_applyDisplayTokenCase(out);
            hotkeys_applyPlatformDisplayNames(out, cap);
            return;
        }
        if (hotkeys_buildBindingString(key, nonRedundantMods, out, cap) && out[0] != '\0') {
            hotkeys_applyDisplayTokenCase(out);
            hotkeys_applyPlatformDisplayNames(out, cap);
            return;
        }
    }

    if (!hotkeys_buildBindingString(key, normalizedMods, out, cap) || out[0] == '\0') {
        out[0] = '\0';
        return;
    }
    hotkeys_applyDisplayTokenCase(out);
    hotkeys_applyPlatformDisplayNames(out, cap);
}

static void
hotkeys_configUpdateEntryButtonLabel(hotkeys_config_entry_t *entry)
{
    if (!entry || !entry->button || !entry->st) {
        return;
    }
    if (entry->st->capturingEntry == entry) {
        if (entry->st->pendingMods != 0) {
            char modsDisplay[64];
            char label[128];
            hotkeys_formatModsDisplay(entry->st->pendingMods, modsDisplay, sizeof(modsDisplay));
            snprintf(label, sizeof(label), "Press key... (%s)", modsDisplay[0] ? modsDisplay : "mods");
            e9ui_button_setLabel(entry->button, label);
        } else {
            e9ui_button_setLabel(entry->button, "Press key...");
        }
        return;
    }
    char display[96];
    hotkeys_buildBindingDisplayString(entry->key, entry->mods, display, sizeof(display));
    e9ui_button_setLabel(entry->button, display);
}

static void
hotkeys_configEndCapture(hotkeys_config_modal_state_t *st, e9ui_context_t *ctx, int restoreButtonLabel)
{
    if (!st) {
        return;
    }
    hotkeys_config_entry_t *entry = st->capturingEntry;
    st->capturingEntry = NULL;
    st->pendingMods = 0;
    st->pendingModifierKey = SDLK_UNKNOWN;
    st->pendingModsExpireMs = 0;
    if (st->container) {
        st->container->focusable = 0;
    }
    if (restoreButtonLabel && entry) {
        hotkeys_configUpdateEntryButtonLabel(entry);
    }
    if (ctx) {
        if (st->keyCaptureReturnFocus) {
            e9ui_setFocus(ctx, st->keyCaptureReturnFocus);
        } else if (e9ui_getFocus(ctx) == st->container) {
            e9ui_setFocus(ctx, NULL);
        }
    }
    st->keyCaptureReturnFocus = NULL;
}

static void
hotkeys_configBeginCapture(hotkeys_config_entry_t *entry, e9ui_context_t *ctx)
{
    if (!entry || !entry->st || !entry->st->container) {
        return;
    }
    hotkeys_config_modal_state_t *st = entry->st;
    if (st->capturingEntry == entry) {
        hotkeys_configEndCapture(st, ctx, 1);
        return;
    }
    hotkeys_configEndCapture(st, ctx, 1);
    st->capturingEntry = entry;
    st->keyCaptureReturnFocus = entry->button;
    st->pendingMods = 0;
    st->pendingModifierKey = SDLK_UNKNOWN;
    st->pendingModsExpireMs = 0;
    st->container->focusable = 1;
    e9ui_setFocus(ctx, st->container);
    hotkeys_configUpdateEntryButtonLabel(entry);
}

static void
hotkeys_configBindingButtonClicked(e9ui_context_t *ctx, void *user)
{
    hotkeys_config_entry_t *entry = (hotkeys_config_entry_t *)user;
    hotkeys_configBeginCapture(entry, ctx);
}

static void
hotkeys_configUnbindClicked(e9ui_context_t *ctx, void *user)
{
    hotkeys_config_entry_t *entry = (hotkeys_config_entry_t *)user;
    if (!entry || !entry->st) {
        return;
    }
    hotkeys_config_modal_state_t *st = entry->st;
    hotkeys_configEndCapture(st, ctx, 1);
    entry->key = SDLK_UNKNOWN;
    entry->mods = 0;
    hotkeys_configUpdateEntryButtonLabel(entry);
    hotkeys_configRecomputeConflicts(st);
    hotkeys_configSetSavePending(st);
}

static void
hotkeys_configApplyDefaults(hotkeys_config_modal_state_t *st)
{
    if (!st || !st->entries) {
        return;
    }
    for (size_t i = 0; i < st->entryCount; ++i) {
        const hotkeys_config_spec_t *spec = &hotkeys_configSpecs[st->entries[i].specIndex];
        st->entries[i].key = spec->defaultKey;
        st->entries[i].mods = hotkeys_normalizeMods(spec->defaultMods);
        hotkeys_configUpdateEntryButtonLabel(&st->entries[i]);
    }
    st->pendingControllerGuid[0] = '\0';
    hotkeys_configRebuildControllerOptions(st);
    hotkeys_configRecomputeConflicts(st);
}

static void
hotkeys_configDefaultsClicked(e9ui_context_t *ctx, void *user)
{
    hotkeys_config_modal_state_t *st = (hotkeys_config_modal_state_t *)user;
    if (!st) {
        return;
    }
    hotkeys_configEndCapture(st, ctx, 1);
    hotkeys_configApplyDefaults(st);
    for (size_t i = 0; i < st->entryCount; ++i) {
        hotkeys_configUpdateEntryButtonLabel(&st->entries[i]);
    }
    hotkeys_configSetSavePending(st);
    e9ui_showTransientMessage("HOTKEYS: DEFAULTS");
}

static void
hotkeys_closeConfigModal(void)
{
    if (!hotkeys_configModal) {
        return;
    }
    e9ui_setHidden(hotkeys_configModal, 1);
    if (!e9ui->pendingRemove) {
        e9ui->pendingRemove = hotkeys_configModal;
    }
    hotkeys_configModal = NULL;
}

static void
hotkeys_configSaveClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    hotkeys_config_modal_state_t *st = (hotkeys_config_modal_state_t *)user;
    if (!st || !st->entries || st->hasConflicts) {
        return;
    }
    for (size_t i = 0; i < st->entryCount; ++i) {
        hotkeys_config_entry_t *entry = &st->entries[i];
        const hotkeys_config_spec_t *spec = &hotkeys_configSpecs[entry->specIndex];
        SDL_Keymod entryMods = hotkeys_normalizeMods(entry->mods);
        SDL_Keymod defMods = hotkeys_normalizeMods(spec->defaultMods);
        int hasOverride = (entry->key != spec->defaultKey || entryMods != defMods) ? 1 : 0;
        hotkeys_setConfigBindingAt(entry->specIndex, entry->key, entryMods, hasOverride);
    }
    strutil_strlcpy(debugger.preferredControllerGuid,
                    sizeof(debugger.preferredControllerGuid),
                    st->pendingControllerGuid);
    e9ui_gamepadSetPreferredGuid(debugger.preferredControllerGuid);
    hotkeys_refreshActionRegistrations();
    ui_refreshHotkeyTooltips();
    config_saveConfig();
    hotkeys_closeConfigModal();
    e9ui_showTransientMessage("HOTKEYS SAVED");
}

static void
hotkeys_configCancelClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    hotkeys_closeConfigModal();
}

static void
hotkeys_configUiClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    (void)user;
    hotkeys_closeConfigModal();
}

static int
hotkeys_configContainerPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
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
hotkeys_configContainerLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    self->bounds = bounds;
    if (!self->children) {
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
hotkeys_configContainerRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->children) {
        return;
    }
    hotkeys_config_modal_state_t *st = (hotkeys_config_modal_state_t *)self->state;
    if (st && st->capturingEntry && st->pendingMods != 0 && st->pendingModsExpireMs != 0) {
        Uint32 nowMs = SDL_GetTicks();
        if (nowMs > st->pendingModsExpireMs) {
            hotkeys_config_entry_t *entry = st->capturingEntry;
            if (entry) {
                SDL_Keycode key = st->pendingModifierKey;
                if (key == SDLK_UNKNOWN) {
                    if (st->pendingMods & KMOD_SHIFT) {
                        key = SDLK_LSHIFT;
                    } else if (st->pendingMods & KMOD_CTRL) {
                        key = SDLK_LCTRL;
                    } else if (st->pendingMods & KMOD_ALT) {
                        key = SDLK_LALT;
                    } else if (st->pendingMods & KMOD_GUI) {
                        key = SDLK_LGUI;
                    }
                }
                entry->key = key;
                entry->mods = hotkeys_normalizeMods(st->pendingMods);
                hotkeys_configRecomputeConflicts(st);
                hotkeys_configSetSavePending(st);
                hotkeys_configEndCapture(st, ctx, 1);
            }
        }
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
    st = (hotkeys_config_modal_state_t *)self->state;
    if (!st || !st->entries) {
        return;
    }
    for (size_t i = 0; i < st->entryCount; ++i) {
        hotkeys_config_entry_t *entry = &st->entries[i];
        if (!entry->conflict || !entry->button || e9ui_getHidden(entry->button)) {
            continue;
        }
        hotkeys_drawInvalidOutline(entry->button, ctx);
    }
}

static int
hotkeys_configContainerHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev || ev->type != SDL_KEYDOWN) {
        return 0;
    }
    hotkeys_config_modal_state_t *st = (hotkeys_config_modal_state_t *)self->state;
    if (!st || !st->capturingEntry || e9ui_getFocus(ctx) != self) {
        return 0;
    }
    if (ev->key.repeat) {
        return 1;
    }

    hotkeys_config_entry_t *entry = st->capturingEntry;
    SDL_Keycode key = ev->key.keysym.sym;
    SDL_Keymod rawMods = hotkeys_normalizeMods(ev->key.keysym.mod);
    Uint32 nowMs = SDL_GetTicks();

    if (st->pendingMods != 0 && st->pendingModsExpireMs != 0 && nowMs > st->pendingModsExpireMs) {
        st->pendingMods = 0;
        st->pendingModsExpireMs = 0;
    }

    if (key == SDLK_ESCAPE) {
        hotkeys_configEndCapture(st, ctx, 1);
        return 1;
    }
    SDL_Keymod modifierKey = 0;
    if (hotkeys_isModifierKey(key, &modifierKey)) {
        st->pendingMods = hotkeys_normalizeMods((SDL_Keymod)(st->pendingMods | modifierKey));
        st->pendingModifierKey = key;
        st->pendingModsExpireMs = nowMs + 1000u;
        hotkeys_configUpdateEntryButtonLabel(entry);
        return 1;
    }

    SDL_Keymod capturedMods = rawMods;
    if (st->pendingMods != 0) {
        capturedMods = hotkeys_normalizeMods((SDL_Keymod)(capturedMods | st->pendingMods));
    }
    entry->key = key;
    entry->mods = capturedMods;
    hotkeys_configRecomputeConflicts(st);
    hotkeys_configSetSavePending(st);
    hotkeys_configEndCapture(st, ctx, 1);
    return 1;
}

static void
hotkeys_configContainerDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    hotkeys_config_modal_state_t *st = (hotkeys_config_modal_state_t *)self->state;
    st->container = NULL;
    st->scroll = NULL;
    st->stack = NULL;
    st->controllerSelect = NULL;
    st->btnSave = NULL;
    st->btnDefaults = NULL;
    st->keyCaptureReturnFocus = NULL;
    st->capturingEntry = NULL;
    for (size_t i = 0; i < st->entryCount; ++i) {
        st->entries[i].button = NULL;
        st->entries[i].btnUnbind = NULL;
    }
    if (st->entries) {
        alloc_free(st->entries);
        st->entries = NULL;
    }
    hotkeys_configFreeControllerOptions(st);
    alloc_free(st);
    self->state = NULL;
}

static hotkeys_config_section_t
hotkeys_configSectionForActionId(const char *actionId)
{
    if (!actionId) {
        return hotkeys_config_section_global;
    }
    if (strcmp(actionId, "prompt_focus") == 0 ||
        strcmp(actionId, "continue") == 0 ||
        strcmp(actionId, "pause") == 0 ||
        strcmp(actionId, "step") == 0 ||
        strcmp(actionId, "next") == 0 ||
        strcmp(actionId, "step_inst") == 0 ||
        strcmp(actionId, "debug_processor_toggle") == 0 ||
        strcmp(actionId, "breakpoint_add_current") == 0 ||
        strcmp(actionId, "frame_back") == 0 ||
        strcmp(actionId, "frame_step") == 0 ||
        strcmp(actionId, "frame_continue") == 0) {
        if (strcmp(actionId, "prompt_focus") != 0) {
            return hotkeys_config_section_execution;
        }
    }
    if (strcmp(actionId, "checkpoint_prev") == 0 ||
        strcmp(actionId, "checkpoint_reset") == 0 ||
        strcmp(actionId, "checkpoint_next") == 0) {
        return hotkeys_config_section_checkpoints;
    }
    return hotkeys_config_section_global;
}

static const char *
hotkeys_configSectionLabel(hotkeys_config_section_t section)
{
    switch (section) {
    case hotkeys_config_section_execution:
        return "HOTKEYS (DEBUGGER)";
    case hotkeys_config_section_checkpoints:
        return "HOTKEYS (CHECKPOINTS)";
    case hotkeys_config_section_global:
    default:
        return "HOTKEYS (GENERAL)";
    }
}

static void
hotkeys_configAddSectionHeading(e9ui_component_t *stack, e9ui_context_t *ctx, hotkeys_config_section_t section, int addTopGap)
{
    if (!stack || !ctx) {
        return;
    }
    if (addTopGap) {
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(10));
    }
    e9ui_component_t *heading = e9ui_text_make(hotkeys_configSectionLabel(section));
    if (heading) {
        e9ui_text_setBold(heading, 1);
        e9ui_text_setColor(heading, (SDL_Color){235, 235, 235, 255});
        e9ui_stack_addFixed(stack, heading);
        e9ui_stack_addFixed(stack, e9ui_vspacer_make(6));
    }
}

static e9ui_component_t *
hotkeys_makeConfigBody(hotkeys_config_modal_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !ctx) {
        return NULL;
    }

    e9ui_component_t *leftStack = e9ui_stack_makeVertical();
    e9ui_component_t *rightStack = e9ui_stack_makeVertical();
    if (!leftStack || !rightStack) {
        return NULL;
    }
    st->stack = leftStack;

    const int labelWidthPx = 260;
    const int rowGapPx = 6;
    int leftHasHeading = 0;
    int rightHasHeading = 0;
    hotkeys_config_section_t leftLastSection = hotkeys_config_section_global;
    hotkeys_config_section_t rightLastSection = hotkeys_config_section_global;
    for (size_t i = 0; i < st->entryCount; ++i) {
        hotkeys_config_entry_t *entry = &st->entries[i];
        const hotkeys_config_spec_t *spec = &hotkeys_configSpecs[entry->specIndex];
        hotkeys_config_section_t section = hotkeys_configSectionForActionId(spec->id);
        e9ui_component_t *targetStack = (section == hotkeys_config_section_global) ? leftStack : rightStack;
        int *hasHeading = (section == hotkeys_config_section_global) ? &leftHasHeading : &rightHasHeading;
        hotkeys_config_section_t *lastSection =
            (section == hotkeys_config_section_global) ? &leftLastSection : &rightLastSection;
        if (!(*hasHeading) || section != *lastSection) {
            hotkeys_configAddSectionHeading(targetStack, ctx, section, (*hasHeading) ? 1 : 0);
            *hasHeading = 1;
            *lastSection = section;
        }
        e9ui_component_t *row = e9ui_hstack_make();
        e9ui_component_t *labelText = e9ui_text_make(spec->label);
        e9ui_component_t *labelBox = e9ui_box_make(labelText);
        e9ui_box_setWidth(labelBox, e9ui_dim_fixed, labelWidthPx);
        e9ui_component_t *button = e9ui_button_make("", hotkeys_configBindingButtonClicked, entry);
        e9ui_button_setLeftJustify(button, 16);
        e9ui_button_setLargestLabel(button, "Press key... (ctrl+shift+alt+cmd)");
        entry->button = button;
        hotkeys_configUpdateEntryButtonLabel(entry);
        e9ui_component_t *btnUnbind = e9ui_button_make("", hotkeys_configUnbindClicked, entry);
        e9ui_button_setMini(btnUnbind, 1);
        e9ui_button_setIconAsset(btnUnbind, "assets/icons/trash.png");
        e9ui_setTooltip(btnUnbind, "Delete Hotkey");
        entry->btnUnbind = btnUnbind;
        int gapPx = e9ui_scale_px(ctx, 8);
        int unbindW = 0;
        int unbindH = 0;
        e9ui_button_measure(btnUnbind, ctx, &unbindW, &unbindH);
        (void)unbindH;
        e9ui_hstack_addFixed(row, labelBox, e9ui_scale_px(ctx, labelWidthPx));
        e9ui_hstack_addFixed(row, e9ui_spacer_make(gapPx), gapPx);
        e9ui_hstack_addFlex(row, button);
        if (unbindW > 0) {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(gapPx), gapPx);
            e9ui_hstack_addFixed(row, btnUnbind, unbindW);
        }
        e9ui_stack_addFixed(targetStack, row);
        e9ui_stack_addFixed(targetStack, e9ui_vspacer_make(rowGapPx));
    }

    e9ui_select_option_t initialOption = { "auto", "Auto (First Connected)" };
    const char *selectedControllerValue = st->pendingControllerGuid[0] ? st->pendingControllerGuid : "auto";
    e9ui_component_t *controllerSelect =
        e9ui_labeled_select_make(NULL,
                                 0,
                                 0,
                                 &initialOption,
                                 1,
                                 selectedControllerValue,
                                 hotkeys_configControllerChanged,
                                 st);
    if (controllerSelect) {
        e9ui_component_t *row = e9ui_hstack_make();
        e9ui_component_t *labelText = e9ui_text_make("Controller");
        e9ui_component_t *labelBox = e9ui_box_make(labelText);
        e9ui_component_t *heading = e9ui_text_make("CONTROLLERS");
        e9ui_box_setWidth(labelBox, e9ui_dim_fixed, labelWidthPx);
        st->controllerSelect = controllerSelect;
        int gapPx = e9ui_scale_px(ctx, 8);
        e9ui_stack_addFixed(rightStack, e9ui_vspacer_make(10));
        if (heading) {
            e9ui_text_setBold(heading, 1);
            e9ui_text_setColor(heading, (SDL_Color){235, 235, 235, 255});
            e9ui_stack_addFixed(rightStack, heading);
            e9ui_stack_addFixed(rightStack, e9ui_vspacer_make(6));
        }
        e9ui_hstack_addFixed(row, labelBox, e9ui_scale_px(ctx, labelWidthPx));
        e9ui_hstack_addFixed(row, e9ui_spacer_make(gapPx), gapPx);
        e9ui_hstack_addFlex(row, controllerSelect);
        e9ui_stack_addFixed(rightStack, row);
        e9ui_stack_addFixed(rightStack, e9ui_vspacer_make(rowGapPx));
        hotkeys_configRebuildControllerOptions(st);
    }

    {
        int funSelected = (e9ui->transition.mode != e9k_transition_none);
        e9ui_component_t *cbLogs = e9ui_checkbox_make("LOGS",
                                                      debugger.settingsEdit.logsEnabled,
                                                      hotkeys_debuggerOptionLogsChanged,
                                                      NULL);
        e9ui_component_t *cbFun = e9ui_checkbox_make("FUN",
                                                     funSelected,
                                                     hotkeys_debuggerOptionFunChanged,
                                                     NULL);
        e9ui_component_t *cbCrt = e9ui_checkbox_make("CRT",
                                                     debugger.settingsEdit.crtEnabled,
                                                     hotkeys_debuggerOptionCrtChanged,
                                                     NULL);
        e9ui_component_t *cbRecord = e9ui_checkbox_make("RECORD",
                                                        debugger.settingsEdit.recordEnabled,
                                                        hotkeys_debuggerOptionRecordChanged,
                                                        NULL);
        e9ui_component_t *cbLogos = e9ui_checkbox_make("LOGOS",
                                                       debugger.settingsEdit.logosEnabled,
                                                       hotkeys_debuggerOptionLogosChanged,
                                                       NULL);
        e9ui_component_t *cbHexColors = e9ui_checkbox_make("HEX COLORS",
                                                           hex_byte_color_isEnabled() ? 1 : 0,
                                                           hotkeys_debuggerOptionHexByteColorsChanged,
                                                           NULL);
        e9ui_component_t *row = e9ui_hstack_make();
        int rowGapPx = e9ui_scale_px(ctx, 8);
        int addedAny = 0;

        if (row) {
            e9ui_stack_addFixed(rightStack, e9ui_vspacer_make(10));
            e9ui_component_t *heading = e9ui_text_make("OPTIONS");
            if (heading) {
                e9ui_text_setBold(heading, 1);
                e9ui_text_setColor(heading, (SDL_Color){235, 235, 235, 255});
                e9ui_stack_addFixed(rightStack, heading);
                e9ui_stack_addFixed(rightStack, e9ui_vspacer_make(6));
            }
        }
        if (row && cbLogs) {
            int width = hotkeys_checkboxMeasureWidth(cbLogs, ctx);
            e9ui_hstack_addFixed(row, cbLogs, width);
            addedAny = 1;
        }
        if (row && cbFun) {
            int width = hotkeys_checkboxMeasureWidth(cbFun, ctx);
            if (addedAny) {
                e9ui_hstack_addFixed(row, e9ui_spacer_make(rowGapPx), rowGapPx);
            }
            e9ui_hstack_addFixed(row, cbFun, width);
            addedAny = 1;
        }
        if (row && cbCrt) {
            int width = hotkeys_checkboxMeasureWidth(cbCrt, ctx);
            if (addedAny) {
                e9ui_hstack_addFixed(row, e9ui_spacer_make(rowGapPx), rowGapPx);
            }
            e9ui_hstack_addFixed(row, cbCrt, width);
            addedAny = 1;
        }
        if (row && cbRecord) {
            int width = hotkeys_checkboxMeasureWidth(cbRecord, ctx);
            if (addedAny) {
                e9ui_hstack_addFixed(row, e9ui_spacer_make(rowGapPx), rowGapPx);
            }
            e9ui_hstack_addFixed(row, cbRecord, width);
            addedAny = 1;
        }
        if (row && cbLogos) {
            int width = hotkeys_checkboxMeasureWidth(cbLogos, ctx);
            if (addedAny) {
                e9ui_hstack_addFixed(row, e9ui_spacer_make(rowGapPx), rowGapPx);
            }
            e9ui_hstack_addFixed(row, cbLogos, width);
            addedAny = 1;
        }
        if (row && cbHexColors) {
            int width = hotkeys_checkboxMeasureWidth(cbHexColors, ctx);
            if (addedAny) {
                e9ui_hstack_addFixed(row, e9ui_spacer_make(rowGapPx), rowGapPx);
            }
            e9ui_hstack_addFixed(row, cbHexColors, width);
            addedAny = 1;
        }
        if (row && addedAny) {
            e9ui_stack_addFixed(rightStack, row);
            e9ui_stack_addFixed(rightStack, e9ui_vspacer_make(6));
        }
    }

    e9ui_stack_addFixed(leftStack, e9ui_vspacer_make(72));
    e9ui_stack_addFixed(rightStack, e9ui_vspacer_make(72));

    e9ui_component_t *columns = e9ui_hstack_make();
    int colGapPx = e9ui_scale_px(ctx, 16);
    if (colGapPx < 8) {
        colGapPx = 8;
    }
    e9ui_hstack_addFlex(columns, leftStack);
    e9ui_hstack_addFixed(columns, e9ui_spacer_make(colGapPx), colGapPx);
    e9ui_hstack_addFlex(columns, rightStack);

    e9ui_component_t *scroll = e9ui_scroll_make(columns);
    st->scroll = scroll;

    e9ui_component_t *content = e9ui_box_make(scroll);
    e9ui_box_setPadding(content, 20);

    e9ui_component_t *btnSave = e9ui_button_make("Save", hotkeys_configSaveClicked, st);
    e9ui_component_t *btnDefaults = e9ui_button_make("Defaults", hotkeys_configDefaultsClicked, st);
    e9ui_component_t *btnCancel = e9ui_button_make("Cancel", hotkeys_configCancelClicked, st);
    st->btnSave = btnSave;
    st->btnDefaults = btnDefaults;
    e9ui_button_setTheme(btnSave, e9ui_theme_button_preset_green());
    e9ui_button_setGlowPulse(btnSave, 0);
    e9ui_button_setTheme(btnCancel, e9ui_theme_button_preset_red());

    e9ui_component_t *footer = e9ui_flow_make();
    e9ui_flow_setPadding(footer, 0);
    e9ui_flow_setSpacing(footer, 8);
    e9ui_flow_setWrap(footer, 0);
    e9ui_flow_add(footer, btnSave);
    e9ui_flow_add(footer, btnDefaults);
    e9ui_flow_add(footer, btnCancel);

    e9ui_component_t *layout = e9ui_overlay_make(content, footer);
    e9ui_overlay_setAnchor(layout, e9ui_anchor_bottom_right);
    e9ui_overlay_setMargin(layout, 12);

    e9ui_component_t *container = (e9ui_component_t *)alloc_calloc(1, sizeof(*container));
    if (!container) {
        return layout;
    }
    container->name = "hotkeys_config_container";
    container->state = st;
    container->preferredHeight = hotkeys_configContainerPreferredHeight;
    container->layout = hotkeys_configContainerLayout;
    container->render = hotkeys_configContainerRender;
    container->handleEvent = hotkeys_configContainerHandleEvent;
    container->dtor = hotkeys_configContainerDtor;
    container->focusable = 0;
    st->container = container;
    e9ui_child_add(container, layout, 0);
    hotkeys_configRecomputeConflicts(st);
    return container;
}

static e9ui_component_t *
hotkeys_findTopModal(void)
{
    if (!e9ui) {
        return NULL;
    }
    e9ui_component_t *searchRoot = e9ui_getOverlayHost();
    if (!searchRoot) {
        searchRoot = e9ui->root;
    }
    if (!searchRoot) {
        return NULL;
    }
    e9ui_child_reverse_iterator iter;
    if (!e9ui_child_iterateChildrenReverse(searchRoot, &iter)) {
        return NULL;
    }
    for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
         it;
         it = e9ui_child_iteratePrev(&iter)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child) || !child->name) {
            continue;
        }
        if (strcmp(child->name, "e9ui_modal") == 0) {
            return child;
        }
    }
    return NULL;
}

void
hotkeys_resetConfigOverrides(void)
{
    size_t count = hotkeys_configSpecCount();
    for (size_t i = 0; i < count; ++i) {
        hotkeys_configOverrides[i].set = 0;
        hotkeys_configOverrides[i].key = SDLK_UNKNOWN;
        hotkeys_configOverrides[i].mods = 0;
    }
}

void
hotkeys_persistConfig(FILE *f)
{
    if (!f) {
        return;
    }
    size_t count = hotkeys_configSpecCount();
    for (size_t i = 0; i < count; ++i) {
        if (!hotkeys_configOverrides[i].set) {
            continue;
        }
        char binding[128];
        if (hotkeys_configOverrides[i].key == SDLK_UNKNOWN) {
            snprintf(binding, sizeof(binding), "unbound");
        } else if (!hotkeys_buildBindingString(hotkeys_configOverrides[i].key,
                                               hotkeys_configOverrides[i].mods,
                                               binding,
                                               sizeof(binding))) {
            continue;
        }
        fprintf(f, "comp.config.hotkey.%s=%s\n", hotkeys_configSpecs[i].id, binding);
    }
}

void
hotkeys_loadConfigProperty(const char *prop, const char *value)
{
    int index = hotkeys_findConfigSpecIndex(prop);
    if (index < 0) {
        return;
    }
    if (!value || !value[0]) {
        hotkeys_setConfigBindingAt((size_t)index, SDLK_UNKNOWN, 0, 0);
        return;
    }
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mods = 0;
    if (!hotkeys_parseBindingString(value, &key, &mods)) {
        hotkeys_setConfigBindingAt((size_t)index, SDLK_UNKNOWN, 0, 0);
        return;
    }
    hotkeys_setConfigBindingAt((size_t)index, key, mods, 1);
}

void
hotkeys_cancelConfigModal(void)
{
    hotkeys_closeConfigModal();
}

void
hotkeys_showConfigModal(e9ui_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (hotkeys_configModal) {
        hotkeys_cancelConfigModal();
        return;
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
    hotkeys_configModal = e9ui_modal_show(ctx, "Debugger Options", rect, hotkeys_configUiClosed, NULL);
    if (!hotkeys_configModal) {
        return;
    }

    hotkeys_config_modal_state_t *st =
        (hotkeys_config_modal_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        hotkeys_closeConfigModal();
        return;
    }
    st->entryCount = hotkeys_configSpecCount();
    st->entries = (hotkeys_config_entry_t *)alloc_calloc(st->entryCount, sizeof(*st->entries));
    if (!st->entries) {
        alloc_free(st);
        hotkeys_closeConfigModal();
        return;
    }
    for (size_t i = 0; i < st->entryCount; ++i) {
        SDL_Keycode key = SDLK_UNKNOWN;
        SDL_Keymod mods = 0;
        hotkeys_getConfigBindingAt(i, &key, &mods);
        st->entries[i].st = st;
        st->entries[i].specIndex = i;
        st->entries[i].key = key;
        st->entries[i].mods = hotkeys_normalizeMods(mods);
    }
    strutil_strlcpy(st->pendingControllerGuid,
                    sizeof(st->pendingControllerGuid),
                    debugger.preferredControllerGuid);

    e9ui_component_t *body = hotkeys_makeConfigBody(st, ctx);
    if (!body) {
        if (st->entries) {
            alloc_free(st->entries);
        }
        alloc_free(st);
        hotkeys_closeConfigModal();
        return;
    }
    e9ui_modal_setBodyChild(hotkeys_configModal, body, ctx);
}

static void
hotkeys_toggleCoreSystemAndRestart(void)
{
    target_nextTarget();
    char message[64];
  
    snprintf(message, sizeof(message), "RESTARTING AS %s", target->name);
    config_saveConfig();
    debugger.restartRequested = 1;
}

int
hotkeys_registerHotkey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod modMask, SDL_Keymod modValue,
                        void (*cb)(e9ui_context_t *ctx, void *user), void *user)
{
    (void)ctx;
    if (!cb) {
        return -1;
    }
    e9k_hotkey_registry_t *hk = &e9ui->hotkeys;
    if (hk->count == hk->cap) {
        int nc = hk->cap ? hk->cap * 2 : 16;
        hk->entries = (e9k_hotkey_entry_t*)alloc_realloc(hk->entries, (size_t)nc * sizeof(e9k_hotkey_entry_t));
        hk->cap = nc;
    }
    int id = (hk->next_id ? hk->next_id : 1);
    hk->next_id = id + 1;
    hk->entries[hk->count++] = (e9k_hotkey_entry_t){ id, (int)key, (int)modMask, (int)modValue, cb, user, 1 };
    return id;
}

int
hotkeys_registerActionHotkey(e9ui_context_t *ctx, const char *actionId,
                             void (*cb)(e9ui_context_t *ctx, void *user), void *user)
{
    if (!ctx || !actionId || !cb) {
        return -1;
    }
    int index = hotkeys_findConfigSpecIndex(actionId);
    if (index < 0) {
        return -1;
    }
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mods = 0;
    hotkeys_getConfigBindingAt((size_t)index, &key, &mods);
    SDL_Keymod mask = 0;
    SDL_Keymod value = 0;
    hotkeys_bindingToMaskValue(mods, &mask, &value);
    int id = hotkeys_registerHotkey(ctx, key, mask, value, cb, user);
    if (id > 0) {
        hotkeys_trackActionRegistration(id, (size_t)index);
    }
    return id;
}

int
hotkeys_registerButtonActionHotkey(e9ui_component_t *btn, e9ui_context_t *ctx, const char *actionId)
{
    if (!btn || !ctx || !actionId) {
        return -1;
    }
    int index = hotkeys_findConfigSpecIndex(actionId);
    if (index < 0) {
        return -1;
    }
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mods = 0;
    hotkeys_getConfigBindingAt((size_t)index, &key, &mods);
    SDL_Keymod mask = 0;
    SDL_Keymod value = 0;
    hotkeys_bindingToMaskValue(mods, &mask, &value);
    int id = e9ui_button_registerHotkey(btn, ctx, key, mask, value);
    if (id > 0) {
        hotkeys_trackActionRegistration(id, (size_t)index);
    }
    return id;
}

int
hotkeys_formatActionBindingDisplay(const char *actionId, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mods = 0;
    if (!hotkeys_getConfigBindingById(actionId, &key, &mods)) {
        return 0;
    }
    if (key == SDLK_UNKNOWN) {
        out[0] = '\0';
        return 1;
    }
    hotkeys_buildBindingDisplayString(key, mods, out, cap);
    return 1;
}

void
hotkeys_unregisterHotkey(e9ui_context_t *ctx, int id)
{
    (void)ctx;
    e9k_hotkey_registry_t *hk = &e9ui->hotkeys;
    for (int i = 0; i < hk->count; i++) {
        if (hk->entries[i].id == id) {
            hk->entries[i] = hk->entries[hk->count - 1];
            hk->count--;
            hotkeys_untrackActionRegistration(id);
            break;
        }
    }
}

int
hotkeys_dispatchHotkey(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
    (void)ctx;
    if (!hotkeys_enabled) {
        return 0;
    }
    if (!kev) {
        return 0;
    }
    if (kev->repeat != 0) {
        return 0;
    }
    SDL_Keycode key = kev->keysym.sym;
    SDL_Keymod rawMods = kev->keysym.mod;
    SDL_Keymod mods = 0;
    if (rawMods & KMOD_CTRL) {
        mods = (SDL_Keymod)(mods | KMOD_CTRL);
    }
    if (rawMods & KMOD_SHIFT) {
        mods = (SDL_Keymod)(mods | KMOD_SHIFT);
    }
    if (rawMods & KMOD_ALT) {
        mods = (SDL_Keymod)(mods | KMOD_ALT);
    }
    if (rawMods & KMOD_GUI) {
        mods = (SDL_Keymod)(mods | KMOD_GUI);
    }
    e9ui_component_t *focus = ctx ? e9ui_getFocus(ctx) : NULL;
    int focusIsTextbox = 0;
    if (focus && focus->name && strcmp(focus->name, "e9ui_textbox") == 0) {
        focusIsTextbox = 1;
    }
    if ((mods & (KMOD_CTRL | KMOD_GUI)) != 0 && (key == SDLK_s || key == SDLK_r)) {
        if (focus && focus->name && strcmp(focus->name, "source_pane") == 0) {
            if (source_pane_getMode(focus) == source_pane_mode_c) {
                return 0;
            }
        }
        if (focusIsTextbox) {
            return 0;
        }
    }
    if (focus) {
        SDL_Keymod noShiftMods = (SDL_Keymod)(mods & (KMOD_CTRL|KMOD_ALT|KMOD_GUI));
        int printable = (key >= 32 && key <= 126);
        if (focusIsTextbox && noShiftMods == 0 && printable) {
            return 0;
        }
        if (noShiftMods == 0 && printable && !hotkeys_eventMatchesRegisteredHotkey(ctx, kev)) {
            return 0;
        }
    }
    e9k_hotkey_registry_t *hk = &e9ui->hotkeys;
    for (int i = 0; i < hk->count; i++) {
        e9k_hotkey_entry_t *e = &hk->entries[i];
        if (!e->active) {
            continue;
        }
        if ((SDL_Keycode)e->key == key) {
            if (e->mask == 0 && e->value == 0) {
                SDL_Keymod disallowMods = (SDL_Keymod)(mods & (KMOD_CTRL | KMOD_ALT));
                if (disallowMods != 0) {
                    continue;
                }
            }
            if ((mods & (SDL_Keymod)e->mask) == (SDL_Keymod)e->value) {
                size_t trackedSpecIndex = 0;
                if (hotkeys_findTrackedSpecIndexForRegistryId(e->id, &trackedSpecIndex)) {
                    if (!hotkeys_trackedActionAllowed(trackedSpecIndex, ctx, kev)) {
                        continue;
                    }
                }
                if (e->cb) {
                    e->cb(ctx, e->user);
                }
                return 1;
            }
        }
    }
    return 0;
}

int
hotkeys_handleMouseCaptureKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
    if (hotkeys_eventMatchesAction(kev, "mouse_release")) {
        if (!kev->repeat && emu_mouseCaptureRelease(ctx)) {
            return 1;
        }
        if (hotkeys_configModal) {
            return 0;
        }
        if (kev->repeat) {
            return 1;
        }
        return emu_mouseCaptureStart(ctx);
    }
    return 0;
}

int
hotkeys_handleKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
    if (!ctx || !kev) {
        return 0;
    }
    hotkeys_installTextboxShortcutMatcher();
    SDL_Keycode key = kev->keysym.sym;
    if (hotkeys_configModal) {
        if (key == SDLK_ESCAPE) {
            e9ui_component_t *focus = e9ui_getFocus(ctx);
            if (focus && focus->name && strcmp(focus->name, "hotkeys_config_container") == 0) {
                return 0;
            }
            hotkeys_cancelConfigModal();
            return 1;
        }
        if (key == SDLK_TAB) {
            if (e9ui_getFocus(ctx) == NULL) {
                e9ui_component_t *modal = hotkeys_findTopModal();
                if (modal) {
                    e9ui_component_t *first = e9ui_focusFindNext(modal, NULL, 0);
                    if (first) {
                        e9ui_setFocus(ctx, first);
                        return 1;
                    }
                }
            }
        }
        return 0;
    }
    SDL_Keymod ctrlMods = (SDL_Keymod)(kev->keysym.mod & (KMOD_CTRL | KMOD_GUI));
    SDL_Keymod blockedMods = (SDL_Keymod)(kev->keysym.mod & KMOD_ALT);
    if (key == SDLK_c && ctrlMods != 0 && blockedMods == 0) {
        if (e9ui_text_select_hasSelection()) {
            e9ui_component_t *focus = e9ui_getFocus(ctx);
            if (!focus || !focus->name || strcmp(focus->name, "e9ui_textbox") != 0) {
                e9ui_text_select_copyToClipboard();
                return 1;
            }
        }
    }
    if (kev->repeat == 0 && hotkeys_eventMatchesAction(kev, "hotkeys_toggle")) {
        hotkeys_enabled = hotkeys_enabled ? 0 : 1;
        e9ui_showTransientMessage(hotkeys_enabled ? "HOTKEYS ON" : "HOTKEYS OFF");
        return 1;
    }
    if (kev->repeat == 0 && hotkeys_eventMatchesAction(kev, "hex_convert")) {
        hex_convert_toggle(ctx);
        return 1;
    }
    if (!hotkeys_enabled &&
        !hotkeys_eventMatchesAction(kev, "fullscreen") &&
        !hotkeys_eventMatchesAction(kev, "hex_convert")) {
        return 0;
    }
    if (key == SDLK_ESCAPE) {
        if (hex_convert_isOpen()) {
            hex_convert_close();
            return 1;
        }
        if (e9ui_windowCloseTopOverlay()) {
            return 1;
        }
        if (e9ui->helpModal) {
            help_cancelModal();
            return 1;
        }
        if (e9ui->coreOptionsModal) {
            core_options_cancelModal();
            return 1;
        }
        if (hotkeys_configModal) {
            hotkeys_cancelConfigModal();
            return 1;
        }
        if (e9ui->settingsModal) {
            debugger_cancelSettingsModal();
            return 1;
        }
        e9ui_component_t *focus = e9ui_getFocus(ctx);
        if (focus && focus->name && strcmp(focus->name, "source_pane") == 0) {
            if (source_pane_getMode(focus) == source_pane_mode_c) {
                return 0;
            }
        }
        if (focus && focus->name &&
            (strcmp(focus->name, "e9ui_textbox") == 0 ||
             strcmp(focus->name, "e9ui_data_edit") == 0)) {
            return 0;
        }
        return 1;
    }
    if (hotkeys_eventMatchesAction(kev, "help")) {
        e9ui_setFocus(ctx, NULL);
        if (e9ui->helpModal) {
            help_cancelModal();
        } else {
            help_showModal(ctx);
        }
        return 1;
    }
    if (hotkeys_eventMatchesAction(kev, "screenshot")) {
        ui_copyFramebufferToClipboard();
        return 1;
    }
    if (hotkeys_eventMatchesAction(kev, "cycle_core_restart")) {
        e9ui_setFocus(ctx, NULL);
        hotkeys_toggleCoreSystemAndRestart();
        return 1;
    }
    if (hotkeys_eventMatchesAction(kev, "rolling_save_toggle")) {
      if (0) {
        int enabled = e9ui_getFpsEnabled();
        e9ui_setFpsEnabled(!enabled);
        e9ui_showTransientMessage(!enabled ? "FPS ON" : "FPS OFF");
        return 1;
      }
      ui_toggleRollingSavePauseResume();
      return 1;
    }
    if (kev->repeat == 0 && hotkeys_eventMatchesAction(kev, "settings")) {
        if (emu_mouseCaptureRelease(ctx)) {
            e9ui_showTransientMessage("MOUSE RELEASED");
        }
        if (e9ui->settingsModal) {
            debugger_cancelSettingsModal();
        } else {
            settings_uiOpen(ctx, NULL);
        }
        return 1;
    }
    if (kev->repeat == 0 && hotkeys_eventMatchesAction(kev, "fullscreen")) {
        if (e9ui->fullscreen) {
            e9ui_clearFullscreenComponent();
        } else {
            e9ui_component_t *geo_box = e9ui_findById(e9ui->root, "libretro_box");
            if (geo_box) {
                e9ui_setFullscreenComponent(geo_box);
            } else {
                e9ui_component_t *geo_view = e9ui_findById(e9ui->root, "geo_view");
                if (geo_view) {
                    e9ui_setFullscreenComponent(geo_view);
                }
            }
        }
      
        return 1;
    }
    if (hotkeys_eventMatchesAction(kev, "checkpoint_prev") ||
        hotkeys_eventMatchesAction(kev, "checkpoint_reset") ||
        hotkeys_eventMatchesAction(kev, "checkpoint_next")) {
        int has_focus = (e9ui_getFocus(ctx) != NULL);
        if (!has_focus) {
            if (!input_record_isPlayback()) {
                input_record_recordUiKeyEvent(debugger.frameCounter + 1,
                                              (unsigned)key,
                                              (uint16_t)kev->keysym.mod,
                                              kev->repeat,
                                              1);
            }
            if (hotkeys_eventMatchesAction(kev, "checkpoint_prev")) {
                profile_checkpoints_toggle();
            } else if (hotkeys_eventMatchesAction(kev, "checkpoint_reset")) {
                profile_checkpoints_reset();
            } else if (hotkeys_eventMatchesAction(kev, "checkpoint_next")) {
                profile_checkpoints_dump();
            }
            return 1;
        }
    }
    if (key == SDLK_TAB) {
        if (e9ui_getFocus(ctx) == NULL) {
            e9ui_component_t *modal = hotkeys_findTopModal();
            if (modal) {
                e9ui_component_t *first = e9ui_focusFindNext(modal, NULL, 0);
                if (first) {
                    e9ui_setFocus(ctx, first);
                    return 1;
                }
            }
        }
    }
    if (ctx->dispatchHotkey) {
        if (ctx->dispatchHotkey(ctx, kev)) {
            return 1;
        }
    }
    return 0;
}

void
hotkeys_shutdown(void)
{
    hotkeys_configModal = NULL;
    if (hotkeys_actionRegistrations) {
        alloc_free(hotkeys_actionRegistrations);
        hotkeys_actionRegistrations = NULL;
    }
    hotkeys_actionRegistrationCount = 0;
    hotkeys_actionRegistrationCap = 0;
    if (e9ui->hotkeys.entries) {
        alloc_free(e9ui->hotkeys.entries);
        e9ui->hotkeys.entries = NULL;
        e9ui->hotkeys.count = e9ui->hotkeys.cap =
            e9ui->hotkeys.next_id = 0;
    }
}
