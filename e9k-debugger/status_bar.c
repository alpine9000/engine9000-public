/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "status_bar.h"
#include "debug_font.h"
#include "debugger.h"
#include "state_buffer.h"
#include "libretro_host.h"
#include "e9ui_text_cache.h"
#include "gl_composite.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct status_bar_state {
    int prefH;
    uint32_t fps_last_tick;
    uint32_t fps_frames;
    int fps_value;
    uint32_t core_last_tick;
    uint64_t core_last_frame;
    int core_fps_value;
    uint64_t cycle_count;
} status_bar_state_t;

static int
status_bar_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW;
    status_bar_state_t *st = (status_bar_state_t*)self->state;
    TTF_Font *font = ctx ? ctx->font : NULL;
    int fh = font ? TTF_FontHeight(font) : 16;
    if (fh <= 0) {
        fh = 16;
    }
    int padV = 8;
    if (st) {
        st->prefH = fh + 2*padV;
    }
    return fh + 2*padV;
}

static void
status_bar_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
status_bar_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    e9ui_rect_t rect = self->bounds;
    SDL_Rect r = { rect.x, rect.y, rect.w, rect.h };
    SDL_Color bg = debugger.theme.titlebar.background;
    SDL_Color fg = debugger.theme.titlebar.text;
    SDL_SetRenderDrawColor(ctx->renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(ctx->renderer, &r);

    const char *stateLabel = machine_getRunning(debugger.machine) ? "RUNNING" : "STOPPED";
    char label[192];
    status_bar_state_t *st = (status_bar_state_t*)self->state;
    if (st) {
        uint32_t now = SDL_GetTicks();
        if (st->fps_last_tick == 0) {
            st->fps_last_tick = now;
        }
        st->fps_frames++;
        if (now - st->fps_last_tick >= 1000) {
            st->fps_value = (int)st->fps_frames;
            st->fps_frames = 0;
            st->fps_last_tick = now;
        }
        if (st->core_last_tick == 0) {
            st->core_last_tick = now;
            st->core_last_frame = debugger.frameCounter;
        }
        if (now - st->core_last_tick >= 1000) {
            uint64_t cur = debugger.frameCounter;
            uint64_t delta = (cur >= st->core_last_frame) ? (cur - st->core_last_frame) : 0;
            st->core_fps_value = (int)delta;
            st->core_last_frame = cur;
            st->core_last_tick = now;
        }
    }
    int recording = state_buffer_isPaused() ? 0 : 1;
    size_t used = state_buffer_getUsedBytes();
    size_t max = state_buffer_getMaxBytes();
    float pct = (max > 0) ? (100.0f * (float)used / (float)max) : 0.0f;
    int fps = st ? st->fps_value : 0;
    int core_fps = st ? st->core_fps_value : 0;
    uint64_t cycles = 0;
    uint64_t frame = debugger.frameCounter;
    if (st) {
        st->cycle_count = libretro_host_debugReadCycleCount();
        cycles = st->cycle_count;
    }
    char profile[64] = "";
    if (debugger.geo.profilerEnabled) {
        snprintf(profile, sizeof(profile), " PROFILE RX:%llu", debugger.geo.streamPacketCount);
    }
    char record[64] = "";
    if (recording) {
        snprintf(record, sizeof(record), " RECORDING:%.1f%%", pct);
    }
    const char *glLabel = gl_composite_isActive() ? " OPENGL" : "";
    snprintf(label, sizeof(label), " %s FRAME:%llu%s FPS:%d/%d CYCLES:%llu%s%s",
             stateLabel,
             (unsigned long long)frame,
             record,
             fps,
             core_fps,
             (unsigned long long)cycles,
             profile,
             glLabel);
    TTF_Font *font = ctx->font;
    if (font) {
        int tw = 0, th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, label, fg, &tw, &th);
        if (tex) {
            SDL_Rect dst = { r.x + 12, r.y + (r.h - th)/2, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
        }
    } else {
        int textY = r.y + 8;
        debug_font_drawText(ctx->renderer, r.x + 12, textY, label, 2);
    }
}


e9ui_component_t *
status_bar_make(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    status_bar_state_t *st = (status_bar_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    c->name = "status_bar";
    c->state = st;
    c->preferredHeight = status_bar_preferredHeight;
    c->layout = status_bar_layout;
    c->render = status_bar_render;
    return c;
}
