/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>

int
gl_composite_init(SDL_Window *window, SDL_Renderer *renderer);

void
gl_composite_shutdown(void);

int
gl_composite_isActive(void);

void
gl_composite_renderFrame(SDL_Renderer *renderer, const uint8_t *data, int width, int height,
                         size_t pitch, const SDL_Rect *dst);

int
gl_composite_captureToRenderer(SDL_Renderer *renderer, const uint8_t *data, int width, int height,
                               size_t pitch, const SDL_Rect *dst);

int
gl_composite_isCrtShaderAdvanced(void);

int
gl_composite_toggleCrtShaderAdvanced(void);


