/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"

typedef struct e9ui_center_state {
    int width_px;
    int height_px;
} e9ui_center_state_t;

static int
e9ui_center_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_center_state_t *st = (e9ui_center_state_t*)self->state;
    if (st && st->height_px > 0) {
        return e9ui_scale_px(ctx, st->height_px);
    }
    int widthHint = availW;
    if (st && st->width_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->width_px);
        if (scaled < widthHint) {
            widthHint = scaled;
        }
    }
    e9ui_component_t *child = NULL;
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    if (e9ui_child_interateNext(p)) {
        child = p->child;
    }
    if (child && child->preferredHeight) {
        return child->preferredHeight(child, ctx, widthHint);
    }
    return 0;
}

static void
e9ui_center_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_component_t *child = NULL;
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    if (!e9ui_child_interateNext(p)) {
        return;
    }
    child = p->child;
    if (!child || !child->layout) {
        return;
    }
    e9ui_center_state_t *st = (e9ui_center_state_t*)self->state;
    int childW = bounds.w;
    int childH = bounds.h;
    if (st && st->width_px > 0) {
        int w = e9ui_scale_px(ctx, st->width_px);
        if (w < childW) {
            childW = w;
        }
    }
    if (st && st->height_px > 0) {
        int h = e9ui_scale_px(ctx, st->height_px);
        if (h < childH) {
            childH = h;
        }
    } else if (child->preferredHeight) {
        int prefH = child->preferredHeight(child, ctx, childW);
        if (prefH < childH) {
            childH = prefH;
        }
    }
    if (childW < 0) childW = 0;
    if (childH < 0) childH = 0;
    int x = bounds.x + (bounds.w - childW) / 2;
    int y = bounds.y + (bounds.h - childH) / 2;
    e9ui_rect_t r = (e9ui_rect_t){ x, y, childW, childH };
    child->layout(child, ctx, r);
}

static void
e9ui_center_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (ctx && ctx->renderer && debugger.inTransition <= 0) {
        SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(ctx->renderer, &bg);
    }
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    if (e9ui_child_interateNext(p)) {
        e9ui_component_t *child = p->child;
        if (child && child->render) {
            child->render(child, ctx);
        }
    }
}

e9ui_component_t *
e9ui_center_make(e9ui_component_t *child)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_center_state_t *st = (e9ui_center_state_t*)alloc_calloc(1, sizeof(*st));
    st->width_px = 0;
    st->height_px = 0;
    c->name = "e9ui_center";
    c->state = st;
    c->preferredHeight = e9ui_center_preferredHeight;
    c->layout = e9ui_center_layout;
    c->render = e9ui_center_render;
    if (child) {
        e9ui_child_add(c, child, 0);
    }
    return c;
}

void
e9ui_center_setSize(e9ui_component_t *center, int width_px, int height_px)
{
    if (!center || !center->state) {
        return;
    }
    e9ui_center_state_t *st = (e9ui_center_state_t*)center->state;
    st->width_px = width_px;
    st->height_px = height_px;
}
