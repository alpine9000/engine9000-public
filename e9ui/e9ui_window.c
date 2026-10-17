/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include <SDL.h>
#include <stdint.h>
#include <string.h>

#include "alloc.h"
#include "e9ui_window.h"

typedef struct e9ui_window_overlay_state
{
    e9ui_window_t *owner;
    e9ui_rect_t rect;
    e9ui_rect_t dragStartRect;
    e9ui_rect_t restoreRect;
    char title[128];
    e9ui_component_t *body;
    SDL_Rect closeRect;
    int dragging;
    int dragStartMouseX;
    int dragStartMouseY;
    int dragOffsetX;
    int dragOffsetY;
    int resizing;
    int resizeMask;
    int resizeStartMouseX;
    int resizeStartMouseY;
    int minWidthPx;
    int minHeightPx;
    int maximized;
    int restoreRectValid;
    int closeOnEscape;
    e9ui_window_close_cb_t onClose;
    void *onCloseUser;
} e9ui_window_overlay_state_t;

typedef struct e9ui_window_deferred_close_call
{
    e9ui_window_t *owner;
    e9ui_window_close_cb_t onClose;
    void *onCloseUser;
} e9ui_window_deferred_close_call_t;

struct e9ui_window
{
    e9ui_window_backend_t backend;
    e9ui_component_t *windowComp;
    int open;
    int minWidthPx;
    int minHeightPx;
    int closeOnEscape;
};

static SDL_Cursor *e9ui_window_overlayCursorArrow = NULL;
static SDL_Cursor *e9ui_window_overlayCursorHand = NULL;
static SDL_Cursor *e9ui_window_overlayCursorMove = NULL;
static SDL_Cursor *e9ui_window_overlayCursorNs = NULL;
static SDL_Cursor *e9ui_window_overlayCursorEw = NULL;
static SDL_Cursor *e9ui_window_overlayCursorNwse = NULL;
static SDL_Cursor *e9ui_window_overlayCursorNesw = NULL;
static SDL_Texture *e9ui_window_overlayCloseIcon = NULL;
static int e9ui_window_overlayCloseIconW = 0;
static int e9ui_window_overlayCloseIconH = 0;

static void
e9ui_window_overlaySyncMaximizedRect(e9ui_window_overlay_state_t *st, const e9ui_context_t *ctx);

static void
e9ui_window_overlayToggleMaximized(e9ui_window_overlay_state_t *st, const e9ui_context_t *ctx);

enum
{
    E9UI_WINDOW_OVERLAY_RESIZE_LEFT   = 1 << 0,
    E9UI_WINDOW_OVERLAY_RESIZE_RIGHT  = 1 << 1,
    E9UI_WINDOW_OVERLAY_RESIZE_TOP    = 1 << 2,
    E9UI_WINDOW_OVERLAY_RESIZE_BOTTOM = 1 << 3
};

static int
e9ui_window_overlayManualChangeThresholdPx(const e9ui_context_t *ctx)
{
    int px = e9ui_scale_px(ctx, 4);

    if (px < 1) {
        px = 1;
    }
    return px;
}

static void
e9ui_window_overlayEnsureCursors(void)
{
    if (!e9ui_window_overlayCursorArrow) {
        e9ui_window_overlayCursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    }
    if (!e9ui_window_overlayCursorHand) {
        e9ui_window_overlayCursorHand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    }
    if (!e9ui_window_overlayCursorMove) {
        e9ui_window_overlayCursorMove = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    }
    if (!e9ui_window_overlayCursorNs) {
        e9ui_window_overlayCursorNs = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    }
    if (!e9ui_window_overlayCursorEw) {
        e9ui_window_overlayCursorEw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    }
    if (!e9ui_window_overlayCursorNwse) {
        e9ui_window_overlayCursorNwse = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    }
    if (!e9ui_window_overlayCursorNesw) {
        e9ui_window_overlayCursorNesw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    }
}

void
e9ui_window_resetOverlayResources(void)
{
    if (e9ui_window_overlayCursorArrow) {
        SDL_FreeCursor(e9ui_window_overlayCursorArrow);
        e9ui_window_overlayCursorArrow = NULL;
    }
    if (e9ui_window_overlayCursorHand) {
        SDL_FreeCursor(e9ui_window_overlayCursorHand);
        e9ui_window_overlayCursorHand = NULL;
    }
    if (e9ui_window_overlayCursorMove) {
        SDL_FreeCursor(e9ui_window_overlayCursorMove);
        e9ui_window_overlayCursorMove = NULL;
    }
    if (e9ui_window_overlayCursorNs) {
        SDL_FreeCursor(e9ui_window_overlayCursorNs);
        e9ui_window_overlayCursorNs = NULL;
    }
    if (e9ui_window_overlayCursorEw) {
        SDL_FreeCursor(e9ui_window_overlayCursorEw);
        e9ui_window_overlayCursorEw = NULL;
    }
    if (e9ui_window_overlayCursorNwse) {
        SDL_FreeCursor(e9ui_window_overlayCursorNwse);
        e9ui_window_overlayCursorNwse = NULL;
    }
    if (e9ui_window_overlayCursorNesw) {
        SDL_FreeCursor(e9ui_window_overlayCursorNesw);
        e9ui_window_overlayCursorNesw = NULL;
    }
    if (e9ui_window_overlayCloseIcon) {
        SDL_DestroyTexture(e9ui_window_overlayCloseIcon);
        e9ui_window_overlayCloseIcon = NULL;
    }
    e9ui_window_overlayCloseIconW = 0;
    e9ui_window_overlayCloseIconH = 0;
}

static void
e9ui_window_overlayRunDeferredClose(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9ui_window_deferred_close_call_t *call = (e9ui_window_deferred_close_call_t *)user;
    if (!call) {
        return;
    }
    if (call->onClose) {
        call->onClose(call->owner, call->onCloseUser);
    }
    alloc_free(call);
}

static void
e9ui_window_overlayNotifyCloseDeferred(e9ui_context_t *ctx, e9ui_window_overlay_state_t *st)
{
    if (!ctx || !st || !st->onClose) {
        return;
    }
    e9ui_window_close_cb_t onClose = st->onClose;
    void *onCloseUser = st->onCloseUser;
    e9ui_window_t *owner = st->owner;
    st->onClose = NULL;
    e9ui_window_deferred_close_call_t *call =
        (e9ui_window_deferred_close_call_t *)alloc_calloc(1, sizeof(*call));
    if (!call) {
        onClose(owner, onCloseUser);
        return;
    }
    call->owner = owner;
    call->onClose = onClose;
    call->onCloseUser = onCloseUser;
    if (!e9ui_defer(ctx, e9ui_window_overlayRunDeferredClose, call)) {
        alloc_free(call);
        onClose(owner, onCloseUser);
    }
}

static SDL_Cursor *
e9ui_window_overlayResizeCursorForMask(int resizeMask)
{
    int horiz = resizeMask & (E9UI_WINDOW_OVERLAY_RESIZE_LEFT | E9UI_WINDOW_OVERLAY_RESIZE_RIGHT);
    int vert = resizeMask & (E9UI_WINDOW_OVERLAY_RESIZE_TOP | E9UI_WINDOW_OVERLAY_RESIZE_BOTTOM);
    if (horiz && vert) {
        int leftTop = (resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_LEFT) && (resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_TOP);
        int rightBottom = (resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_RIGHT) && (resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_BOTTOM);
        if (leftTop || rightBottom) {
            return e9ui_window_overlayCursorNwse;
        }
        return e9ui_window_overlayCursorNesw;
    }
    if (horiz) {
        return e9ui_window_overlayCursorEw;
    }
    if (vert) {
        return e9ui_window_overlayCursorNs;
    }
    return e9ui_window_overlayCursorArrow;
}

static SDL_Texture *
e9ui_window_overlayGetCloseIcon(SDL_Renderer *renderer, int *outW, int *outH)
{
    if (!renderer) {
        return NULL;
    }
    if (e9ui_window_overlayCloseIcon) {
        if (outW) {
            *outW = e9ui_window_overlayCloseIconW;
        }
        if (outH) {
            *outH = e9ui_window_overlayCloseIconH;
        }
        return e9ui_window_overlayCloseIcon;
    }
    char path[PATH_MAX];
    if (!file_getAssetPath("assets/icons/close.png", path, sizeof(path))) {
        return NULL;
    }
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        debug_error("e9ui_window: failed to load close icon %s: %s", path, IMG_GetError());
        return NULL;
    }
    e9ui_window_overlayCloseIcon = SDL_CreateTextureFromSurface(renderer, s);
    e9ui_window_overlayCloseIconW = s->w;
    e9ui_window_overlayCloseIconH = s->h;
    SDL_FreeSurface(s);
    if (outW) {
        *outW = e9ui_window_overlayCloseIconW;
    }
    if (outH) {
        *outH = e9ui_window_overlayCloseIconH;
    }
    return e9ui_window_overlayCloseIcon;
}

static int
e9ui_window_overlayIsForeground(const e9ui_component_t *self)
{
    if (!self || !e9ui) {
        return 0;
    }
    e9ui_component_t *hostRoot = e9ui_getOverlayHost();
    if (!hostRoot) {
        hostRoot = e9ui->root;
    }
    if (!hostRoot || !hostRoot->children) {
        return 0;
    }
    list_t *last = list_last(hostRoot->children);
    if (!last || !last->data) {
        return 0;
    }
    e9ui_component_child_t *container = (e9ui_component_child_t *)last->data;
    return (container && container->component == self) ? 1 : 0;
}

static Uint8
e9ui_window_overlayLighten(Uint8 value, Uint8 amount)
{
    int out = (int)value + (int)amount;
    if (out > 255) {
        out = 255;
    }
    return (Uint8)out;
}

static int
e9ui_window_overlayTitlebarHeight(e9ui_context_t *ctx)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    int textH = font ? TTF_FontHeight(font) : 16;
    if (textH <= 0) {
        textH = 16;
    }
    int padY = e9ui_scale_px(ctx, 4);
    return textH + padY * 2;
}

static int
e9ui_window_overlayPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
e9ui_window_overlayLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)bounds;
    if (!self || !self->state) {
        return;
    }
    e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)self->state;
    e9ui_window_overlaySyncMaximizedRect(st, ctx);
    self->bounds = st->rect;
    int titleH = e9ui_window_overlayTitlebarHeight(ctx);
    int frameInset = e9ui_scale_px(ctx, 4);
    int bodyX = st->rect.x + frameInset;
    int bodyW = st->rect.w - frameInset * 2;
    int bodyY = st->rect.y + titleH;
    int bodyH = st->rect.h - titleH - frameInset;
    if (bodyW < 0) {
        bodyW = 0;
    }
    if (bodyH < 0) {
        bodyH = 0;
    }
    if (st->body && st->body->layout) {
        st->body->layout(st->body, ctx, (e9ui_rect_t){ bodyX, bodyY, bodyW, bodyH });
    }
}

static void
e9ui_window_overlayDrawTitlebar(e9ui_component_t *self, e9ui_window_overlay_state_t *st, e9ui_context_t *ctx, SDL_Rect rect)
{
    const e9k_theme_titlebar_t *theme = &e9ui->theme.titlebar;
    SDL_Color titleBackground = theme->background;
    if (e9ui_window_overlayIsForeground(self)) {
        titleBackground.r = e9ui_window_overlayLighten(titleBackground.r, 10);
        titleBackground.g = e9ui_window_overlayLighten(titleBackground.g, 10);
        titleBackground.b = e9ui_window_overlayLighten(titleBackground.b, 10);
    }
    SDL_SetRenderDrawColor(ctx->renderer,
                           titleBackground.r,
                           titleBackground.g,
                           titleBackground.b,
                           titleBackground.a);
    SDL_RenderFillRect(ctx->renderer, &rect);

    int closePad = e9ui_scale_px(ctx, 6);
    int closeSize = rect.h - closePad * 2;
    if (closeSize < e9ui_scale_px(ctx, 12)) {
        closeSize = e9ui_scale_px(ctx, 12);
    }
    int closeX = rect.x + rect.w - closePad - closeSize;
    int closeY = rect.y + (rect.h - closeSize) / 2;
    st->closeRect = (SDL_Rect){ closeX, closeY, closeSize, closeSize };

    int padX = e9ui_scale_px(ctx, 8);
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    if (font && st->title[0]) {
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->title, theme->text, &tw, &th);
        if (tex) {
            int textY = rect.y + (rect.h - th) / 2;
            if (textY < rect.y) {
                textY = rect.y;
            }
            int titleRightGap = e9ui_scale_px(ctx, 8);
            int maxTextW = (closeX - titleRightGap) - (rect.x + padX);
            if (maxTextW > 0) {
                SDL_Rect dst = { rect.x + padX, textY, tw, th };
                if (dst.w > maxTextW) {
                    SDL_Rect src = { 0, 0, maxTextW, th };
                    dst.w = maxTextW;
                    SDL_RenderCopy(ctx->renderer, tex, &src, &dst);
                } else {
                    SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
                }
            }
        }
    }
    SDL_Texture *icon = e9ui_window_overlayGetCloseIcon(ctx->renderer, NULL, NULL);
    if (icon) {
        SDL_Rect dst = st->closeRect;
        SDL_RenderCopy(ctx->renderer, icon, NULL, &dst);
    } else {
        SDL_SetRenderDrawColor(ctx->renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(ctx->renderer, &st->closeRect);
        SDL_RenderDrawLine(ctx->renderer, closeX + 3, closeY + 3, closeX + closeSize - 4, closeY + closeSize - 4);
        SDL_RenderDrawLine(ctx->renderer, closeX + closeSize - 4, closeY + 3, closeX + 3, closeY + closeSize - 4);
    }
}

static void
e9ui_window_overlayRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }
    e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)self->state;
    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    int hadClip = SDL_RenderIsClipEnabled(ctx->renderer) ? 1 : 0;
    SDL_Rect prevClip = { 0, 0, 0, 0 };
    if (hadClip) {
        SDL_RenderGetClipRect(ctx->renderer, &prevClip);
        SDL_Rect clipped = bg;
        if (SDL_IntersectRect(&prevClip, &bg, &clipped)) {
            SDL_RenderSetClipRect(ctx->renderer, &clipped);
        } else {
            SDL_RenderSetClipRect(ctx->renderer, &bg);
        }
    } else {
        SDL_RenderSetClipRect(ctx->renderer, &bg);
    }
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);

    SDL_Rect titleRect = { self->bounds.x, self->bounds.y, self->bounds.w, e9ui_window_overlayTitlebarHeight(ctx) };
    e9ui_window_overlayDrawTitlebar(self, st, ctx, titleRect);
    if (st->body && st->body->render) {
        st->body->render(st->body, ctx);
    }
    SDL_SetRenderDrawColor(ctx->renderer, 70, 70, 70, 255);
    SDL_RenderDrawRect(ctx->renderer, &bg);
    if (hadClip) {
        SDL_RenderSetClipRect(ctx->renderer, &prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

static int
e9ui_window_overlayPointInRect(const SDL_Rect *rect, int x, int y)
{
    if (!rect) {
        return 0;
    }
    return x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

static int
e9ui_window_overlayPointInE9Rect(const e9ui_rect_t *rect, int x, int y)
{
    if (!rect) {
        return 0;
    }
    return x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

static void
e9ui_window_overlayRaiseToFront(e9ui_component_t *self)
{
    if (!self || !e9ui) {
        return;
    }
    e9ui_component_t *hostRoot = e9ui_getOverlayHost();
    if (!hostRoot) {
        hostRoot = e9ui->root;
    }
    if (!hostRoot) {
        return;
    }
    e9ui_component_child_t *container = NULL;
    for (list_t *ptr = hostRoot->children; ptr; ptr = ptr->next) {
        e9ui_component_child_t *it = (e9ui_component_child_t *)ptr->data;
        if (it && it->component == self) {
            container = it;
            break;
        }
    }
    if (!container) {
        return;
    }
    list_t *last = list_last(hostRoot->children);
    if (last && last->data == container) {
        return;
    }
    list_remove(&hostRoot->children, container, 0);
    list_append(&hostRoot->children, container);
}

static int
e9ui_window_componentContainsComponent(const e9ui_component_t *root, const e9ui_component_t *needle)
{
    if (!root || !needle) {
        return 0;
    }
    if (root == needle) {
        return 1;
    }
    for (list_t *ptr = root->children; ptr; ptr = ptr->next) {
        e9ui_component_child_t *container = (e9ui_component_child_t *)ptr->data;
        if (!container || !container->component) {
            continue;
        }
        if (e9ui_window_componentContainsComponent(container->component, needle)) {
            return 1;
        }
    }
    return 0;
}

static int
e9ui_window_overlayResizeMaskAt(const e9ui_window_overlay_state_t *st, const e9ui_context_t *ctx, int x, int y)
{
    if (!st || !ctx) {
        return 0;
    }
    if (!e9ui_window_overlayPointInE9Rect(&st->rect, x, y)) {
        return 0;
    }
    int hit = e9ui_scale_px(ctx, 6);
    if (hit < 2) {
        hit = 2;
    }
    int left = (x - st->rect.x) <= hit ? 1 : 0;
    int right = (st->rect.x + st->rect.w - 1 - x) <= hit ? 1 : 0;
    int top = (y - st->rect.y) <= hit ? 1 : 0;
    int bottom = (st->rect.y + st->rect.h - 1 - y) <= hit ? 1 : 0;
    int mask = 0;
    if (left) {
        mask |= E9UI_WINDOW_OVERLAY_RESIZE_LEFT;
    }
    if (right) {
        mask |= E9UI_WINDOW_OVERLAY_RESIZE_RIGHT;
    }
    if (top) {
        mask |= E9UI_WINDOW_OVERLAY_RESIZE_TOP;
    }
    if (bottom) {
        mask |= E9UI_WINDOW_OVERLAY_RESIZE_BOTTOM;
    }
    return mask;
}

static void
e9ui_window_overlayClampRectToBounds(e9ui_rect_t *rect, const e9ui_context_t *ctx, int minW, int minH)
{
    if (!rect) {
        return;
    }
    if (rect->w < minW) {
        rect->w = minW;
    }
    if (rect->h < minH) {
        rect->h = minH;
    }
    if (rect->x < 0) {
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->y = 0;
    }
    if (ctx && ctx->winW > 0 && rect->w > ctx->winW) {
        rect->w = ctx->winW;
    }
    if (ctx && ctx->winH > 0 && rect->h > ctx->winH) {
        rect->h = ctx->winH;
    }
    if (ctx && ctx->winW > 0 && rect->x + rect->w > ctx->winW) {
        rect->x = ctx->winW - rect->w;
    }
    if (ctx && ctx->winH > 0 && rect->y + rect->h > ctx->winH) {
        rect->y = ctx->winH - rect->h;
    }
    if (rect->x < 0) {
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->y = 0;
    }
}

static void
e9ui_window_overlaySyncMaximizedRect(e9ui_window_overlay_state_t *st, const e9ui_context_t *ctx)
{
    if (!st || !ctx || !st->maximized) {
        return;
    }
    int minW = e9ui_scale_px(ctx, (st->minWidthPx > 0) ? st->minWidthPx : 360);
    int minH = e9ui_scale_px(ctx, (st->minHeightPx > 0) ? st->minHeightPx : 320);
    st->rect = (e9ui_rect_t){ 0, 0, ctx->winW, ctx->winH };
    e9ui_window_overlayClampRectToBounds(&st->rect, ctx, minW, minH);
}

static void
e9ui_window_overlayToggleMaximized(e9ui_window_overlay_state_t *st, const e9ui_context_t *ctx)
{
    if (!st || !ctx) {
        return;
    }
    int minW = e9ui_scale_px(ctx, (st->minWidthPx > 0) ? st->minWidthPx : 360);
    int minH = e9ui_scale_px(ctx, (st->minHeightPx > 0) ? st->minHeightPx : 320);
    if (st->maximized) {
        st->maximized = 0;
        if (st->restoreRectValid) {
            st->rect = st->restoreRect;
        }
        e9ui_window_overlayClampRectToBounds(&st->rect, ctx, minW, minH);
        return;
    }
    st->restoreRect = st->rect;
    st->restoreRectValid = 1;
    st->maximized = 1;
    e9ui_window_overlaySyncMaximizedRect(st, ctx);
}

static void
e9ui_window_overlayExitMaximizedForManualChange(e9ui_window_overlay_state_t *st)
{
    if (!st || !st->maximized) {
        return;
    }
    st->maximized = 0;
}

static void
e9ui_window_overlayApplyResizeDrag(e9ui_window_overlay_state_t *st, const e9ui_context_t *ctx, int mouseX, int mouseY)
{
    if (!st || !ctx || !st->resizing || !st->resizeMask) {
        return;
    }
    int minW = e9ui_scale_px(ctx, (st->minWidthPx > 0) ? st->minWidthPx : 360);
    int minH = e9ui_scale_px(ctx, (st->minHeightPx > 0) ? st->minHeightPx : 320);
    int dx = mouseX - st->resizeStartMouseX;
    int dy = mouseY - st->resizeStartMouseY;

    int left = st->dragStartRect.x;
    int top = st->dragStartRect.y;
    int right = st->dragStartRect.x + st->dragStartRect.w;
    int bottom = st->dragStartRect.y + st->dragStartRect.h;

    if (st->resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_LEFT) {
        left = st->dragStartRect.x + dx;
    }
    if (st->resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_RIGHT) {
        right = st->dragStartRect.x + st->dragStartRect.w + dx;
    }
    if (st->resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_TOP) {
        top = st->dragStartRect.y + dy;
    }
    if (st->resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_BOTTOM) {
        bottom = st->dragStartRect.y + st->dragStartRect.h + dy;
    }

    if (ctx->winW > 0) {
        if (left < 0) {
            left = 0;
        }
        if (right > ctx->winW) {
            right = ctx->winW;
        }
    }
    if (ctx->winH > 0) {
        if (top < 0) {
            top = 0;
        }
        if (bottom > ctx->winH) {
            bottom = ctx->winH;
        }
    }

    if (right - left < minW) {
        if (st->resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_LEFT) {
            left = right - minW;
        } else {
            right = left + minW;
        }
    }
    if (bottom - top < minH) {
        if (st->resizeMask & E9UI_WINDOW_OVERLAY_RESIZE_TOP) {
            top = bottom - minH;
        } else {
            bottom = top + minH;
        }
    }

    if (ctx->winW > 0) {
        if (left < 0) {
            left = 0;
            if (right - left < minW) {
                right = left + minW;
            }
        }
        if (right > ctx->winW) {
            right = ctx->winW;
            if (right - left < minW) {
                left = right - minW;
            }
        }
    }
    if (ctx->winH > 0) {
        if (top < 0) {
            top = 0;
            if (bottom - top < minH) {
                bottom = top + minH;
            }
        }
        if (bottom > ctx->winH) {
            bottom = ctx->winH;
            if (bottom - top < minH) {
                top = bottom - minH;
            }
        }
    }

    st->rect.x = left;
    st->rect.y = top;
    st->rect.w = right - left;
    st->rect.h = bottom - top;
    e9ui_window_overlayClampRectToBounds(&st->rect, ctx, minW, minH);
}

static void
e9ui_window_overlayUpdateCursor(e9ui_component_t *self,
                                 e9ui_window_overlay_state_t *st,
                                 e9ui_context_t *ctx,
                                 int mx,
                                 int my)
{
    if (!self || !st || !ctx) {
        return;
    }
    e9ui_window_overlayEnsureCursors();
    int titleH = e9ui_window_overlayTitlebarHeight(ctx);
    SDL_Rect titleRect = { self->bounds.x, self->bounds.y, self->bounds.w, titleH };
    int hoverClose = e9ui_window_overlayPointInRect(&st->closeRect, mx, my);
    int hoverResizeMask = hoverClose ? 0 : e9ui_window_overlayResizeMaskAt(st, ctx, mx, my);
    int hoverTitle = e9ui_window_overlayPointInRect(&titleRect, mx, my) ? 1 : 0;
    if (hoverTitle && (hoverClose || hoverResizeMask)) {
        hoverTitle = 0;
    }
    SDL_Cursor *cursor = NULL;
    if (st->resizing) {
        cursor = e9ui_window_overlayResizeCursorForMask(st->resizeMask);
    } else if (st->dragging) {
        cursor = e9ui_window_overlayCursorMove;
    } else if (hoverClose) {
        cursor = e9ui_window_overlayCursorHand ? e9ui_window_overlayCursorHand : e9ui_window_overlayCursorArrow;
    } else if (hoverResizeMask) {
        cursor = e9ui_window_overlayResizeCursorForMask(hoverResizeMask);
    } else if (hoverTitle) {
        cursor = e9ui_window_overlayCursorMove ? e9ui_window_overlayCursorMove : e9ui_window_overlayCursorArrow;
    } else if (e9ui_window_overlayPointInE9Rect(&st->rect, mx, my)) {
        cursor = e9ui_window_overlayCursorArrow;
    }
    if (cursor) {
        if (st->resizing || st->dragging) {
            e9ui_cursorCapture(ctx, self, cursor);
        } else {
            e9ui_cursorRequest(ctx, self, cursor);
        }
    }
}

static int
e9ui_window_overlayHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !self->state || !ctx || !ev) {
        return 0;
    }
    e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)self->state;
    int titleH = e9ui_window_overlayTitlebarHeight(ctx);
    SDL_Rect titleRect = { self->bounds.x, self->bounds.y, self->bounds.w, titleH };
    if (ev->type == SDL_MOUSEMOTION) {
        e9ui_window_overlayUpdateCursor(self, st, ctx, ev->motion.x, ev->motion.y);
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        if (e9ui_window_overlayPointInE9Rect(&st->rect, mx, my)) {
            e9ui_window_overlayRaiseToFront(self);
        }
        if (e9ui_window_overlayPointInRect(&st->closeRect, mx, my)) {
            if (st->onClose) {
                e9ui_window_overlayNotifyCloseDeferred(ctx, st);
            }
            return 1;
        }
        if (e9ui_window_overlayPointInRect(&titleRect, mx, my) && ev->button.clicks >= 2) {
            st->dragging = 0;
            st->resizing = 0;
            st->resizeMask = 0;
            e9ui_cursorRelease(ctx, self);
            e9ui_window_overlayToggleMaximized(st, ctx);
            e9ui_window_overlayUpdateCursor(self, st, ctx, mx, my);
            return 1;
        }
        int resizeMask = e9ui_window_overlayResizeMaskAt(st, ctx, mx, my);
        if (resizeMask) {
            st->resizing = 1;
            st->resizeMask = resizeMask;
            st->resizeStartMouseX = mx;
            st->resizeStartMouseY = my;
            st->dragStartRect = st->rect;
            st->dragging = 0;
            e9ui_window_overlayUpdateCursor(self, st, ctx, mx, my);
            return 1;
        }
        if (e9ui_window_overlayPointInRect(&titleRect, mx, my)) {
            st->dragging = 1;
            st->dragStartRect = st->rect;
            st->dragStartMouseX = mx;
            st->dragStartMouseY = my;
            st->dragOffsetX = mx - st->rect.x;
            st->dragOffsetY = my - st->rect.y;
            e9ui_window_overlayUpdateCursor(self, st, ctx, mx, my);
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (st->resizing) {
            st->resizing = 0;
            st->resizeMask = 0;
            e9ui_cursorRelease(ctx, self);
            e9ui_window_overlayUpdateCursor(self, st, ctx, ev->button.x, ev->button.y);
            return 1;
        }
        if (st->dragging) {
            st->dragging = 0;
            e9ui_cursorRelease(ctx, self);
            e9ui_window_overlayUpdateCursor(self, st, ctx, ev->button.x, ev->button.y);
            return 1;
        }
        e9ui_window_overlayUpdateCursor(self, st, ctx, ev->button.x, ev->button.y);
    }
    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode key = ev->key.keysym.sym;
        SDL_Keymod mods = ev->key.keysym.mod;
        int accel = (mods & KMOD_GUI) || (mods & KMOD_CTRL);
        if (!accel && key == SDLK_TAB) {
            e9ui_component_t *focus = e9ui_getFocus(ctx);
            int focusInWindow = (focus && e9ui_window_componentContainsComponent(self, focus)) ? 1 : 0;
            int reverse = (mods & KMOD_SHIFT) ? 1 : 0;
            e9ui_component_t *next = e9ui_focusFindNext(self, focusInWindow ? focus : NULL, reverse);
            if (next) {
                e9ui_setFocus(ctx, next);
                return 1;
            }
        }
    }
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) {
        e9ui_component_t *focus = e9ui_getFocus(ctx);
        if (focus && e9ui_window_componentContainsComponent(self, focus)) {
            if (st->closeOnEscape && st->onClose) {
                e9ui_window_overlayNotifyCloseDeferred(ctx, st);
            }
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEMOTION && st->resizing) {
        if (st->maximized) {
            int dx = ev->motion.x - st->resizeStartMouseX;
            int dy = ev->motion.y - st->resizeStartMouseY;
            int threshold = e9ui_window_overlayManualChangeThresholdPx(ctx);

            if (abs(dx) < threshold && abs(dy) < threshold) {
                return 1;
            }
        }
        e9ui_window_overlayExitMaximizedForManualChange(st);
        e9ui_window_overlayApplyResizeDrag(st, ctx, ev->motion.x, ev->motion.y);
        return 1;
    }
    if (ev->type == SDL_MOUSEMOTION && st->dragging) {
        if (st->maximized) {
            int dx = ev->motion.x - st->dragStartMouseX;
            int dy = ev->motion.y - st->dragStartMouseY;
            int threshold = e9ui_window_overlayManualChangeThresholdPx(ctx);

            if (abs(dx) < threshold && abs(dy) < threshold) {
                return 1;
            }
        }
        e9ui_window_overlayExitMaximizedForManualChange(st);
        int nextX = ev->motion.x - st->dragOffsetX;
        int nextY = ev->motion.y - st->dragOffsetY;
        st->rect.x = nextX;
        st->rect.y = nextY;
        return 1;
    }
    if (ev->type == SDL_MOUSEMOTION) {
        if (e9ui_window_overlayPointInE9Rect(&st->rect, ev->motion.x, ev->motion.y)) {
            return 1;
        }
    } else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
        if (e9ui_window_overlayPointInE9Rect(&st->rect, ev->button.x, ev->button.y)) {
            return 1;
        }
    } else if (ev->type == SDL_MOUSEWHEEL) {
        if (e9ui_window_overlayPointInE9Rect(&st->rect, ev->wheel.mouseX, ev->wheel.mouseY)) {
            return 1;
        }
    }
    return 0;
}

static int
e9ui_window_dispatchEmbeddedKeydownRecursive(e9ui_component_t *comp, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!comp || e9ui_getHidden(comp)) {
        return 0;
    }
    e9ui_child_reverse_iterator iter;
    if (e9ui_child_iterateChildrenReverse(comp, &iter)) {
        for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
             it;
             it = e9ui_child_iteratePrev(&iter)) {
            if (!it->child) {
                continue;
            }
            if (e9ui_window_dispatchEmbeddedKeydownRecursive(it->child, ctx, ev)) {
                return 1;
            }
        }
    }
    if (comp->name &&
        strcmp(comp->name, "e9ui_window_overlay") == 0 &&
        comp->handleEvent) {
        if (comp->handleEvent(comp, ctx, ev)) {
            return 1;
        }
    }
    return 0;
}

static void
e9ui_window_overlayDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)self->state;
    if (st->owner) {
        st->owner->windowComp = NULL;
        st->owner->open = 0;
    }
}

static e9ui_component_t *
e9ui_window_overlayMake(e9ui_window_t *owner,
                         const char *title,
                         e9ui_rect_t rect,
                         e9ui_component_t *body,
                         e9ui_window_close_cb_t onClose,
                         void *onCloseUser)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    if (!comp) {
        return NULL;
    }
    e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }
    st->owner = owner;
    st->rect = rect;
    st->body = body;
    st->minWidthPx = owner ? owner->minWidthPx : 0;
    st->minHeightPx = owner ? owner->minHeightPx : 0;
    st->closeOnEscape = owner ? owner->closeOnEscape : 1;
    st->onClose = onClose;
    st->onCloseUser = onCloseUser;
    if (title && *title) {
        strncpy(st->title, title, sizeof(st->title) - 1);
        st->title[sizeof(st->title) - 1] = '\0';
    }
    comp->name = "e9ui_window_overlay";
    comp->state = st;
    comp->preferredHeight = e9ui_window_overlayPreferredHeight;
    comp->layout = e9ui_window_overlayLayout;
    comp->render = e9ui_window_overlayRender;
    comp->handleEvent = e9ui_window_overlayHandleEvent;
    comp->dtor = e9ui_window_overlayDtor;
    if (body) {
        e9ui_child_add(comp, body, alloc_strdup("window_body"));
    }
    return comp;
}

e9ui_window_t *
e9ui_windowCreate(e9ui_window_backend_t backend)
{
    if (backend != e9ui_window_backend_overlay) {
        return NULL;
    }
    e9ui_window_t *window = (e9ui_window_t *)alloc_calloc(1, sizeof(*window));
    if (!window) {
        return NULL;
    }
    window->backend = backend;
    window->closeOnEscape = 1;
    return window;
}

void
e9ui_windowSetMinSize(e9ui_window_t *window, int minWidthPx, int minHeightPx)
{
    if (!window) {
        return;
    }
    window->minWidthPx = (minWidthPx > 0) ? minWidthPx : 0;
    window->minHeightPx = (minHeightPx > 0) ? minHeightPx : 0;
    if (window->windowComp && window->windowComp->state &&
        window->backend == e9ui_window_backend_overlay) {
        e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)window->windowComp->state;
        st->minWidthPx = window->minWidthPx;
        st->minHeightPx = window->minHeightPx;
    }
}

void
e9ui_windowSetCloseOnEscape(e9ui_window_t *window, int closeOnEscape)
{
    if (!window) {
        return;
    }
    window->closeOnEscape = closeOnEscape ? 1 : 0;
    if (window->windowComp && window->windowComp->state &&
        window->backend == e9ui_window_backend_overlay) {
        e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)window->windowComp->state;
        st->closeOnEscape = window->closeOnEscape;
    }
}

void
e9ui_windowDestroy(e9ui_window_t *window)
{
    if (!window) {
        return;
    }
    e9ui_windowClose(window);
    alloc_free(window);
}

int
e9ui_windowOpen(e9ui_window_t *window,
                const char *title,
                e9ui_rect_t rect,
                e9ui_component_t *body,
                e9ui_window_close_cb_t onClose,
                void *onCloseUser,
                e9ui_context_t *ctx)
{
    if (!window || window->backend != e9ui_window_backend_overlay || !ctx || !e9ui) {
        return 0;
    }
    e9ui_component_t *hostRoot = e9ui_getOverlayHost();
    if (!hostRoot) {
        hostRoot = e9ui->root;
    }
    if (!hostRoot) {
        return 0;
    }
    if (window->windowComp) {
        return 1;
    }
    e9ui_component_t *host = e9ui_window_overlayMake(window, title, rect, body, onClose, onCloseUser);
    if (!host) {
        return 0;
    }
    if (hostRoot->name && strcmp(hostRoot->name, "e9ui_stack") == 0) {
        e9ui_stack_addFixed(hostRoot, host);
    } else {
        e9ui_child_add(hostRoot, host, alloc_strdup("e9ui_window_overlay"));
    }
    window->windowComp = host;
    window->open = 1;
    if (ctx->blockingWindowOpened) {
        ctx->blockingWindowOpened(ctx);
    }
    return 1;
}

void
e9ui_windowClose(e9ui_window_t *window)
{
    if (!window) {
        return;
    }
    if (window->windowComp && e9ui && e9ui->ctx.window) {
        e9ui_component_t *hostRoot = e9ui_getOverlayHost();
        e9ui_component_t *removeParent = NULL;
        if (hostRoot) {
            for (list_t *ptr = hostRoot->children; ptr; ptr = ptr->next) {
                e9ui_component_child_t *container = (e9ui_component_child_t *)ptr->data;
                if (container && container->component == window->windowComp) {
                    removeParent = hostRoot;
                    break;
                }
            }
        }
        if (!removeParent) {
            removeParent = e9ui->root;
        }
        if (removeParent) {
            if (removeParent->name && strcmp(removeParent->name, "e9ui_stack") == 0) {
                e9ui_stack_remove(removeParent, &e9ui->ctx, window->windowComp);
            } else {
                e9ui_childRemove(removeParent, window->windowComp, &e9ui->ctx);
            }
        }
        window->windowComp = NULL;
    }
    window->open = 0;
}

void
e9ui_windowCloseAllOverlay(void)
{
    if (!e9ui) {
        return;
    }
    e9ui_component_t *hostRoot = e9ui_getOverlayHost();
    if (!hostRoot) {
        hostRoot = e9ui->root;
    }
    if (!hostRoot) {
        return;
    }
    for (;;) {
        e9ui_component_t *host = NULL;
        e9ui_child_reverse_iterator iter;
        if (!e9ui_child_iterateChildrenReverse(hostRoot, &iter)) {
            break;
        }
        for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
             it;
             it = e9ui_child_iteratePrev(&iter)) {
            if (!it->child || !it->child->name) {
                continue;
            }
            if (strcmp(it->child->name, "e9ui_window_overlay") == 0) {
                host = it->child;
                break;
            }
        }
        if (!host) {
            break;
        }
        e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)host->state;
        if (st && st->owner) {
            e9ui_windowClose(st->owner);
        } else {
            if (hostRoot->name && strcmp(hostRoot->name, "e9ui_stack") == 0) {
                e9ui_stack_remove(hostRoot, &e9ui->ctx, host);
            } else {
                e9ui_childRemove(hostRoot, host, &e9ui->ctx);
            }
        }
    }
}

int
e9ui_windowCloseTopOverlay(void)
{
    if (!e9ui) {
        return 0;
    }
    e9ui_component_t *hostRoot = e9ui_getOverlayHost();
    if (!hostRoot) {
        hostRoot = e9ui->root;
    }
    if (!hostRoot) {
        return 0;
    }
    e9ui_child_reverse_iterator iter;
    if (!e9ui_child_iterateChildrenReverse(hostRoot, &iter)) {
        return 0;
    }
    for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
         it;
         it = e9ui_child_iteratePrev(&iter)) {
        if (!it->child || !it->child->name) {
            continue;
        }
        if (strcmp(it->child->name, "e9ui_window_overlay") != 0) {
            continue;
        }
        e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)it->child->state;
        if (st && !st->closeOnEscape) {
            return 0;
        }
        if (st && st->onClose) {
            e9ui_window_overlayNotifyCloseDeferred(&e9ui->ctx, st);
            return 1;
        }
        if (st && st->owner) {
            e9ui_windowClose(st->owner);
            return 1;
        }
        if (hostRoot->name && strcmp(hostRoot->name, "e9ui_stack") == 0) {
            e9ui_stack_remove(hostRoot, &e9ui->ctx, it->child);
        } else {
            e9ui_childRemove(hostRoot, it->child, &e9ui->ctx);
        }
        return 1;
    }
    return 0;
}

int
e9ui_windowIsOpen(const e9ui_window_t *window)
{
    if (!window) {
        return 0;
    }
    return window->open ? 1 : 0;
}

e9ui_rect_t
e9ui_windowGetRect(const e9ui_window_t *window)
{
    e9ui_rect_t rect = { 0, 0, 0, 0 };
    if (!window || !window->windowComp || !window->windowComp->state) {
        return rect;
    }
    e9ui_window_overlay_state_t *st = (e9ui_window_overlay_state_t *)window->windowComp->state;
    return st->rect;
}

int
e9ui_windowCaptureRectToInts(const e9ui_window_t *window,
                             const e9ui_context_t *ctx,
                             int *outX,
                             int *outY,
                             int *outW,
                             int *outH)
{
    if (!window || !ctx || !outX || !outY || !outW || !outH) {
        return 0;
    }
    e9ui_rect_t rect = e9ui_windowGetRect(window);
    if (rect.w <= 0 || rect.h <= 0) {
        return 0;
    }
    *outX = e9ui_unscale_px(ctx, rect.x);
    *outY = e9ui_unscale_px(ctx, rect.y);
    *outW = e9ui_unscale_px(ctx, rect.w);
    *outH = e9ui_unscale_px(ctx, rect.h);
    return 1;
}

e9ui_rect_t
e9ui_windowRestoreRect(const e9ui_context_t *ctx,
                             e9ui_rect_t defaultRect,
                             int hasPos,
                             int hasSize,
                             int x,
                             int y,
                             int w,
                             int h)
{
    e9ui_rect_t rect = defaultRect;
    if (!ctx) {
        return rect;
    }
    if (hasPos) {
        rect.x = e9ui_scale_px(ctx, x);
        rect.y = e9ui_scale_px(ctx, y);
    }
    if (hasSize && w > 0 && h > 0) {
        rect.w = e9ui_scale_px(ctx, w);
        rect.h = e9ui_scale_px(ctx, h);
    }
    return rect;
}

void
e9ui_windowClampRectSize(e9ui_rect_t *rect,
                         const e9ui_context_t *ctx,
                         int minWidthPx,
                         int minHeightPx)
{
    if (!rect || !ctx) {
        return;
    }
    int minW = e9ui_scale_px(ctx, minWidthPx);
    int minH = e9ui_scale_px(ctx, minHeightPx);
    if (rect->w < minW) {
        rect->w = minW;
    }
    if (rect->h < minH) {
        rect->h = minH;
    }
    if (ctx->winW > 0 && rect->w > ctx->winW) {
        rect->w = ctx->winW;
    }
    if (ctx->winH > 0 && rect->h > ctx->winH) {
        rect->h = ctx->winH;
    }
}

int
e9ui_windowHasSavedPosition(int x, int y)
{
    return (x != E9UI_WINDOW_COORD_UNSET && y != E9UI_WINDOW_COORD_UNSET) ? 1 : 0;
}

int
e9ui_windowHasSavedSize(int w, int h)
{
    return (w > 0 && h > 0) ? 1 : 0;
}

int
e9ui_windowCaptureRectChanged(e9ui_window_t *window,
                              const e9ui_context_t *ctx,
                              int *hasSaved,
                              int *x,
                              int *y,
                              int *w,
                              int *h)
{
    if (!x || !y || !w || !h) {
        return 0;
    }
    int prevHasSaved = hasSaved ? (*hasSaved ? 1 : 0) : 1;
    int prevX = *x;
    int prevY = *y;
    int prevW = *w;
    int prevH = *h;
    if (!e9ui_windowCaptureRectToInts(window, ctx, x, y, w, h)) {
        return 0;
    }
    if (hasSaved) {
        *hasSaved = 1;
    }
    if (hasSaved && !prevHasSaved) {
        return 1;
    }
    if (*x != prevX || *y != prevY || *w != prevW || *h != prevH) {
        return 1;
    }
    return 0;
}

int
e9ui_windowCaptureRectSnapshot(const e9ui_window_t *window,
                               const e9ui_context_t *ctx,
                               int *hasSaved,
                               int *x,
                               int *y,
                               int *w,
                               int *h)
{
    if (!x || !y || !w || !h) {
        return 0;
    }
    if (!e9ui_windowCaptureRectToInts(window, ctx, x, y, w, h)) {
        return 0;
    }
    if (hasSaved) {
        *hasSaved = 1;
    }
    return 1;
}

int
e9ui_windowCaptureStateRectChanged(e9ui_window_state_t *state,
                                   const e9ui_context_t *ctx)
{
    if (!state) {
        return 0;
    }
    return e9ui_windowCaptureRectChanged(state->windowHost,
                                         ctx,
                                         &state->winHasSaved,
                                         &state->winX,
                                         &state->winY,
                                         &state->winW,
                                         &state->winH);
}

int
e9ui_windowCaptureStateRectSnapshot(e9ui_window_state_t *state,
                                    const e9ui_context_t *ctx)
{
    if (!state) {
        return 0;
    }
    return e9ui_windowCaptureRectSnapshot(state->windowHost,
                                          ctx,
                                          &state->winHasSaved,
                                          &state->winX,
                                          &state->winY,
                                          &state->winW,
                                          &state->winH);
}

void
e9ui_windowPersistRect(FILE *file,
                       const char *prefix,
                       const e9ui_window_t *window,
                       const e9ui_context_t *ctx,
                       int *hasSaved,
                       int *x,
                       int *y,
                       int *w,
                       int *h)
{
    if (!file || !prefix || !prefix[0] || !hasSaved || !x || !y || !w || !h) {
        return;
    }
    if (window && ctx) {
        (void)e9ui_windowCaptureRectSnapshot(window, ctx, hasSaved, x, y, w, h);
    }
    *hasSaved = e9ui_windowHasSavedPosition(*x, *y);
    if (!*hasSaved || !e9ui_windowHasSavedSize(*w, *h)) {
        return;
    }
    fprintf(file, "%s.win_x=%d\n", prefix, *x);
    fprintf(file, "%s.win_y=%d\n", prefix, *y);
    fprintf(file, "%s.win_w=%d\n", prefix, *w);
    fprintf(file, "%s.win_h=%d\n", prefix, *h);
}

void
e9ui_windowPersistStateRect(FILE *file,
                            const char *prefix,
                            e9ui_window_state_t *state,
                            const e9ui_context_t *ctx)
{
    if (!state) {
        return;
    }
    e9ui_windowPersistRect(file,
                           prefix,
                           state->open ? state->windowHost : NULL,
                           ctx,
                           &state->winHasSaved,
                           &state->winX,
                           &state->winY,
                           &state->winW,
                           &state->winH);
}

e9ui_rect_t
e9ui_windowResolveOpenRect(const e9ui_context_t *ctx,
                           e9ui_rect_t defaultRect,
                           int minWidthPx,
                           int minHeightPx,
                           int centerWhenNoSaved,
                           int x,
                           int y,
                           int w,
                           int h)
{
    int hasPos = e9ui_windowHasSavedPosition(x, y);
    int hasSize = e9ui_windowHasSavedSize(w, h);
    e9ui_rect_t rect = e9ui_windowRestoreRect(ctx, defaultRect, hasPos, hasSize, x, y, w, h);
    if (minWidthPx > 0 || minHeightPx > 0) {
        e9ui_windowClampRectSize(&rect, ctx, minWidthPx, minHeightPx);
    }
    if (centerWhenNoSaved && !hasPos && ctx) {
        int winW = ctx->winW > 0 ? ctx->winW : 1280;
        int winH = ctx->winH > 0 ? ctx->winH : 720;
        rect.x = (winW - rect.w) / 2;
        rect.y = (winH - rect.h) / 2;
    }
    return rect;
}

e9ui_rect_t
e9ui_windowResolveStateOpenRect(const e9ui_context_t *ctx,
                                e9ui_rect_t defaultRect,
                                const e9ui_window_state_t *state)
{
    int x = E9UI_WINDOW_COORD_UNSET;
    int y = E9UI_WINDOW_COORD_UNSET;
    int w = 0;
    int h = 0;
    int minWidthPx = 0;
    int minHeightPx = 0;
    int centerWhenNoSaved = 0;
    if (state) {
        x = state->winX;
        y = state->winY;
        w = state->winW;
        h = state->winH;
        minWidthPx = state->openMinWidthPx;
        minHeightPx = state->openMinHeightPx;
        centerWhenNoSaved = state->openCenterWhenNoSaved ? 1 : 0;
        if (!e9ui_windowHasSavedSize(w, h)) {
            if (state->openMinWidthNoSavedSizePx > minWidthPx) {
                minWidthPx = state->openMinWidthNoSavedSizePx;
            }
            if (state->openMinHeightNoSavedSizePx > minHeightPx) {
                minHeightPx = state->openMinHeightNoSavedSizePx;
            }
        }
    }
    return e9ui_windowResolveOpenRect(ctx,
                                      defaultRect,
                                      minWidthPx,
                                      minHeightPx,
                                      centerWhenNoSaved,
                                      x,
                                      y,
                                      w,
                                      h);
}

int
e9ui_windowDispatchKeydown(e9ui_component_t *root, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!root || !ctx || !ev || ev->type != SDL_KEYDOWN) {
        return 0;
    }
    return e9ui_window_dispatchEmbeddedKeydownRecursive(root, ctx, ev);
}
