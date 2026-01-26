/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef struct e9ui_spacer_state {
    int width;
} e9ui_spacer_state_t;

static int
e9ui_spacer_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW; return 0;
}

static void
e9ui_spacer_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx; self->bounds = bounds;
}

static void
e9ui_spacer_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)self; (void)ctx; /* no-op */
}


e9ui_component_t *
e9ui_spacer_make(int width_px)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_spacer_state_t *st = (e9ui_spacer_state_t*)alloc_calloc(1, sizeof(*st));
    st->width = width_px >= 0 ? width_px : 0;
    c->name = "e9ui_spacer";
    c->state = st;
    c->preferredHeight = e9ui_spacer_preferredHeight;
    c->layout = e9ui_spacer_layout;
    c->render = e9ui_spacer_render;
    return c;
}
