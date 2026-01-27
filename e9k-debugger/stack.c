/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "stack.h"
#include "e9ui_context.h"
#include "e9ui_text_cache.h"
#include "debugger.h"
#include "ui.h"
#include "machine.h"

static int
stack_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW; return 0; // flexible
}

static void
stack_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx; self->bounds = bounds;
}

static void
stack_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)self;
    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 22, 20, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &r);
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) {
        return;
    }
    int lh = TTF_FontHeight(font);
    if (lh <= 0) {
        lh = 16;
    }
    const machine_frame_t *frames = NULL; int count = 0;
    machine_getStack(&debugger.machine, &frames, &count);
    int pad = 8; int y = r.y + pad;
    for (int i=0;i<count;i++) {
        SDL_Color col = {220,220,180,255};
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "#%d %s (%s:%d)", frames[i].level, frames[i].func[0]?frames[i].func:"?", frames[i].file[0]?frames[i].file:"?", frames[i].line);
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, tmp, col, &tw, &th);
        if (t) {
            SDL_Rect tr = { r.x + pad, y, tw, th };
            SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
        }
        y += lh;
        if (frames[i].source[0]) {
            SDL_Color src_col = {160,160,140,255};
            int sw = 0, sh = 0;
            SDL_Texture *tt = e9ui_text_cache_getText(ctx->renderer, font, frames[i].source, src_col, &sw, &sh);
            if (tt) {
                SDL_Rect tr = { r.x + pad + 18, y, sw, sh };
                SDL_RenderCopy(ctx->renderer, tt, NULL, &tr);
            }
            y += lh;
        }
        if (y > r.y + r.h - pad) {
            break;
        }
    }
    if (count == 0) {
        SDL_Color col = {180,160,160,255};
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, "No frames (running?)", col, &tw, &th);
        if (t) {
            SDL_Rect tr = { r.x + pad, y, tw, th };
            SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
        }
    }
    
}

static int
stack_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    if (ev->type != SDL_MOUSEBUTTONDOWN || ev->button.button != SDL_BUTTON_LEFT) {
        return 0;
    }
    int mx = ev->button.x;
    int my = ev->button.y;
    if (mx < self->bounds.x || mx >= self->bounds.x + self->bounds.w ||
        my < self->bounds.y || my >= self->bounds.y + self->bounds.h) {
        return 0;
    }
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) {
        return 0;
    }
    int lh = TTF_FontHeight(font);
    if (lh <= 0) {
        lh = 16;
    }
    const machine_frame_t *frames = NULL; int count = 0;
    machine_getStack(&debugger.machine, &frames, &count);
    if (!frames || count <= 0) {
        return 0;
    }
    int pad = 8;
    int y = self->bounds.y + pad;
    for (int i = 0; i < count; i++) {
        int lines = frames[i].source[0] ? 2 : 1;
        int h = lines * lh;
        if (my >= y && my < y + h) {
            ui_centerSourceOnAddress(frames[i].addr);
            return 1;
        }
        y += h;
        if (y > self->bounds.y + self->bounds.h - pad) {
            break;
        }
    }
    return 0;
}

e9ui_component_t *
stack_makeComponent(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    c->name = "stack";
    
    c->preferredHeight = stack_preferredHeight;
    c->layout = stack_layout;
    c->render = stack_render;
    c->handleEvent = stack_handleEvent;
    return c;
}

 
