/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>

#include <SDL_image.h>

#include "system_badge.h"

#include "debug.h"
#include "file.h"

typedef struct system_badge_cache {
    SDL_Renderer *renderer;
    SDL_Texture *amigaTex;
    int amigaW;
    int amigaH;
    SDL_Texture *neogeoTex;
    int neogeoW;
    int neogeoH;
} system_badge_cache_t;

static system_badge_cache_t system_badge_cache;

static void
system_badge_resetCache(SDL_Renderer *renderer)
{
    if (system_badge_cache.amigaTex) {
        SDL_DestroyTexture(system_badge_cache.amigaTex);
        system_badge_cache.amigaTex = NULL;
    }
    if (system_badge_cache.neogeoTex) {
        SDL_DestroyTexture(system_badge_cache.neogeoTex);
        system_badge_cache.neogeoTex = NULL;
    }
    system_badge_cache.amigaW = 0;
    system_badge_cache.amigaH = 0;
    system_badge_cache.neogeoW = 0;
    system_badge_cache.neogeoH = 0;
    system_badge_cache.renderer = renderer;
}

static SDL_Texture *
system_badge_loadTexture(SDL_Renderer *renderer, const char *asset, int *outW, int *outH)
{
    if (outW) {
        *outW = 0;
    }
    if (outH) {
        *outH = 0;
    }
    if (!renderer || !asset || !*asset) {
        return NULL;
    }

    char path[PATH_MAX];
    if (!file_getAssetPath(asset, path, sizeof(path))) {
        return NULL;
    }
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        debug_error("system_badge: IMG_Load failed for %s: %s", path, IMG_GetError());
        return NULL;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, s);
    if (outW) {
        *outW = s->w;
    }
    if (outH) {
        *outH = s->h;
    }
    SDL_FreeSurface(s);
    return tex;
}

SDL_Texture *
system_badge_getTexture(SDL_Renderer *renderer, debugger_system_type_t coreSystem, int *outW, int *outH)
{
    if (outW) {
        *outW = 0;
    }
    if (outH) {
        *outH = 0;
    }
    if (!renderer) {
        return NULL;
    }

    if (system_badge_cache.renderer != renderer) {
        system_badge_resetCache(renderer);
    }

    if (coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        if (!system_badge_cache.amigaTex) {
            system_badge_cache.amigaTex =
                system_badge_loadTexture(renderer, "assets/amiga.png", &system_badge_cache.amigaW, &system_badge_cache.amigaH);
        }
        if (outW) {
            *outW = system_badge_cache.amigaW;
        }
        if (outH) {
            *outH = system_badge_cache.amigaH;
        }
        return system_badge_cache.amigaTex;
    }

    if (!system_badge_cache.neogeoTex) {
        system_badge_cache.neogeoTex =
            system_badge_loadTexture(renderer, "assets/neogeo.png", &system_badge_cache.neogeoW, &system_badge_cache.neogeoH);
    }
    if (outW) {
        *outW = system_badge_cache.neogeoW;
    }
    if (outH) {
        *outH = system_badge_cache.neogeoH;
    }
    return system_badge_cache.neogeoTex;
}

