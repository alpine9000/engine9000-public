/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "profile_list.h"
#include "analyse.h"
#include "breakpoints.h"
#include "libretro_host.h"
#include "debugger.h"
#include "e9ui.h"

typedef enum profile_hotspot_role {
    PROFILE_HOTSPOT_ROLE_LINK = 1,
} profile_hotspot_role_t;

typedef struct profile_hotspot_meta {
    profile_hotspot_role_t role;
} profile_hotspot_meta_t;

typedef struct {
    unsigned int pc;
    unsigned long long samples;
    char sampleText[32];
    char locationText[ANALYSE_LOCATION_TEXT_CAP];
    SDL_Rect locationRect;
    SDL_Rect sampleRect;
} profile_hotspot_state_t;

static int  profile_hotspot_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW);
static void profile_hotspot_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds);
static void profile_hotspot_render(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_hotspot_dtor(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_hotspot_linkClicked(e9ui_context_t *ctx, void *user);
static int  profile_hotspot_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev);

static e9ui_component_t*
profile_hotspot_find_link(e9ui_component_t *self)
{
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        profile_hotspot_meta_t *meta = (profile_hotspot_meta_t*)p->meta;
        if (meta && meta->role == PROFILE_HOTSPOT_ROLE_LINK) {
            return p->child;
        }
    }
    return NULL;
}

static int
profile_hotspot_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) lh = 16;

    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    return padY * 2 + lh;
}

static void
profile_hotspot_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    profile_hotspot_state_t *st = (profile_hotspot_state_t*)self->state;
    if (!st) return;

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);

    int padX = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_X);
    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    int margin = e9ui_scale_px(ctx, 8);

    int sampleWidth = 0;
    int sampleHeight = font ? TTF_FontHeight(font) : 16;
    if (sampleHeight <= 0) sampleHeight = 16;

    if (font && st->sampleText[0]) {
        TTF_SizeUTF8(font, st->sampleText, &sampleWidth, NULL);
    }

    int locW = 0;
    int locH = sampleHeight;
    if (font && st->locationText[0]) {
        TTF_SizeUTF8(font, st->locationText, &locW, &locH);
    }

    int locationX = bounds.x + padX;
    int textY = bounds.y + padY;

    int sampleX = bounds.x + bounds.w - padX - sampleWidth;
    int minSampleX = locationX + locW + margin;
    if (sampleX < minSampleX) sampleX = minSampleX;

    int maxSampleX = bounds.x + bounds.w - padX - sampleWidth;
    if (sampleX > maxSampleX) sampleX = maxSampleX;

    st->locationRect = (SDL_Rect){ locationX, textY, locW, locH };
    st->sampleRect   = (SDL_Rect){ sampleX,   textY, sampleWidth, sampleHeight };

    e9ui_component_t *link = profile_hotspot_find_link(self);
    if (link && st->locationRect.w > 0 && st->locationRect.h > 0 && link->layout) {
        e9ui_rect_t linkBounds = { st->locationRect.x, st->locationRect.y, st->locationRect.w, st->locationRect.h };
        link->layout(link, ctx, linkBounds);
    }
}

static void
profile_hotspot_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) return;

    profile_hotspot_state_t *st = (profile_hotspot_state_t*)self->state;

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font) return;

    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 18, 18, 24, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);

    SDL_Color primary = { 230, 230, 230, 255 };

    int tw = 0, th = 0;
    SDL_Texture *samplesTex = e9ui_text_cache_getUTF8(ctx->renderer, font, st->sampleText, primary, &tw, &th);
    if (samplesTex) {
        SDL_Rect samplesRect = { st->sampleRect.x, st->sampleRect.y, tw, th };
        SDL_RenderCopy(ctx->renderer, samplesTex, NULL, &samplesRect);
    }

    e9ui_component_t *link = profile_hotspot_find_link(self);
    if (link && st->locationRect.w > 0 && st->locationRect.h > 0 && link->render) {
        link->render(link, ctx);
    }
}

static int
profile_hotspot_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !self->state || !ev) return 0;

    e9ui_component_t *link = profile_hotspot_find_link(self);
    if (!link || !link->handleEvent) return 0;

    return link->handleEvent(link, ctx, ev);
}

static void
profile_hotspot_linkClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    profile_hotspot_state_t *st = (profile_hotspot_state_t*)user;
    if (!st) return;

    uint32_t addr = (uint32_t)st->pc & 0x00ffffffu;
    machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, addr);
    if (bp) {
        if (!bp->enabled) {
            bp->enabled = 1;
            libretro_host_debugAddBreakpoint(addr);
        }
        breakpoints_resolveLocation(bp);
    } else {
        bp = machine_addBreakpoint(&debugger.machine, addr, 1);
        if (bp) {
            libretro_host_debugAddBreakpoint(addr);
            breakpoints_resolveLocation(bp);
        }
    }
    breakpoints_markDirty();
}

static void
profile_hotspot_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) return;
    profile_list_freeChildMeta(self);
}

e9ui_component_t *
profile_hotspot_make(unsigned int pc, unsigned long long samples, const char *location)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) return NULL;

    profile_hotspot_state_t *st = (profile_hotspot_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }

    st->pc = pc;
    st->samples = samples;

    snprintf(st->sampleText, sizeof(st->sampleText), "%llu", samples);

    if (location && *location) {
        snprintf(st->locationText, sizeof(st->locationText), "%s", location);
    } else {
        snprintf(st->locationText, sizeof(st->locationText), "PC: 0x%08X", pc);
    }

    // Create link as a child component; we do NOT keep a cached pointer in state.
    e9ui_component_t *link = e9ui_link_make(st->locationText, profile_hotspot_linkClicked, st);
    if (link) {
        e9ui_setDisableVariable(link, machine_getRunningState(debugger.machine), 1);

        profile_hotspot_meta_t *meta = (profile_hotspot_meta_t*)alloc_alloc(sizeof(*meta));
        if (meta) {
            meta->role = PROFILE_HOTSPOT_ROLE_LINK;
            e9ui_child_add(comp, link, meta);
        } else {
            // If meta allocation fails, we still attach link without meta so it is owned & destroyed.
            // (We won't be able to find it via role though; could alternatively free link here.)
            e9ui_child_add(comp, link, 0);
        }
    }

    comp->name = "profile_hotspot";
    comp->state = st;
    comp->preferredHeight = profile_hotspot_preferredHeight;
    comp->layout = profile_hotspot_layout;
    comp->render = profile_hotspot_render;
    comp->handleEvent = profile_hotspot_handleEvent;
    comp->dtor = profile_hotspot_dtor;

    return comp;
}
