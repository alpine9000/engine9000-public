/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"
#include "core_config.h"

typedef struct core_options_kv {
    char *key;
    char *value;
} core_options_kv_t;

typedef struct core_options_category_cb {
    struct core_options_modal_state *st;
    const char *categoryKey;
    e9ui_component_t *button;
    int (*origHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
} core_options_category_cb_t;

typedef struct core_options_option_cb {
    struct core_options_modal_state *st;
    const char *key;
    const char *enabledValue;
    const char *disabledValue;
    e9ui_component_t *button;
    e9ui_component_t *clearButton;
    e9ui_component_t *focusComp;
    int keyboardBinding;
    int (*origHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
    int (*clearOrigHandleEvent)(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
} core_options_option_cb_t;

typedef struct core_options_modal_state {
    core_options_kv_t *entries;
    size_t entryCount;
    size_t entryCap;

    const struct retro_core_option_v2_category *cats;
    size_t catCount;
    const struct retro_core_option_v2_definition *defs;
    size_t defCount;
    struct retro_core_option_v2_category *ownedCats;
    size_t ownedCatCount;
    struct retro_core_option_v2_definition *ownedDefs;
    size_t ownedDefCount;

    const char *selectedCategoryKey;

    e9ui_component_t *container;
    e9ui_component_t *categoryScroll;
    e9ui_component_t *categoryStack;
    int categoryWidthPx;

    e9ui_component_t *optionsScroll;
    e9ui_component_t *optionsStack;
    int optionsWidthPx;

    e9ui_component_t *btnSave;
    e9ui_component_t *btnDefaults;
    e9ui_component_t *keyCaptureReturnFocus;
    core_options_option_cb_t *capturingKeybind;

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

void
core_options_cancelModal(void);

void
core_options_showModal(e9ui_context_t *ctx);

void
core_options_uiOpen(e9ui_context_t *ctx, void *user);

const struct e9k_system_config *
core_options_selectConfig(void);

const char *
core_options_findDefaultValue(const core_options_modal_state_t *st,
                                          const char *key);

int
core_options_stringsEqual(const char *a, const char *b);

void
core_options_closeModal(void);
