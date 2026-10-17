/*
 * COPYRIGHT (C) 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>

#include "e9ui.h"


int
hotkeys_registerHotkey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod modMask, SDL_Keymod modValue,
                        void (*cb)(e9ui_context_t *ctx, void *user), void *user);

int
hotkeys_registerActionHotkey(e9ui_context_t *ctx, const char *actionId,
                             void (*cb)(e9ui_context_t *ctx, void *user), void *user);

int
hotkeys_registerButtonActionHotkey(e9ui_component_t *btn, e9ui_context_t *ctx, const char *actionId);

void
hotkeys_unregisterHotkey(e9ui_context_t *ctx, int id);

int
hotkeys_dispatchHotkey(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev);

int
hotkeys_handleMouseCaptureKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev);

int
hotkeys_handleKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev);

int
hotkeys_eventMatchesAction(const SDL_KeyboardEvent *kev, const char *actionId);

int
hotkeys_formatActionBindingDisplay(const char *actionId, char *out, size_t cap);

void
hotkeys_resetConfigOverrides(void);

void
hotkeys_persistConfig(FILE *f);

void
hotkeys_loadConfigProperty(const char *prop, const char *value);

void
hotkeys_showConfigModal(e9ui_context_t *ctx);

void
hotkeys_cancelConfigModal(void);

void
hotkeys_shutdown(void);
