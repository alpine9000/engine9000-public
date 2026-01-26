// e9ui badge component implementation
#include "e9ui_badge.h"
#include "e9ui_context.h"
#include "e9ui_text_cache.h"
#include "debugger.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct e9ui_badge_state {
    char *left;
    char *right; // optional; when NULL render single segment
    SDL_Color leftBg;
    SDL_Color rightBg;
    SDL_Color text;
    int prefW;
    int prefH;
} e9ui_badge_state_t;

static void
e9ui_badge_measure(e9ui_badge_state_t *st, e9ui_context_t *ctx)
{
    TTF_Font *font = debugger.theme.button.font ? debugger.theme.button.font : ctx->font;
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int padV = 6; int padH = 12;
    int wL = 0, wR = 0, th = lh;
    if (font) {
        if (st->left) {
            TTF_SizeText(font, st->left, &wL, &th);
        }
        if (st->right) {
            TTF_SizeText(font, st->right, &wR, &th);
        }
    } else {
        if (st->left) {
            wL = (int)strlen(st->left) * 8;
        }
        if (st->right) {
            wR = (int)strlen(st->right) * 8;
        }
    }
    int totalW = 0;
    if (st->right) {
        totalW = (wL + 2*padH) + (wR + 2*padH);
    } else {
        totalW = (wL + 2*padH);
    }
    st->prefW = totalW;
    st->prefH = lh + 2*padV;
}

static int
e9ui_badge_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    e9ui_badge_state_t *st = (e9ui_badge_state_t*)self->state;
    e9ui_badge_measure(st, ctx);
    return st->prefH;
}

static void
e9ui_badge_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
e9ui_badge_render_segment(SDL_Renderer *r, SDL_Rect rr, SDL_Color bg, int roundLeft, int roundRight)
{
    // Fully rounded ends: radius = half height
    int radius = rr.h / 2;
    if (radius < 2) {
        radius = 2;
    }
    if (radius*2 > rr.w) {
        radius = rr.w/2;
    }
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 255);
    for (int yy = 0; yy < rr.h; ++yy) {
        // Use mid-pixel sampling for smoother curve
        float dy = fabsf(((float)yy + 0.5f) - (float)radius);
        float dx = 0.0f;
        if (dy < (float)radius) {
            dx = sqrtf((float)(radius*radius) - dy*dy);
        }
        int xoff = (int)ceilf((float)radius - dx);
        if (xoff < 0) {
            xoff = 0;
        }
        int x1 = rr.x + (roundLeft ? xoff : 0);
        int x2 = rr.x + rr.w - 1 - (roundRight ? xoff : 0);
        if (x1 <= x2) {
            SDL_RenderDrawLine(r, x1, rr.y + yy, x2, rr.y + yy);
        }
    }
}

static void
e9ui_badge_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    e9ui_badge_state_t *st = (e9ui_badge_state_t*)self->state;
    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    e9ui_badge_measure(st, ctx);
    // Center inside bounds
    if (r.w > st->prefW) {
        r.x += (r.w - st->prefW)/2;
        r.w = st->prefW;
    }
    if (r.h > st->prefH) {
        r.y += (r.h - st->prefH)/2;
        r.h = st->prefH;
    }

    int padH = 12;
    TTF_Font *font = debugger.theme.button.font ? debugger.theme.button.font : ctx->font;
    int wL=0, wR=0, th=font ? TTF_FontHeight(font) : 16;
    if (th <= 0) {
        th = 16;
    }
    if (font) {
        if (st->left) {
            TTF_SizeText(font, st->left, &wL, &th);
        }
        if (st->right) {
            TTF_SizeText(font, st->right, &wR, &th);
        }
    } else {
        if (st->left) {
            wL = (int)strlen(st->left) * 8;
        }
        if (st->right) {
            wR = (int)strlen(st->right) * 8;
        }
    }

    if (st->right) {
        int wSegL = wL + 2*padH;
        int wSegR = wR + 2*padH;
        SDL_Rect rl = { r.x, r.y, wSegL, r.h };
        SDL_Rect rr = { r.x + wSegL, r.y, wSegR, r.h };
        e9ui_badge_render_segment(ctx->renderer, rl, st->leftBg, 1, 0);
        e9ui_badge_render_segment(ctx->renderer, rr, st->rightBg, 0, 1);
        if (font) {
            int lw = 0, lh = 0;
            int rw = 0, rh = 0;
            SDL_Texture *tl = e9ui_text_cache_getText(ctx->renderer, font, st->left ? st->left : "", st->text, &lw, &lh);
            SDL_Texture *tr = e9ui_text_cache_getText(ctx->renderer, font, st->right ? st->right : "", st->text, &rw, &rh);
            if (tl) {
                SDL_Rect trl = { rl.x + padH, rl.y + (rl.h - lh)/2, lw, lh };
                SDL_RenderCopy(ctx->renderer, tl, NULL, &trl);
            }
            if (tr) {
                SDL_Rect trr = { rr.x + padH, rr.y + (rr.h - rh)/2, rw, rh };
                SDL_RenderCopy(ctx->renderer, tr, NULL, &trr);
            }
        }
    } else {
        e9ui_badge_render_segment(ctx->renderer, r, st->leftBg, 1, 1);
        if (font) {
            int tw = 0, th = 0;
            SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, st->left ? st->left : "", st->text, &tw, &th);
            if (t) {
                SDL_Rect tr = { r.x + (r.w - tw)/2, r.y + (r.h - th)/2, tw, th };
                SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
            }
        }
    }
}


static void
e9ui_badge_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  e9ui_badge_state_t *st = (e9ui_badge_state_t*)self->state;
  alloc_free(st->left);
  alloc_free(st->right);
}

void
e9ui_badge_set(e9ui_component_t *badge,
                 const char *left,
                 const char *right,
                 SDL_Color leftBg,
                 SDL_Color rightBg,
                 SDL_Color text)
{
    if (!badge) {
        return;
    }
    e9ui_badge_state_t *st = (e9ui_badge_state_t*)badge->state;
    alloc_free(st->left);
    alloc_free(st->right);
    st->left = left ? alloc_strdup(left) : NULL;
    st->right = right ? alloc_strdup(right) : NULL;
    st->leftBg = leftBg;
    st->rightBg = rightBg;
    st->text = text;
}

e9ui_component_t *
e9ui_badge_make(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_badge_state_t *st = (e9ui_badge_state_t*)alloc_calloc(1, sizeof(*st));
    c->name = "e9ui_badge";
    c->state = st;
    c->preferredHeight = e9ui_badge_preferredHeight;
    c->layout = e9ui_badge_layout;
    c->render = e9ui_badge_render;
    c->dtor = e9ui_badge_dtor;
    // Default: empty neutral
    st->left = alloc_strdup("");
    st->right = NULL;
    st->leftBg = debugger.theme.button.background;
    st->rightBg = st->leftBg;
    st->text = debugger.theme.button.text;
    return c;
}
