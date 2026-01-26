/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"
#include "e9ui_context.h"

typedef void (*e9ui_textbox_submit_cb_t)(e9ui_context_t *ctx, void *user);
typedef void (*e9ui_textbox_change_cb_t)(e9ui_context_t *ctx, void *user);
typedef int (*e9ui_textbox_key_cb_t)(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user);

e9ui_component_t *
e9ui_textbox_make(int maxLen, e9ui_textbox_submit_cb_t onSubmit, e9ui_textbox_change_cb_t onChange,
                      void *user);

void
e9ui_textbox_setText(e9ui_component_t *comp, const char *text);

const char *
e9ui_textbox_getText(const e9ui_component_t *comp);

int
e9ui_textbox_getCursor(const e9ui_component_t *comp);

void
e9ui_textbox_setCursor(e9ui_component_t *comp, int cursor);

void
e9ui_textbox_setKeyHandler(e9ui_component_t *comp, e9ui_textbox_key_cb_t cb, void *user);

void *
e9ui_textbox_getUser(const e9ui_component_t *comp);

void
e9ui_textbox_setPlaceholder(e9ui_component_t *comp, const char *placeholder);

void
e9ui_textbox_setFrameVisible(e9ui_component_t *comp, int visible);

void
e9ui_textbox_setEditable(e9ui_component_t *comp, int editable);

int
e9ui_textbox_isEditable(const e9ui_component_t *comp);

void
e9ui_textbox_setNumericOnly(e9ui_component_t *comp, int numeric_only);


