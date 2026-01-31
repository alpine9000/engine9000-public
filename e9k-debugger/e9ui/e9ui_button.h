/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"

struct e9k_theme_button;
typedef void (*e9ui_button_cb)(e9ui_context_t *ctx, void *user);

e9ui_component_t *
e9ui_button_make(const char *label, e9ui_button_cb onClick, void *user);

void
e9ui_button_measure(e9ui_component_t *btn, e9ui_context_t *ctx, int *outW, int *outH);

void
e9ui_button_setLabel(e9ui_component_t *btn, const char *label);

void
e9ui_button_setLargestLabel(e9ui_component_t *btn, const char *largest_label);

void
e9ui_button_setMini(e9ui_component_t *btn, int enable);

void
e9ui_button_setGlowPulse(e9ui_component_t *btn, int enable);

void
e9ui_button_setIconAsset(e9ui_component_t *btn, const char *rel_asset_png);

void
e9ui_button_setLeftJustify(e9ui_component_t *btn, int padding_px);

void
e9ui_button_setIconRightPadding(e9ui_component_t *btn, int padding_px);

int
e9ui_button_registerHotkey(e9ui_component_t *btn, e9ui_context_t *ctx,
                             SDL_Keycode key, SDL_Keymod modMask, SDL_Keymod modValue);

void
e9ui_button_setTheme(e9ui_component_t *btn, const struct e9k_theme_button *theme);

void
e9ui_button_clearTheme(e9ui_component_t *btn);
