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

typedef struct e9ui_select_option {
    const char *value;
    const char *label;
} e9ui_select_option_t;

typedef void (*e9ui_labeled_select_change_cb_t)(e9ui_context_t *ctx, e9ui_component_t *comp,
                                               const char *value, void *user);

e9ui_component_t *
e9ui_labeled_select_make(const char *label, int labelWidth_px, int totalWidth_px,
                         const e9ui_select_option_t *options, int optionCount,
                         const char *initialValue,
                         e9ui_labeled_select_change_cb_t cb, void *user);

void
e9ui_labeled_select_setLabelWidth(e9ui_component_t *comp, int labelWidth_px);

void
e9ui_labeled_select_setTotalWidth(e9ui_component_t *comp, int totalWidth_px);

void
e9ui_labeled_select_setValue(e9ui_component_t *comp, const char *value);

const char *
e9ui_labeled_select_getValue(const e9ui_component_t *comp);

void
e9ui_labeled_select_setOnChange(e9ui_component_t *comp, e9ui_labeled_select_change_cb_t cb, void *user);

e9ui_component_t *
e9ui_labeled_select_getButton(const e9ui_component_t *comp);

