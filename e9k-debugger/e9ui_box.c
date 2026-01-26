// e9ui box container
#include "e9ui_box.h"
#include "e9ui.h"
#include "e9ui_context.h"
#include "e9ui_text_cache.h"
#include "e9ui_types.h"
#include "debugger.h"
#include "file.h"
#include "debug.h"
#include "e9ui_scale.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct e9ui_box_state {
    e9ui_component_t *child;
    int                 pad;
    e9ui_dim_mode_t   wMode;
    int                 wPx;
    e9ui_dim_mode_t   hMode;
    int                 hPx;
    e9ui_valign_t     vAlign;
    // Optional borders
    int                 borderMask;
    SDL_Color           borderColor;
    int                 borderThick;
    // Collapse state toggled via titlebar clicks
    int                 collapseEnabled;
    int                 collapsed;
    // Optional titlebar
    char               *title;
    char               *titleIconAsset;
    SDL_Texture        *titleIcon;
    int                 titleIconW;
    int                 titleIconH;
    char               *fullscreenIconAsset;
    SDL_Texture        *fullscreenIcon;
    int                 fullscreenIconW;
    int                 fullscreenIconH;
    int                 fullscreenHover;
} e9ui_box_state_t;

static SDL_Cursor *s_cursor_hand = NULL;
static SDL_Cursor *s_cursor_arrow = NULL;

void
e9ui_box_resetCursors(void)
{
    if (s_cursor_hand) {
        SDL_FreeCursor(s_cursor_hand);
        s_cursor_hand = NULL;
    }
    if (s_cursor_arrow) {
        SDL_FreeCursor(s_cursor_arrow);
        s_cursor_arrow = NULL;
    }
}

static void
e9ui_box_ensureCursors(void)
{
    if (!s_cursor_hand) {
        s_cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    }
    if (!s_cursor_arrow) {
        s_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    }
}

static int
e9ui_box_titlebarIconMaxH(int textH)
{
    int iconMaxH = (int)floorf(textH * 0.75f);
    if (iconMaxH < 10) {
        iconMaxH = (textH > 0) ? textH : 10;
    }
    return iconMaxH;
}

static void
e9ui_box_ensureTitleIcon(e9ui_box_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->titleIconAsset || st->titleIcon) {
        return;
    }
    if (!ctx || !ctx->renderer) {
        return;
    }
    char path[1024];
    if (!file_getAssetPath(st->titleIconAsset, path, sizeof(path))) {
        return;
    }
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        debug_error("Titlebar icon load failed: %s (SDL_image: %s)", path, IMG_GetError());
        return;
    }
    st->titleIcon = SDL_CreateTextureFromSurface(ctx->renderer, surf);
    if (!st->titleIcon) {
        debug_error("Titlebar icon texture failed: %s", SDL_GetError());
    } else {
        st->titleIconW = surf->w;
        st->titleIconH = surf->h;
    }
    SDL_FreeSurface(surf);
}

static void
e9ui_box_ensureFullscreenIcon(e9ui_box_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->fullscreenIconAsset || st->fullscreenIcon) {
        return;
    }
    if (!ctx || !ctx->renderer) {
        return;
    }
    char path[1024];
    if (!file_getAssetPath(st->fullscreenIconAsset, path, sizeof(path))) {
        return;
    }
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        debug_error("Titlebar icon load failed: %s (SDL_image: %s)", path, IMG_GetError());
        return;
    }
    st->fullscreenIcon = SDL_CreateTextureFromSurface(ctx->renderer, surf);
    if (!st->fullscreenIcon) {
        debug_error("Titlebar icon texture failed: %s", SDL_GetError());
    } else {
        st->fullscreenIconW = surf->w;
        st->fullscreenIconH = surf->h;
    }
    SDL_FreeSurface(surf);
}

static void
e9ui_box_getTitleIconDisplay(e9ui_box_state_t *st, int textH, int *outIconW, int *outIconH)
{
    if (!st || !st->titleIcon || st->titleIconH <= 0) {
        if (outIconW) {
            *outIconW = 0;
        }
        if (outIconH) {
            *outIconH = 0;
        }
        return;
    }
    int maxH = e9ui_box_titlebarIconMaxH(textH);
    int dispH = st->titleIconH;
    if (dispH > maxH) {
        dispH = maxH;
    }
    if (dispH < 0) {
        dispH = 0;
    }
    float scale = (st->titleIconH > 0) ? (float)dispH / (float)st->titleIconH : 1.f;
    int dispW = st->titleIconW;
    if (scale != 1.f) {
        dispW = (int)ceilf((float)st->titleIconW * scale);
    }
    if (outIconW) {
        *outIconW = dispW;
    }
    if (outIconH) {
        *outIconH = dispH;
    }
}

static void
e9ui_box_getFullscreenIconDisplay(e9ui_box_state_t *st, int textH, int *outIconW, int *outIconH)
{
    if (!st || !st->fullscreenIcon || st->fullscreenIconH <= 0) {
        if (outIconW) {
            *outIconW = 0;
        }
        if (outIconH) {
            *outIconH = 0;
        }
        return;
    }
    int maxH = e9ui_box_titlebarIconMaxH(textH);
    int dispH = st->fullscreenIconH;
    if (dispH > maxH) {
        dispH = maxH;
    }
    if (dispH < 0) {
        dispH = 0;
    }
    float scale = (st->fullscreenIconH > 0) ? (float)dispH / (float)st->fullscreenIconH : 1.f;
    int dispW = st->fullscreenIconW;
    if (scale != 1.f) {
        dispW = (int)ceilf((float)st->fullscreenIconW * scale);
    }
    if (outIconW) {
        *outIconW = dispW;
    }
    if (outIconH) {
        *outIconH = dispH;
    }
}

static int
e9ui_box_getFullscreenButtonRect(e9ui_box_state_t *st, e9ui_context_t *ctx, SDL_Rect titleRect, SDL_Rect *outRect)
{
    if (!st || st->collapsed || !st->fullscreenIconAsset || !outRect) {
        return 0;
    }
    e9ui_box_ensureFullscreenIcon(st, ctx);
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int textH = font ? TTF_FontHeight(font) : 16;
    if (textH <= 0) {
        textH = 16;
    }
    int iconDrawW = 0;
    int iconDrawH = 0;
    e9ui_box_getFullscreenIconDisplay(st, textH, &iconDrawW, &iconDrawH);
    if (iconDrawW <= 0 || iconDrawH <= 0) {
        return 0;
    }
    int padX = e9ui_scale_px(ctx, 8);
    int iconY = titleRect.y + (titleRect.h - iconDrawH) / 2;
    if (iconY < titleRect.y) {
        iconY = titleRect.y;
    }
    int iconX = titleRect.x + titleRect.w - padX - iconDrawW;
    outRect->x = iconX;
    outRect->y = iconY;
    outRect->w = iconDrawW;
    outRect->h = iconDrawH;
    return 1;
}

static int
e9ui_box_titlebarHeight(e9ui_box_state_t *st, e9ui_context_t *ctx)
{
    if (!st) {
        return 0;
    }
    if ((!st->title || !*st->title) && !st->titleIconAsset) {
        return 0;
    }
    e9ui_box_ensureTitleIcon(st, ctx);
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int textH = font ? TTF_FontHeight(font) : 16;
    if (textH <= 0) {
        textH = 16;
    }
    int iconDispH = 0;
    e9ui_box_getTitleIconDisplay(st, textH, NULL, &iconDispH);
    int contentH = textH;
    if (iconDispH > contentH) {
        contentH = iconDispH;
    }
    int padY = e9ui_scale_px(ctx, 4);
    return contentH + padY * 2;
}

static void
e9ui_box_drawTitlebar(e9ui_box_state_t *st, e9ui_context_t *ctx, SDL_Rect rect)
{
    if (!st || !ctx) {
        return;
    }
    const e9k_theme_titlebar_t *theme = &debugger.theme.titlebar;
    int padX = e9ui_scale_px(ctx, 8);
    int x = rect.x + padX;
    int iconSpacing = e9ui_scale_px(ctx, 6);
    e9ui_box_ensureTitleIcon(st, ctx);
    e9ui_box_ensureFullscreenIcon(st, ctx);
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    int textH = font ? TTF_FontHeight(font) : 16;
    if (textH <= 0) {
        textH = 16;
    }
    int iconDrawW = 0;
    int iconDrawH = 0;
    e9ui_box_getTitleIconDisplay(st, textH, &iconDrawW, &iconDrawH);
    if (st->titleIcon) {
        if (iconDrawW > 0 && iconDrawH > 0) {
            int iconY = rect.y + (rect.h - iconDrawH) / 2;
            if (iconY < rect.y) {
                iconY = rect.y;
            }
            SDL_Rect iconRect = { x, iconY, iconDrawW, iconDrawH };
            SDL_RenderCopy(ctx->renderer, st->titleIcon, NULL, &iconRect);
            x += iconDrawW + iconSpacing;
        }
    }
    if (st->title && *st->title && font) {
        int tw = 0, th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->title, theme->text, &tw, &th);
        if (tex) {
            int textY = rect.y + (rect.h - th) / 2;
            if (textY < rect.y) {
                textY = rect.y;
            }
            SDL_Rect textRect = { x, textY, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &textRect);
        }
    }
    SDL_Rect fsRect;
    if (e9ui_box_getFullscreenButtonRect(st, ctx, rect, &fsRect)) {
        SDL_RenderCopy(ctx->renderer, st->fullscreenIcon, NULL, &fsRect);
    }
}

static int
e9ui_box_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    int pad = e9ui_scale_px(ctx, st->pad);
    if (st->hMode == e9ui_dim_fixed) {
        int hPx = e9ui_scale_px(ctx, st->hPx);
        return hPx + pad*2;
    }
    int titleH = e9ui_box_titlebarHeight(st, ctx);
    if (st->collapsed && titleH > 0) {
        return pad*2 + titleH;
    }
    int childH = 0;
    if (st->child && st->child->preferredHeight) {
        int innerW = (availW - pad*2);
        if (innerW < 0) {
            innerW = 0;
        }
        childH = st->child->preferredHeight(st->child, ctx, innerW);
    }
    return pad*2 + titleH + childH;
}

static void
e9ui_box_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    if (st->collapsed) {
        return;
    }
    if (!st->child || !st->child->layout) {
        return;
    }
    int pad = e9ui_scale_px(ctx, st->pad);
    int titleH = e9ui_box_titlebarHeight(st, ctx);
    if (e9ui_isFullscreenComponent(self)) {
        titleH = 0;
    }
    int innerX = bounds.x + pad;
    int innerY = bounds.y + pad + titleH;
    int innerW = bounds.w - pad*2;
    if (innerW < 0) {
        innerW = 0;
    }
    int innerH = bounds.h - pad*2 - titleH;
    if (innerH < 0) {
        innerH = 0;
    }
    int childW = innerW;
    int childH = innerH;
    if (st->wMode == e9ui_dim_fixed) {
        int wPx = e9ui_scale_px(ctx, st->wPx);
        if (wPx < childW && wPx >= 0) {
            childW = wPx;
        }
    }
    if (st->hMode == e9ui_dim_fixed) {
        int hPx = e9ui_scale_px(ctx, st->hPx);
        if (hPx < childH && hPx >= 0) {
            childH = hPx;
        }
    }
    int childX = innerX;
    int childY = innerY;
    int freeH = innerH - childH;
    if (freeH < 0) {
        freeH = 0;
    }
    if (st->vAlign == e9ui_valign_center) {
        childY = innerY + freeH/2;
    } else if (st->vAlign == e9ui_valign_end) {
        childY = innerY + freeH;
    }
    e9ui_rect_t childR = (e9ui_rect_t){ childX, childY, childW, childH };
    st->child->layout(st->child, ctx, childR);
}

static void
e9ui_box_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (ctx && ctx->renderer && debugger.inTransition <= 0) {
        SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(ctx->renderer, &bg);
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    int titleH = e9ui_box_titlebarHeight(st, ctx);
    if (titleH > 0) {
        if (e9ui_isFullscreenComponent(self)) {
            titleH = 0;
        }
    }
    if (titleH > 0) {
        SDL_Color bg = debugger.theme.titlebar.background;
        SDL_Rect titleRect = { self->bounds.x, self->bounds.y, self->bounds.w, titleH };
        SDL_SetRenderDrawColor(ctx->renderer, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(ctx->renderer, &titleRect);
        e9ui_box_drawTitlebar(st, ctx, titleRect);
    }
    if (st->child && !st->collapsed && st->child->render) {
        st->child->render(st->child, ctx);
    }
    // Render optional borders on top of child
    int thickness = e9ui_scale_px(ctx, st->borderThick);
    if (st->borderMask && thickness > 0 && debugger.inTransition <= 0) {
        SDL_SetRenderDrawColor(ctx->renderer, st->borderColor.r, st->borderColor.g, st->borderColor.b, st->borderColor.a);
        SDL_Rect b = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
        int t = thickness;
        if (st->borderMask & E9UI_BORDER_TOP) {
            SDL_Rect r = { b.x, b.y, b.w, t };
            SDL_RenderFillRect(ctx->renderer, &r);
        }
        if (st->borderMask & E9UI_BORDER_BOTTOM) {
            SDL_Rect r = { b.x, b.y + b.h - t, b.w, t };
            SDL_RenderFillRect(ctx->renderer, &r);
        }
        if (st->borderMask & E9UI_BORDER_LEFT) {
            SDL_Rect r = { b.x, b.y, t, b.h };
            SDL_RenderFillRect(ctx->renderer, &r);
        }
        if (st->borderMask & E9UI_BORDER_RIGHT) {
            SDL_Rect r = { b.x + b.w - t, b.y, t, b.h };
            SDL_RenderFillRect(ctx->renderer, &r);
        }
    }
}

static void
e9ui_box_titlebarClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    if (!self || !ctx || !mouse_ev) {
        return;
    }
    if (mouse_ev->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    if (!st) {
        return;
    }
    if (e9ui_isFullscreenComponent(self)) {
        return;
    }
    int titleH = e9ui_box_titlebarHeight(st, ctx);
    if (titleH <= 0) {
        return;
    }
    if (mouse_ev->y < self->bounds.y || mouse_ev->y >= self->bounds.y + titleH) {
        return;
    }
    if (!st->collapsed) {
        SDL_Rect titleRect = { self->bounds.x, self->bounds.y, self->bounds.w, titleH };
        SDL_Rect fsRect;
        if (e9ui_box_getFullscreenButtonRect(st, ctx, titleRect, &fsRect)) {
            if (mouse_ev->x >= fsRect.x && mouse_ev->x < fsRect.x + fsRect.w &&
                mouse_ev->y >= fsRect.y && mouse_ev->y < fsRect.y + fsRect.h) {
                e9ui_setFullscreenComponent(self);
                return;
            }
        }
    }
    if (!st->collapseEnabled) {
        return;
    }
    int pad = e9ui_scale_px(ctx, st->pad);
    int collapsedH = pad*2 + titleH;
    if (collapsedH < 0) {
        collapsedH = 0;
    }
    st->collapsed = !st->collapsed;
    if (st->collapsed) {
        self->collapsed = 1;
        self->collapsedHeight = collapsedH;
    } else {
        self->collapsed = 0;
        self->collapsedHeight = 0;
    }
}

static void e9ui_box_persistSave(e9ui_component_t *self, e9ui_context_t *ctx, FILE *f);
static void e9ui_box_persistLoad(e9ui_component_t *self, e9ui_context_t *ctx, const char *key, const char *value);


static void
e9ui_box_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)self;
  (void)ctx;

  e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    if (st) {
    if (st->title) {
      alloc_free(st->title);
    }
    if (st->titleIcon) {
      SDL_DestroyTexture(st->titleIcon);
    }
    if (st->titleIconAsset) {
        alloc_free(st->titleIconAsset);
    }
    if (st->fullscreenIcon) {
      SDL_DestroyTexture(st->fullscreenIcon);
    }
    if (st->fullscreenIconAsset) {
      alloc_free(st->fullscreenIconAsset);
    }
  }
}

static void
e9ui_box_onMouseMove(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    if (!self || !ctx || !mouse_ev) {
        return;
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    if (!st) {
        return;
    }
    int titleH = e9ui_box_titlebarHeight(st, ctx);
    if (titleH <= 0 || st->collapsed) {
        if (st->fullscreenHover) {
            st->fullscreenHover = 0;
            e9ui_box_ensureCursors();
            if (s_cursor_arrow) {
                SDL_SetCursor(s_cursor_arrow);
            }
        }
        return;
    }
    SDL_Rect titleRect = { self->bounds.x, self->bounds.y, self->bounds.w, titleH };
    SDL_Rect fsRect;
    int over = 0;
    if (e9ui_box_getFullscreenButtonRect(st, ctx, titleRect, &fsRect)) {
        if (mouse_ev->x >= fsRect.x && mouse_ev->x < fsRect.x + fsRect.w &&
            mouse_ev->y >= fsRect.y && mouse_ev->y < fsRect.y + fsRect.h) {
            over = 1;
        }
    }
    if (over) {
        st->fullscreenHover = 1;
        e9ui_box_ensureCursors();
        if (s_cursor_hand) {
            SDL_SetCursor(s_cursor_hand);
            ctx->cursorOverride = 1;
        }
    } else if (st->fullscreenHover) {
        st->fullscreenHover = 0;
        e9ui_box_ensureCursors();
        if (s_cursor_arrow) {
            SDL_SetCursor(s_cursor_arrow);
        }
    }
}

static void
e9ui_box_onLeave(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    (void)ctx;
    (void)mouse_ev;
    if (!self) {
        return;
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->fullscreenHover) {
        st->fullscreenHover = 0;
        e9ui_box_ensureCursors();
        if (s_cursor_arrow) {
            SDL_SetCursor(s_cursor_arrow);
        }
    }
}

e9ui_component_t *
e9ui_box_make(e9ui_component_t *child)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_box_state_t *st = (e9ui_box_state_t*)alloc_calloc(1, sizeof(*st));
    st->child = child;
    e9ui_child_add(c, child, 0);
    st->pad = 0;
    st->wMode = e9ui_dim_grow; st->wPx = 0;
    st->hMode = e9ui_dim_grow; st->hPx = 0; st->vAlign = e9ui_valign_start;
    st->borderMask = 0; st->borderColor = (SDL_Color){80,80,80,255}; st->borderThick = 1;
    st->fullscreenIconAsset = alloc_strdup("assets/icons/fullscreen.png");
    c->name = "e9ui_box";
    c->state = st;
    c->preferredHeight = e9ui_box_preferredHeight;
    c->layout = e9ui_box_layout;
    c->render = e9ui_box_render;
    c->dtor = e9ui_box_dtor;
    c->persistSave = e9ui_box_persistSave;
    c->persistLoad = e9ui_box_persistLoad;
    c->onMouseMove = e9ui_box_onMouseMove;
    c->onLeave = e9ui_box_onLeave;
    return c;
}

static void
e9ui_box_persistSave(e9ui_component_t *self, e9ui_context_t *ctx, FILE *f)
{
    (void)ctx;
    if (!self || !self->state || !self->persist_id || !f) {
        return;
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    fprintf(f, "comp.%s.collapsed=%d\n", self->persist_id, st->collapsed ? 1 : 0);
    fprintf(f, "comp.%s.fullscreen=%d\n", self->persist_id, e9ui_isFullscreenComponent(self) ? 1 : 0);
}

static void
e9ui_box_persistLoad(e9ui_component_t *self, e9ui_context_t *ctx, const char *key, const char *value)
{
    if (!self || !self->state || !key || !value) {
        return;
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)self->state;
    if (strcmp(key, "collapsed") == 0) {
        int collapsed = atoi(value) ? 1 : 0;
        st->collapsed = collapsed;
        if (collapsed) {
            self->collapsed = 1;
            int titleH = e9ui_box_titlebarHeight(st, ctx);
            int pad = e9ui_scale_px(ctx, st->pad);
            int collapsedH = pad*2 + titleH;
            if (collapsedH < 0) {
                collapsedH = 0;
            }
            self->collapsedHeight = collapsedH;
        } else {
            self->collapsed = 0;
            self->collapsedHeight = 0;
        }
        return;
    }
    if (strcmp(key, "fullscreen") == 0) {
        int fullscreen = atoi(value) ? 1 : 0;
        if (fullscreen) {
            e9ui_setFullscreenComponent(self);
        }
        return;
    }
}

void
e9ui_box_setPadding(e9ui_component_t *box, int pad_px)
{
    e9ui_box_state_t *st = (e9ui_box_state_t*)box->state;
    st->pad = (pad_px >= 0) ? pad_px : 0;
}

void
e9ui_box_setWidth(e9ui_component_t *box, e9ui_dim_mode_t mode, int pixels)
{
    e9ui_box_state_t *st = (e9ui_box_state_t*)box->state;
    st->wMode = mode; st->wPx = (pixels >= 0) ? pixels : 0;
}

void
e9ui_box_setHeight(e9ui_component_t *box, e9ui_dim_mode_t mode, int pixels)
{
    e9ui_box_state_t *st = (e9ui_box_state_t*)box->state;
    st->hMode = mode; st->hPx = (pixels >= 0) ? pixels : 0;
}

void
e9ui_box_setVAlign(e9ui_component_t *box, e9ui_valign_t align)
{
    e9ui_box_state_t *st = (e9ui_box_state_t*)box->state;
    st->vAlign = align;
}

void
e9ui_box_setBorder(e9ui_component_t *box, int sides_mask, SDL_Color color, int thickness_px)
{
    e9ui_box_state_t *st = (e9ui_box_state_t*)box->state;
    st->borderMask = sides_mask;
    st->borderColor = color;
    st->borderThick = (thickness_px > 0) ? thickness_px : 1;
}

void
e9ui_box_setTitlebar(e9ui_component_t *box, const char *title, const char *iconAsset)
{
    if (!box) {
        return;
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)box->state;
    if (!st) {
        return;
    }
    if (st->title) {
        alloc_free(st->title);
        st->title = NULL;
    }
    if (title && *title) {
        st->title = alloc_strdup(title);
    }
    if (st->titleIcon) {
        SDL_DestroyTexture(st->titleIcon);
        st->titleIcon = NULL;
        st->titleIconW = 0;
        st->titleIconH = 0;
    }
    if (st->titleIconAsset) {
        alloc_free(st->titleIconAsset);
        st->titleIconAsset = NULL;
    }
    if (iconAsset && *iconAsset) {
        st->titleIconAsset = alloc_strdup(iconAsset);
    }
    int hasTitlebar = 0;
    if ((st->title && *st->title) || (st->titleIconAsset && *st->titleIconAsset)) {
        hasTitlebar = 1;
    }
    st->collapseEnabled = hasTitlebar;
    if (!st->collapseEnabled) {
        st->collapsed = 0;
    }
    box->onClick = hasTitlebar ? e9ui_box_titlebarClick : NULL;
}

void
e9ui_box_setChild(e9ui_component_t *box, e9ui_component_t *child, e9ui_context_t *ctx)
{
    if (!box || !box->state) {
        return;
    }
    e9ui_box_state_t *st = (e9ui_box_state_t*)box->state;
    if (st->child == child) {
        return;
    }
    if (st->child) {
        e9ui_childRemove(box, st->child, ctx);
    }
    st->child = child;
    if (child) {
        e9ui_child_add(box, child, 0);
    }
}
