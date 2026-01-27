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

typedef void (*e9ui_checkbox_cb_t)(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user);

e9ui_component_t *
e9ui_checkbox_make(const char *label, int selected, e9ui_checkbox_cb_t cb, void *user);

void
e9ui_checkbox_setSelected(e9ui_component_t *checkbox, int selected, e9ui_context_t *ctx);

int
e9ui_checkbox_isSelected(e9ui_component_t *checkbox);

int
e9ui_checkbox_getMargin(const e9ui_context_t *ctx);

int
e9ui_checkbox_getTextGap(const e9ui_context_t *ctx);

void
e9ui_checkbox_setLeftMargin(e9ui_component_t *checkbox, int margin);

int
e9ui_checkbox_getLeftMargin(const e9ui_component_t *checkbox, const e9ui_context_t *ctx);


