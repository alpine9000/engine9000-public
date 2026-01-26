// e9ui button component
#include "e9ui_button.h"
#include "e9ui_context.h"
#include "e9ui_scale.h"
#include "e9ui_text_cache.h"
#include "e9ui_types.h"
#include "debugger.h"
#include "file.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL_image.h>
#include <stdint.h>


typedef struct e9ui_button_state {
  char               *label;
  char               *largestLabel;
  e9ui_button_cb    onClick;
  void               *user;
  int                 hover;
  int                 pressed;
  int                 glowPulse;
  int                 prefW;
  int                 prefH;
  int                 hotkeyId;
  SDL_Texture        *icon;
  int                 iconW;
  int                 iconH;
  int                 useMini;
  int                 useCustomTheme;
  e9k_theme_button_t  customTheme;
  SDL_Texture        *bgCache;
  int                 bgCacheW;
  int                 bgCacheH;
  uint64_t            bgCacheKey;
} e9ui_button_state_t;

static SDL_Color
e9ui_button_scaleColor(SDL_Color src, float scale)
{
    int r = (int)(src.r * scale);
    int g = (int)(src.g * scale);
    int b = (int)(src.b * scale);
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    SDL_Color out = { (Uint8)r, (Uint8)g, (Uint8)b, src.a };
    return out;
}

static SDL_Color
e9ui_button_applyGlow(const SDL_Color *src, float phase)
{
    float pulse = 0.85f + 0.25f * phase;
    return e9ui_button_scaleColor(*src, pulse);
}

static SDL_Color
e9ui_button_disabledBorderColor(const SDL_Color *src)
{
    return e9ui_button_scaleColor(*src, debugger.theme.disabled.borderScale);
}

static SDL_Color
e9ui_button_disabledFillColor(const SDL_Color *src)
{
    return e9ui_button_scaleColor(*src, debugger.theme.disabled.fillScale);
}

static SDL_Color
e9ui_button_disabledTextColor(const SDL_Color *src)
{
    return e9ui_button_scaleColor(*src, debugger.theme.disabled.textScale);
}

static Uint32
e9ui_button_colorKey(SDL_Color c)
{
    return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) | ((Uint32)c.b << 8) | (Uint32)c.a;
}

static uint64_t
e9ui_button_hash64(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t
e9ui_button_backgroundKey(const SDL_Rect *r,
                          SDL_Color fill,
                          SDL_Color hi,
                          SDL_Color sh,
                          SDL_Color ed,
                          int radius)
{
    uint64_t h = 1469598103934665603ULL;
    h = e9ui_button_hash64(h, (uint64_t)r->w);
    h = e9ui_button_hash64(h, (uint64_t)r->h);
    h = e9ui_button_hash64(h, (uint64_t)radius);
    h = e9ui_button_hash64(h, (uint64_t)e9ui_button_colorKey(fill));
    h = e9ui_button_hash64(h, (uint64_t)e9ui_button_colorKey(hi));
    h = e9ui_button_hash64(h, (uint64_t)e9ui_button_colorKey(sh));
    h = e9ui_button_hash64(h, (uint64_t)e9ui_button_colorKey(ed));
    return h;
}

static void
e9ui_button_drawBackground(SDL_Renderer *renderer,
                           SDL_Rect r,
                           SDL_Color fillColor,
                           SDL_Color hi,
                           SDL_Color sh,
                           SDL_Color ed,
                           int radius)
{
    const Uint8 fillR = fillColor.r, fillG = fillColor.g, fillB = fillColor.b;
    const Uint8 hiR = hi.r, hiG = hi.g, hiB = hi.b;
    const Uint8 shR = sh.r, shG = sh.g, shB = sh.b;
    const Uint8 edR = ed.r, edG = ed.g, edB = ed.b;
    // Draw dithered rounded fill
    if (radius*2 > r.h) {
        radius = r.h/2;
    }
    if (radius*2 > r.w) {
        radius = r.w/2;
    }
    if (radius < 2) {
        radius = 2;
    }
    // Fill scanlines with rounded ends
    SDL_SetRenderDrawColor(renderer, fillR, fillG, fillB, 255);
    for (int yy = 0; yy < r.h; ++yy) {
        int xpad = 0;
        if (yy < radius) {
            float dy = (float)(radius - yy - 0.5f);
            float dx = sqrtf((float)(radius*radius) - dy*dy);
            xpad = radius - (int)floorf(dx);
        } else if (yy >= r.h - radius) {
            int y2 = r.h - 1 - yy;
            float dy = (float)(radius - y2 - 0.5f);
            float dx = sqrtf((float)(radius*radius) - dy*dy);
            xpad = radius - (int)floorf(dx);
        }
        int x1 = r.x + xpad;
        int x2 = r.x + r.w - 1 - xpad;
        if (x1 <= x2) {
            SDL_RenderDrawLine(renderer, x1, r.y + yy, x2, r.y + yy);
        }
        // Dithered edge one pixel beyond to soften
        if (xpad > 0) {
            int exL = x1 - 1; int exR = x2 + 1;
            if (exL >= r.x) {
                if (((exL + (r.y+yy)) & 1) == 0) {
                    SDL_RenderDrawPoint(renderer, exL, r.y + yy);
                }
            }
            if (exR < r.x + r.w) {
                if (((exR + (r.y+yy)) & 1) == 0) {
                    SDL_RenderDrawPoint(renderer, exR, r.y + yy);
                }
            }
        }
    }
    // Draw border with highlight (top) and shadow (bottom). No effect on sides.
    // Top horizontal segment (highlight)
    int xh1 = r.x + radius; int xh2 = r.x + r.w - 1 - radius;
    SDL_SetRenderDrawColor(renderer, hiR, hiG, hiB, 255);
    SDL_RenderDrawLine(renderer, xh1, r.y, xh2, r.y);
    // Bottom horizontal segment (shadow)
    SDL_SetRenderDrawColor(renderer, shR, shG, shB, 255);
    SDL_RenderDrawLine(renderer, xh1, r.y + r.h - 1, xh2, r.y + r.h - 1);
    // Left and right vertical segments (neutral edge)
    SDL_SetRenderDrawColor(renderer, edR, edG, edB, 255);
    int yv1 = r.y + radius; int yv2 = r.y + r.h - 1 - radius;
    SDL_RenderDrawLine(renderer, r.x, yv1, r.x, yv2);
    SDL_RenderDrawLine(renderer, r.x + r.w - 1, yv1, r.x + r.w - 1, yv2);
    // Corner arcs with highlight/shadow falloff by angle (based on ny component)
    // Top-left corner center
    int cx_tl = r.x + radius, cy_tl = r.y + radius;
    int cx_tr = r.x + r.w - 1 - radius, cy_tr = r.y + radius;
    int cx_bl = r.x + radius, cy_bl = r.y + r.h - 1 - radius;
    int cx_br = r.x + r.w - 1 - radius, cy_br = r.y + r.h - 1 - radius;
    for (int a = 0; a < radius; ++a) {
        float dy = (float)(radius - a - 0.5f);
        float dx = sqrtf((float)(radius*radius) - dy*dy);
        int off = (int)floorf(dx);
        // sample points for each corner and shade by ny (normal y component)
        // TL top edge point (x = cx_tl - off, y = cy_tl - (radius - a))
        int pxtl = cx_tl - off; int pytl = r.y + a;
        float ny_tl = ((float)pytl - cy_tl) / (float)radius; // [-1..0]
        float amt_hi_tl = fmaxf(0.f, -ny_tl);
        Uint8 col_tl_r = (Uint8)fminf(255.f, edR*(1.f-amt_hi_tl) + hiR*amt_hi_tl);
        Uint8 col_tl_g = (Uint8)fminf(255.f, edG*(1.f-amt_hi_tl) + hiG*amt_hi_tl);
        Uint8 col_tl_b = (Uint8)fminf(255.f, edB*(1.f-amt_hi_tl) + hiB*amt_hi_tl);
        SDL_SetRenderDrawColor(renderer, col_tl_r, col_tl_g, col_tl_b, 255);
        SDL_RenderDrawPoint(renderer, pxtl, pytl);
        // TL left side partner (x = cx_tl - (radius - a), y = cy_tl - off)
        int pxtl2 = r.x + a; int pytl2 = cy_tl - off;
        float ny_tl2 = ((float)pytl2 - cy_tl) / (float)radius; // [-1..0]
        float amt_hi_tl2 = fmaxf(0.f, -ny_tl2);
        Uint8 col_tl2_r = (Uint8)fminf(255.f, edR*(1.f-amt_hi_tl2) + hiR*amt_hi_tl2);
        Uint8 col_tl2_g = (Uint8)fminf(255.f, edG*(1.f-amt_hi_tl2) + hiG*amt_hi_tl2);
        Uint8 col_tl2_b = (Uint8)fminf(255.f, edB*(1.f-amt_hi_tl2) + hiB*amt_hi_tl2);
        SDL_SetRenderDrawColor(renderer, col_tl2_r, col_tl2_g, col_tl2_b, 255);
        SDL_RenderDrawPoint(renderer, pxtl2, pytl2);

        // TR
        int pxtr = cx_tr + off; int pytr = r.y + a;
        float ny_tr = ((float)pytr - cy_tr) / (float)radius;
        float amt_hi_tr = fmaxf(0.f, -ny_tr);
        Uint8 col_tr_r = (Uint8)fminf(255.f, edR*(1.f-amt_hi_tr) + hiR*amt_hi_tr);
        Uint8 col_tr_g = (Uint8)fminf(255.f, edG*(1.f-amt_hi_tr) + hiG*amt_hi_tr);
        Uint8 col_tr_b = (Uint8)fminf(255.f, edB*(1.f-amt_hi_tr) + hiB*amt_hi_tr);
        SDL_SetRenderDrawColor(renderer, col_tr_r, col_tr_g, col_tr_b, 255);
        SDL_RenderDrawPoint(renderer, pxtr, pytr);
        int pxtr2 = r.x + r.w - 1 - a; int pytr2 = cy_tr - off;
        float ny_tr2 = ((float)pytr2 - cy_tr) / (float)radius;
        float amt_hi_tr2 = fmaxf(0.f, -ny_tr2);
        Uint8 col_tr2_r = (Uint8)fminf(255.f, edR*(1.f-amt_hi_tr2) + hiR*amt_hi_tr2);
        Uint8 col_tr2_g = (Uint8)fminf(255.f, edG*(1.f-amt_hi_tr2) + hiG*amt_hi_tr2);
        Uint8 col_tr2_b = (Uint8)fminf(255.f, edB*(1.f-amt_hi_tr2) + hiB*amt_hi_tr2);
        SDL_SetRenderDrawColor(renderer, col_tr2_r, col_tr2_g, col_tr2_b, 255);
        SDL_RenderDrawPoint(renderer, pxtr2, pytr2);

        // BL
        int pxbl = cx_bl - off; int pybl = r.y + r.h - 1 - a;
        float ny_bl = ((float)pybl - cy_bl) / (float)radius; // [0..1]
        float amt_sh_bl = fmaxf(0.f, ny_bl);
        Uint8 col_bl_r = (Uint8)fmaxf(0.f, edR*(1.f-amt_sh_bl) + shR*amt_sh_bl);
        Uint8 col_bl_g = (Uint8)fmaxf(0.f, edG*(1.f-amt_sh_bl) + shG*amt_sh_bl);
        Uint8 col_bl_b = (Uint8)fmaxf(0.f, edB*(1.f-amt_sh_bl) + shB*amt_sh_bl);
        SDL_SetRenderDrawColor(renderer, col_bl_r, col_bl_g, col_bl_b, 255);
        SDL_RenderDrawPoint(renderer, pxbl, pybl);
        int pxbl2 = r.x + a; int pybl2 = cy_bl + off;
        float ny_bl2 = ((float)pybl2 - cy_bl) / (float)radius;
        float amt_sh_bl2 = fmaxf(0.f, ny_bl2);
        Uint8 col_bl2_r = (Uint8)fmaxf(0.f, edR*(1.f-amt_sh_bl2) + shR*amt_sh_bl2);
        Uint8 col_bl2_g = (Uint8)fmaxf(0.f, edG*(1.f-amt_sh_bl2) + shG*amt_sh_bl2);
        Uint8 col_bl2_b = (Uint8)fmaxf(0.f, edB*(1.f-amt_sh_bl2) + shB*amt_sh_bl2);
        SDL_SetRenderDrawColor(renderer, col_bl2_r, col_bl2_g, col_bl2_b, 255);
        SDL_RenderDrawPoint(renderer, pxbl2, pybl2);

        // BR
        int pxbr = cx_br + off; int pybr = r.y + r.h - 1 - a;
        float ny_br = ((float)pybr - cy_br) / (float)radius;
        float amt_sh_br = fmaxf(0.f, ny_br);
        Uint8 col_br_r = (Uint8)fmaxf(0.f, edR*(1.f-amt_sh_br) + shR*amt_sh_br);
        Uint8 col_br_g = (Uint8)fmaxf(0.f, edG*(1.f-amt_sh_br) + shG*amt_sh_br);
        Uint8 col_br_b = (Uint8)fmaxf(0.f, edB*(1.f-amt_sh_br) + shB*amt_sh_br);
        SDL_SetRenderDrawColor(renderer, col_br_r, col_br_g, col_br_b, 255);
        SDL_RenderDrawPoint(renderer, pxbr, pybr);
        int pxbr2 = r.x + r.w - 1 - a; int pybr2 = cy_br + off;
        float ny_br2 = ((float)pybr2 - cy_br) / (float)radius;
        float amt_sh_br2 = fmaxf(0.f, ny_br2);
        Uint8 col_br2_r = (Uint8)fmaxf(0.f, edR*(1.f-amt_sh_br2) + shR*amt_sh_br2);
        Uint8 col_br2_g = (Uint8)fmaxf(0.f, edG*(1.f-amt_sh_br2) + shG*amt_sh_br2);
        Uint8 col_br2_b = (Uint8)fmaxf(0.f, edB*(1.f-amt_sh_br2) + shB*amt_sh_br2);
        SDL_SetRenderDrawColor(renderer, col_br2_r, col_br2_g, col_br2_b, 255);
        SDL_RenderDrawPoint(renderer, pxbr2, pybr2);
    }
}


static const e9k_theme_button_t *
e9ui_button_getTheme(const e9ui_button_state_t *st)
{
    if (st && st->useCustomTheme) {
        return &st->customTheme;
    }
    if (st && st->useMini) {
        return &debugger.theme.miniButton;
    }
    return &debugger.theme.button;
}

static int
e9ui_button_scaledPadding(const e9k_theme_button_t *theme, const e9ui_context_t *ctx)
{
    if (!theme) {
        return 0;
    }
    if (theme->padding <= 0) {
        return 0;
    }
    return e9ui_scale_px(ctx, theme->padding);
}

static void
e9ui_button_updateMeasure(e9ui_button_state_t *st, e9ui_context_t *ctx)
{
    const e9k_theme_button_t *theme = e9ui_button_getTheme(st);
    TTF_Font *useFont = (theme->font ? theme->font : (ctx?ctx->font:NULL));
    int lh = useFont ? TTF_FontHeight(useFont) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int w = 0, h = lh;
    if (st->icon) {
        int iw = st->iconW, ih = st->iconH;
        int iconMaxH = (int)floorf(lh * 0.75f);
        if (iconMaxH < 10) {
            iconMaxH = (lh > 0 ? lh : 10);
        }
        if (ih > iconMaxH) {
            float s = (float)iconMaxH / (float)ih;
            iw = (int)ceilf((float)iw * s);
            ih = iconMaxH;
        }
        w += iw;
        if (h < ih) {
            h = ih;
        }
        if (st->label && *st->label) {
            w += 6; // spacing
        }
    }
    const char *measure_label = (st->largestLabel && st->largestLabel[0]) ? st->largestLabel : st->label;
    if (useFont && measure_label) {
        int tw=0, th=lh;
        TTF_SizeText(useFont, measure_label, &tw, &th);
        w += tw;
        if (h < th) {
            h = th;
        }
    } else if (measure_label) {
        w += (int)strlen(measure_label) * 8;
    }
    int padding = e9ui_button_scaledPadding(theme, ctx);
    st->prefW = w + 16 + padding * 2; // base padding plus theme padding
    st->prefH = h + 8 + padding * 2;
}

static int
e9ui_button_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    if (st && e9ui_getHidden(self)) {
        return 0;
    }
    e9ui_button_updateMeasure(st, ctx);
    return st->prefH;
}

static void
e9ui_button_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
e9ui_button_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    if (!st) {
        return;
    }
    if (e9ui_getHidden(self)) {
        return;
    }
    const e9k_theme_button_t *theme = e9ui_button_getTheme(st);
    const int padding = e9ui_button_scaledPadding(theme, ctx);
    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    int pressed = st->pressed && !self->disabled;
    // Color palette (hex):
    // highlight: 0x7B7C7C, background: 0x5A5B5C, shadow: 0x1C1D1D, text: 0xE6E7E7
    const int disabled = self->disabled;
    SDL_Color fillColor;
    if (disabled) {
        fillColor = e9ui_button_disabledFillColor(&theme->background);
    } else if (pressed) {
        fillColor = theme->pressedBackground;
    } else {
        fillColor = theme->background;
    }
    SDL_Color hi = disabled ? e9ui_button_disabledBorderColor(&theme->highlight) : theme->highlight;
    SDL_Color sh = disabled ? e9ui_button_disabledBorderColor(&theme->shadow) : theme->shadow;
    SDL_Color textColor = disabled ? e9ui_button_disabledTextColor(&theme->text) : theme->text;
    int allow_cache = 1;
    if (st->glowPulse && !disabled && !pressed) {
        float t = (float)SDL_GetTicks() / 1000.0f;
        float phase = 0.5f + 0.5f * sinf(t * 3.2f);
        fillColor = e9ui_button_applyGlow(&fillColor, phase);
        hi = e9ui_button_applyGlow(&hi, phase);
        sh = e9ui_button_applyGlow(&sh, phase);
        allow_cache = 0;
    }
    SDL_Color ed = {
        disabled ? fillColor.r : theme->background.r,
        disabled ? fillColor.g : theme->background.g,
        disabled ? fillColor.b : theme->background.b,
        255
    };
    int radius = theme->borderRadius > 0 ? theme->borderRadius : 6;
    int drew_cached = 0;
    if (allow_cache && SDL_RenderTargetSupported(ctx->renderer)) {
        SDL_Rect cache_rect = { 0, 0, r.w, r.h };
        uint64_t key = e9ui_button_backgroundKey(&cache_rect, fillColor, hi, sh, ed, radius);
        if (!st->bgCache || st->bgCacheW != r.w || st->bgCacheH != r.h || st->bgCacheKey != key) {
            if (st->bgCache) {
                SDL_DestroyTexture(st->bgCache);
                st->bgCache = NULL;
            }
            st->bgCache = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, r.w, r.h);
            if (st->bgCache) {
                SDL_SetTextureBlendMode(st->bgCache, SDL_BLENDMODE_BLEND);
                SDL_Texture *prev = SDL_GetRenderTarget(ctx->renderer);
                SDL_SetRenderTarget(ctx->renderer, st->bgCache);
                SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 0);
                SDL_RenderClear(ctx->renderer);
                e9ui_button_drawBackground(ctx->renderer, cache_rect, fillColor, hi, sh, ed, radius);
                SDL_SetRenderTarget(ctx->renderer, prev);
                st->bgCacheW = r.w;
                st->bgCacheH = r.h;
                st->bgCacheKey = key;
            }
        }
        if (st->bgCache) {
            SDL_RenderCopy(ctx->renderer, st->bgCache, NULL, &r);
            drew_cached = 1;
        }
    }
    if (!drew_cached) {
        e9ui_button_drawBackground(ctx->renderer, r, fillColor, hi, sh, ed, radius);
    }
    // Content: optional icon + text
    int cy = r.y + r.h/2;
    int innerStartX = r.x + 8 + padding;
    TTF_Font *tf = theme->font ? theme->font : ctx->font;
    int textW = 0;
    int textH = 0;
    SDL_Texture *textTexture = NULL;
    if (tf && st->label) {
        SDL_Color tc = textColor;
        textTexture = e9ui_text_cache_getText(ctx->renderer, tf, st->label, tc, &textW, &textH);
    }
    int iconRenderW = 0;
    int iconRenderH = 0;
    int iconMargin = 0;
    if (st->icon) {
        int lh = 0;
        if (theme->font ? theme->font : ctx->font) {
            TTF_Font *tfm = theme->font ? theme->font : ctx->font;
            lh = TTF_FontHeight(tfm);
        }
        if (lh <= 0) {
            lh = r.h - 8;
        }
        if (lh < 8) {
            lh = r.h; // fallback
        }
        int iconMaxH = (int)floorf(lh * 0.75f);
        if (iconMaxH < 10) {
            iconMaxH = (lh > 0 ? lh : 10);
        }
        int iw = st->iconW, ih = st->iconH;
        if (ih > iconMaxH) {
            float s = (float)iconMaxH / (float)ih;
            iw = (int)ceilf((float)iw * s);
            ih = iconMaxH;
        }
        iconRenderW = iw;
        iconRenderH = ih;
        iconMargin = (textW > 0 && textTexture) ? 6 : 0;
    }
    int contentWidth = iconRenderW + iconMargin + textW;
    int contentStart = r.x + (r.w - contentWidth) / 2;
    int minStart = innerStartX;
    if (contentStart < minStart) {
        contentStart = minStart;
    }
    if (st->icon) {
        Uint8 prevR = 255, prevG = 255, prevB = 255, prevA = 255;
        SDL_GetTextureColorMod(st->icon, &prevR, &prevG, &prevB);
        SDL_GetTextureAlphaMod(st->icon, &prevA);
        if (disabled) {
            SDL_SetTextureColorMod(st->icon, textColor.r, textColor.g, textColor.b);
            SDL_SetTextureAlphaMod(st->icon, 0xE0);
        }
        SDL_Rect ir = { contentStart, cy - iconRenderH/2, iconRenderW, iconRenderH };
        SDL_RenderCopy(ctx->renderer, st->icon, NULL, &ir);
        if (disabled) {
            SDL_SetTextureColorMod(st->icon, prevR, prevG, prevB);
            SDL_SetTextureAlphaMod(st->icon, prevA);
        }
    }
    if (textTexture) {
        SDL_Rect tr = { contentStart + iconRenderW + iconMargin, cy - textH/2, textW, textH };
        SDL_RenderCopy(ctx->renderer, textTexture, NULL, &tr);
    }
}

static void
e9ui_button_onHover(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    (void)ctx;
    (void)mouse_ev;
    if (!st) {
        return;
    }
    st->hover = 1;
}

static void
e9ui_button_onLeave(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    (void)ctx;
    (void)mouse_ev;
    if (!st) {
        return;
    }
    st->hover = 0;
}

static void
e9ui_button_onMouseDown(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    (void)mouse_ev;
    (void)ctx;
    if (!st || e9ui_getHidden(self) || self->disabled) {
        return;
    }
    st->pressed = 1;
}

static void
e9ui_button_onMouseUp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    (void)ctx;
    (void)mouse_ev;
    if (!st) {
        return;
    }
    st->pressed = 0;
}

static void
e9ui_button_fireClick(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *mouse_ev)
{
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    (void)mouse_ev;
    if (!st || e9ui_getHidden(self) || self->disabled) {
        return;
    }
    if (st->onClick) {
        st->onClick(ctx, st->user);
    }
}

static void
e9ui_button_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
  if (ctx && ctx->unregisterHotkey && st->hotkeyId >= 0) {
    ctx->unregisterHotkey(ctx, st->hotkeyId);
    st->hotkeyId = -1;
  }
  if (st->icon) {
    SDL_DestroyTexture(st->icon);
    st->icon = NULL;
  }
  if (st->bgCache) {
    SDL_DestroyTexture(st->bgCache);
    st->bgCache = NULL;
  }
  alloc_free(st->label);
  alloc_free(st->largestLabel);
}

e9ui_component_t *
e9ui_button_make(const char *label, e9ui_button_cb onClick, void *user)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_button_state_t *st = (e9ui_button_state_t*)alloc_calloc(1, sizeof(*st));
    if (label) {
        st->label = alloc_strdup(label);
    }
    st->onClick = onClick;
    st->user = user;
    st->hover = 0;
    st->pressed = 0;
    st->prefW = 0;
    st->prefH = 0;
    st->hotkeyId = -1;
    st->icon=NULL;
    st->iconW=0;
    st->iconH=0;
    st->useMini = 0;
    st->largestLabel = NULL;
    st->bgCache = NULL;
    st->bgCacheW = 0;
    st->bgCacheH = 0;
    st->bgCacheKey = 0;
    c->name = "e9ui_button";
    c->state = st;
    c->preferredHeight = e9ui_button_preferredHeight;
    c->layout = e9ui_button_layout;
    c->render = e9ui_button_render;
    c->dtor = e9ui_button_dtor;
    c->onHover = e9ui_button_onHover;
    c->onLeave = e9ui_button_onLeave;
    c->onMouseDown = e9ui_button_onMouseDown;
    c->onMouseUp = e9ui_button_onMouseUp;
    c->onClick = e9ui_button_fireClick;
    
    return c;
}

void
e9ui_button_measure(e9ui_component_t *btn, e9ui_context_t *ctx, int *outW, int *outH)
{
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (st && e9ui_getHidden(btn)) {
        if (outW) { *outW = 0; }
        if (outH) { *outH = 0; }
            return;
    }
    e9ui_button_updateMeasure(st, ctx);
    if (outW) {
        *outW = st->prefW;
    }
    if (outH) {
        *outH = st->prefH;
    }
}

void
e9ui_button_setLabel(e9ui_component_t *btn, const char *label)
{
    if (!btn) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (!st) {
        return;
    }
    if (strcmp(label, st->label) == 0) {
      return;
    }
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
    if (label) {
        st->label = alloc_strdup(label);
    }
}

void
e9ui_button_setLargestLabel(e9ui_component_t *btn, const char *largest_label)
{
    if (!btn) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (!st) {
        return;
    }
    if (st->largestLabel) {
        alloc_free(st->largestLabel);
        st->largestLabel = NULL;
    }
    if (largest_label && *largest_label) {
        st->largestLabel = alloc_strdup(largest_label);
    }
}



void
e9ui_button_setTheme(e9ui_component_t *btn, const e9k_theme_button_t *theme)
{
    if (!btn || !theme) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (!st) {
        return;
    }
    e9k_theme_button_t merged = *e9ui_button_getTheme(st);
    uint32_t mask = theme->mask ? theme->mask : E9K_THEME_BUTTON_MASK_ALL;
    if (mask & E9K_THEME_BUTTON_MASK_HIGHLIGHT) {
        merged.highlight = theme->highlight;
    }
    if (mask & E9K_THEME_BUTTON_MASK_BACKGROUND) {
        merged.background = theme->background;
    }
    if (mask & E9K_THEME_BUTTON_MASK_PRESSED) {
        merged.pressedBackground = theme->pressedBackground;
    }
    if (mask & E9K_THEME_BUTTON_MASK_SHADOW) {
        merged.shadow = theme->shadow;
    }
    if (mask & E9K_THEME_BUTTON_MASK_TEXT) {
        merged.text = theme->text;
    }
    if (mask & E9K_THEME_BUTTON_MASK_RADIUS) {
        merged.borderRadius = theme->borderRadius;
    }
    if (mask & E9K_THEME_BUTTON_MASK_FONT_SIZE) {
        merged.fontSize = theme->fontSize;
    }
    if (mask & E9K_THEME_BUTTON_MASK_PADDING) {
        merged.padding = theme->padding;
    }
    if (mask & E9K_THEME_BUTTON_MASK_FONT_ASSET) {
        merged.fontAsset = theme->fontAsset;
    }
    if (mask & E9K_THEME_BUTTON_MASK_FONT_STYLE) {
        merged.fontStyle = theme->fontStyle;
    }
    if (mask & E9K_THEME_BUTTON_MASK_FONT) {
        merged.font = theme->font;
    }
    merged.mask = 0;
    st->customTheme = merged;
    st->useCustomTheme = 1;
}

void
e9ui_button_clearTheme(e9ui_component_t *btn)
{
    if (!btn) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (!st) {
        return;
    }
    st->useCustomTheme = 0;
}

void
e9ui_button_setMini(e9ui_component_t *btn, int enable)
{
    if (!btn) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (!st) {
        return;
    }
    st->useMini = enable ? 1 : 0;
}

void
e9ui_button_setGlowPulse(e9ui_component_t *btn, int enable)
{
    if (!btn) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (!st) {
        return;
    }
    st->glowPulse = enable ? 1 : 0;
}

static void
e9ui_button_hotkey_tramp(e9ui_context_t *ctx, void *user)
{
    e9ui_component_t *self = (e9ui_component_t*)user;
    if (!self) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)self->state;
    if (!st) {
        return;
    }
    if (self->disabled || e9ui_getHidden(self)) {
        return;
    }
    if (st->onClick) {
        st->onClick(ctx, st->user);
    }
}

int
e9ui_button_registerHotkey(e9ui_component_t *btn, e9ui_context_t *ctx,
                             SDL_Keycode key, SDL_Keymod modMask, SDL_Keymod modValue)
{
    if (!btn || !ctx || !ctx->registerHotkey) {
        return -1;
    }
    int id = ctx->registerHotkey(ctx, key, modMask, modValue, e9ui_button_hotkey_tramp, btn);
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (st) {
        st->hotkeyId = id;
    }
    return id;
}

void
e9ui_button_setIconAsset(e9ui_component_t *btn, const char *rel_asset_png)
{
    if (!btn || !rel_asset_png || !*rel_asset_png) {
        return;
    }
    e9ui_button_state_t *st = (e9ui_button_state_t*)btn->state;
    if (st->icon) {
        SDL_DestroyTexture(st->icon);
        st->icon = NULL;
        st->iconW = st->iconH = 0;
    }
    char path[1024];
    if (!file_getAssetPath(rel_asset_png, path, sizeof(path))) {
        return;
    }
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        debug_error("ICON load failed: %s (SDL_image: %s)", path, IMG_GetError());
        return;
    }
    st->icon = SDL_CreateTextureFromSurface(debugger.ui.ctx.renderer, s);
    st->iconW = s->w;
    st->iconH = s->h;
    SDL_FreeSurface(s);
}
