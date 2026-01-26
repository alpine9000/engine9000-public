/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"

e9ui_component_t *
e9ui_flow_make(void);

void
e9ui_flow_setPadding(e9ui_component_t *flow, int pad_px);

void
e9ui_flow_setSpacing(e9ui_component_t *flow, int gap_px);

void
e9ui_flow_add(e9ui_component_t *flow, e9ui_component_t *child);

void
e9ui_flow_setWrap(e9ui_component_t *flow, int wrap);

void
e9ui_flow_setBaseMargin(e9ui_component_t *flow, int margin_px);

void
e9ui_flow_measure(e9ui_component_t *flow, e9ui_context_t *ctx, int *outW, int *outH);


