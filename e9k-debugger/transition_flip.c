/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include "transition.h"
#include "debugger.h"


static void
transition_flip_renderToTexture(e9ui_component_t *comp, SDL_Texture *target,
                                e9ui_component_t *fullscreenComp, int w, int h)
{
    if (!target) {
        return;
    }
    SDL_Texture *prev = SDL_GetRenderTarget(e9ui->ctx.renderer);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(e9ui->ctx.renderer, target);
    SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, 255);
    SDL_RenderClear(e9ui->ctx.renderer);
    if (!comp) {
        SDL_SetRenderTarget(e9ui->ctx.renderer, prev);
        return;
    }
    e9ui_component_t *prevRoot = e9ui->root;
    e9ui_component_t *prevFullscreen = e9ui->fullscreen;
    e9ui->fullscreen = fullscreenComp;
    e9ui->root = comp;
    if (comp->layout) {
        e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
        comp->layout(comp, &e9ui->ctx, full);
    }
    e9ui_renderFrameNoLayoutNoPresent();
    e9ui->root = prevRoot;
    e9ui->fullscreen = prevFullscreen;
    SDL_SetRenderTarget(e9ui->ctx.renderer, prev);
}

void
transition_flip_run(e9ui_component_t *from, e9ui_component_t *to, int w, int h)
{
    transition_flip_runTo(from, to, w, h);
}

void
transition_flip_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h)
{
    if (!e9ui->ctx.renderer || (!from && !to)) {
        return;
    }

    SDL_Texture *prevTarget = SDL_GetRenderTarget(e9ui->ctx.renderer);
    SDL_Texture *fromTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_Texture *toTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, w, h);
    if (!fromTex || !toTex) {
        if (fromTex) {
            SDL_DestroyTexture(fromTex);
        }
        if (toTex) {
            SDL_DestroyTexture(toTex);
        }
        e9ui->transition.inTransition = 0;
        return;
    }

    e9ui_component_t *fromFullscreen = (from == e9ui->fullscreen) ? from : NULL;
    e9ui_component_t *toFullscreen = (to && to != e9ui->root) ? to : NULL;
    transition_flip_renderToTexture(from, fromTex, fromFullscreen, w, h);
    transition_flip_renderToTexture(to, toTex, toFullscreen, w, h);

    const int frames = 20;
    const double frameMs = 1000.0 / 60.0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();
    SDL_Rect src = { 0, 0, w, h };
    for (int f = 0; f < frames; ++f) {
        SDL_PumpEvents();
        float t = (frames > 1) ? (float)f / (float)(frames - 1) : 1.0f;
        SDL_SetRenderTarget(e9ui->ctx.renderer, prevTarget);
        SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, 255);
        SDL_RenderClear(e9ui->ctx.renderer);

        SDL_Texture *tex = NULL;
        float scale = 1.0f;
        if (t < 0.5f) {
            tex = fromTex;
            scale = 1.0f - (t / 0.5f);
        } else {
            tex = toTex;
            scale = (t - 0.5f) / 0.5f;
        }
        int width = (int)((float)w * scale);
        if (width < 1) {
            width = 1;
        }
        SDL_Rect dst = { (w - width) / 2, 0, width, h };
        SDL_RenderCopy(e9ui->ctx.renderer, tex, &src, &dst);

        SDL_RenderPresent(e9ui->ctx.renderer);
        uint64_t now = SDL_GetPerformanceCounter();
        double elapsedMs = (double)(now - last) * 1000.0 / (double)freq;
        if (elapsedMs < frameMs) {
            SDL_Delay((Uint32)(frameMs - elapsedMs));
        }
        last = SDL_GetPerformanceCounter();
    }

    SDL_DestroyTexture(fromTex);
    SDL_DestroyTexture(toTex);
    e9ui->transition.inTransition = -100;
}
