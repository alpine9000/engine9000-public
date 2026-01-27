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

typedef void (*e9ui_labeled_textbox_change_cb_t)(e9ui_context_t *ctx, e9ui_component_t *comp,
                                                const char *text, void *user);

e9ui_component_t *
e9ui_labeled_textbox_make(const char *label, int labelWidth_px, int totalWidth_px,
                          e9ui_labeled_textbox_change_cb_t cb, void *user);

void
e9ui_labeled_textbox_setLabelWidth(e9ui_component_t *comp, int labelWidth_px);

void
e9ui_labeled_textbox_setTotalWidth(e9ui_component_t *comp, int totalWidth_px);

void
e9ui_labeled_textbox_setText(e9ui_component_t *comp, const char *text);

const char *
e9ui_labeled_textbox_getText(const e9ui_component_t *comp);

void
e9ui_labeled_textbox_setOnChange(e9ui_component_t *comp, e9ui_labeled_textbox_change_cb_t cb, void *user);

e9ui_component_t *
e9ui_labeled_textbox_getTextbox(const e9ui_component_t *comp);

