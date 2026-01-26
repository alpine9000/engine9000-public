/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>

#include "seek_bar.h"
#include "alloc.h"
#include "state_buffer.h"
#include "e9ui_text_cache.h"

typedef struct seek_bar_state {
    float percent;
    int dragging;
    int margin_left;
    int margin_right;
    int margin_bottom;
    int height;
    int hover_margin;
    float tooltip_scale;
    float tooltip_offset;
    seek_bar_change_cb_t cb;
    void *cb_user;
    seek_bar_drag_cb_t drag_cb;
    void *drag_user;
    char *tooltip_prefix;
    char *tooltip_unit;
    seek_bar_tooltip_cb_t tooltip_cb;
    void *tooltip_user;
} seek_bar_state_t;

static void
seek_bar_updateFromX(seek_bar_state_t *st, int x, const e9ui_rect_t *bounds)
{
    if (!st || !bounds || bounds->w <= 0) {
        return;
    }
    float p = (float)(x - bounds->x) / (float)bounds->w;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    if (p != st->percent) {
        st->percent = p;
        if (st->cb) {
            st->cb(st->percent, st->cb_user);
        }
    }
}

static int
seek_bar_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)ctx;
    if (!self || !self->state || !ev) {
        return 0;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)self->state;
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        int grab = e9ui_scale_px(ctx, 6);
        if (grab < 0) {
            grab = 0;
        }
        if (mx >= self->bounds.x - grab && mx < self->bounds.x + self->bounds.w + grab &&
            my >= self->bounds.y - grab && my < self->bounds.y + self->bounds.h + grab) {
            st->dragging = 1;
            if (st->drag_cb) {
                st->drag_cb(1, st->percent, st->drag_user);
            }
            seek_bar_updateFromX(st, mx, &self->bounds);
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (st->dragging) {
            st->dragging = 0;
            if (st->drag_cb) {
                st->drag_cb(0, st->percent, st->drag_user);
            }
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEMOTION) {
        if (st->dragging) {
            seek_bar_updateFromX(st, ev->motion.x, &self->bounds);
            return 1;
        }
    }
    return 0;
}

static void
seek_bar_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer || !self->state) {
        return;
    }
    if (e9ui_getHidden(self)) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)self->state;
    SDL_Renderer *r = ctx->renderer;
    int x = self->bounds.x;
    int y = self->bounds.y;
    int w = self->bounds.w;
    int h = self->bounds.h;
    if (w <= 0 || h <= 0) {
        return;
    }

    int track_h = h / 3;
    if (track_h < 3) track_h = 3;
    int track_y = y + (h - track_h) / 2;
    SDL_Rect track = { x, track_y, w, track_h };
    SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
    SDL_RenderFillRect(r, &track);

    int filled_w = (int)(st->percent * (float)w);
    if (filled_w < 0) filled_w = 0;
    if (filled_w > w) filled_w = w;
    SDL_Rect filled = { x, track_y, filled_w, track_h };
    SDL_SetRenderDrawColor(r, 230, 33, 23, 255);
    SDL_RenderFillRect(r, &filled);

    int knob_r = h / 2;
    if (knob_r < 6) knob_r = 6;
    int knob_x = x + filled_w;
    int knob_y = y + h / 2;
    SDL_SetRenderDrawColor(r, 250, 250, 250, 255);
    SDL_Rect knob = { knob_x - knob_r, knob_y - knob_r, knob_r * 2, knob_r * 2 };
    SDL_RenderFillRect(r, &knob);

    if (st->dragging) {
        char tip[128];
        tip[0] = '\0';
        if (st->tooltip_cb) {
            st->tooltip_cb(st->percent, tip, sizeof(tip), st->tooltip_user);
        }
        if (!tip[0]) {
            if (st->tooltip_prefix || st->tooltip_unit) {
                float value = st->tooltip_offset + st->percent * st->tooltip_scale;
                const char *prefix = st->tooltip_prefix ? st->tooltip_prefix : "";
                const char *unit = st->tooltip_unit ? st->tooltip_unit : "";
                snprintf(tip, sizeof(tip), "%s%.2f%s", prefix, value, unit);
            } else {
                size_t count = state_buffer_getCount();
                uint64_t frame_no = 0;
                if (count > 0) {
                    state_frame_t *frame = state_buffer_getFrameAtPercent(st->percent);
                    if (frame) {
                        frame_no = frame->frame_no;
                    }
                }
                snprintf(tip, sizeof(tip), "Frame %llu", (unsigned long long)frame_no);
            }
        }
        TTF_Font *font = ctx->font;
        if (font) {
            SDL_Color fg = { 255, 255, 255, 255 };
            int tw = 0, th = 0;
            SDL_Texture *tex = e9ui_text_cache_getText(r, font, tip, fg, &tw, &th);
            if (tex) {
                int pad_x = 6;
                int pad_y = 4;
                int tip_w = tw + pad_x * 2;
                int tip_h = th + pad_y * 2;
                int tip_x = knob_x - tip_w / 2;
                int tip_y = y - tip_h - 6;
                if (tip_x < x) {
                    tip_x = x;
                }
                if (tip_x + tip_w > x + w) {
                    tip_x = x + w - tip_w;
                }
                SDL_Rect tip_bg = { tip_x, tip_y, tip_w, tip_h };
                SDL_SetRenderDrawColor(r, 30, 30, 30, 230);
                SDL_RenderFillRect(r, &tip_bg);
                SDL_Rect tip_dst = { tip_x + pad_x, tip_y + pad_y, tw, th };
                SDL_RenderCopy(r, tex, NULL, &tip_dst);
            }
        }
    }
}

e9ui_component_t *
seek_bar_make(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    st->percent = 1.0f;
    st->margin_left = 100;
    st->margin_right = 100;
    st->margin_bottom = 8;
    st->height = 14;
    st->hover_margin = 18;
    st->tooltip_scale = 1.0f;
    st->tooltip_offset = 0.0f;
    c->name = "seek_bar";
    c->state = st;
    c->render = seek_bar_render;
    c->handleEvent = seek_bar_handleEvent;
    return c;
}

void
seek_bar_setMargins(e9ui_component_t *comp, int left, int right, int bottom)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    st->margin_left = left;
    st->margin_right = right;
    st->margin_bottom = bottom;
}

void
seek_bar_setHeight(e9ui_component_t *comp, int height)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    st->height = height;
}

void
seek_bar_setHoverMargin(e9ui_component_t *comp, int margin)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    st->hover_margin = margin;
}

int
seek_bar_getHoverMargin(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return 0;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    return st->hover_margin;
}

void
seek_bar_setCallback(e9ui_component_t *comp, seek_bar_change_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    st->cb = cb;
    st->cb_user = user;
}

void
seek_bar_setDragCallback(e9ui_component_t *comp, seek_bar_drag_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    st->drag_cb = cb;
    st->drag_user = user;
}

void
seek_bar_setPercent(e9ui_component_t *comp, float percent)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    st->percent = percent;
}

void
seek_bar_setVisible(e9ui_component_t *comp, int visible)
{
    if (!comp) {
        return;
    }
    e9ui_setHidden(comp, visible ? 0 : 1);
}

void
seek_bar_setTooltipPrefix(e9ui_component_t *comp, const char *prefix)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    if (st->tooltip_prefix) {
        alloc_free(st->tooltip_prefix);
        st->tooltip_prefix = NULL;
    }
    if (prefix && *prefix) {
        st->tooltip_prefix = alloc_strdup(prefix);
    }
}

void
seek_bar_setTooltipUnit(e9ui_component_t *comp, const char *unit)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    if (st->tooltip_unit) {
        alloc_free(st->tooltip_unit);
        st->tooltip_unit = NULL;
    }
    if (unit && *unit) {
        st->tooltip_unit = alloc_strdup(unit);
    }
}

void
seek_bar_setTooltipScale(e9ui_component_t *comp, float scale)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    st->tooltip_scale = scale;
}

void
seek_bar_setTooltipOffset(e9ui_component_t *comp, float offset)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    st->tooltip_offset = offset;
}

void
seek_bar_setTooltipCallback(e9ui_component_t *comp, seek_bar_tooltip_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    st->tooltip_cb = cb;
    st->tooltip_user = user;
}

void
seek_bar_layoutInParent(e9ui_component_t *comp, e9ui_context_t *ctx, e9ui_rect_t parent)
{
    if (!comp || !comp->state) {
        return;
    }
    seek_bar_state_t *st = (seek_bar_state_t*)comp->state;
    int left = st->margin_left;
    int right = st->margin_right;
    int bottom = st->margin_bottom;
    int height = st->height;
    if (ctx) {
        left = e9ui_scale_px(ctx, left);
        right = e9ui_scale_px(ctx, right);
        bottom = e9ui_scale_px(ctx, bottom);
        height = e9ui_scale_px(ctx, height);
    }
    int w = parent.w - left - right;
    if (w < 1) w = 1;
    comp->bounds.x = parent.x + left;
    comp->bounds.w = w;
    comp->bounds.h = height;
    comp->bounds.y = parent.y + parent.h - bottom - height;
}
