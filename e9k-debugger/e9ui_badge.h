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
e9ui_badge_make(void);

void
e9ui_badge_set(e9ui_component_t *badge,
                 const char *left,
                 const char *right,
                 SDL_Color leftBg,
                 SDL_Color rightBg,
                 SDL_Color text);


