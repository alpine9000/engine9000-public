/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "debugger.h"

typedef struct e9ui_console_state {
    int unused;
} e9ui_console_state_t;

static int
e9ui_console_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW; (void)ctx; return 0;
}

static void
e9ui_console_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx; self->bounds = bounds;
}

static void
e9ui_console_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    e9ui_console_state_t *st = (e9ui_console_state_t*)self->state;
    (void)st;
    TTF_Font *useFont = debugger.theme.text.console ? debugger.theme.text.console : (ctx?ctx->font:NULL);
    if (!useFont) {
        return; // no TTF font available; skip console text rendering
    }
    int lh = TTF_FontHeight(useFont); if (lh <= 0) lh = 16;
    
    int y = self->bounds.y + 4;
    int pad = 10; int availH = self->bounds.h - pad - 10; if (availH < lh) availH = lh; int visLines = availH / lh; if (visLines < 1) visLines = 1;
    int count = debugger.console.n;
    int start = count - visLines - debugger.consoleScrollLines; if (start < 0) start = 0; int end = start + visLines; if (end > count) end = count;
    if (debugger.consoleScrollLines <= 0 && end < count) { start = count - visLines; if (start < 0) start = 0; end = count; }
    for (int i=start; i<end; ++i) {
        int phys = linebuf_phys_index(&debugger.console, i);
        const char *ln = debugger.console.lines[phys];
        unsigned char iserr = debugger.console.is_err[phys];
        SDL_Color colc = iserr ? (SDL_Color){220,120,120,255} : (SDL_Color){200,200,200,255};
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, useFont, ln ? ln : "", colc, &tw, &th);
        if (tex) {
            SDL_Rect r = { self->bounds.x + 10, y, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &r);
        }
        y += lh; if (y > self->bounds.y + self->bounds.h - 10) break;
    }
    
}

static int
e9ui_console_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)self; (void)ctx;

    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode kc = ev->key.keysym.sym;
        if (kc == SDLK_PAGEUP) { debugger.consoleScrollLines += 8; return 1; }
        if (kc == SDLK_PAGEDOWN) { debugger.consoleScrollLines -= 8; if (debugger.consoleScrollLines < 0) debugger.consoleScrollLines = 0; return 1; }
        if (kc == SDLK_HOME) { debugger.consoleScrollLines = debugger.console.n; return 1; }
        if (kc == SDLK_END) { debugger.consoleScrollLines = 0; return 1; }
    } else if (ev->type == SDL_MOUSEWHEEL) {
        if (!ctx) {
            return 0;
        }
        int mx = ctx->mouseX;
        int my = ctx->mouseY;
        if (mx < self->bounds.x || mx >= self->bounds.x + self->bounds.w ||
            my < self->bounds.y || my >= self->bounds.y + self->bounds.h) {
            return 0;
        }
        int linesPerWheel = 3;
        if (ev->wheel.y > 0) { debugger.consoleScrollLines += linesPerWheel * ev->wheel.y; }
        else if (ev->wheel.y < 0) { debugger.consoleScrollLines += linesPerWheel * ev->wheel.y; if (debugger.consoleScrollLines < 0) debugger.consoleScrollLines = 0; }
        return 1;
    }
    return 0;
}

static void
e9ui_console_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  (void)self;
}

e9ui_component_t *
e9ui_console_makeComponent(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    c->name = "e9ui_console";
    e9ui_console_state_t *st = (e9ui_console_state_t*)alloc_calloc(1, sizeof(e9ui_console_state_t));
    st->unused = 0;
    c->state = st;
    c->preferredHeight = e9ui_console_preferredHeight;
    c->layout = e9ui_console_layout;
    c->render = e9ui_console_render;
    c->handleEvent = e9ui_console_handleEvent;
    c->dtor = e9ui_console_dtor;
    return c;
}
