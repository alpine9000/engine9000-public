/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "debugger.h"

typedef struct e9ui_separator_state {
    int width;
} e9ui_separator_state_t;

static int
e9ui_separator_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0;
}

static void
e9ui_separator_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
e9ui_separator_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    SDL_Renderer *r = ctx->renderer;
    int x = self->bounds.x;
    int y = self->bounds.y;
    int w = self->bounds.w;
    int h = self->bounds.h;
    if (w <= 0 || h <= 0) {
        return;
    }
    int mid_x = x + w / 2;
    int pad = e9ui_scale_px(ctx, 3);
    if (pad * 2 >= h) {
        pad = 0;
    }
    SDL_SetRenderDrawColor(r, 100, 100, 100, 255);
    SDL_RenderDrawLine(r, mid_x, y + pad, mid_x, y + h - pad - 1);
}

e9ui_component_t *
e9ui_separator_make(int width_px)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    e9ui_separator_state_t *st = (e9ui_separator_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    st->width = (width_px > 0) ? width_px : 9;
    c->name = "e9ui_separator";
    c->state = st;
    c->preferredHeight = e9ui_separator_preferredHeight;
    c->layout = e9ui_separator_layout;
    c->render = e9ui_separator_render;
    return c;
}

void
e9ui_separator_setWidth(e9ui_component_t *comp, int width_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_separator_state_t *st = (e9ui_separator_state_t*)comp->state;
    st->width = (width_px > 0) ? width_px : 9;
}

void
e9ui_separator_measure(e9ui_component_t *comp, e9ui_context_t *ctx, int *out_w, int *out_h)
{
    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }
    if (!comp || !comp->state) {
        return;
    }
    e9ui_separator_state_t *st = (e9ui_separator_state_t*)comp->state;
    int w = st->width > 0 ? st->width : 9;
    if (ctx) {
        w = e9ui_scale_px(ctx, w);
    }
    int pad = ctx ? e9ui_scale_px(ctx, debugger.theme.button.padding) : 0;
    TTF_Font *useFont = debugger.theme.button.font ? debugger.theme.button.font : (ctx ? ctx->font : NULL);
    int lh = useFont ? TTF_FontHeight(useFont) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int h = lh + pad * 2;
    if (out_w) {
        *out_w = w;
    }
    if (out_h) {
        *out_h = h;
    }
}
