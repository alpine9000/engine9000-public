/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef struct e9ui_vspacer_state {
    int height_px;
} e9ui_vspacer_state_t;

static int
e9ui_vspacer_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    e9ui_vspacer_state_t *st = (e9ui_vspacer_state_t*)self->state;
    if (!st) {
        return 0;
    }
    if (st->height_px <= 0) {
        return 0;
    }
    return e9ui_scale_px(ctx, st->height_px);
}

static void
e9ui_vspacer_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
e9ui_vspacer_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)self;
    (void)ctx;
}

e9ui_component_t *
e9ui_vspacer_make(int height_px)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_vspacer_state_t *st = (e9ui_vspacer_state_t*)alloc_calloc(1, sizeof(*st));
    st->height_px = height_px >= 0 ? height_px : 0;
    c->name = "e9ui_vspacer";
    c->state = st;
    c->preferredHeight = e9ui_vspacer_preferredHeight;
    c->layout = e9ui_vspacer_layout;
    c->render = e9ui_vspacer_render;
    return c;
}
