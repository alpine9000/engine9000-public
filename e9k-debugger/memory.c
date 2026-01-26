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
#include <stdarg.h>
#include <ctype.h>

#include "memory.h"
#include "e9ui_context.h"
#include "e9ui_stack.h"
#include "e9ui_textbox.h"
#include "e9ui_text_cache.h"
#include "debugger.h"
#include "libretro_host.h"
#include "libretro.h"

typedef struct memory_view_state {
    unsigned int   base;
    unsigned int   size;
    unsigned char *data;
    e9ui_component_t *textbox;
    char           error[128];
} memory_view_state_t;

static memory_view_state_t *g_memory_view_state = NULL;

#define GEO_MAIN_RAM_BASE 0x00100000u
#define GEO_MAIN_RAM_END  0x001fffffu

static void
memory_setError(memory_view_state_t *panel, const char *fmt, ...)
{
    if (!panel) {
        return;
    }
    panel->error[0] = '\0';
    if (!fmt || !*fmt) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(panel->error, sizeof(panel->error), fmt, ap);
    va_end(ap);
    panel->error[sizeof(panel->error) - 1] = '\0';
}

static void
memory_clearData(memory_view_state_t *panel)
{
    if (!panel || !panel->data) {
        return;
    }
    memset(panel->data, 0, panel->size);
}

static int
memory_parseAddress(memory_view_state_t *st, unsigned int *out_addr)
{
    if (!st || !st->textbox || !out_addr) {
        return 0;
    }
    const char *t = e9ui_textbox_getText(st->textbox);
    if (!t || !*t) {
        memory_setError(st, "Invalid address: empty input");
        return 0;
    }
    char *end = NULL;
    unsigned long long val = strtoull(t, &end, 0);
    if (!end || end == t) {
        memory_setError(st, "Invalid address: \"%s\"", t);
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end) {
        memory_setError(st, "Invalid address: \"%s\"", t);
        return 0;
    }
    if (val < GEO_MAIN_RAM_BASE || val > GEO_MAIN_RAM_END) {
        memory_setError(st, "Address outside main RAM (0x%06X-0x%06X)", GEO_MAIN_RAM_BASE, GEO_MAIN_RAM_END);
        return 0;
    }
    *out_addr = (unsigned int)val;
    return 1;
}

static void
memory_fillFromram(memory_view_state_t *st, unsigned int base)
{
    if (!st || !st->data) {
        return;
    }
    size_t ram_size = 0;
    const unsigned char *ram = (const unsigned char *)libretro_host_getMemory(RETRO_MEMORY_SYSTEM_RAM, &ram_size);
    if (!ram || ram_size == 0) {
        memory_clearData(st);
        memory_setError(st, "Main RAM unavailable");
        return;
    }
    memory_setError(st, NULL);
    int range_error = 0;
    for (unsigned int i = 0; i < st->size; ++i) {
        unsigned int addr = base + i;
        if (addr < GEO_MAIN_RAM_BASE || addr > GEO_MAIN_RAM_END) {
            st->data[i] = 0;
            range_error = 1;
            continue;
        }
        unsigned int offset = addr & 0xFFFFu;
        if (offset >= ram_size) {
            st->data[i] = 0;
            range_error = 1;
            continue;
        }
        st->data[i] = ram[offset];
    }
    if (range_error) {
        memory_setError(st, "Range exceeds main RAM (0x%06X-0x%06X)", GEO_MAIN_RAM_BASE, GEO_MAIN_RAM_END);
    }
}

static void
memory_onAddressSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_view_state_t *st = (memory_view_state_t*)user;
    if (!st || !st->textbox) {
        return;
    }
    unsigned int addr = 0;
    if (!memory_parseAddress(st, &addr)) {
        return;
    }
    st->base = addr;
    memory_fillFromram(st, st->base);
}

static int
memory_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0;
}

static void
memory_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
memory_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    memory_view_state_t *st = (memory_view_state_t*)self->state;
    if (!st) {
        return;
    }
    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 20, 22, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &r);
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font || !st->data) {
        return;
    }
    int lh = TTF_FontHeight(font);
    if (lh <= 0) {
        lh = 16;
    }
    unsigned int addr = st->base;
    int pad = 8;
    int y = r.y + pad;
    if (st->error[0]) {
        SDL_Color err = {220, 80, 80, 255};
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, st->error, err, &tw, &th);
        if (t) {
            SDL_Rect tr = { r.x + pad, y, tw, th };
            SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
        }
        y += lh;
    }
    char line[256];
    for (unsigned int off = 0; off < st->size; off += 16) {
        int n = snprintf(line, sizeof(line), "%08X: ", addr + off);
        for (unsigned int i=0; i<16; ++i) {
            if (off + i < st->size) {
                n += snprintf(line + n, sizeof(line) - n, "%02X ", st->data[off + i]);
            } else {
                n += snprintf(line + n, sizeof(line) - n, "   ");
            }
        }
        n += snprintf(line + n, sizeof(line) - n, " ");
        for (unsigned int i=0; i<16 && off + i < st->size; ++i) {
            unsigned char c = st->data[off + i];
            line[n++] = (c >= 32 && c <= 126) ? (char)c : '.';
        }
        line[n] = '\0';
        SDL_Color col = {200,220,200,255};
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, line, col, &tw, &th);
        if (t) {
            SDL_Rect tr = { r.x + pad, y, tw, th };
            SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
        }
        y += lh;
        if (y > r.y + r.h - pad) {
            break;
        }
    }
}

static void
memory_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  if (!self) {
    return;
  }
  memory_view_state_t *st = (memory_view_state_t*)self->state;
  if (st) {
    alloc_free(st->data);
  }
}

e9ui_component_t *
memory_makeComponent(void)
{
    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    memory_view_state_t *st = (memory_view_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    c->name = "memory_view";
    c->state = st;
    c->preferredHeight = memory_preferredHeight;
    c->layout = memory_layout;
    c->render = memory_render;
    c->dtor = memory_dtor;
    
    st->base = 0x00100000u;
    st->size = 16u * 32u;
    st->data = (unsigned char*)alloc_alloc(st->size);
    memory_clearData(st);
    memory_setError(st, NULL);

    st->textbox = e9ui_textbox_make(32, memory_onAddressSubmit, NULL, st);
    e9ui_setDisableVariable(st->textbox, machine_getRunningState(debugger.machine), 1);        

    e9ui_textbox_setPlaceholder(st->textbox, "Base address (hex)");
    e9ui_textbox_setText(st->textbox, "0x00100000");
    
    e9ui_stack_addFixed(stack, st->textbox);

    e9ui_stack_addFlex(stack, c);

    g_memory_view_state = st;

    return stack;
}

void
memory_refreshOnBreak(void)
{
    if (!g_memory_view_state) {
        return;
    }
    unsigned int addr = 0;
    if (!memory_parseAddress(g_memory_view_state, &addr)) {
        return;
    }
    g_memory_view_state->base = addr;
    memory_fillFromram(g_memory_view_state, g_memory_view_state->base);
}
