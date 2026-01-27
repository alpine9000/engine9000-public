/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "geo9000.h"
#include "runtime.h"
#include "gl_composite.h"
#include "alloc.h"
#include "libretro.h"
#include "libretro_host.h"
#include "seek_bar.h"
#include "debug.h"
#include "state_buffer.h"
#include "debugger.h"
#include "ui.h"
#include "e9ui_button.h"
#include "sprite_debug.h"
#include "shader_ui.h"

typedef struct geo9000_state {
    int wasFocused;
    char *seekBarMeta;
    char *histogramBtnMeta;
    int histogramEnabled;
    char *spriteDebugBtnMeta;
    char *shaderUiBtnMeta;
    char *buttonStackMeta;
} geo9000_state_t;

#define GEO_SPRITE_COUNT 382u
#define GEO_SPRITES_PER_LINE_MAX 96u
#define GEO_SPRITE_LINE_OFFSET 32

static void
geo9000_hueToRgb(float h, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (h < 0.0f) {
        h -= (int)h;
    }
    if (h >= 1.0f) {
        h -= (int)h;
    }
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float q = 1.0f - f;
    int ii = ((int)i) % 6;
    float rr = 0.0f;
    float gg = 0.0f;
    float bb = 0.0f;
    switch (ii) {
    case 0: rr = 1.0f; gg = f; bb = 0.0f; break;
    case 1: rr = q; gg = 1.0f; bb = 0.0f; break;
    case 2: rr = 0.0f; gg = 1.0f; bb = f; break;
    case 3: rr = 0.0f; gg = q; bb = 1.0f; break;
    case 4: rr = f; gg = 0.0f; bb = 1.0f; break;
    case 5: rr = 1.0f; gg = 0.0f; bb = q; break;
    }
    *r = (Uint8)(rr * 255.0f);
    *g = (Uint8)(gg * 255.0f);
    *b = (Uint8)(bb * 255.0f);
}

static uint32_t
geo9000_argb(Uint8 a, Uint8 r, Uint8 g, Uint8 b)
{
    return (uint32_t)((a << 24) | (r << 16) | (g << 8) | b);
}

static uint32_t
geo9000_spriteHash(const uint16_t *scb2, const uint16_t *scb3, const uint16_t *scb4, unsigned count)
{
    if (!scb2 || !scb3 || !scb4 || count == 0u) {
        return 0u;
    }
    uint32_t h = 2166136261u;
    for (unsigned i = 1; i < count; ++i) {
        h ^= scb2[i];
        h *= 16777619u;
        h ^= scb3[i];
        h *= 16777619u;
        h ^= scb4[i];
        h *= 16777619u;
    }
    return h;
}

static void
geo9000_fillRectPixels(uint32_t *pixels, int width, int height, int x, int y, int w, int h, uint32_t color)
{
    if (!pixels || width <= 0 || height <= 0 || w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > width) {
        x1 = width;
    }
    if (y1 > height) {
        y1 = height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t *row = pixels + (size_t)yy * (size_t)width + x0;
        for (int xx = x0; xx < x1; ++xx) {
            row[xx - x0] = color;
        }
    }
}

static void
geo9000_drawDigits3x5Pixels(uint32_t *pixels, int width, int height,
                            int x, int y, const char *buf, uint32_t color)
{
    static const uint8_t digits[10][5] = {
        {0b111,0b101,0b101,0b101,0b111},
        {0b010,0b110,0b010,0b010,0b111},
        {0b111,0b001,0b111,0b100,0b111},
        {0b111,0b001,0b111,0b001,0b111},
        {0b101,0b101,0b111,0b001,0b001},
        {0b111,0b100,0b111,0b001,0b111},
        {0b111,0b100,0b111,0b101,0b111},
        {0b111,0b001,0b010,0b010,0b010},
        {0b111,0b101,0b111,0b101,0b111},
        {0b111,0b101,0b111,0b001,0b111},
    };
    const int glyph_w = 3;
    const int glyph_h = 5;
    const int spacing = 1;
    int cx = x;
    int cy = y;
    if (!pixels || !buf) {
        return;
    }
    for (int i = 0; buf[i]; ++i) {
        char ch = buf[i];
        if (ch < '0' || ch > '9') {
            cx += glyph_w + spacing;
            continue;
        }
        int d = ch - '0';
        for (int ry = 0; ry < glyph_h; ++ry) {
            uint8_t rowbits = digits[d][ry];
            for (int rx = 0; rx < glyph_w; ++rx) {
                if (rowbits & (uint8_t)(1u << (glyph_w - 1 - rx))) {
                    int px = cx + rx;
                    int py = cy + ry;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        pixels[py * width + px] = color;
                    }
                }
            }
        }
        cx += glyph_w + spacing;
    }
}

typedef struct geo9000_overlay_cache {
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t pixels_cap;
    int tex_w;
    int tex_h;
    uint32_t last_hash;
    int valid;
    uint32_t *grad;
    size_t grad_cap;
    int grad_w;
    int last_screen_w;
    int last_screen_h;
    int last_crop_t;
    int last_crop_b;
    int last_crop_l;
    int last_crop_r;
    unsigned last_sprlimit;
    SDL_Renderer *renderer;
} geo9000_overlay_cache_t;

static geo9000_overlay_cache_t geo9000_overlayCache = {0};

static void
geo9000_toggleHistogram(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9ui_component_t *comp = (e9ui_component_t *)user;
    if (!comp || !comp->state) {
        return;
    }
    geo9000_state_t *state = (geo9000_state_t *)comp->state;
    state->histogramEnabled = state->histogramEnabled ? 0 : 1;
}

static void
geo9000_toggleSpriteDebug(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    sprite_debug_toggle();
}

typedef struct geo9000_button_stack_state {
    int padding;
    int gap;
} geo9000_button_stack_state_t;

static void
geo9000_buttonStackMeasure(e9ui_component_t *self, e9ui_context_t *ctx, int *outW, int *outH)
{
    if (outW) {
        *outW = 0;
    }
    if (outH) {
        *outH = 0;
    }
    if (!self || !ctx || !self->state) {
        return;
    }
    geo9000_button_stack_state_t *st = (geo9000_button_stack_state_t*)self->state;
    int pad = e9ui_scale_px(ctx, st->padding);
    int gap = e9ui_scale_px(ctx, st->gap);
    int maxH = 0;
    int totalW = 0;
    int count = 0;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        int width = 0;
        int height = 0;
        e9ui_button_measure(child, ctx, &width, &height);
        if (height > maxH) {
            maxH = height;
        }
        totalW += width;
        count++;
    }
    if (count > 1) {
        totalW += gap * (count - 1);
    }
    if (outW) {
        *outW = totalW + pad * 2;
    }
    if (outH) {
        *outH = maxH + pad * 2;
    }
}

static int
geo9000_buttonStackPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    int h = 0;
    geo9000_buttonStackMeasure(self, ctx, NULL, &h);
    return h;
}

static void
geo9000_buttonStackLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    geo9000_button_stack_state_t *st = (geo9000_button_stack_state_t*)self->state;
    self->bounds = bounds;
    int pad = e9ui_scale_px(ctx, st->padding);
    int gap = e9ui_scale_px(ctx, st->gap);
    int maxH = 0;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        int width = 0;
        int height = 0;
        e9ui_button_measure(child, ctx, &width, &height);
        if (height > maxH) {
            maxH = height;
        }
    }
    int x = bounds.x + pad;
    it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        int width = 0;
        int height = 0;
        e9ui_button_measure(child, ctx, &width, &height);
        child->bounds.x = x;
        child->bounds.y = bounds.y + pad + (maxH - height) / 2;
        child->bounds.w = width;
        child->bounds.h = height;
        x += width + gap;
    }
}

static void
geo9000_buttonStackRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (child && child->render) {
            child->render(child, ctx);
        }
    }
}

static e9ui_component_t *
geo9000_buttonStackMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    geo9000_button_stack_state_t *st = (geo9000_button_stack_state_t*)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->padding = 6;
    st->gap = 6;
    comp->name = "geo9000_button_stack";
    comp->state = st;
    comp->preferredHeight = geo9000_buttonStackPreferredHeight;
    comp->layout = geo9000_buttonStackLayout;
    comp->render = geo9000_buttonStackRender;
    return comp;
}

static void
geo9000_toggleShaderUi(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (shader_ui_isOpen()) {
        shader_ui_shutdown();
        return;
    }
    if (!shader_ui_init()) {
        debug_error("shader ui: init failed");
    }
}

static void
geo9000_seekTooltip(float percent, char *out, size_t cap, void *user)
{
    (void)user;
    if (!out || cap == 0) {
        return;
    }
    size_t count = state_buffer_getCount();
    uint64_t frame_no = 0;
    if (count > 0) {
        state_frame_t *frame = state_buffer_getFrameAtPercent(percent);
        if (frame) {
            frame_no = frame->frame_no;
        }
    }
    snprintf(out, cap, "Frame %llu", (unsigned long long)frame_no);
}

static void
geo9000_spriteOverlayRender(SDL_Renderer *renderer, const SDL_Rect *dst, const geo_debug_sprite_state_t *st)
{
    if (!renderer || !dst || !st || !st->vram) {
        return;
    }
    int screen_w = (st->screen_w > 0) ? st->screen_w : 320;
    int screen_h = (st->screen_h > 0) ? st->screen_h : 224;
    int crop_t = st->crop_t;
    int crop_b = st->crop_b;
    int crop_l = st->crop_l;
    int crop_r = st->crop_r;
    int vis_w = screen_w - crop_l - crop_r;
    int vis_h = screen_h - crop_t - crop_b;
    if (vis_w <= 0 || vis_h <= 0) {
        return;
    }
    if (!st->vram_words || st->vram_words <= (0x8400u + GEO_SPRITE_COUNT)) {
        return;
    }

    unsigned sprlimit = st->sprlimit ? st->sprlimit : GEO_SPRITES_PER_LINE_MAX;
    if (sprlimit == 0) {
        sprlimit = GEO_SPRITES_PER_LINE_MAX;
    }

    unsigned sprcount_line[256];
    int lines = screen_h;
    if (lines > (int)(sizeof(sprcount_line) / sizeof(sprcount_line[0]))) {
        lines = (int)(sizeof(sprcount_line) / sizeof(sprcount_line[0]));
    }

    const uint16_t *vram = st->vram;
    const uint16_t *scb2 = vram + 0x8000;
    const uint16_t *scb3 = vram + 0x8200;
    const uint16_t *scb4 = vram + 0x8400;

    uint32_t hash = geo9000_spriteHash(scb2, scb3, scb4, GEO_SPRITE_COUNT);
    int params_changed = 0;
    if (geo9000_overlayCache.renderer != renderer) {
        if (geo9000_overlayCache.texture) {
            SDL_DestroyTexture(geo9000_overlayCache.texture);
            geo9000_overlayCache.texture = NULL;
        }
        geo9000_overlayCache.renderer = renderer;
        geo9000_overlayCache.valid = 0;
    }
    if (geo9000_overlayCache.last_screen_w != screen_w ||
        geo9000_overlayCache.last_screen_h != screen_h ||
        geo9000_overlayCache.last_crop_t != crop_t ||
        geo9000_overlayCache.last_crop_b != crop_b ||
        geo9000_overlayCache.last_crop_l != crop_l ||
        geo9000_overlayCache.last_crop_r != crop_r ||
        geo9000_overlayCache.last_sprlimit != sprlimit) {
        params_changed = 1;
    }
    if (geo9000_overlayCache.tex_w != vis_w || geo9000_overlayCache.tex_h != vis_h) {
        params_changed = 1;
    }
    if (!geo9000_overlayCache.grad || geo9000_overlayCache.grad_w != screen_w) {
        size_t needed = (size_t)(screen_w > 0 ? screen_w : 1);
        if (needed > geo9000_overlayCache.grad_cap) {
            uint32_t *next = (uint32_t *)realloc(geo9000_overlayCache.grad, needed * sizeof(uint32_t));
            if (!next) {
                return;
            }
            geo9000_overlayCache.grad = next;
            geo9000_overlayCache.grad_cap = needed;
        }
        geo9000_overlayCache.grad_w = screen_w;
        int denomx = (screen_w > 1) ? (screen_w - 1) : 1;
        for (int dx = 0; dx < screen_w; ++dx) {
            float t = (float)dx / (float)denomx;
            float h = (1.0f / 3.0f) * (1.0f - t);
            Uint8 rr;
            Uint8 gg;
            Uint8 bb;
            geo9000_hueToRgb(h, &rr, &gg, &bb);
            geo9000_overlayCache.grad[dx] = geo9000_argb(160, rr, gg, bb);
        }
    }
    if (geo9000_overlayCache.valid && !params_changed && geo9000_overlayCache.last_hash == hash) {
        SDL_SetTextureBlendMode(geo9000_overlayCache.texture, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(renderer, geo9000_overlayCache.texture, NULL, dst);
        return;
    }

    if (!geo9000_overlayCache.texture || geo9000_overlayCache.tex_w != vis_w || geo9000_overlayCache.tex_h != vis_h) {
        if (geo9000_overlayCache.texture) {
            SDL_DestroyTexture(geo9000_overlayCache.texture);
        }
        geo9000_overlayCache.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                         SDL_TEXTUREACCESS_STREAMING, vis_w, vis_h);
        geo9000_overlayCache.tex_w = vis_w;
        geo9000_overlayCache.tex_h = vis_h;
        if (!geo9000_overlayCache.texture) {
            return;
        }
    }
    size_t pix_needed = (size_t)vis_w * (size_t)vis_h;
    if (pix_needed > geo9000_overlayCache.pixels_cap) {
        uint32_t *next = (uint32_t *)realloc(geo9000_overlayCache.pixels, pix_needed * sizeof(uint32_t));
        if (!next) {
            return;
        }
        geo9000_overlayCache.pixels = next;
        geo9000_overlayCache.pixels_cap = pix_needed;
    }
    uint32_t *pixels = geo9000_overlayCache.pixels;
    for (size_t i = 0; i < pix_needed; ++i) {
        pixels[i] = 0u;
    }

    int active_total = 0;
    for (unsigned i = 1; i < GEO_SPRITE_COUNT; ) {
        if (scb3[i] & 0x40u) {
            ++i;
            continue;
        }
        uint16_t scb3b = scb3[i];
        unsigned bh = (unsigned)(scb3b & 0x3f);
        unsigned by = (unsigned)((scb3b >> 7) & 0x01ff);
        unsigned len = 1;
        while ((i + len) < GEO_SPRITE_COUNT && (scb3[i + len] & 0x40u)) {
            ++len;
        }
        if (bh != 0 && by != (unsigned)screen_h) {
            active_total += (int)len;
        }
        i += len;
    }

    int maxcnt = 0;
    for (int line = 0; line < lines; ++line) {
        unsigned sprcount = 0;
        unsigned xpos = 0;
        unsigned ypos = 0;
        unsigned sprsize = 0;
        unsigned hshrink = 0x0f;

        for (unsigned i = 1; i < GEO_SPRITE_COUNT; ++i) {
            uint16_t scb3w = scb3[i];
            uint16_t scb2w = scb2[i];
            uint16_t scb4w = scb4[i];
            if (scb3w & 0x40u) {
                xpos = (unsigned)((xpos + (hshrink + 1)) & 0x1ff);
            } else {
                xpos = (unsigned)((scb4w >> 7) & 0x1ff);
                ypos = (unsigned)((scb3w >> 7) & 0x1ff);
                sprsize = (unsigned)(scb3w & 0x3f);
            }
            hshrink = (unsigned)((scb2w >> 8) & 0x0f);
            int vline = line + GEO_SPRITE_LINE_OFFSET;
            unsigned srow = (unsigned)(((vline - (int)(0x200 - (int)ypos))) & 0x1ff);
            if ((sprsize == 0) || (srow >= (sprsize << 4))) {
                continue;
            }
            sprcount++;
        }
        sprcount_line[line] = sprcount;
        if ((int)sprcount > maxcnt) {
            maxcnt = (int)sprcount;
        }
    }

    for (int line = 0; line < lines; ++line) {
        int cnt = (int)sprcount_line[line];
        int bar_len = (int)((cnt * (int)screen_w) / (int)sprlimit);
        if (bar_len > screen_w) {
            bar_len = screen_w;
        }
        if (bar_len <= 0) {
            continue;
        }
        int vy = line - crop_t;
        if (vy < 0 || vy >= vis_h) {
            continue;
        }
        int start = crop_l;
        int end = crop_l + vis_w - 1;
        if (start < 0) {
            start = 0;
        }
        if (end >= bar_len) {
            end = bar_len - 1;
        }
        if (start > end) {
            continue;
        }
        uint32_t *row = pixels + (size_t)vy * (size_t)vis_w;
        for (int dx = start; dx <= end; ++dx) {
            int vx = dx - crop_l;
            row[vx] = geo9000_overlayCache.grad[dx];
        }
    }

    {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%d", maxcnt);
        if (n < 1) {
            n = 1;
        }
        if (n > (int)(sizeof(buf) - 1)) {
            n = (int)(sizeof(buf) - 1);
            buf[n] = '\0';
        }
        int glyph_w = 3;
        int glyph_h = 5;
        int spacing = 1;
        int text_w = n * glyph_w + (n - 1) * spacing;
        int text_h = glyph_h;
        int pad = 4;
        int badge_w = text_w + pad * 2;
        int badge_h = text_h + pad * 2;
        int bx = vis_w - badge_w - 4;
        int by = 4;
        if (bx < 0) {
            bx = 0;
        }
        uint32_t badge_col = (maxcnt > (int)sprlimit)
                             ? geo9000_argb(200, 200, 0, 0)
                             : geo9000_argb(180, 64, 64, 64);
        geo9000_fillRectPixels(pixels, vis_w, vis_h, bx, by, badge_w, badge_h, badge_col);
        geo9000_drawDigits3x5Pixels(pixels, vis_w, vis_h, bx + pad, by + pad, buf, geo9000_argb(255, 255, 255, 255));
    }

    {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%d", active_total);
        if (n < 1) {
            n = 1;
        }
        if (n > (int)(sizeof(buf) - 1)) {
            n = (int)(sizeof(buf) - 1);
            buf[n] = '\0';
        }
        int glyph_w = 3;
        int glyph_h = 5;
        int spacing = 1;
        int text_w = n * glyph_w + (n - 1) * spacing;
        int text_h = glyph_h;
        int pad = 4;
        int bx = 4;
        int by = 4;
        uint32_t badge_col = (active_total > (int)(GEO_SPRITE_COUNT - 1))
                             ? geo9000_argb(200, 200, 0, 0)
                             : geo9000_argb(180, 64, 64, 64);
        geo9000_fillRectPixels(pixels, vis_w, vis_h, bx, by, text_w + pad * 2, text_h + pad * 2, badge_col);
        geo9000_drawDigits3x5Pixels(pixels, vis_w, vis_h, bx + pad, by + pad, buf, geo9000_argb(255, 255, 255, 255));
    }

    SDL_UpdateTexture(geo9000_overlayCache.texture, NULL, pixels, vis_w * (int)sizeof(uint32_t));
    SDL_SetTextureBlendMode(geo9000_overlayCache.texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, geo9000_overlayCache.texture, NULL, dst);
    geo9000_overlayCache.last_hash = hash;
    geo9000_overlayCache.valid = 1;
    geo9000_overlayCache.last_screen_w = screen_w;
    geo9000_overlayCache.last_screen_h = screen_h;
    geo9000_overlayCache.last_crop_t = crop_t;
    geo9000_overlayCache.last_crop_b = crop_b;
    geo9000_overlayCache.last_crop_l = crop_l;
    geo9000_overlayCache.last_crop_r = crop_r;
    geo9000_overlayCache.last_sprlimit = sprlimit;
}

static int
geo9000_viewPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
geo9000_viewLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static int
geo9000_mapKeyToJoypad(SDL_Keycode key, unsigned *id)
{
    if (!id) {
        return 0;
    }
    switch (key) {
    case SDLK_UP:
        *id = RETRO_DEVICE_ID_JOYPAD_UP;
        return 1;
    case SDLK_DOWN:
        *id = RETRO_DEVICE_ID_JOYPAD_DOWN;
        return 1;
    case SDLK_LEFT:
        *id = RETRO_DEVICE_ID_JOYPAD_LEFT;
        return 1;
    case SDLK_RIGHT:
        *id = RETRO_DEVICE_ID_JOYPAD_RIGHT;
        return 1;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        *id = RETRO_DEVICE_ID_JOYPAD_B;
        return 1;
    case SDLK_LALT:
    case SDLK_RALT:
        *id = RETRO_DEVICE_ID_JOYPAD_A;
        return 1;
    case SDLK_SPACE:
        *id = RETRO_DEVICE_ID_JOYPAD_Y;
        return 1;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        *id = RETRO_DEVICE_ID_JOYPAD_X;
        return 1;
    case SDLK_1:
        *id = RETRO_DEVICE_ID_JOYPAD_START;
        return 1;
    case SDLK_5:
        *id = RETRO_DEVICE_ID_JOYPAD_SELECT;
        return 1;
    default:
        break;
    }
    return 0;
}

static uint16_t
geo9000_translateModifiers(SDL_Keymod mod)
{
    uint16_t out = 0;
    if (mod & KMOD_SHIFT) {
        out |= RETROKMOD_SHIFT;
    }
    if (mod & KMOD_CTRL) {
        out |= RETROKMOD_CTRL;
    }
    if (mod & KMOD_ALT) {
        out |= RETROKMOD_ALT;
    }
    if (mod & KMOD_GUI) {
        out |= RETROKMOD_META;
    }
    if (mod & KMOD_NUM) {
        out |= RETROKMOD_NUMLOCK;
    }
    if (mod & KMOD_CAPS) {
        out |= RETROKMOD_CAPSLOCK;
    }
    return out;
}

static uint32_t
geo9000_translateCharacter(SDL_Keycode key, SDL_Keymod mod)
{
    if (key < 32 || key >= 127) {
        return 0;
    }
    int shift = (mod & KMOD_SHIFT) ? 1 : 0;
    int caps = (mod & KMOD_CAPS) ? 1 : 0;
    if (key >= 'a' && key <= 'z') {
        if (shift ^ caps) {
            return (uint32_t)toupper((int)key);
        }
        return (uint32_t)key;
    }
    if (!shift) {
        return (uint32_t)key;
    }
    switch (key) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    case '`': return '~';
    default:
        break;
    }
    return (uint32_t)key;
}

static unsigned
geo9000_translateKey(SDL_Keycode key)
{
    if (key >= 32 && key < 127) {
        if (key >= 'A' && key <= 'Z') {
            return (unsigned)tolower((int)key);
        }
        return (unsigned)key;
    }
    switch (key) {
    case SDLK_BACKSPACE: return RETROK_BACKSPACE;
    case SDLK_TAB: return RETROK_TAB;
    case SDLK_RETURN: return RETROK_RETURN;
    case SDLK_ESCAPE: return RETROK_ESCAPE;
    case SDLK_DELETE: return RETROK_DELETE;
    case SDLK_INSERT: return RETROK_INSERT;
    case SDLK_HOME: return RETROK_HOME;
    case SDLK_END: return RETROK_END;
    case SDLK_PAGEUP: return RETROK_PAGEUP;
    case SDLK_PAGEDOWN: return RETROK_PAGEDOWN;
    case SDLK_UP: return RETROK_UP;
    case SDLK_DOWN: return RETROK_DOWN;
    case SDLK_LEFT: return RETROK_LEFT;
    case SDLK_RIGHT: return RETROK_RIGHT;
    case SDLK_F1: return RETROK_F1;
    case SDLK_F2: return RETROK_F2;
    case SDLK_F3: return RETROK_F3;
    case SDLK_F4: return RETROK_F4;
    case SDLK_F5: return RETROK_F5;
    case SDLK_F6: return RETROK_F6;
    case SDLK_F7: return RETROK_F7;
    case SDLK_F8: return RETROK_F8;
    case SDLK_F9: return RETROK_F9;
    case SDLK_F10: return RETROK_F10;
    case SDLK_F11: return RETROK_F11;
    case SDLK_F12: return RETROK_F12;
    case SDLK_LSHIFT: return RETROK_LSHIFT;
    case SDLK_RSHIFT: return RETROK_RSHIFT;
    case SDLK_LCTRL: return RETROK_LCTRL;
    case SDLK_RCTRL: return RETROK_RCTRL;
    case SDLK_LALT: return RETROK_LALT;
    case SDLK_RALT: return RETROK_RALT;
    case SDLK_LGUI: return RETROK_LMETA;
    case SDLK_RGUI: return RETROK_RMETA;
    default:
        break;
    }
    return RETROK_UNKNOWN;
}

static void
geo9000_seekBarChanged(float percent, void *user)
{
    (void)user;
    debugger.frameCounter = state_buffer_getCurrentFrameNo();
    if (debugger_isSeeking()) {
      state_frame_t* frame = state_buffer_getFrameAtPercent(percent);
      if (!frame) {
          return;
      }
      debugger.frameCounter = frame->frame_no;
      runtime_executeFrame(DEBUGGER_RUNMODE_RESTORE, frame->frame_no);
      if (!*machine_getRunningState(debugger.machine)) {
          ui_refreshOnPause();
      }
    }
    (void)percent;
}

static void
geo9000_seekBarDragging(int dragging, float percent, void *user)
{
    e9ui_component_t *seek = (e9ui_component_t*)user;
    state_buffer_setPaused(dragging ? 1 : 0);
    debugger_setSeeking(dragging ? 1 : 0);
    if (!dragging) {
        state_buffer_trimAfterPercent(percent);
        if (seek) {
            seek_bar_setPercent(seek, 1.0f);
        }
    }
}

static int
geo9000_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)ctx;
    if (!self || !ev) {
        return 0;
    }
    geo9000_state_t *state = (geo9000_state_t *)self->state;
    if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEMOTION) {
        if (state && state->seekBarMeta) {
            e9ui_component_t *seek = e9ui_child_find(self, state->seekBarMeta);
            if (seek && seek->handleEvent && seek->handleEvent(seek, ctx, ev)) {
                return 1;
            }
        }
    }
    if (ev->type != SDL_KEYDOWN && ev->type != SDL_KEYUP) {
        return 0;
    }
    unsigned id = 0;
    if (ev->type == SDL_KEYDOWN && ev->key.repeat) {
        return 1;
    }
    int pressed = (ev->type == SDL_KEYDOWN) ? 1 : 0;
    if (ev->key.keysym.sym == SDLK_F5) {
        if (pressed) {
            debugger_toggleSpeed();
        }
        return 1;
    }
    if (ev->key.keysym.sym == SDLK_f) {
        if (pressed) {
            debugger.frameStepMode = 1;
            debugger.frameStepPending = 1;
        }
        return 1;
    }
    if (ev->key.keysym.sym == SDLK_b) {
        if (pressed) {
            debugger.frameStepMode = 1;
            debugger.frameStepPending = -1;
        }
        return 1;
    }    
    if (ev->key.keysym.sym == SDLK_g) {
        if (pressed) {
            debugger.frameStepMode = 0;
            debugger.frameStepPending = 0;
        }
        return 1;
    }
    if (geo9000_mapKeyToJoypad(ev->key.keysym.sym, &id)) {
        libretro_host_setJoypadState(0, id, pressed);
    } else {
        SDL_Keycode key = ev->key.keysym.sym;
        uint32_t character = geo9000_translateCharacter(key, ev->key.keysym.mod);
        unsigned retro_key = geo9000_translateKey(key);
        uint16_t mods = geo9000_translateModifiers(ev->key.keysym.mod);
        libretro_host_sendKeyEvent(retro_key, character, mods, pressed);
    }
    return 1;
}

static SDL_Rect
geo9000_fitRect(e9ui_rect_t bounds, int tex_w, int tex_h)
{
    SDL_Rect dst = { bounds.x, bounds.y, bounds.w, bounds.h };
    if (tex_w <= 0 || tex_h <= 0 || bounds.w <= 0 || bounds.h <= 0) {
        return dst;
    }
    double tex_aspect = (double)tex_w / (double)tex_h;
    double bound_aspect = (double)bounds.w / (double)bounds.h;
    if (tex_aspect > bound_aspect) {
        int height = (int)((double)bounds.w / tex_aspect);
        int y = bounds.y + (bounds.h - height) / 2;
        dst.x = bounds.x;
        dst.y = y;
        dst.w = bounds.w;
        dst.h = height;
    } else {
        int width = (int)((double)bounds.h * tex_aspect);
        int x = bounds.x + (bounds.w - width) / 2;
        dst.x = x;
        dst.y = bounds.y;
        dst.w = width;
        dst.h = bounds.h;
    }
    return dst;
}

static void
geo9000_viewRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    geo9000_state_t *state = (geo9000_state_t *)self->state;
    int focused = (e9ui_getFocus(ctx) == self);
    if (!focused && state && state->wasFocused) {
        libretro_host_clearJoypadState();
    }
    if (state) {
        state->wasFocused = focused;
    }
    const uint8_t *data = NULL;
    int tex_w = 0;
    int tex_h = 0;
    size_t pitch = 0;
    if (!libretro_host_getFrame(&data, &tex_w, &tex_h, &pitch)) {
        return;
    }
    SDL_Rect dst = geo9000_fitRect(self->bounds, tex_w, tex_h);
    if (gl_composite_isActive()) {
        if (e9ui->glCompositeCapture) {
            if (gl_composite_captureToRenderer(ctx->renderer, data, tex_w, tex_h, pitch, &dst)) {
                /* Base drawn. */
            }
        } else {
            gl_composite_renderFrame(ctx->renderer, data, tex_w, tex_h, pitch, &dst);
        }
    } else {
        SDL_Texture *tex = libretro_host_getTexture(ctx->renderer);
        if (!tex) {
            return;
        }
        SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
    }

    if (state && state->histogramEnabled && debugger.spriteShadowReady) {
        geo9000_spriteOverlayRender(ctx->renderer, &dst, &debugger.spriteShadow);
    }

    if (sprite_debug_is_open() && debugger.spriteShadowReady) {
        sprite_debug_render(&debugger.spriteShadow);
    }

    if (state && state->buttonStackMeta) {
        e9ui_component_t *stack = e9ui_child_find(self, state->buttonStackMeta);
        if (stack) {
            int margin = e9ui_scale_px(ctx, 8);
            int stackW = 0;
            int stackH = 0;
            geo9000_buttonStackMeasure(stack, ctx, &stackW, &stackH);
            if (stackW <= 0 || stackH <= 0) {
                return;
            }
            stack->bounds.x = dst.x + dst.w - stackW - margin;
            stack->bounds.y = dst.y + margin;
            stack->bounds.w = stackW;
            stack->bounds.h = stackH;
            if (stack->layout) {
                stack->layout(stack, ctx, stack->bounds);
            }
            e9ui_setAutoHideClip(stack, &self->bounds);
            if (!e9ui_getHidden(stack) && stack->render) {
                stack->render(stack, ctx);
            }
        }
    }

    if (state && state->seekBarMeta) {
            e9ui_component_t *seek = e9ui_child_find(self, state->seekBarMeta);
            if (seek) {
                e9ui_rect_t vid_bounds = { dst.x, dst.y, dst.w, dst.h };
                seek_bar_layoutInParent(seek, ctx, vid_bounds);
                e9ui_setAutoHideClip(seek, &self->bounds);
                if (!e9ui_getHidden(seek) && seek->render) {
                    seek->render(seek, ctx);
                }
            }
        }
}

static void
geo9000_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    (void)self;
}

e9ui_component_t *
geo9000_makeComponent(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    geo9000_state_t *state = (geo9000_state_t*)alloc_calloc(1, sizeof(*state));
    comp->name = "geo9000";
    comp->preferredHeight = geo9000_viewPreferredHeight;
    comp->layout = geo9000_viewLayout;
    comp->render = geo9000_viewRender;
    comp->handleEvent = geo9000_handleEvent;
    comp->dtor = geo9000_dtor;
    comp->focusable = 1;
    comp->state = state;

    state->histogramEnabled = 0;
    e9ui_component_t *button_stack = geo9000_buttonStackMake();
    if (button_stack) {
        e9ui_setAutoHide(button_stack, 1, 64);
        e9ui_setFocusTarget(button_stack, comp);
        state->buttonStackMeta = alloc_strdup("button_stack");
        e9ui_child_add(comp, button_stack, state->buttonStackMeta);
    }

    e9ui_component_t *btn = e9ui_button_make("Histogram", geo9000_toggleHistogram, comp);
    if (btn) {
        e9ui_button_setMini(btn, 1);
        e9ui_setFocusTarget(btn, comp);
        state->histogramBtnMeta = alloc_strdup("histogram");
        if (button_stack) {
            e9ui_child_add(button_stack, btn, state->histogramBtnMeta);
        } else {
            e9ui_child_add(comp, btn, state->histogramBtnMeta);
        }
    }

    e9ui_component_t *btn_debug = e9ui_button_make("Sprite Debug", geo9000_toggleSpriteDebug, comp);
    if (btn_debug) {
        e9ui_button_setMini(btn_debug, 1);
        e9ui_setFocusTarget(btn_debug, comp);
        state->spriteDebugBtnMeta = alloc_strdup("sprite_debug");
        if (button_stack) {
            e9ui_child_add(button_stack, btn_debug, state->spriteDebugBtnMeta);
        } else {
            e9ui_child_add(comp, btn_debug, state->spriteDebugBtnMeta);
        }
    }

    e9ui_component_t *btn_shader = e9ui_button_make("CRT Settings", geo9000_toggleShaderUi, comp);
    if (btn_shader) {
        e9ui_button_setMini(btn_shader, 1);
        e9ui_setFocusTarget(btn_shader, comp);
        state->shaderUiBtnMeta = alloc_strdup("shader_ui");
        if (button_stack) {
            e9ui_child_add(button_stack, btn_shader, state->shaderUiBtnMeta);
        } else {
            e9ui_child_add(comp, btn_shader, state->shaderUiBtnMeta);
        }
    }

    e9ui_component_t *seek = seek_bar_make();
    if (seek) {
        seek_bar_setMargins(seek, 18, 18, 10);
        seek_bar_setHeight(seek, 14);
        seek_bar_setHoverMargin(seek, 18);
        seek_bar_setCallback(seek, geo9000_seekBarChanged, NULL);
        seek_bar_setDragCallback(seek, geo9000_seekBarDragging, seek);
        seek_bar_setTooltipCallback(seek, geo9000_seekTooltip, NULL);
        e9ui_setAutoHide(seek, 1, seek_bar_getHoverMargin(seek));
        state->seekBarMeta = alloc_strdup("seek_bar");
        e9ui_child_add(comp, seek, state->seekBarMeta);
    }
    return comp;
}
