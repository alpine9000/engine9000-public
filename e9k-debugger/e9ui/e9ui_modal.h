/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"

typedef void (*e9ui_modal_close_cb_t)(e9ui_component_t *modal, void *user);

e9ui_component_t *
e9ui_modal_make(const char *title, e9ui_rect_t rect, e9ui_modal_close_cb_t onClose, void *user);

e9ui_component_t *
e9ui_modal_show(e9ui_context_t *ctx, const char *title, e9ui_rect_t rect,
           e9ui_modal_close_cb_t onClose, void *user);

void
e9ui_modal_setBodyChild(e9ui_component_t *modal, e9ui_component_t *child, e9ui_context_t *ctx);


