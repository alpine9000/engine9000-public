/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <stdio.h>

#include "geo_debug_sprite.h"

void
sprite_debug_toggle(void);

int
sprite_debug_is_open(void);

void
sprite_debug_render(const geo_debug_sprite_state_t *st);

void
sprite_debug_handleWindowEvent(const SDL_Event *ev);

int
sprite_debug_is_window_id(uint32_t window_id);

void
sprite_debug_persistConfig(FILE *file);

int
sprite_debug_loadConfigProperty(const char *prop, const char *value);


