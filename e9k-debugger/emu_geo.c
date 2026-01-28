/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"
#include "e9ui.h"
#include "sprite_debug.h"
#include "libretro.h"

#define GEO_SPRITE_COUNT 382u
#define GEO_SPRITES_PER_LINE_MAX 96u
#define GEO_SPRITE_LINE_OFFSET 32

typedef struct  {
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
} emu_geo_overlay_cache_t;

static emu_geo_overlay_cache_t emu_geo_overlayCache = {0};
static int emu_geo_histogramEnabled = 0;

static void
emu_geo_toggleHistogram(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9ui_component_t *comp = (e9ui_component_t *)user;
    if (!comp || !comp->state) {
        return;
    }
    emu_geo_histogramEnabled = emu_geo_histogramEnabled ? 0 : 1;
}

static void
emu_geo_toggleSpriteDebug(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    sprite_debug_toggle();
}

void
emu_geo_createOverlays(e9ui_component_t* comp, e9ui_component_t* button_stack)
{
  e9ui_component_t *btn = e9ui_button_make("Histogram", emu_geo_toggleHistogram, comp);
  if (btn) {
    e9ui_button_setMini(btn, 1);
    e9ui_setFocusTarget(btn, comp);
    void* histogramBtnMeta = alloc_strdup("histogram");
    if (button_stack) {
      e9ui_child_add(button_stack, btn, histogramBtnMeta);
    } else {
      e9ui_child_add(comp, btn, histogramBtnMeta);
    }
  }
  
  e9ui_component_t *btn_debug = e9ui_button_make("Sprite Debug", emu_geo_toggleSpriteDebug, comp);
  if (btn_debug) {
    e9ui_button_setMini(btn_debug, 1);
    e9ui_setFocusTarget(btn_debug, comp);
    void* spriteDebugBtnMeta = alloc_strdup("sprite_debug");
    if (button_stack) {
      e9ui_child_add(button_stack, btn_debug, spriteDebugBtnMeta);
    } else {
      e9ui_child_add(comp, btn_debug, spriteDebugBtnMeta);
    }
  }
}


static void
emu_geo_hueToRgb(float h, Uint8 *r, Uint8 *g, Uint8 *b)
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
emu_geo_argb(Uint8 a, Uint8 r, Uint8 g, Uint8 b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t
emu_geo_spriteHash(const uint16_t *scb2, const uint16_t *scb3, const uint16_t *scb4, unsigned count)
{
    if (!scb2 || !scb3 || !scb4 || count == 0u) {
        return 0u;
    }
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < count; ++i) {
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
emu_geo_fillRectPixels(uint32_t *pixels, int width, int height, int x, int y, int w, int h, uint32_t color)
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
emu_geo_drawDigits3x5Pixels(uint32_t *pixels, int width, int height,
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


static void
emu_geo_spriteOverlayRender(SDL_Renderer *renderer, const SDL_Rect *dst, const geo_debug_sprite_state_t *st)
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

    uint32_t hash = emu_geo_spriteHash(scb2, scb3, scb4, GEO_SPRITE_COUNT);
    int params_changed = 0;
    if (emu_geo_overlayCache.renderer != renderer) {
        if (emu_geo_overlayCache.texture) {
            SDL_DestroyTexture(emu_geo_overlayCache.texture);
            emu_geo_overlayCache.texture = NULL;
        }
        emu_geo_overlayCache.renderer = renderer;
        emu_geo_overlayCache.valid = 0;
    }
    if (emu_geo_overlayCache.last_screen_w != screen_w ||
        emu_geo_overlayCache.last_screen_h != screen_h ||
        emu_geo_overlayCache.last_crop_t != crop_t ||
        emu_geo_overlayCache.last_crop_b != crop_b ||
        emu_geo_overlayCache.last_crop_l != crop_l ||
        emu_geo_overlayCache.last_crop_r != crop_r ||
        emu_geo_overlayCache.last_sprlimit != sprlimit) {
        params_changed = 1;
    }
    if (emu_geo_overlayCache.tex_w != vis_w || emu_geo_overlayCache.tex_h != vis_h) {
        params_changed = 1;
    }
    if (!emu_geo_overlayCache.grad || emu_geo_overlayCache.grad_w != screen_w) {
        size_t needed = (size_t)(screen_w > 0 ? screen_w : 1);
        if (needed > emu_geo_overlayCache.grad_cap) {
            uint32_t *next = (uint32_t *)realloc(emu_geo_overlayCache.grad, needed * sizeof(uint32_t));
            if (!next) {
                return;
            }
            emu_geo_overlayCache.grad = next;
            emu_geo_overlayCache.grad_cap = needed;
        }
        emu_geo_overlayCache.grad_w = screen_w;
        int denomx = (screen_w > 1) ? (screen_w - 1) : 1;
        for (int dx = 0; dx < screen_w; ++dx) {
            float t = (float)dx / (float)denomx;
            float h = (1.0f / 3.0f) * (1.0f - t);
            Uint8 rr;
            Uint8 gg;
            Uint8 bb;
            emu_geo_hueToRgb(h, &rr, &gg, &bb);
            emu_geo_overlayCache.grad[dx] = emu_geo_argb(160, rr, gg, bb);
        }
    }
    if (emu_geo_overlayCache.valid && !params_changed && emu_geo_overlayCache.last_hash == hash) {
        SDL_SetTextureBlendMode(emu_geo_overlayCache.texture, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(renderer, emu_geo_overlayCache.texture, NULL, dst);
        return;
    }

    if (!emu_geo_overlayCache.texture || emu_geo_overlayCache.tex_w != vis_w || emu_geo_overlayCache.tex_h != vis_h) {
        if (emu_geo_overlayCache.texture) {
            SDL_DestroyTexture(emu_geo_overlayCache.texture);
        }
        emu_geo_overlayCache.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                         SDL_TEXTUREACCESS_STREAMING, vis_w, vis_h);
        emu_geo_overlayCache.tex_w = vis_w;
        emu_geo_overlayCache.tex_h = vis_h;
        if (!emu_geo_overlayCache.texture) {
            return;
        }
    }
    size_t pix_needed = (size_t)vis_w * (size_t)vis_h;
    if (pix_needed > emu_geo_overlayCache.pixels_cap) {
        uint32_t *next = (uint32_t *)realloc(emu_geo_overlayCache.pixels, pix_needed * sizeof(uint32_t));
        if (!next) {
            return;
        }
        emu_geo_overlayCache.pixels = next;
        emu_geo_overlayCache.pixels_cap = pix_needed;
    }
    uint32_t *pixels = emu_geo_overlayCache.pixels;
    for (size_t i = 0; i < pix_needed; ++i) {
        pixels[i] = 0u;
    }

    int active_total = 0;
    for (unsigned i = 0; i < GEO_SPRITE_COUNT; ) {
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

        for (unsigned i = 0; i < GEO_SPRITE_COUNT; ++i) {
            uint16_t scb3w = scb3[i];
            uint16_t scb2w = scb2[i];
            uint16_t scb4w = scb4[i];
            if (i && (scb3w & 0x40u)) {
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
            row[vx] = emu_geo_overlayCache.grad[dx];
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
                             ? emu_geo_argb(200, 200, 0, 0)
                             : emu_geo_argb(180, 64, 64, 64);
        emu_geo_fillRectPixels(pixels, vis_w, vis_h, bx, by, badge_w, badge_h, badge_col);
        emu_geo_drawDigits3x5Pixels(pixels, vis_w, vis_h, bx + pad, by + pad, buf, emu_geo_argb(255, 255, 255, 255));
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
                             ? emu_geo_argb(200, 200, 0, 0)
                             : emu_geo_argb(180, 64, 64, 64);
        emu_geo_fillRectPixels(pixels, vis_w, vis_h, bx, by, text_w + pad * 2, text_h + pad * 2, badge_col);
        emu_geo_drawDigits3x5Pixels(pixels, vis_w, vis_h, bx + pad, by + pad, buf, emu_geo_argb(255, 255, 255, 255));
    }

    SDL_UpdateTexture(emu_geo_overlayCache.texture, NULL, pixels, vis_w * (int)sizeof(uint32_t));
    SDL_SetTextureBlendMode(emu_geo_overlayCache.texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, emu_geo_overlayCache.texture, NULL, dst);
    emu_geo_overlayCache.last_hash = hash;
    emu_geo_overlayCache.valid = 1;
    emu_geo_overlayCache.last_screen_w = screen_w;
    emu_geo_overlayCache.last_screen_h = screen_h;
    emu_geo_overlayCache.last_crop_t = crop_t;
    emu_geo_overlayCache.last_crop_b = crop_b;
    emu_geo_overlayCache.last_crop_l = crop_l;
    emu_geo_overlayCache.last_crop_r = crop_r;
    emu_geo_overlayCache.last_sprlimit = sprlimit;
}

int
emu_geo_mapKeyToJoypad(SDL_Keycode key, unsigned *id)
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

uint16_t
emu_geo_translateModifiers(SDL_Keymod mod)
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

uint32_t
emu_geo_translateCharacter(SDL_Keycode key, SDL_Keymod mod)
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

unsigned
emu_geo_translateKey(SDL_Keycode key)
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

void
emu_geo_render(e9ui_context_t *ctx, SDL_Rect* dst)    
{
  if (emu_geo_histogramEnabled && debugger.spriteShadowReady) {
    emu_geo_spriteOverlayRender(ctx->renderer, dst, &debugger.spriteShadow);
  }
  
  if (sprite_debug_is_open() && debugger.spriteShadowReady) {
    sprite_debug_render(&debugger.spriteShadow);
  }  
}

const emu_system_iface_t emu_geo_iface = {  
  .translateCharacter = emu_geo_translateCharacter,
  .translateModifiers = emu_geo_translateModifiers,
  .translateKey = emu_geo_translateKey,
  .mapKeyToJoypad = emu_geo_mapKeyToJoypad,
  .createOverlays = emu_geo_createOverlays,
  .render = emu_geo_render,
};

void
emu_geo_render(e9ui_context_t *ctx, SDL_Rect* dst);
