/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef struct e9ui_image_state {
    SDL_Texture *tex;
    int          texW;
    int          texH;
} e9ui_image_state_t;

static int
e9ui_image_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0; // flexible by default
}

static void
e9ui_image_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
e9ui_image_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    e9ui_image_state_t *st = (e9ui_image_state_t*)self->state;
    if (!ctx || !ctx->renderer || !st || !st->tex) {
        return;
    }
    SDL_Rect dst = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_RenderCopy(ctx->renderer, st->tex, NULL, &dst);
}

e9ui_component_t *
e9ui_image_makeFromTexture(SDL_Texture *tex, int tex_w, int tex_h)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_image_state_t *st = (e9ui_image_state_t*)alloc_calloc(1, sizeof(*st));
    st->tex = tex; st->texW = tex_w; st->texH = tex_h;
    c->name = "e9ui_image";
    c->state = st;
    c->preferredHeight = e9ui_image_preferredHeight;
    c->layout = e9ui_image_layout;
    c->render = e9ui_image_render;
    return c;
}
