/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include "transition.h"
#include "alloc.h"
#include "debugger.h"

typedef struct transition_explode_item {
    e9ui_component_t *comp;
    e9ui_rect_t target;
    e9ui_rect_t start;
    e9ui_rect_t end;
} transition_explode_item_t;

typedef struct transition_explode_bounds {
    e9ui_component_t *comp;
    e9ui_rect_t bounds;
} transition_explode_bounds_t;

static void
transition_explode_collectBounds(e9ui_component_t *comp, transition_explode_bounds_t **items,
                                 size_t *count, size_t *cap)
{
    if (!comp || !items || !count || !cap) {
        return;
    }
    if (*count >= *cap) {
        size_t next = *cap ? (*cap * 2) : 64;
        transition_explode_bounds_t *grown =
            (transition_explode_bounds_t *)alloc_realloc(*items, next * sizeof(*grown));
        if (!grown) {
            return;
        }
        *items = grown;
        *cap = next;
    }
    (*items)[*count].comp = comp;
    (*items)[*count].bounds = comp->bounds;
    (*count)++;

    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (it->child) {
            transition_explode_collectBounds(it->child, items, count, cap);
        }
    }
}

static void
transition_explode_restoreBounds(transition_explode_bounds_t *items, size_t count)
{
    if (!items) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        items[i].comp->bounds = items[i].bounds;
    }
}

static void
transition_explode_renderToTexture(e9ui_component_t *comp, SDL_Texture *target,
                                   e9ui_component_t *fullscreenComp, int w, int h)
{
    if (!target) {
        return;
    }
    SDL_Texture *prev = SDL_GetRenderTarget(e9ui->ctx.renderer);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(e9ui->ctx.renderer, target);
    SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, 255);
    SDL_RenderClear(e9ui->ctx.renderer);
    if (!comp) {
        SDL_SetRenderTarget(e9ui->ctx.renderer, prev);
        return;
    }
    e9ui_component_t *prevRoot = e9ui->root;
    e9ui_component_t *prevFullscreen = e9ui->fullscreen;
    e9ui->root = comp;
    e9ui->fullscreen = fullscreenComp;
    if (comp->layout) {
        e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
        comp->layout(comp, &e9ui->ctx, full);
    }
    e9ui_renderFrameNoLayoutNoPresent();
    e9ui->root = prevRoot;
    e9ui->fullscreen = prevFullscreen;
    SDL_SetRenderTarget(e9ui->ctx.renderer, prev);
}

static void
transition_explode_renderToTextureNoLayout(e9ui_component_t *comp, SDL_Texture *target,
                                           e9ui_component_t *fullscreenComp)
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
    e9ui->root = comp;
    e9ui->fullscreen = fullscreenComp;
    e9ui->glCompositeCapture = 1;
    e9ui_renderFrameNoLayoutNoPresentNoClear();
    e9ui->glCompositeCapture = 0;
    e9ui->root = prevRoot;
    e9ui->fullscreen = prevFullscreen;
    SDL_SetRenderTarget(e9ui->ctx.renderer, prev);
}

static void
transition_explode_collectComponents(e9ui_component_t *comp, transition_explode_item_t **items,
                                     size_t *count, size_t *cap)
{
    if (!comp || !items || !count || !cap) {
        return;
    }
    if (*count >= *cap) {
        size_t next = *cap ? (*cap * 2) : 64;
        transition_explode_item_t *grown =
            (transition_explode_item_t *)alloc_realloc(*items, next * sizeof(*grown));
        if (!grown) {
            return;
        }
        *items = grown;
        *cap = next;
    }
    (*items)[*count].comp = comp;
    (*items)[*count].target = comp->bounds;
    (*items)[*count].start = comp->bounds;
    (*count)++;

    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (it->child) {
            transition_explode_collectComponents(it->child, items, count, cap);
        }
    }
}

void
transition_explode_run(e9ui_component_t *from, e9ui_component_t *to, int w, int h)
{
    if (!e9ui->ctx.renderer || (!from && !to)) {
        return;
    }

    e9ui_component_t *prevRoot = e9ui->root;
    e9ui_component_t *prevFullscreen = e9ui->fullscreen;
    SDL_Texture *prevTarget = SDL_GetRenderTarget(e9ui->ctx.renderer);

    SDL_Texture *fromTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, w, h);
    if (!fromTex) {
        e9ui->transition.inTransition = 0;
        return;
    }
    e9ui_component_t *fromFullscreen = (from && from == prevFullscreen) ? from : NULL;
    transition_explode_renderToTexture(from, fromTex, fromFullscreen, w, h);

    SDL_Texture *toTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, w, h);
    if (!toTex) {
        SDL_DestroyTexture(fromTex);
        e9ui->transition.inTransition = 0;
        return;
    }

    transition_explode_item_t *items = NULL;
    size_t count = 0;
    size_t cap = 0;
    if (to) {
        e9ui->root = to;
        e9ui->fullscreen = NULL;
        if (to->layout) {
            e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
            to->layout(to, &e9ui->ctx, full);
        }
        transition_explode_collectComponents(to, &items, &count, &cap);
    }
    e9ui->root = prevRoot;
    e9ui->fullscreen = prevFullscreen;

    int centerX = w / 2;
    int centerY = h / 2;
    for (size_t i = 0; i < count; ++i) {
        transition_explode_item_t *item = &items[i];
        item->start.x = centerX - item->target.w / 2;
        item->start.y = centerY - item->target.h / 2;
        item->start.w = item->target.w;
        item->start.h = item->target.h;
        item->end = item->target;
        item->comp->bounds = item->start;
    }

    SDL_SetTextureBlendMode(fromTex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(toTex, SDL_BLENDMODE_BLEND);
    const int frames = 20;
    const double frameMs = 1000.0 / 60.0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();
    SDL_Rect dst = { 0, 0, w, h };
    for (int f = 0; f < frames; ++f) {
        SDL_PumpEvents();
        float t = (frames > 1) ? (float)f / (float)(frames - 1) : 1.0f;
        SDL_SetRenderTarget(e9ui->ctx.renderer, prevTarget);
        SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, 255);
        SDL_RenderClear(e9ui->ctx.renderer);
        Uint8 fromAlpha = (Uint8)(255.0f * (1.0f - t));
        SDL_SetTextureAlphaMod(fromTex, fromAlpha);
        SDL_RenderCopy(e9ui->ctx.renderer, fromTex, NULL, &dst);

        if (to && items) {
            for (size_t i = 0; i < count; ++i) {
                transition_explode_item_t *item = &items[i];
                int x = (int)((float)item->start.x + (float)(item->end.x - item->start.x) * t);
                int y = (int)((float)item->start.y + (float)(item->end.y - item->start.y) * t);
                item->comp->bounds.x = x;
                item->comp->bounds.y = y;
                item->comp->bounds.w = item->target.w;
                item->comp->bounds.h = item->target.h;
            }
            e9ui_component_t *toFullscreen = (to && to != prevRoot) ? to : NULL;
            transition_explode_renderToTextureNoLayout(to, toTex, toFullscreen);
            Uint8 toAlpha = (Uint8)(255.0f * t);
            SDL_SetTextureAlphaMod(toTex, toAlpha);
            SDL_RenderCopy(e9ui->ctx.renderer, toTex, NULL, &dst);
        }

        SDL_RenderPresent(e9ui->ctx.renderer);
        uint64_t now = SDL_GetPerformanceCounter();
        double elapsedMs = (double)(now - last) * 1000.0 / (double)freq;
        if (elapsedMs < frameMs) {
            SDL_Delay((Uint32)(frameMs - elapsedMs));
        }
        last = SDL_GetPerformanceCounter();
    }

    for (size_t i = 0; i < count; ++i) {
        items[i].comp->bounds = items[i].target;
    }
    SDL_DestroyTexture(fromTex);
    SDL_DestroyTexture(toTex);
    alloc_free(items);
    e9ui->transition.inTransition = -100;
}

void
transition_explode_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h)
{
    if (!e9ui->ctx.renderer || (!from && !to)) {
        return;
    }

    e9ui_component_t *prevRoot = e9ui->root;
    e9ui_component_t *prevFullscreen = e9ui->fullscreen;
    SDL_Texture *prevTarget = SDL_GetRenderTarget(e9ui->ctx.renderer);

    transition_explode_item_t *items = NULL;
    size_t count = 0;
    size_t cap = 0;
    if (from) {
        e9ui->root = from;
        e9ui->fullscreen = NULL;
        if (from->layout) {
            e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
            from->layout(from, &e9ui->ctx, full);
        }
        transition_explode_collectComponents(from, &items, &count, &cap);
    }
    e9ui->root = prevRoot;
    e9ui->fullscreen = prevFullscreen;

    int centerX = w / 2;
    int centerY = h / 2;
    for (size_t i = 0; i < count; ++i) {
        transition_explode_item_t *item = &items[i];
        item->start = item->target;
        item->end.x = centerX - item->target.w / 2;
        item->end.y = centerY - item->target.h / 2;
        item->end.w = item->target.w;
        item->end.h = item->target.h;
        item->comp->bounds = item->start;
    }

    SDL_Texture *fromTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, w, h);
    if (!fromTex) {
        alloc_free(items);
        e9ui->transition.inTransition = 0;
        return;
    }

    SDL_Texture *toTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, w, h);
    if (!toTex) {
        SDL_DestroyTexture(fromTex);
        alloc_free(items);
        e9ui->transition.inTransition = 0;
        return;
    }

    transition_explode_bounds_t *toBounds = NULL;
    size_t toCount = 0;
    size_t toCap = 0;
    if (to) {
        transition_explode_collectBounds(to, &toBounds, &toCount, &toCap);
    }

    e9ui_component_t *toFullscreen = (to && to != prevRoot) ? to : NULL;
    transition_explode_renderToTexture(to, toTex, toFullscreen, w, h);
    transition_explode_restoreBounds(toBounds, toCount);
    alloc_free(toBounds);

    SDL_SetTextureBlendMode(toTex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(fromTex, SDL_BLENDMODE_BLEND);
    const int frames = 20;
    const double frameMs = 1000.0 / 60.0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();
    SDL_Rect dst = { 0, 0, w, h };
    for (int f = 0; f < frames; ++f) {
        SDL_PumpEvents();
        float t = (frames > 1) ? (float)f / (float)(frames - 1) : 1.0f;
        SDL_SetRenderTarget(e9ui->ctx.renderer, prevTarget);
        SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, 255);
        SDL_RenderClear(e9ui->ctx.renderer);
        if (from && items) {
            for (size_t i = 0; i < count; ++i) {
                transition_explode_item_t *item = &items[i];
                int x = (int)((float)item->start.x + (float)(item->end.x - item->start.x) * t);
                int y = (int)((float)item->start.y + (float)(item->end.y - item->start.y) * t);
                item->comp->bounds.x = x;
                item->comp->bounds.y = y;
                item->comp->bounds.w = item->target.w;
                item->comp->bounds.h = item->target.h;
            }
        }
        e9ui_component_t *fromFullscreen = (from && from == prevFullscreen) ? from : NULL;
        transition_explode_renderToTextureNoLayout(from, fromTex, fromFullscreen);
        Uint8 fromAlpha = (Uint8)(255.0f * (1.0f - t));
        SDL_SetTextureAlphaMod(fromTex, fromAlpha);
        SDL_RenderCopy(e9ui->ctx.renderer, fromTex, NULL, &dst);
        Uint8 toAlpha = (Uint8)(255.0f * t);
        SDL_SetTextureAlphaMod(toTex, toAlpha);
        SDL_RenderCopy(e9ui->ctx.renderer, toTex, NULL, &dst);

        SDL_RenderPresent(e9ui->ctx.renderer);
        uint64_t now = SDL_GetPerformanceCounter();
        double elapsedMs = (double)(now - last) * 1000.0 / (double)freq;
        if (elapsedMs < frameMs) {
            SDL_Delay((Uint32)(frameMs - elapsedMs));
        }
        last = SDL_GetPerformanceCounter();
    }

    for (size_t i = 0; i < count; ++i) {
        items[i].comp->bounds = items[i].target;
    }
    SDL_DestroyTexture(fromTex);
    SDL_DestroyTexture(toTex);
    alloc_free(items);
    e9ui->transition.inTransition = -100;
}
