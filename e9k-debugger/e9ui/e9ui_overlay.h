/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"

typedef enum e9ui_overlay_anchor {
    e9ui_anchor_top_left = 0,
    e9ui_anchor_top_right = 1,
    e9ui_anchor_bottom_left = 2,
    e9ui_anchor_bottom_right = 3
} e9ui_overlay_anchor_t;

e9ui_component_t *
e9ui_overlay_make(e9ui_component_t *content, e9ui_component_t *overlay);

void
e9ui_overlay_setAnchor(e9ui_component_t *c, e9ui_overlay_anchor_t anchor);

void
e9ui_overlay_setMargin(e9ui_component_t *c, int margin_px);


