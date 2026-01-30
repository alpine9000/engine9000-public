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
e9ui_header_flow_make(e9ui_component_t *left,
                      int leftWidth_px,
                      e9ui_component_t *right,
                      int rightWidth_px,
                      int headerHeight_px);

void
e9ui_header_flow_setPadding(e9ui_component_t *flow, int pad_px);

void
e9ui_header_flow_setSpacing(e9ui_component_t *flow, int gap_px);

void
e9ui_header_flow_setWrap(e9ui_component_t *flow, int wrap);

void
e9ui_header_flow_add(e9ui_component_t *flow, e9ui_component_t *child);

