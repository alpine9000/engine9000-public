/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <strings.h>

#include "registers.h"
#include "debugger.h"
#include "machine.h" 

static int
registers_findAny(const char **cands, int ncand, unsigned long *out)
{
    for (int i=0;i<ncand;i++) {
        if (machine_findReg(&debugger.machine, cands[i], out)) return 1;
    }
    return 0;
}

static int
registers_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW;
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    int lh = font ? TTF_FontHeight(font) : 16; if (lh <= 0) lh = 16;
    return lh * 4 + 8;
}

static void
registers_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
registers_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 22, 22, 22, 255);
    SDL_RenderFillRect(ctx->renderer, &r);
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    int lh = font ? TTF_FontHeight(font) : 16; if (lh <= 0) lh = 16;
    
    int padX = 12;
    int padY = 4;
    int curX = r.x + padX;
    int curY = r.y + padY;
    SDL_Color txt = (SDL_Color){220,220,220,255};
    int force_breaks = 0;
    if (font) {
        const char *dlabels[] = {
            "D0: FFFFFFFF", "D1: FFFFFFFF", "D2: FFFFFFFF", "D3: FFFFFFFF",
            "D4: FFFFFFFF", "D5: FFFFFFFF", "D6: FFFFFFFF", "D7: FFFFFFFF"
        };
        int total = padX;
        for (size_t i = 0; i < sizeof(dlabels)/sizeof(dlabels[0]); ++i) {
            int tw = 0;
            if (TTF_SizeText(font, dlabels[i], &tw, NULL) == 0) {
                total += tw + padX;
            }
        }
        if (total <= r.w) {
            force_breaks = 1;
        }
    }
    const char *order[] = {
        "D0","D1","D2","D3","D4","D5","D6","D7",
        "A0","A1","A2","A3","A4","A5","A6","A7",
        "SP","PC","SR"
    };
    for (size_t i = 0; i < sizeof(order)/sizeof(order[0]); ++i) {
        unsigned long v = 0;
        int found = 0;
        if (strcmp(order[i], "A6") == 0) {
            const char *alts[] = { "A6", "FP" , "fp" };
            found = registers_findAny(alts, (int)(sizeof(alts)/sizeof(alts[0])), &v);
        } else if (strcmp(order[i], "SP") == 0) {
            const char *alts[] = { "SP", "sp", "A7", "a7" };
            found = registers_findAny(alts, (int)(sizeof(alts)/sizeof(alts[0])), &v);
        } else if (strcmp(order[i], "PC") == 0) {
            const char *alts[] = { "PC", "pc" };
            found = registers_findAny(alts, (int)(sizeof(alts)/sizeof(alts[0])), &v);
        } else {
            found = machine_findReg(&debugger.machine, order[i], &v);
        }
        if (!found) {
            v = 0;
        }
        char label[64]; snprintf(label, sizeof(label), "%s: %08X", order[i], (unsigned int)v);
        int tw = 0, th = 0; if (font) { TTF_SizeText(font, label, &tw, &th); }
        if (curX + tw > r.x + r.w - padX) {
            curX = r.x + padX;
            curY += lh + padY;
            if (curY + lh > r.y + r.h - padY) { break; }
        }
        if (font) {
            int rw = 0, rh = 0;
            SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, label, txt, &rw, &rh);
            if (t) {
                SDL_Rect tr = { curX, curY, rw, rh };
                SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
            }
        }
        curX += (tw > 0 ? tw : 0) + padX;
        if (force_breaks && (strcmp(order[i], "D7") == 0 || strcmp(order[i], "A7") == 0)) {
            curX = r.x + padX;
            curY += lh + padY;
            if (curY + lh > r.y + r.h - padY) {
                break;
            }
        }
    }
}

e9ui_component_t *
registers_makeComponent(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    c->name = "e9ui_registers";
    
    c->preferredHeight = registers_preferredHeight;
    c->layout = registers_layout;
    c->render = registers_render;
    return c;
}

 
