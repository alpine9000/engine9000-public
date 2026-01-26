/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>
#include <stddef.h>
#include "e9ui_component.h"
#include "e9ui_context.h"

typedef enum source_pane_mode {
    source_pane_mode_c = 0,
    source_pane_mode_a = 1 
} source_pane_mode_t;
e9ui_component_t *
source_pane_make(void);

void
source_pane_setMode(e9ui_component_t *comp, source_pane_mode_t mode);

source_pane_mode_t
source_pane_getMode(e9ui_component_t *comp);

void
source_pane_setToggleVisible(e9ui_component_t *comp, int visible);

void
source_pane_markNeedsRefresh(e9ui_component_t *comp);

void
source_pane_centerOnAddress(e9ui_component_t *comp, e9ui_context_t *ctx, uint32_t addr);

int
source_pane_getCurrentFile(e9ui_component_t *comp, char *out, size_t cap);


