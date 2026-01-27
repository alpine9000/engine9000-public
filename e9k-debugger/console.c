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
    int bucketConsole;
} console_state_t;

static int
console_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW; (void)ctx; return 0;
}

static void
console_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx; self->bounds = bounds;
}

static void
console_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    console_state_t *st = (console_state_t*)self->state;
    (void)st;
    TTF_Font *useFont = e9ui->theme.text.console ? e9ui->theme.text.console : (ctx?ctx->font:NULL);
    if (!useFont) {
        return; // no TTF font available; skip console text rendering
    }
    int lh = TTF_FontHeight(useFont); if (lh <= 0) lh = 16;
    
    int pad = 10;
    int textX = self->bounds.x + pad;
    int hitW = self->bounds.w - pad * 2;
    if (hitW < 0) {
        hitW = 0;
    }
    int y = self->bounds.y + 4;
    int availH = self->bounds.h - pad - 10;
    if (availH < lh) {
        availH = lh;
    }
    int visLines = availH / lh;
    if (visLines < 1) {
        visLines = 1;
    }
    int count = debugger.console.n;
    int start = count - visLines - debugger.consoleScrollLines; if (start < 0) start = 0; int end = start + visLines; if (end > count) end = count;
    if (debugger.consoleScrollLines <= 0 && end < count) { start = count - visLines; if (start < 0) start = 0; end = count; }
    for (int i=start; i<end; ++i) {
        int phys = linebuf_phys_index(&debugger.console, i);
        const char *ln = debugger.console.lines[phys];
        unsigned char iserr = debugger.console.is_err[phys];
        SDL_Color colc = iserr ? (SDL_Color){220,120,120,255} : (SDL_Color){200,200,200,255};
        e9ui_drawSelectableText(ctx, self, useFont, ln ? ln : "", colc, textX, y, lh,
                                hitW, &st->bucketConsole, 0, 1);
        y += lh; if (y > self->bounds.y + self->bounds.h - 10) break;
    }
    
}

static int
console_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
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
console_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  (void)self;
}

e9ui_component_t *
console_makeComponent(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    c->name = "e9ui_console";
    console_state_t *st = (console_state_t*)alloc_calloc(1, sizeof(console_state_t));
    c->state = st;
    c->preferredHeight = console_preferredHeight;
    c->layout = console_layout;
    c->render = console_render;
    c->handleEvent = console_handleEvent;
    c->dtor = console_dtor;
    return c;
}
