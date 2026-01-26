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

typedef void (*e9ui_labeled_checkbox_cb_t)(e9ui_component_t *self, e9ui_context_t *ctx,
                                           int selected, void *user);

e9ui_component_t *
e9ui_labeled_checkbox_make(const char *label, int labelWidth_px, int totalWidth_px,
                           int selected, e9ui_labeled_checkbox_cb_t cb, void *user);

void
e9ui_labeled_checkbox_setLabelWidth(e9ui_component_t *comp, int labelWidth_px);

void
e9ui_labeled_checkbox_setTotalWidth(e9ui_component_t *comp, int totalWidth_px);

void
e9ui_labeled_checkbox_setSelected(e9ui_component_t *comp, int selected, e9ui_context_t *ctx);

int
e9ui_labeled_checkbox_isSelected(e9ui_component_t *comp);

e9ui_component_t *
e9ui_labeled_checkbox_getCheckbox(const e9ui_component_t *comp);


