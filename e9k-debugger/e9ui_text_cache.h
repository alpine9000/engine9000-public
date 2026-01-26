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

#define E9UI_TEXT_CACHE_DEFAULT_MAX 1024

void e9ui_text_cache_setMaxEntries(int max_entries);
void e9ui_text_cache_clear(void);
void e9ui_text_cache_clearRenderer(SDL_Renderer *renderer);

SDL_Texture *e9ui_text_cache_get(SDL_Renderer *renderer,
                                 TTF_Font *font,
                                 const char *text,
                                 SDL_Color color,
                                 int use_utf8,
                                 int *out_w,
                                 int *out_h);

static inline SDL_Texture *
e9ui_text_cache_getText(SDL_Renderer *renderer,
                        TTF_Font *font,
                        const char *text,
                        SDL_Color color,
                        int *out_w,
                        int *out_h)
{
    return e9ui_text_cache_get(renderer, font, text, color, 0, out_w, out_h);
}

static inline SDL_Texture *
e9ui_text_cache_getUTF8(SDL_Renderer *renderer,
                        TTF_Font *font,
                        const char *text,
                        SDL_Color color,
                        int *out_w,
                        int *out_h)
{
    return e9ui_text_cache_get(renderer, font, text, color, 1, out_w, out_h);
}
