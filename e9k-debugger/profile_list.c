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
#include "profile_hotspot.h"
#include "profile_empty.h"
#include "debugger.h"
#include "e9ui.h"

typedef enum profile_list_role {
    PROFILE_LIST_ROLE_ENTRIES = 1,
} profile_list_role_t;

typedef struct profile_list_meta {
    profile_list_role_t role;
} profile_list_meta_t;


typedef struct {
    unsigned int pc;
    unsigned long long samples;
    char location[ANALYSE_LOCATION_TEXT_CAP];
} profile_aggregate_entry_t;

typedef struct {
    char *text;
} profile_empty_state_t;

typedef struct {
    int dirty;
} profile_list_state_t;

static profile_list_state_t *s_profile_list_state = NULL;

static void profile_list_rebuild(profile_list_state_t *st, e9ui_context_t *ctx, e9ui_component_t *entries);

static void profile_list_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds);
static void profile_list_render(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_list_dtor(e9ui_component_t *self, e9ui_context_t *ctx);

static int  profile_empty_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW);
static void profile_empty_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds);
static void profile_empty_render(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_empty_dtor(e9ui_component_t *self, e9ui_context_t *ctx);

void
profile_list_freeChildMeta(e9ui_component_t *self)
{
    if (!self) return;

    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        if (p->meta) {
            alloc_free(p->meta);
            p->meta = NULL;
        }
    }
}

static e9ui_component_t*
profile_list_findEntries(e9ui_component_t *self)
{
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        profile_list_meta_t *meta = (profile_list_meta_t*)p->meta;
        if (meta && meta->role == PROFILE_LIST_ROLE_ENTRIES) {
            return p->child;
        }
    }
    return NULL;
}


static int
profile_list_aggregateCompare(const void *a, const void *b)
{
    const profile_aggregate_entry_t *ea = (const profile_aggregate_entry_t*)a;
    const profile_aggregate_entry_t *eb = (const profile_aggregate_entry_t*)b;
    if (ea->samples > eb->samples) return -1;
    if (ea->samples < eb->samples) return 1;
    return strcmp(ea->location, eb->location);
}

void
profile_list_notifyUpdate(void)
{
    if (s_profile_list_state) {
        s_profile_list_state->dirty = 1;
    }
}

e9ui_component_t *
profile_list_makeComponent(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) return NULL;

    profile_list_state_t *st = (profile_list_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }

    e9ui_component_t *entries = e9ui_stack_makeVertical();
    if (!entries) {
        alloc_free(st);
        alloc_free(comp);
        return NULL;
    }

    profile_list_meta_t *meta = (profile_list_meta_t*)alloc_alloc(sizeof(*meta));
    if (!meta) {
        // best-effort cleanup (entries not yet attached; framework won't see it)
        // If you have e9ui_destroy(), call it; otherwise free raw.
        alloc_free(entries->state);
        alloc_free(entries);
        alloc_free(st);
        alloc_free(comp);
        return NULL;
    }
    meta->role = PROFILE_LIST_ROLE_ENTRIES;

    // comp owns entries (and meta) via child list
    e9ui_child_add(comp, entries, meta);

    st->dirty = 1;
    s_profile_list_state = st;

    comp->name = "profile_list";
    comp->state = st;
    comp->layout = profile_list_layout;
    comp->render = profile_list_render;
    comp->dtor   = profile_list_dtor;

    return comp;
}

static void
profile_list_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    profile_list_state_t *st = (profile_list_state_t*)self->state;
    if (!st) return;

    e9ui_component_t *entries = profile_list_findEntries(self);
    if (!entries) return;

    profile_list_rebuild(st, ctx, entries);

    if (entries->layout) {
        entries->layout(entries, ctx, bounds);
    }
}

static void
profile_list_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) return;

    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &r);

    e9ui_component_t *entries = profile_list_findEntries(self);
    if (entries && entries->render) {
        entries->render(entries, ctx);
    }
}

static void
profile_list_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) return;

    // free per-child meta (entries role tag)
    profile_list_freeChildMeta(self);

    // clear global state pointer
    if (s_profile_list_state == (profile_list_state_t*)self->state) {
        s_profile_list_state = NULL;
    }
}

static void
profile_list_rebuild(profile_list_state_t *st, e9ui_context_t *ctx, e9ui_component_t *entries)
{
    if (!st || !entries || !st->dirty) return;

    st->dirty = 0;

    // Clear old entries (stack expected to remove/destroy its children)
    e9ui_stack_removeAll(entries, ctx);

    analyse_profile_sample_entry *samples = NULL;
    size_t count = 0;

    if (!analyse_profileSnapshot(&samples, &count) || count == 0) {
        e9ui_component_t *empty = profile_empty_make();
        if (empty) {
            e9ui_stack_addFlex(entries, empty);
        }
        analyse_profileSnapshotFree(samples);
        return;
    }

    analyse_populateSampleLocations(samples, count);

    profile_aggregate_entry_t *aggregates = NULL;
    size_t aggregateCount = 0;

    if (count > 0) {
        aggregates = (profile_aggregate_entry_t*)alloc_calloc(count, sizeof(*aggregates));
    }

    if (aggregates) {
        for (size_t i = 0; i < count; ++i) {
            int matched = -1;
            for (size_t j = 0; j < aggregateCount; ++j) {
                if (strcmp(aggregates[j].location, samples[i].location) == 0) {
                    matched = (int)j;
                    break;
                }
            }
            if (matched >= 0) {
                aggregates[matched].samples += samples[i].samples;
            } else {
                aggregates[aggregateCount].pc = samples[i].pc;
                aggregates[aggregateCount].samples = samples[i].samples;
                strncpy(aggregates[aggregateCount].location, samples[i].location, ANALYSE_LOCATION_TEXT_CAP - 1);
                aggregates[aggregateCount].location[ANALYSE_LOCATION_TEXT_CAP - 1] = '\0';
                aggregateCount++;
            }
        }

        qsort(aggregates, aggregateCount, sizeof(*aggregates), profile_list_aggregateCompare);

        size_t limit = aggregateCount > PROFILE_LIST_MAX_ENTRIES ? PROFILE_LIST_MAX_ENTRIES : aggregateCount;
        for (size_t i = 0; i < limit; ++i) {
            e9ui_component_t *entry =
                profile_hotspot_make(aggregates[i].pc, aggregates[i].samples, aggregates[i].location);
            if (entry) {
                e9ui_stack_addFixed(entries, entry);
            }
        }
        alloc_free(aggregates);
    } else {
        size_t limit = count > PROFILE_LIST_MAX_ENTRIES ? PROFILE_LIST_MAX_ENTRIES : count;
        for (size_t i = 0; i < limit; ++i) {
            e9ui_component_t *entry = profile_hotspot_make(samples[i].pc, samples[i].samples, samples[i].location);
            if (entry) {
                e9ui_stack_addFixed(entries, entry);
            }
        }
    }

    analyse_profileSnapshotFree(samples);
}



static int
profile_empty_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
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
profile_empty_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
profile_empty_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) return;

    profile_empty_state_t *st = (profile_empty_state_t*)self->state;

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font) return;

    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 18, 18, 24, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);

    SDL_Color textColor = { 200, 200, 200, 255 };
    int tw = 0, th = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->text ? st->text : "Profiling", textColor, &tw, &th);
    if (tex) {
        int padX = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_X);
        int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
        SDL_Rect rect = { self->bounds.x + padX, self->bounds.y + padY, tw, th };
        SDL_RenderCopy(ctx->renderer, tex, NULL, &rect);
    }
}

// dtor: free only non-component resources; state struct itself freed by framework.
static void
profile_empty_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) return;

    profile_empty_state_t *st = (profile_empty_state_t*)self->state;
    if (st->text) {
        alloc_free(st->text);
        st->text = NULL;
    }
}

e9ui_component_t *
profile_empty_make(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) return NULL;

    profile_empty_state_t *st = (profile_empty_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }

    st->text = alloc_strdup("No profiling samples");
    if (!st->text) {
        alloc_free(st);
        alloc_free(comp);
        return NULL;
    }

    comp->name = "profile_empty";
    comp->state = st;
    comp->preferredHeight = profile_empty_preferredHeight;
    comp->layout = profile_empty_layout;
    comp->render = profile_empty_render;
    comp->dtor = profile_empty_dtor;

    return comp;
}
