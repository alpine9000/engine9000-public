/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <limits.h>
#include <string.h>

#include "e9ui.h"
#include "debugger.h"
#include "file.h"

typedef struct modal_state {
    e9ui_rect_t rect;
    char title[128];
    e9ui_component_t *titlebar;
    e9ui_component_t *body;
    SDL_Rect close_rect;
    e9ui_modal_close_cb_t onClose;
    void *onCloseUser;
} e9ui_modal_state_t;

static SDL_Texture *
e9ui_modal_getCloseIcon(SDL_Renderer *renderer, int *out_w, int *out_h)
{
    static SDL_Texture *icon = NULL;
    static int icon_w = 0;
    static int icon_h = 0;
    if (!renderer) {
        return NULL;
    }
    if (icon) {
        if (out_w) *out_w = icon_w;
        if (out_h) *out_h = icon_h;
        return icon;
    }
    char path[PATH_MAX];
    if (!file_getAssetPath("assets/icons/close.png", path, sizeof(path))) {
        return NULL;
    }
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        debug_error("modal: failed to load close icon %s: %s", path, IMG_GetError());
        return NULL;
    }
    icon = SDL_CreateTextureFromSurface(renderer, s);
    icon_w = s->w;
    icon_h = s->h;
    SDL_FreeSurface(s);
    if (out_w) *out_w = icon_w;
    if (out_h) *out_h = icon_h;
    return icon;
}

static e9ui_component_t *
e9ui_modal_title_make(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    c->name = "modal_titlebar";
    return c;
}

static int
e9ui_modal_titlebarHeight(e9ui_context_t *ctx)
{
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int textH = font ? TTF_FontHeight(font) : 16;
    if (textH <= 0) {
        textH = 16;
    }
    int padY = e9ui_scale_px(ctx, 4);
    return textH + padY * 2;
}

static void
e9ui_modal_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)bounds;
    e9ui_modal_state_t *st = (e9ui_modal_state_t*)self->state;
    if (!st) {
        return;
    }
    self->bounds = st->rect;
    int titleH = e9ui_modal_titlebarHeight(ctx);
    if (st->titlebar && st->titlebar->layout) {
        st->titlebar->layout(st->titlebar, ctx,
                             (e9ui_rect_t){ self->bounds.x, self->bounds.y, self->bounds.w, titleH });
    }
    if (st->body && st->body->layout) {
        int bodyY = self->bounds.y + titleH;
        int bodyH = self->bounds.h - titleH;
        if (bodyH < 0) {
            bodyH = 0;
        }
        st->body->layout(st->body, ctx, (e9ui_rect_t){ self->bounds.x, bodyY, self->bounds.w, bodyH });
    }
}

static int
e9ui_modal_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
e9ui_modal_drawTitlebar(e9ui_modal_state_t *st, e9ui_context_t *ctx, SDL_Rect rect)
{
    const e9k_theme_titlebar_t *theme = &debugger.theme.titlebar;
    SDL_SetRenderDrawColor(ctx->renderer, theme->background.r, theme->background.g, theme->background.b, theme->background.a);
    SDL_RenderFillRect(ctx->renderer, &rect);

    int padX = e9ui_scale_px(ctx, 8);
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    if (font && st->title[0]) {
        int tw = 0, th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->title, theme->text, &tw, &th);
        if (tex) {
            int textY = rect.y + (rect.h - th) / 2;
            if (textY < rect.y) {
                textY = rect.y;
            }
            SDL_Rect dst = { rect.x + padX, textY, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
        }
    }

    int close_pad = e9ui_scale_px(ctx, 6);
    int close_size = rect.h - close_pad * 2;
    if (close_size < e9ui_scale_px(ctx, 12)) {
        close_size = e9ui_scale_px(ctx, 12);
    }
    int close_x = rect.x + rect.w - close_pad - close_size;
    int close_y = rect.y + (rect.h - close_size) / 2;
    st->close_rect = (SDL_Rect){ close_x, close_y, close_size, close_size };

    SDL_Texture *icon = e9ui_modal_getCloseIcon(ctx->renderer, NULL, NULL);
    if (icon) {
        SDL_Rect dst = st->close_rect;
        SDL_RenderCopy(ctx->renderer, icon, NULL, &dst);
    } else {
        SDL_SetRenderDrawColor(ctx->renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(ctx->renderer, &st->close_rect);
        SDL_RenderDrawLine(ctx->renderer, close_x + 3, close_y + 3,
                           close_x + close_size - 4, close_y + close_size - 4);
        SDL_RenderDrawLine(ctx->renderer, close_x + close_size - 4, close_y + 3,
                           close_x + 3, close_y + close_size - 4);
    }
}

static void
e9ui_modal_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    e9ui_modal_state_t *st = (e9ui_modal_state_t*)self->state;
    if (!st) {
        return;
    }
    SDL_SetRenderDrawColor(ctx->renderer, 24, 24, 24, 255);
    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_RenderFillRect(ctx->renderer, &bg);

    SDL_Rect titleRect = { self->bounds.x, self->bounds.y, self->bounds.w, e9ui_modal_titlebarHeight(ctx) };
    e9ui_modal_drawTitlebar(st, ctx, titleRect);

    if (st->body && st->body->render) {
        st->body->render(st->body, ctx);
    }
}

static int
e9ui_modal_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    e9ui_modal_state_t *st = (e9ui_modal_state_t*)self->state;
    if (!st) {
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        if (mx >= st->close_rect.x && mx < st->close_rect.x + st->close_rect.w &&
            my >= st->close_rect.y && my < st->close_rect.y + st->close_rect.h) {
            if (st->onClose) {
                st->onClose(self, st->onCloseUser);
            }
            e9ui_setHidden(self, 1);
            if (!debugger.ui.pendingRemove) {
                debugger.ui.pendingRemove = self;
            }
            return 1;
        }
    }
    return 0;
}

e9ui_component_t *
e9ui_modal_make(const char *title, e9ui_rect_t rect, e9ui_modal_close_cb_t onClose, void *user)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    e9ui_modal_state_t *st = (e9ui_modal_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    st->rect = rect;
    st->onClose = onClose;
    st->onCloseUser = user;
    if (title && *title) {
        strncpy(st->title, title, sizeof(st->title) - 1);
        st->title[sizeof(st->title) - 1] = '\0';
    } else {
        st->title[0] = '\0';
    }
    st->titlebar = e9ui_modal_title_make();
    st->body = e9ui_box_make(NULL);
    c->name = "e9ui_modal";
    c->state = st;
    c->preferredHeight = e9ui_modal_preferredHeight;
    c->layout = e9ui_modal_layout;
    c->render = e9ui_modal_render;
    c->handleEvent = e9ui_modal_handleEvent;
    if (st->titlebar) {
        e9ui_child_add(c, st->titlebar, alloc_strdup("modal_titlebar"));
    }
    if (st->body) {
        e9ui_child_add(c, st->body, alloc_strdup("modal_body"));
    }
    return c;
}

e9ui_component_t *
e9ui_modal_show(e9ui_context_t *ctx, const char *title, e9ui_rect_t rect,
           e9ui_modal_close_cb_t onClose, void *user)
{
    if (!ctx || !debugger.ui.root) {
        return NULL;
    }
    e9ui_component_t *modal = e9ui_modal_make(title, rect, onClose, user);
    if (!modal) {
        return NULL;
    }
    if (debugger.ui.root->name && strcmp(debugger.ui.root->name, "e9ui_stack") == 0) {
        e9ui_stack_addFixed(debugger.ui.root, modal);
    } else {
        e9ui_child_add(debugger.ui.root, modal, alloc_strdup("modal"));
    }
    return modal;
}

void
e9ui_modal_setBodyChild(e9ui_component_t *modal, e9ui_component_t *child, e9ui_context_t *ctx)
{
    if (!modal || !modal->state) {
        return;
    }
    e9ui_modal_state_t *st = (e9ui_modal_state_t*)modal->state;
    if (!st || !st->body) {
        return;
    }
    e9ui_box_setChild(st->body, child, ctx);
}
