/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "debugger.h"

typedef struct e9ui_scroll_state {
    e9ui_component_t *child;
    int scrollY;
    int contentH;
    int contentHeightPx;
    int lineHeight;
} e9ui_scroll_state_t;

static int
scroll_measureLineHeight(e9ui_context_t *ctx)
{
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int lineHeight = font ? TTF_FontHeight(font) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    return lineHeight;
}

static void
scroll_clamp(e9ui_scroll_state_t *st, int viewH)
{
    if (!st) {
        return;
    }
    int maxScroll = st->contentH - viewH;
    if (maxScroll < 0) {
        maxScroll = 0;
    }
    if (st->scrollY < 0) {
        st->scrollY = 0;
    }
    if (st->scrollY > maxScroll) {
        st->scrollY = maxScroll;
    }
}

static int
scroll_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (st && st->contentHeightPx > 0) {
        return st->contentHeightPx;
    }
    if (!st || !st->child || !st->child->preferredHeight) {
        return 0;
    }
    return st->child->preferredHeight(st->child, ctx, availW);
}

static void
scroll_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (!st || !st->child || !st->child->layout) {
        return;
    }
    int contentH = bounds.h;
    if (st->contentHeightPx > 0) {
        contentH = st->contentHeightPx;
    } else if (st->child->preferredHeight) {
        contentH = st->child->preferredHeight(st->child, ctx, bounds.w);
    }
    st->contentH = contentH;
    st->lineHeight = scroll_measureLineHeight(ctx);
    scroll_clamp(st, bounds.h);
    e9ui_rect_t childBounds = { bounds.x, bounds.y - st->scrollY, bounds.w, st->contentH };
    st->child->layout(st->child, ctx, childBounds);
}

static void
scroll_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (!st || !st->child || !st->child->render) {
        return;
    }
    SDL_Rect prev;
    SDL_bool clip_enabled = SDL_RenderIsClipEnabled(ctx->renderer);
    SDL_RenderGetClipRect(ctx->renderer, &prev);
    SDL_Rect clip = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_RenderSetClipRect(ctx->renderer, &clip);
    st->child->render(st->child, ctx);
    if (clip_enabled) {
        SDL_RenderSetClipRect(ctx->renderer, &prev);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static int
scroll_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    if (!st) {
        return 0;
    }
    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = ctx->mouseX;
        int my = ctx->mouseY;
        if (mx >= self->bounds.x && mx < self->bounds.x + self->bounds.w &&
            my >= self->bounds.y && my < self->bounds.y + self->bounds.h) {
            int wheelY = ev->wheel.y;
            if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wheelY = -wheelY;
            }
            if (wheelY != 0) {
                const int linesPerTick = 1;
                int delta = wheelY * linesPerTick;
                int step = st->lineHeight > 0 ? st->lineHeight : 16;
                st->scrollY += delta * step;
                scroll_clamp(st, self->bounds.h);
            }
            return 1;
        }
    }
    if (st->child && st->child->handleEvent) {
        return st->child->handleEvent(st->child, ctx, ev);
    }
    return 0;
}

static void
scroll_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)self->state;
    alloc_free(st);
    self->state = NULL;
}

e9ui_component_t *
e9ui_scroll_make(e9ui_component_t *child)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)alloc_calloc(1, sizeof(*st));
    if (!c || !st) {
        alloc_free(c);
        alloc_free(st);
        return NULL;
    }
    st->child = child;
    c->name = "e9ui_scroll";
    c->state = st;
    c->preferredHeight = scroll_preferredHeight;
    c->layout = scroll_layout;
    c->render = scroll_render;
    c->handleEvent = scroll_handleEvent;
    c->dtor = scroll_dtor;
    if (child) {
        e9ui_child_add(c, child, 0);
    }
    return c;
}

void
e9ui_scroll_setContentHeightPx(e9ui_component_t *scroll, int contentHeight_px)
{
    if (!scroll || !scroll->state) {
        return;
    }
    e9ui_scroll_state_t *st = (e9ui_scroll_state_t*)scroll->state;
    st->contentHeightPx = contentHeight_px > 0 ? contentHeight_px : 0;
}
