/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>

#include "debugger.h"

SDL_Texture *
system_badge_getTexture(SDL_Renderer *renderer, debugger_system_type_t coreSystem, int *outW, int *outH);

