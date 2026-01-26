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
#include <string.h>

#include "debugger.h"


typedef struct e9ui_checkbox_state {
    char      *label;
    int        selected;
    int        leftMargin;
    e9ui_checkbox_cb_t cb;
    void      *user;
} e9ui_checkbox_state_t;

static void
e9ui_checkbox_toggle(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state) {
        return;
    }
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)self->state;
    st->selected = !st->selected;
    if (st->cb) {
        st->cb(self, ctx, st->selected, st->user);
    }
}

int
e9ui_checkbox_getMargin(const e9ui_context_t *ctx)
{
    int base = debugger.theme.checkbox.margin;
    if (base <= 0) {
        base = E9UI_THEME_CHECKBOX_MARGIN;
    }
    int scaled = e9ui_scale_px(ctx, base);
    return scaled > 0 ? scaled : base;
}

int
e9ui_checkbox_getTextGap(const e9ui_context_t *ctx)
{
    int base = debugger.theme.checkbox.textGap;
    if (base <= 0) {
        base = E9UI_THEME_CHECKBOX_TEXT_GAP;
    }
    int scaled = e9ui_scale_px(ctx, base);
    return scaled > 0 ? scaled : base;
}

static int
e9ui_checkbox_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    TTF_Font *font = debugger.theme.text.source;
    if (!font && ctx) {
        font = ctx->font;
    }
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) {
        padY = E9UI_THEME_CHECKBOX_MARGIN;
    }
    return padY + lh + padY;
}

static void
e9ui_checkbox_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
e9ui_checkbox_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)self->state;
    int leftMargin = 0;
    if (st->leftMargin > 0) {
        int scaled = e9ui_scale_px(ctx, st->leftMargin);
        leftMargin = scaled > 0 ? scaled : st->leftMargin;
    }
    const int size = self->bounds.h > 24 ? 24 : (self->bounds.h - 4 > 0 ? self->bounds.h - 4 : 16);
    int gap = e9ui_checkbox_getTextGap(ctx);
    if (gap <= 0) {
        gap = E9UI_THEME_CHECKBOX_TEXT_GAP;
    }
    SDL_Rect box = {
        self->bounds.x + leftMargin,
        self->bounds.y + (self->bounds.h - size) / 2,
        size,
        size
    };
    SDL_SetRenderDrawColor(ctx->renderer, 36, 36, 40, 255);
    SDL_RenderFillRect(ctx->renderer, &box);
    SDL_SetRenderDrawColor(ctx->renderer, 150, 150, 170, 255);
    SDL_RenderDrawRect(ctx->renderer, &box);
    if (st->selected) {
        SDL_SetRenderDrawColor(ctx->renderer, 120, 220, 120, 255);
        SDL_Rect inner = { box.x + 3, box.y + 3, box.w - 6, box.h - 6 };
        SDL_RenderFillRect(ctx->renderer, &inner);
    }
    if (st->label && st->label[0]) {
        TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
        if (font) {
            SDL_Color col = {220, 220, 220, 255};
            int tw = 0, th = 0;
            SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->label, col, &tw, &th);
            if (tex) {
                SDL_Rect tr = {
                    box.x + box.w + gap,
                    self->bounds.y + (self->bounds.h - th) / 2,
                    tw,
                    th
                };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &tr);
            }
        }
    }
}

static void
e9ui_checkbox_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  if (!self) {
    return;
  }
  e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)self->state;
  if (st) {
    alloc_free(st->label);
  }
}

static e9ui_checkbox_state_t *
e9ui_checkbox_stateCreate(const char *label, int selected, e9ui_checkbox_cb_t cb, void *user)
{
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        return NULL;
    }
    if (label && *label) {
        st->label = alloc_strdup(label);
        if (!st->label) {
            alloc_free(st);
            return NULL;
        }
    }
    st->selected = selected ? 1 : 0;
    st->cb = cb;
    st->user = user;
    return st;
}

static void
e9ui_checkbox_onClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)mouse_ev;
    e9ui_checkbox_toggle(self, ctx);
}

static void
e9ui_checkbox_invokeCallback(e9ui_component_t *self, e9ui_context_t *ctx)
{
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)self->state;
    if (st && st->cb) {
        st->cb(self, ctx, st->selected, st->user);
    }
}

static void
e9ui_checkbox_setSelectedInternal(e9ui_component_t *self, int selected)
{
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)self->state;
    if (!st) {
        return;
    }
    st->selected = selected ? 1 : 0;
}

e9ui_component_t *
e9ui_checkbox_make(const char *label, int selected, e9ui_checkbox_cb_t cb, void *user)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) {
        return NULL;
    }
    e9ui_checkbox_state_t *st = e9ui_checkbox_stateCreate(label, selected, cb, user);
    if (!st) {
        alloc_free(comp);
        return NULL;
    }
    comp->name = "e9ui_checkbox";
    comp->state = st;
    comp->preferredHeight = e9ui_checkbox_preferredHeight;
    comp->layout = e9ui_checkbox_layout;
    comp->render = e9ui_checkbox_render;
    comp->onClick = e9ui_checkbox_onClick;
    comp->dtor = e9ui_checkbox_dtor;
    return comp;
}

void
e9ui_checkbox_setLeftMargin(e9ui_component_t *checkbox, int margin)
{
    if (!checkbox || !checkbox->state) {
        return;
    }
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)checkbox->state;
    st->leftMargin = margin > 0 ? margin : 0;
}

int
e9ui_checkbox_getLeftMargin(const e9ui_component_t *checkbox, const e9ui_context_t *ctx)
{
    if (!checkbox || !checkbox->state) {
        return 0;
    }
    const e9ui_checkbox_state_t *st = (const e9ui_checkbox_state_t*)checkbox->state;
    if (st->leftMargin <= 0) {
        return 0;
    }
    int scaled = e9ui_scale_px(ctx, st->leftMargin);
    return scaled > 0 ? scaled : st->leftMargin;
}

void
e9ui_checkbox_setSelected(e9ui_component_t *checkbox, int selected, e9ui_context_t *ctx)
{
    if (!checkbox || !checkbox->state) {
        return;
    }
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)checkbox->state;
    if (st->selected == (selected ? 1 : 0)) {
        return;
    }
    e9ui_checkbox_setSelectedInternal(checkbox, selected);
    e9ui_checkbox_invokeCallback(checkbox, ctx);
}

int
e9ui_checkbox_isSelected(e9ui_component_t *checkbox)
{
    if (!checkbox || !checkbox->state) {
        return 0;
    }
    e9ui_checkbox_state_t *st = (e9ui_checkbox_state_t*)checkbox->state;
    return st->selected;
}
