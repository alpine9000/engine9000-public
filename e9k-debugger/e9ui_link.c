/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui_link.h"
#include "debugger.h"
#include "e9ui_context.h"
#include "e9ui_text_cache.h"
#include "e9ui_scale.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

typedef struct e9ui_link_state {
    char               *text;
    e9ui_link_cb        cb;
    void               *user;
    int                 hover;
} e9ui_link_state_t;

static SDL_Cursor *s_cursor_hand = NULL;
static SDL_Cursor *s_cursor_arrow = NULL;

static void
e9ui_link_ensureCursors(void)
{
    if (!s_cursor_hand) {
        s_cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    }
    if (!s_cursor_arrow) {
        s_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    }
}

static int
e9ui_link_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int padY = e9ui_scale_px(ctx, 2);
    return lh + padY * 2;
}

static void
e9ui_link_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
e9ui_link_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }
    e9ui_link_state_t *st = (e9ui_link_state_t*)self->state;
    if (!st || !st->text || !*st->text) {
        return;
    }

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font) {
        return;
    }
    SDL_Color normal = { 170, 190, 230, 255 };
    SDL_Color hover = { 205, 225, 255, 255 };
    SDL_Color disabled = { 160, 160, 160, 255 };
    SDL_Color color = self->disabled ? disabled : (st->hover ? hover : normal);
    int tw = 0, th = 0;
    SDL_Texture *tex = e9ui_text_cache_getUTF8(ctx->renderer, font, st->text, color, &tw, &th);
    if (tex) {
        int textY = self->bounds.y + (self->bounds.h - th) / 2;
        if (textY < self->bounds.y) {
            textY = self->bounds.y;
        }
        SDL_Rect rect = { self->bounds.x, textY, tw, th };
        SDL_RenderCopy(ctx->renderer, tex, NULL, &rect);
    }
}

static void
e9ui_link_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  if (!self) {
    return;
  }
  e9ui_link_state_t *st = (e9ui_link_state_t*)self->state;
  if (st) {
    if (st->text) {
      alloc_free(st->text);
    }
  }
}

static void
e9ui_link_onHover(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_link_state_t *st = (e9ui_link_state_t*)self->state;
    if (!st) {
        return;
    }
    if (self->disabled) {
        return;
    }
    st->hover = 1;
    e9ui_link_ensureCursors();
    if (s_cursor_hand) {
        SDL_SetCursor(s_cursor_hand);
        if (ctx) {
            ctx->cursorOverride = 1;
        }
    }
    (void)mouse_ev;
}

static void
e9ui_link_onMouseMove(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_link_state_t *st = (e9ui_link_state_t*)self->state;
    if (!st) {
        return;
    }
    if (self->disabled) {
        return;
    }
    e9ui_link_ensureCursors();
    if (ctx && s_cursor_hand) {
        SDL_SetCursor(s_cursor_hand);
        ctx->cursorOverride = 1;
    }
    (void)mouse_ev;
}

static void
e9ui_link_onLeave(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_link_state_t *st = (e9ui_link_state_t*)self->state;
    if (!st) {
        return;
    }
    st->hover = 0;
    e9ui_link_ensureCursors();
    if (s_cursor_arrow) {
        SDL_SetCursor(s_cursor_arrow);
    }
    (void)ctx;
    (void)mouse_ev;
}

static void
e9ui_link_onClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_link_state_t *st = (e9ui_link_state_t*)self->state;
    if (!st) {
        return;
    }
    if (self->disabled || !st->cb) {
        return;
    }
    st->cb(ctx, st->user);
    (void)mouse_ev;
}

e9ui_component_t *
e9ui_link_make(const char *text, e9ui_link_cb cb, void *user)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) {
        return NULL;
    }
    e9ui_link_state_t *st = (e9ui_link_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }
    if (text && *text) {
        st->text = alloc_strdup(text);
    }
    st->cb = cb;
    st->user = user;
    comp->name = "e9ui_link";
    comp->state = st;
    comp->preferredHeight = e9ui_link_preferredHeight;
    comp->layout = e9ui_link_layout;
    comp->render = e9ui_link_render;
    comp->onHover = e9ui_link_onHover;
    comp->onMouseMove = e9ui_link_onMouseMove;
    comp->onLeave = e9ui_link_onLeave;
    comp->onClick = e9ui_link_onClick;
    comp->dtor = e9ui_link_dtor;
    return comp;
}

void
e9ui_link_setText(e9ui_component_t *link, const char *text)
{
    if (!link || !link->state) {
        return;
    }
    e9ui_link_state_t *st = (e9ui_link_state_t*)link->state;
    if (st->text) {
        alloc_free(st->text);
        st->text = NULL;
    }
    if (text && *text) {
        st->text = alloc_strdup(text);
    }
}
