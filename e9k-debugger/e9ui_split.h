/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include "e9ui_component.h"

typedef enum {
    e9ui_orient_vertical = 0,  
    e9ui_orient_horizontal     
} e9ui_orient_t;

e9ui_component_t *
e9ui_split_makeComponent(e9ui_component_t *a,
                           e9ui_component_t *b,
                           e9ui_orient_t orient,
                           float ratio,
                           int grip_px);

float
e9ui_split_getRatio(e9ui_component_t *split);

void
e9ui_split_setRatio(e9ui_component_t *split, float ratio);

void
e9ui_split_setId(e9ui_component_t *split, const char *id);

void
e9ui_split_resetCursors(void);


