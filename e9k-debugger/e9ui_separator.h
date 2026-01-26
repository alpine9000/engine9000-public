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

e9ui_component_t *
e9ui_separator_make(int width_px);

void
e9ui_separator_setWidth(e9ui_component_t *comp, int width_px);

void
e9ui_separator_measure(e9ui_component_t *comp, e9ui_context_t *ctx, int *out_w, int *out_h);


