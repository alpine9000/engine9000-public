/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"
#include <SDL.h>

typedef enum {
    e9ui_dim_grow = 0,
    e9ui_dim_fixed,
} e9ui_dim_mode_t;

typedef enum {
    e9ui_valign_start = 0,
    e9ui_valign_center,
    e9ui_valign_end,
} e9ui_valign_t;

e9ui_component_t *
e9ui_box_make(e9ui_component_t *child);

void
e9ui_box_setPadding(e9ui_component_t *box, int pad_px);

void
e9ui_box_setWidth(e9ui_component_t *box, e9ui_dim_mode_t mode, int pixels);

void
e9ui_box_setHeight(e9ui_component_t *box, e9ui_dim_mode_t mode, int pixels);

void
e9ui_box_setVAlign(e9ui_component_t *box, e9ui_valign_t align);

enum {
    E9UI_BORDER_TOP    = 1 << 0,
    E9UI_BORDER_RIGHT  = 1 << 1,
    E9UI_BORDER_BOTTOM = 1 << 2,
    E9UI_BORDER_LEFT   = 1 << 3,
};

void
e9ui_box_setBorder(e9ui_component_t *box, int sides_mask, SDL_Color color, int thickness_px);

void
e9ui_box_setTitlebar(e9ui_component_t *box, const char *title, const char *iconAsset);

void
e9ui_box_setChild(e9ui_component_t *box, e9ui_component_t *child, e9ui_context_t *ctx);

void
e9ui_box_resetCursors(void);


