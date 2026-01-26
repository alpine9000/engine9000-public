/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <stdlib.h>
#include "transition.h"
#include "alloc.h"
#include "debugger.h"

static void
transition_doom_renderToTexture(e9ui_component_t *comp, SDL_Texture *target,
                                e9ui_component_t *fullscreenComp, int w, int h)
{
    if (!target) {
        return;
    }
    SDL_Texture *prev = SDL_GetRenderTarget(debugger.ui.ctx.renderer);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(debugger.ui.ctx.renderer, target);
    SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 0, 0, 0, 255);
    SDL_RenderClear(debugger.ui.ctx.renderer);
    if (!comp) {
        SDL_SetRenderTarget(debugger.ui.ctx.renderer, prev);
        return;
    }
    e9ui_component_t *prevRoot = debugger.ui.root;
    e9ui_component_t *prevFullscreen = debugger.ui.fullscreen;
    debugger.ui.fullscreen = fullscreenComp;
    debugger.ui.root = comp;
    if (comp->layout) {
        e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
        comp->layout(comp, &debugger.ui.ctx, full);
    }
    debugger.glCompositeCapture = 1;
    e9ui_renderFrameNoLayoutNoPresent();
    debugger.glCompositeCapture = 0;
    debugger.ui.root = prevRoot;
    debugger.ui.fullscreen = prevFullscreen;
    SDL_SetRenderTarget(debugger.ui.ctx.renderer, prev);
}

void
transition_doom_run(e9ui_component_t *root, int w, int h)
{
    transition_doom_runTo(NULL, root, w, h);
}

void
transition_doom_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h)
{
    if (!debugger.ui.ctx.renderer || (!from && !to)) {
        return;
    }

    SDL_Texture *fromTex = SDL_CreateTexture(debugger.ui.ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_Texture *toTex = SDL_CreateTexture(debugger.ui.ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, w, h);
    if (!fromTex || !toTex) {
        if (fromTex) {
            SDL_DestroyTexture(fromTex);
        }
        if (toTex) {
            SDL_DestroyTexture(toTex);
        }
        debugger.inTransition = 0;
        return;
    }

    e9ui_component_t *fromFullscreen = (from == debugger.ui.fullscreen) ? from : NULL;
    e9ui_component_t *toFullscreen = (to && to != debugger.ui.root) ? to : NULL;
    transition_doom_renderToTexture(from, fromTex, fromFullscreen, w, h);
    transition_doom_renderToTexture(to, toTex, toFullscreen, w, h);

    const int slices = 256;
    const int frames = 40;
    const int maxDelayFrames = 20;
    const double frameMs = 1000.0 / 60.0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();
    int *delayFrames = (int *)alloc_calloc((size_t)slices, sizeof(*delayFrames));
    if (!delayFrames) {
        alloc_free(delayFrames);
        SDL_DestroyTexture(fromTex);
        SDL_DestroyTexture(toTex);
        debugger.inTransition = 0;
        return;
    }

    int maxFrameSpan = (frames - 1) - maxDelayFrames;
    if (maxFrameSpan < 1) {
        maxFrameSpan = 1;
    }
    for (int i = 0; i < slices; ++i) {
        delayFrames[i] = rand() % (maxDelayFrames + 1);
    }
    float v = (h > 0) ? ((float)h / (float)maxFrameSpan) : 0.0f;

    for (int f = 0; f < frames; ++f) {
        SDL_PumpEvents();
        SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 16, 16, 16, 255);
        SDL_RenderClear(debugger.ui.ctx.renderer);
        for (int i = 0; i < slices; ++i) {
            int x0 = (w * i) / slices;
            int x1 = (w * (i + 1)) / slices;
            int sw = x1 - x0;
            int localFrame = f - delayFrames[i];
            float yF = (float)(-h);
            if (localFrame >= 0) {
                yF += v * (float)localFrame;
            }
            int y = (yF > 0.0f) ? 0 : (int)yF;
            SDL_Rect src = { x0, 0, sw, h };
            SDL_Rect dstTop = { x0, y, sw, h };
            SDL_Rect dstBottom = { x0, y + h, sw, h };
            SDL_RenderCopy(debugger.ui.ctx.renderer, toTex, &src, &dstTop);
            SDL_RenderCopy(debugger.ui.ctx.renderer, fromTex, &src, &dstBottom);
        }
        SDL_RenderPresent(debugger.ui.ctx.renderer);
        uint64_t now = SDL_GetPerformanceCounter();
        double elapsedMs = (double)(now - last) * 1000.0 / (double)freq;
        if (elapsedMs < frameMs) {
            SDL_Delay((Uint32)(frameMs - elapsedMs));
        }
        last = SDL_GetPerformanceCounter();
    }

    alloc_free(delayFrames);
    SDL_DestroyTexture(fromTex);
    SDL_DestroyTexture(toTex);
    debugger.inTransition = -100;
}
