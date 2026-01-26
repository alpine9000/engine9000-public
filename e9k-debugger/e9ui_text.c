/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include <SDL_ttf.h>
#include <string.h>
#include <limits.h>

#include "e9ui.h"
#include "debugger.h"
#include "file.h"
#include "alloc.h"
#include "debug.h"


typedef struct e9ui_text_state {
    char *text;
    int fontSize_px;
    int fontStyle;
    int fontSizeScaled;
    int fontStyleScaled;
    SDL_Color color;
    TTF_Font *font;
} e9ui_text_state_t;

static int
text_getBaseSize(void)
{
    int base = debugger.theme.text.fontSize;
    if (base <= 0) {
        base = E9UI_THEME_TEXT_FONT_SIZE;
    }
    return base;
}

static TTF_Font *
text_getFont(e9ui_text_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !ctx) {
        return NULL;
    }
    int base = st->fontSize_px > 0 ? st->fontSize_px : text_getBaseSize();
    int scaled = e9ui_scale_px(ctx, base);
    int style = st->fontStyle;
    if (scaled <= 0) {
        scaled = base > 0 ? base : 16;
    }
    if (st->font && st->fontSizeScaled == scaled && st->fontStyleScaled == style) {
        return st->font;
    }
    if (st->font) {
        TTF_CloseFont(st->font);
        st->font = NULL;
    }
    const char *asset = debugger.theme.text.fontAsset ? debugger.theme.text.fontAsset : E9UI_THEME_TEXT_FONT_ASSET;
    char path[PATH_MAX];
    if (!file_getAssetPath(asset, path, sizeof(path))) {
        return NULL;
    }
    st->font = TTF_OpenFont(path, scaled);
    if (!st->font) {
        debug_error("text: failed to load font %s", path);
        return NULL;
    }
    if (style != TTF_STYLE_NORMAL) {
        TTF_SetFontStyle(st->font, style);
    }
    st->fontSizeScaled = scaled;
    st->fontStyleScaled = style;
    return st->font;
}

static int
text_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    e9ui_text_state_t *st = (e9ui_text_state_t*)self->state;
    if (!st) {
        return 0;
    }
    TTF_Font *font = text_getFont(st, ctx);
    if (!font) {
        return e9ui_scale_px(ctx, text_getBaseSize());
    }
    return TTF_FontHeight(font);
}

static void
text_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
text_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    e9ui_text_state_t *st = (e9ui_text_state_t*)self->state;
    if (!st || !st->text || !*st->text) {
        return;
    }
    TTF_Font *font = text_getFont(st, ctx);
    if (!font) {
        return;
    }
    int tw = 0;
    int th = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->text, st->color, &tw, &th);
    if (!tex) {
        return;
    }
    int x = self->bounds.x;
    int y = self->bounds.y + (self->bounds.h - th) / 2;
    if (y < self->bounds.y) {
        y = self->bounds.y;
    }
    SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
}

static void
text_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    e9ui_text_state_t *st = (e9ui_text_state_t*)self->state;
    if (st->text) {
        alloc_free(st->text);
        st->text = NULL;
    }
    if (st->font) {
        TTF_CloseFont(st->font);
        st->font = NULL;
    }
    alloc_free(st);
    self->state = NULL;
}

e9ui_component_t *
e9ui_text_make(const char *text)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    e9ui_text_state_t *st = (e9ui_text_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    st->color = (SDL_Color){220, 220, 220, 255};
    st->fontStyle = TTF_STYLE_NORMAL;
    if (text && *text) {
        st->text = alloc_strdup(text);
    }
    c->name = "e9ui_text";
    c->state = st;
    c->preferredHeight = text_preferredHeight;
    c->layout = text_layout;
    c->render = text_render;
    c->dtor = text_dtor;
    return c;
}

void
e9ui_text_setText(e9ui_component_t *comp, const char *text)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_text_state_t *st = (e9ui_text_state_t*)comp->state;
    if (st->text) {
        alloc_free(st->text);
        st->text = NULL;
    }
    if (text && *text) {
        st->text = alloc_strdup(text);
    }
}

void
e9ui_text_setFontSize(e9ui_component_t *comp, int fontSize_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_text_state_t *st = (e9ui_text_state_t*)comp->state;
    st->fontSize_px = fontSize_px;
}

void
e9ui_text_setBold(e9ui_component_t *comp, int bold)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_text_state_t *st = (e9ui_text_state_t*)comp->state;
    st->fontStyle = bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
}

void
e9ui_text_setColor(e9ui_component_t *comp, SDL_Color color)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_text_state_t *st = (e9ui_text_state_t*)comp->state;
    st->color = color;
}
