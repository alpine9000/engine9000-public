/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif
#include <time.h>
#include <limits.h>
#include <math.h>
#if !defined(_WIN32) || defined(__MINGW32__)
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "e9ui.h"
#include "debugger.h"
#include "ui.h"
#include "config.h"
#include "help.h"
#include "file.h"
#include "debug_font.h"
#include "e9ui_theme.h"
#include "debug.h"
#include "smoke_test.h"
#include "sprite_debug.h"
#include "libretro_host.h"
#include "libretro.h"
#include "e9ui_text_cache.h"
#include "e9ui_theme_defaults.h"
#include "transition.h"
#include "crt.h"
#include "gl_composite.h"
#include "input_record.h"
#include "shader_ui.h"
#include "memory_track_ui.h"
#include "prompt.h"

static SDL_GameController *e9ui_controller = NULL;
static SDL_JoystickID e9ui_controllerId = -1;
static int e9ui_controllerLeft = 0;
static int e9ui_controllerRight = 0;
static int e9ui_controllerUp = 0;
static int e9ui_controllerDown = 0;
static const int e9ui_controllerDeadzone = 8000;
static uint32_t e9ui_fullscreenHintStart = 0;
static TTF_Font *e9ui_fullscreenHintFont = NULL;
static int e9ui_fullscreenHintSize = 0;
static const char *e9ui_transientMessage = NULL;
static const char e9ui_fullscreenMessage[] = "PRESS ESC TO EXIT FULLSCREEN";
static int e9ui_loadingLayout = 0;
static int e9ui_fpsEnabled = 0;
static uint32_t e9ui_fpsLastTick = 0;
static int e9ui_fpsFrames = 0;
static float e9ui_fpsValue = 0.0f;
static TTF_Font *e9ui_fpsFont = NULL;
static int e9ui_fpsFontSize = 0;

static void
e9ui_applyWindowIcon(SDL_Window *win)
{
  if (!win) {
    return;
  }
#ifdef _WIN32
  const char *icon_asset = "assets/icons/w64/engine9000.ico";
#else
  const char *icon_asset = "assets/icons/osx/engine9000.png";
#endif
  char path[PATH_MAX];
  if (!file_getAssetPath(icon_asset, path, sizeof(path))) {
    return;
  }
  SDL_Surface *s = IMG_Load(path);
  if (!s) {
    debug_error("icon: failed to load %s: %s", path, IMG_GetError());
    return;
  }
  SDL_SetWindowIcon(win, s);
  SDL_FreeSurface(s);
}

static void
e9ui_drawRoundedFill(SDL_Renderer *renderer, const SDL_Rect *rect, SDL_Color color)
{
  if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) {
    return;
  }
  int radius = rect->h / 2;
  if (radius < 1) {
    radius = 1;
  }
  if (radius * 2 > rect->w) {
    radius = rect->w / 2;
  }
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  for (int yy = 0; yy < rect->h; ++yy) {
    int xoff = 0;
    if (yy < radius) {
      float dy = (float)(radius - yy - 0.5f);
      float dx = sqrtf((float)(radius * radius) - dy * dy);
      xoff = radius - (int)ceilf(dx);
    } else if (yy >= rect->h - radius) {
      float dy = ((float)yy + 0.5f) - (float)(rect->h - radius);
      float dx = sqrtf((float)(radius * radius) - dy * dy);
      xoff = radius - (int)ceilf(dx);
    }
    int x1 = rect->x + xoff;
    int x2 = rect->x + rect->w - 1 - xoff;
    SDL_RenderDrawLine(renderer, x1, rect->y + yy, x2, rect->y + yy);
  }
}

static void
e9ui_renderTransientMessage(e9ui_context_t *ctx, int w, int h)
{
  if (!ctx || !ctx->renderer || e9ui_fullscreenHintStart == 0 || !e9ui_transientMessage) {
    return;
  }
  uint32_t now = SDL_GetTicks();
  uint32_t elapsed = now - e9ui_fullscreenHintStart;
  if (elapsed >= 1000) {
    return;
  }
  Uint8 alpha = 255;
  if (elapsed > 500) {
    float t = (float)(elapsed - 500) / 500.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    alpha = (Uint8)(255.0f * (1.0f - t));
  }
  int size = (h > 0) ? (h / 30) : 0;
  if (size <= 0) {
    size = 16;
  }
  if (size != e9ui_fullscreenHintSize) {
    if (e9ui_fullscreenHintFont) {
      TTF_CloseFont(e9ui_fullscreenHintFont);
      e9ui_fullscreenHintFont = NULL;
    }
    e9ui_fullscreenHintSize = size;
    const char *asset = debugger.theme.text.fontAsset ? debugger.theme.text.fontAsset : E9UI_THEME_TEXT_FONT_ASSET;
    char path[PATH_MAX];
    if (file_getAssetPath(asset, path, sizeof(path))) {
      e9ui_fullscreenHintFont = TTF_OpenFont(path, size);
    }
  }
  TTF_Font *font = e9ui_fullscreenHintFont;
  if (!font) {
    return;
  }
  SDL_Color color = (SDL_Color){255, 255, 255, 255};
  int tw = 0;
  int th = 0;
  const char *text = e9ui_transientMessage;
  SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, text, color, &tw, &th);
  if (!tex) {
    return;
  }
  SDL_SetTextureAlphaMod(tex, alpha);
  int padY = e9ui_scale_px(ctx, 8);
  int radius = (th / 2) + padY;
  int padX = radius;
  int bgW = tw + padX * 2;
  int bgH = th + padY * 2;
  int x = (w - bgW) / 2;
  int y = th;
  SDL_Rect bg = { x, y, bgW, bgH };
  SDL_Color bgColor = { 80, 80, 80, 220 };
  bgColor.a = (Uint8)((uint32_t)bgColor.a * alpha / 255u);
  SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
  e9ui_drawRoundedFill(ctx->renderer, &bg, bgColor);
  SDL_Rect dst = { x + padX, y + padY, tw, th };
  SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
}

static void
e9ui_renderFpsOverlay(e9ui_context_t *ctx, int w, int h)
{
  if (!ctx || !ctx->renderer || !e9ui_fpsEnabled || !debugger.ui.fullscreen) {
    return;
  }
  uint32_t now = SDL_GetTicks();
  if (e9ui_fpsLastTick == 0) {
    e9ui_fpsLastTick = now;
  }
  e9ui_fpsFrames++;
  uint32_t elapsed = now - e9ui_fpsLastTick;
  if (elapsed >= 500) {
    e9ui_fpsValue = (elapsed > 0) ? ((float)e9ui_fpsFrames * 1000.0f / (float)elapsed) : 0.0f;
    e9ui_fpsFrames = 0;
    e9ui_fpsLastTick = now;
  }

  int size = (h > 0) ? (h / 30) : 0;
  if (size <= 0) {
    size = 8;
  }
  if (size != e9ui_fpsFontSize) {
    if (e9ui_fpsFont) {
      TTF_CloseFont(e9ui_fpsFont);
      e9ui_fpsFont = NULL;
    }
    e9ui_fpsFontSize = size;
    const char *asset = debugger.theme.text.fontAsset ? debugger.theme.text.fontAsset : E9UI_THEME_TEXT_FONT_ASSET;
    char path[PATH_MAX];
    if (file_getAssetPath(asset, path, sizeof(path))) {
      e9ui_fpsFont = TTF_OpenFont(path, size);
    }
  }
  if (!e9ui_fpsFont) {
    return;
  }
  char text[32];
  snprintf(text, sizeof(text), "FPS %.1f", e9ui_fpsValue);
  SDL_Color color = (SDL_Color){255, 255, 255, 255};
  int tw = 0;
  int th = 0;
  SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, e9ui_fpsFont, text, color, &tw, &th);
  if (!tex) {
    return;
  }
  Uint8 alpha = 192;
  SDL_SetTextureAlphaMod(tex, alpha);
  int margin = (h > 0) ? (h / 40) : 8;
  if (margin < 6) {
    margin = 6;
  }
  int x = w - tw - margin;
  int y = h - th - margin;
  SDL_Color outline = (SDL_Color){0, 0, 0, 255};
  SDL_Texture *stroke = e9ui_text_cache_getText(ctx->renderer, e9ui_fpsFont, text, outline, &tw, &th);
  if (stroke) {
    SDL_SetTextureAlphaMod(stroke, alpha);
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) {
          continue;
        }
        SDL_Rect odst = { x + dx, y + dy, tw, th };
        SDL_RenderCopy(ctx->renderer, stroke, NULL, &odst);
      }
    }
  }
  SDL_Rect dst = { x, y, tw, th };
  SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
}

static void
e9ui_controllerClose(void)
{
  if (e9ui_controller) {
    SDL_GameControllerClose(e9ui_controller);
    e9ui_controller = NULL;
  }
  e9ui_controllerId = -1;
  e9ui_controllerLeft = 0;
  e9ui_controllerRight = 0;
  e9ui_controllerUp = 0;
  e9ui_controllerDown = 0;
  libretro_host_clearJoypadState();
}

static void
e9ui_controllerOpenIndex(int index)
{
  if (e9ui_controller || index < 0) {
    return;
  }
  if (!SDL_IsGameController(index)) {
    return;
  }
  SDL_GameController *pad = SDL_GameControllerOpen(index);
  if (!pad) {
    return;
  }
  SDL_Joystick *joy = SDL_GameControllerGetJoystick(pad);
  if (!joy) {
    SDL_GameControllerClose(pad);
    return;
  }
  e9ui_controller = pad;
  e9ui_controllerId = SDL_JoystickInstanceID(joy);
}

static void
e9ui_controllerInit(void)
{
  int count = SDL_NumJoysticks();
  for (int i = 0; i < count; ++i) {
    if (SDL_IsGameController(i)) {
      e9ui_controllerOpenIndex(i);
      if (e9ui_controller) {
        break;
      }
    }
  }
}

static int
e9ui_controllerMapButton(SDL_GameControllerButton button, unsigned *outId)
{
  if (!outId) {
    return 0;
  }
  switch (button) {
    case SDL_CONTROLLER_BUTTON_A: *outId = RETRO_DEVICE_ID_JOYPAD_B; return 1;
    case SDL_CONTROLLER_BUTTON_B: *outId = RETRO_DEVICE_ID_JOYPAD_A; return 1;
    case SDL_CONTROLLER_BUTTON_X: *outId = RETRO_DEVICE_ID_JOYPAD_Y; return 1;
    case SDL_CONTROLLER_BUTTON_Y: *outId = RETRO_DEVICE_ID_JOYPAD_X; return 1;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: *outId = RETRO_DEVICE_ID_JOYPAD_L; return 1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: *outId = RETRO_DEVICE_ID_JOYPAD_R; return 1;
    case SDL_CONTROLLER_BUTTON_START: *outId = RETRO_DEVICE_ID_JOYPAD_START; return 1;
    case SDL_CONTROLLER_BUTTON_BACK: *outId = RETRO_DEVICE_ID_JOYPAD_SELECT; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: *outId = RETRO_DEVICE_ID_JOYPAD_UP; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: *outId = RETRO_DEVICE_ID_JOYPAD_DOWN; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: *outId = RETRO_DEVICE_ID_JOYPAD_LEFT; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: *outId = RETRO_DEVICE_ID_JOYPAD_RIGHT; return 1;
    default:
      break;
  }
  return 0;
}

static void
e9ui_controllerSetDir(unsigned id, int *state, int pressed)
{
  if (!state) {
    return;
  }
  if (*state == pressed) {
    return;
  }
  *state = pressed;
  libretro_host_setJoypadState(0, id, pressed);
}

static void
e9ui_controllerHandleAxis(SDL_GameControllerAxis axis, int value)
{
  if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
    int left = (value < -e9ui_controllerDeadzone) ? 1 : 0;
    int right = (value > e9ui_controllerDeadzone) ? 1 : 0;
    e9ui_controllerSetDir(RETRO_DEVICE_ID_JOYPAD_LEFT, &e9ui_controllerLeft, left);
    e9ui_controllerSetDir(RETRO_DEVICE_ID_JOYPAD_RIGHT, &e9ui_controllerRight, right);
  } else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
    int up = (value < -e9ui_controllerDeadzone) ? 1 : 0;
    int down = (value > e9ui_controllerDeadzone) ? 1 : 0;
    e9ui_controllerSetDir(RETRO_DEVICE_ID_JOYPAD_UP, &e9ui_controllerUp, up);
    e9ui_controllerSetDir(RETRO_DEVICE_ID_JOYPAD_DOWN, &e9ui_controllerDown, down);
  }
}


static int
e9ui_registerHotkey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod modMask, SDL_Keymod modValue,
                    void (*cb)(e9ui_context_t *ctx, void *user), void *user)
{
    (void)ctx;
    if (!cb) {
        return -1;
    }
    e9k_hotkey_registry_t *hk = &debugger.ui.hotkeys;
    if (hk->count == hk->cap) {
        int nc = hk->cap ? hk->cap * 2 : 16;
        hk->entries = (e9k_hotkey_entry_t*)alloc_realloc(hk->entries, (size_t)nc * sizeof(e9k_hotkey_entry_t));
        hk->cap = nc;
    }
    int id = (hk->next_id ? hk->next_id : 1);
    hk->next_id = id + 1;
    hk->entries[hk->count++] = (e9k_hotkey_entry_t){ id, (int)key, (int)modMask, (int)modValue, cb, user, 1 };
    return id;
}

static void
e9ui_unregisterHotkey(e9ui_context_t *ctx, int id)
{
    (void)ctx;
    e9k_hotkey_registry_t *hk = &debugger.ui.hotkeys;
    for (int i=0;i<hk->count;i++) {
        if (hk->entries[i].id == id) {
            hk->entries[i] = hk->entries[hk->count-1];
            hk->count--;
            break;
        }
    }
}

static int
e9ui_dispatchHotkey(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
  (void)ctx;
  if (!kev) {
    return 0;
  }
  SDL_Keycode key = kev->keysym.sym;
  SDL_Keymod mods = (SDL_Keymod)(kev->keysym.mod & (KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI));
  // If a text-input capable component is focused, prevent bare printable keys from triggering hotkeys
  if (ctx && (e9ui_getFocus(ctx))) {
    SDL_Keymod noShiftMods = (SDL_Keymod)(mods & (KMOD_CTRL|KMOD_ALT|KMOD_GUI));
    int printable = (key >= 32 && key <= 126); // ASCII printable range
    if (noShiftMods == 0 && printable) {
      return 0; // let TEXTINPUT path handle it
    }
  }
  if (key == SDLK_TAB && ctx) {
    if (prompt_isFocused(ctx, debugger.ui.prompt)) {
      return 0;
    }
  }
  e9k_hotkey_registry_t *hk = &debugger.ui.hotkeys;
  for (int i=0;i<hk->count;i++) {
    e9k_hotkey_entry_t *e = &hk->entries[i];
    if (!e->active) {
      continue;
    }
    if ((SDL_Keycode)e->key == key) {
      if ((mods & (SDL_Keymod)e->mask) == (SDL_Keymod)e->value) {
	if (e->cb) {
	  e->cb(ctx, e->user);
	}
	return 1;
      }
    }
  }
  return 0;
}

static void    
e9ui_updateDisabledState(e9ui_component_t *comp)
{
  if (comp->disabledVariable) {
    int flagVal = *comp->disabledVariable ? 1 : 0;
    int disabled = comp->disableWhenTrue ? flagVal : !flagVal;
    comp->disabled = disabled;
  }
}

static void    
e9ui_updateHiddenState(e9ui_component_t *comp)
{
  if (comp->hiddenVariable) {
    int flagVal = *comp->hiddenVariable ? 1 : 0;
    int hidden = comp->hiddenWhenTrue ? flagVal : !flagVal;
    e9ui_setHidden(comp, hidden);
  }
}

static void
e9ui_updateState(e9ui_component_t *comp, e9ui_context_t *ctx)
{
  if (!comp) {
    return;
  }
  e9ui_updateDisabledState(comp);
  e9ui_updateHiddenState(comp);
  e9ui_child_iterator iter;
  if (!e9ui_child_iterateChildren(comp, &iter)) {
    return;
  }
  for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
       it;
       it = e9ui_child_interateNext(&iter)) {
    if (it->child) {
      e9ui_updateState(it->child, ctx);
    }
  }
}

void
e9ui_setDisabled(e9ui_component_t *comp, int disabled)
{
  if (!comp) {
    return;
  }
  comp->disabled = disabled ? 1 : 0;
}


void
e9ui_setDisableVariable(e9ui_component_t *comp, const int *stateFlag, int disableWhenTrue)
{
    if (!comp) {
        return;
    }
    comp->disabledVariable = stateFlag;
    comp->disableWhenTrue = disableWhenTrue ? 1 : 0;
    e9ui_updateDisabledState(comp);
}

void
e9ui_setHidden(e9ui_component_t *comp, int hidden)
{
  comp->_hidden = hidden;
}

void
e9ui_setAutoHide(e9ui_component_t *comp, int enable, int margin_px)
{
  if (!comp) {
    return;
  }
  comp->autoHide = enable ? 1 : 0;
  comp->autoHideMargin = margin_px;
}

void
e9ui_setAutoHideClip(e9ui_component_t *comp, const e9ui_rect_t *rect)
{
  if (!comp) {
    return;
  }
  if (!rect) {
    comp->autoHideHasClip = 0;
    return;
  }
  comp->autoHideHasClip = 1;
  comp->autoHideClip = *rect;
}

void
e9ui_setFocusTarget(e9ui_component_t *comp, e9ui_component_t *target)
{
  if (!comp) {
    return;
  }
  comp->focusTarget = target;
}

static int
e9ui_hiddenByVariable(const e9ui_component_t *comp)
{
  if (!comp || !comp->hiddenVariable) {
    return 0;
  }
  int flagVal = *comp->hiddenVariable ? 1 : 0;
  return comp->hiddenWhenTrue ? flagVal : !flagVal;
}

static void
e9ui_updateAutoHide(e9ui_component_t *comp, e9ui_context_t *ctx)
{
  if (!comp || !ctx) {
    return;
  }
  int hidden_forced = e9ui_hiddenByVariable(comp);
  if (hidden_forced) {
    e9ui_setHidden(comp, 1);
  } else if (comp->autoHide) {
    int margin = comp->autoHideMargin;
    if (margin < 0) {
      margin = 0;
    }
    margin = e9ui_scale_px(ctx, margin);
    int x0 = comp->bounds.x - margin;
    int y0 = comp->bounds.y - margin;
    int x1 = comp->bounds.x + comp->bounds.w + margin;
    int y1 = comp->bounds.y + comp->bounds.h + margin;
    if (comp->autoHideHasClip) {
      int cx0 = comp->autoHideClip.x;
      int cy0 = comp->autoHideClip.y;
      int cx1 = comp->autoHideClip.x + comp->autoHideClip.w;
      int cy1 = comp->autoHideClip.y + comp->autoHideClip.h;
      if (x0 < cx0) x0 = cx0;
      if (y0 < cy0) y0 = cy0;
      if (x1 > cx1) x1 = cx1;
      if (y1 > cy1) y1 = cy1;
    }
    int mx = ctx->mouseX;
    int my = ctx->mouseY;
    int inside = (x1 > x0 && y1 > y0 &&
                  mx >= x0 && mx < x1 &&
                  my >= y0 && my < y1);
    e9ui_setHidden(comp, inside ? 0 : 1);
  }
  e9ui_child_iterator iter;
  if (!e9ui_child_iterateChildren(comp, &iter)) {
    return;
  }
  for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
       it;
       it = e9ui_child_interateNext(&iter)) {
    if (it->child) {
      e9ui_updateAutoHide(it->child, ctx);
    }
  }
}
void
e9ui_setHiddenVariable(e9ui_component_t *comp, const int *var, int hiddenWhenTrue)
{
    if (!comp) {
        return;
    }

    comp->hiddenVariable = var;
    comp->hiddenWhenTrue = hiddenWhenTrue ? 1 : 0;
}


void
e9ui_setFocus(e9ui_context_t *ctx, e9ui_component_t *comp)
{
  ctx->_focus = comp;

}

void
e9ui_setTooltip(e9ui_component_t *comp, const char *tooltip)
{
    if (!comp) {
        return;
    }
    comp->tooltip = tooltip;
}

void
e9ui_debugDrawBounds(e9ui_component_t *c, e9ui_context_t *ctx, int depth)
{
    if (!c || !ctx || !ctx->renderer) {
        return;
    }
    // Choose alternating colors by depth
    const SDL_Color cols[] = {
        {255,  64,  64, 255}, // red
        { 64, 200,  64, 255}, // green
        { 64, 160, 255, 255}, // blue
        {255, 200,  64, 255}, // yellow
        {200,  64, 200, 255}, // magenta
    };
    const int ncols = (int)(sizeof(cols)/sizeof(cols[0]));
    SDL_Color cc = cols[depth % ncols];
    SDL_SetRenderDrawColor(ctx->renderer, cc.r, cc.g, cc.b, cc.a);
    SDL_Rect r = { c->bounds.x, c->bounds.y, c->bounds.w, c->bounds.h };
    // Draw 2px outline for visibility
    SDL_RenderDrawRect(ctx->renderer, &r);
    if (r.w > 2 && r.h > 2) {
        SDL_Rect r2 = { r.x+1, r.y+1, r.w-2, r.h-2 };
        SDL_RenderDrawRect(ctx->renderer, &r2);
    }
    // Recurse into children if available
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(c, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
      if (it->child) {
        e9ui_debugDrawBounds(it->child, ctx, depth+1);
      }
    }
}

static void
e9ui_saveLayoutRecursive(e9ui_component_t *comp, e9ui_context_t *ctx, FILE *f)
{
    if (!comp) {
        return;
    }
    if (comp->persistSave) {
        comp->persistSave(comp, ctx, f);
    }
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (it->child) {
            e9ui_saveLayoutRecursive(it->child, ctx, f);
        }
    }
}


void
e9ui_saveLayout(void)
{
    if (debugger.smokeTestMode != 0) {
        return;
    }
    const char *p = debugger_configPath();
    if (!p) {
        return;
    }
    FILE *f = fopen(p, "w");
    if (!f) {
        return;
    }
    // Save component state by traversing the tree (even if root itself has no persistSave)
    if (debugger.ui.root) {
        e9ui_saveLayoutRecursive(debugger.ui.root, &debugger.ui.ctx, f);
    }
    // Save window geometry (last known)
    int wx = debugger.layout.winX, wy = debugger.layout.winY, ww = debugger.layout.winW, wh = debugger.layout.winH;
    if (debugger.ui.ctx.window) {
        SDL_GetWindowPosition(debugger.ui.ctx.window, &wx, &wy);
        SDL_GetWindowSize(debugger.ui.ctx.window, &ww, &wh);
    }
    fprintf(f, "win_x=%d\nwin_y=%d\nwin_w=%d\nwin_h=%d\n", wx, wy, ww, wh);
    config_persistConfig(f);
    fclose(f);
}

static e9ui_component_t *
e9ui_findByIdRecursive(e9ui_component_t *comp, const char *id)
{
    if (!comp) {
        return NULL;
    }
    if (comp->persist_id && strcmp(comp->persist_id, id) == 0) {
        return comp;
    }
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return NULL;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        e9ui_component_t *found = e9ui_findByIdRecursive(it->child, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}


e9ui_component_t *
e9ui_findById(e9ui_component_t *root, const char *id)
{
    if (!root || !id || !*id) {
        return NULL;
    }
    return e9ui_findByIdRecursive(root, id);
}

void
e9ui_loadLayoutComponents(void)
{
    if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE) {
        e9ui_component_t *geoBox = e9ui_findById(debugger.ui.root, "libretro_box");
        if (geoBox) {
            debugger.ui.fullscreen = geoBox;
        }
        return;
    }
    const char *p = debugger_configPath();
    if (!p) {
        return;
    }
    FILE *f = fopen(p, "r");
    if (!f) {
        return;
    }
    char key[256]; char val[256];
    e9ui_loadingLayout = 1;
    while (fscanf(f, "%255[^=]=%255s\n", key, val) == 2) {
        if (strncmp(key, "comp.", 5) != 0) {
            continue;
        }
        const char *rest = key + 5;
        const char *dot = strchr(rest, '.');
        if (!dot) {
            continue;
        }
        char id[200];
        size_t idl = (size_t)(dot - rest);
        if (idl >= sizeof(id)) {
            idl = sizeof(id)-1;
        }
        memcpy(id, rest, idl);
        id[idl] = '\0';
        const char *prop = dot + 1;
        e9ui_component_t *c = e9ui_findById(debugger.ui.root, id);
        if (c && c->persistLoad) {
            c->persistLoad(c, &debugger.ui.ctx, prop, val);
        }
    }
    fclose(f);
    e9ui_loadingLayout = 0;
}

static void
e9ui_onSplitChanged(e9ui_context_t *ctx, e9ui_component_t *split, float ratio)
{
    (void)ctx; (void)ratio; (void)split;
    // Persist all split ratios to disk
    config_saveConfig();
}

static float
e9ui_computeDpiScale(void)
{
    if (!debugger.ui.ctx.window || !debugger.ui.ctx.renderer) {
        return 1.0f;
    }
    int winW = 0, winH = 0;
    int renW = 0, renH = 0;
    SDL_GetWindowSize(debugger.ui.ctx.window, &winW, &winH);
    SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &renW, &renH);
    if (winW <= 0 || winH <= 0) {
        return 1.0f;
    }
    float scaleX = (float)renW / (float)winW;
    float scaleY = (float)renH / (float)winH;
    float scale = scaleX > scaleY ? scaleX : scaleY;
    return scale < 1.0f ? 1.0f : scale;
}

static int
e9ui_scaledFontSize(int baseSize)
{
    if (baseSize <= 0) {
        return 1;
    }
    float scale = debugger.ui.ctx.dpiScale;
    if (scale <= 1.0f) {
        return baseSize;
    }
    int scaled = (int)(baseSize * scale + 0.5f);
    return scaled > 0 ? scaled : 1;
}

int
e9ui_scale_px(const e9ui_context_t *ctx, int px)
{
    if (px <= 0) {
        return px;
    }
    float scale = (ctx && ctx->dpiScale > 0.0f) ? ctx->dpiScale : 1.0f;
    if (scale <= 1.0f) {
        return px;
    }
    int scaled = (int)(px * scale + 0.5f);
    return scaled > 0 ? scaled : 1;
}

int
e9ui_unscale_px(const e9ui_context_t *ctx, int px)
{
    if (px <= 0) {
        return px;
    }
    float scale = (ctx && ctx->dpiScale > 0.0f) ? ctx->dpiScale : 1.0f;
    if (scale <= 1.0f) {
        return px;
    }
    int unscaled = (int)(px / scale + 0.5f);
    return unscaled > 0 ? unscaled : 1;
}

int
e9ui_scale_coord(const e9ui_context_t *ctx, int coord)
{
    float scale = (ctx && ctx->dpiScale > 0.0f) ? ctx->dpiScale : 1.0f;
    if (scale <= 1.0f) {
        return coord;
    }
    float scaled = (float)coord * scale;
    if (scaled >= 0.0f) {
        return (int)(scaled + 0.5f);
    }
    return (int)(scaled - 0.5f);
}

static TTF_Font*
e9ui_loadFont(void)
{
  TTF_Font *f = NULL;
  char exedir[PATH_MAX];
  if (file_getExeDir(exedir, sizeof(exedir))) {
    char apath[PATH_MAX];
    size_t n = strlen(exedir);
    if (n < sizeof(apath)) {
      memcpy(apath, exedir, n);
      if (n > 0 && apath[n-1] != '/') apath[n++] = '/';
      const char *rel = "assets/RobotoMono-Regular.ttf";
      size_t rl = strlen(rel);
      if (n + rl < sizeof(apath)) {
        memcpy(apath + n, rel, rl + 1);
        int fontSize = e9ui_scaledFontSize(14);
        f = TTF_OpenFont(apath, fontSize);
        if (f) return f;
      }
    }
  }

  return f;
}

static void
e9ui_updateFontScale(void)
{
    float newScale = e9ui_computeDpiScale();
    if (newScale <= 0.0f) {
        newScale = 1.0f;
    }
    float prevScale = debugger.ui.ctx.dpiScale;
    if (fabsf(newScale - prevScale) < 0.01f) {
        debugger.ui.ctx.dpiScale = newScale;
        return;
    }
    debugger.ui.ctx.dpiScale = newScale;
    if (debugger.ui.ctx.font) {
        TTF_CloseFont(debugger.ui.ctx.font);
        debugger.ui.ctx.font = NULL;
    }
    debugger.ui.ctx.font = e9ui_loadFont();
    e9ui_theme_reloadFonts();
    e9ui_text_cache_clear();
}

typedef struct {
    const char *text;
    int depth;
    e9ui_component_t *comp;
} e9ui_tooltip_result_t;

static int
e9ui_pointInBounds(const e9ui_component_t *comp, int x, int y)
{
    if (!comp) {
        return 0;
    }
    return x >= comp->bounds.x && x < comp->bounds.x + comp->bounds.w &&
           y >= comp->bounds.y && y < comp->bounds.y + comp->bounds.h;
}

static e9ui_tooltip_result_t
e9ui_findTooltipRecursive(e9ui_component_t *comp, e9ui_context_t *ctx, int x, int y, int depth)
{
    e9ui_tooltip_result_t best = { NULL, -1, NULL };
    if (!comp || !e9ui_pointInBounds(comp, x, y)) {
        return best;
    }
    e9ui_child_iterator iter;
    if (e9ui_child_iterateChildren(comp, &iter)) {
        for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
             it;
             it = e9ui_child_interateNext(&iter)) {
            e9ui_tooltip_result_t candidate =
                e9ui_findTooltipRecursive(it->child, ctx, x, y, depth + 1);
            if (!e9ui_getHidden(candidate.comp) && candidate.depth > best.depth) {
                best = candidate;
            }
        }
    }
    if (comp->tooltip && depth > best.depth) {
        best.text = comp->tooltip;
        best.depth = depth;
        best.comp = comp;
    }
    return best;
}

static void
e9ui_drawTooltip(const e9ui_context_t *ctx, const char *text, int baseX, int baseY)
{
    if (!ctx || !ctx->renderer || !ctx->font || !text || !*text) {
        return;
    }
    int textW = 0;
    int textH = 0;
    if (TTF_SizeText(ctx->font, text, &textW, &textH) != 0 || textW <= 0 || textH <= 0) {
        return;
    }
    int pad = e9ui_scale_px(ctx, 6);
    int offset = e9ui_scale_px(ctx, 8);
    int bgW = textW + pad * 2;
    int bgH = textH + pad * 2;
    if (bgW <= 0 || bgH <= 0) {
        return;
    }
    int x = baseX + offset;
    int y = baseY + offset;
    int maxX = ctx->winW > 8 ? ctx->winW - 4 : 4;
    int maxY = ctx->winH > 8 ? ctx->winH - 4 : 4;
    if (x + bgW > maxX) {
        x = maxX - bgW;
    }
    if (y + bgH > maxY) {
        y = maxY - bgH;
    }
    if (x < 4) {
        x = 4;
    }
    if (y < 4) {
        y = 4;
    }
    SDL_Rect bg = { x, y, bgW, bgH };
    SDL_Color background = { 16, 16, 16, 220 };
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, background.r, background.g, background.b, background.a);
    SDL_RenderFillRect(ctx->renderer, &bg);
    SDL_Color border = { 170, 170, 170, 255 };
    SDL_SetRenderDrawColor(ctx->renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(ctx->renderer, &bg);
    SDL_Color textColor = { 235, 235, 235, 255 };
    int tw = 0;
    int th = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, ctx->font, text, textColor, &tw, &th);
    if (tex) {
        SDL_Rect textRect = { x + pad, y + pad, tw, th };
        SDL_RenderCopy(ctx->renderer, tex, NULL, &textRect);
    }
}

static void
e9ui_renderTooltipOverlay(void)
{
    e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
    if (!root) {
        return;
    }
    static struct {
        const char *text;
        const e9ui_component_t *comp;
        int x;
        int y;
        int active;
    } tooltip_state = {0};

    e9ui_tooltip_result_t tooltip = e9ui_findTooltipRecursive(root, &debugger.ui.ctx,
                                                              debugger.ui.ctx.mouseX, debugger.ui.ctx.mouseY, 0);
    if (!tooltip.text) {
        tooltip_state.active = 0;
        tooltip_state.text = NULL;
        tooltip_state.comp = NULL;
        return;
    }

    if (!tooltip_state.active || tooltip_state.comp != tooltip.comp || tooltip_state.text != tooltip.text) {
        tooltip_state.active = 1;
        tooltip_state.comp = tooltip.comp;
        tooltip_state.text = tooltip.text;
        tooltip_state.x = debugger.ui.ctx.mouseX;
        tooltip_state.y = debugger.ui.ctx.mouseY;
    }

    e9ui_drawTooltip(&debugger.ui.ctx, tooltip_state.text, tooltip_state.x, tooltip_state.y);
}

static e9ui_component_t *
e9ui_findFocusable(e9ui_component_t *comp, e9ui_context_t *ctx)
{
    if (!comp) {
        return NULL;
    }
    if (comp->focusable) {
        return comp;
    }
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return NULL;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        e9ui_component_t *found = e9ui_findFocusable(it->child, ctx);
        if (found) {
            return found;
        }
    }
    return NULL;
}

void
e9ui_setFullscreenComponent(e9ui_component_t *comp)
{
    e9ui_component_t *prev = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
    if (comp) {
    } else {
        if (e9ui_transientMessage == e9ui_fullscreenMessage) {
            e9ui_fullscreenHintStart = 0;
            e9ui_transientMessage = NULL;
        }
    }
    if (comp) {
        e9ui_component_t *focus = e9ui_findFocusable(comp, &debugger.ui.ctx);
        if (focus) {
            e9ui_setFocus(&debugger.ui.ctx, focus);
        }
    }
    if (comp && prev) {
        int w = 0;
        int h = 0;
        SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);
        if (!e9ui_loadingLayout) {
            e9k_transition_mode_t mode = transition_pickFullscreenMode(1);
            if (mode != e9k_transition_none) {
                debugger.inTransition = 1;
                if (mode == e9k_transition_slide) {
                    transition_slide_runTo(prev, comp, w, h);
                } else if (mode == e9k_transition_explode) {
                    transition_explode_runTo(prev, comp, w, h);
                } else if (mode == e9k_transition_doom) {
                    transition_doom_runTo(prev, comp, w, h);
                } else if (mode == e9k_transition_flip) {
                    transition_flip_runTo(prev, comp, w, h);
                } else if (mode == e9k_transition_rbar) {
                    transition_rbar_runTo(prev, comp, w, h);
                }
            }
        }
    }
    debugger.ui.fullscreen = comp;
    if (comp) {
        e9ui_fullscreenHintStart = SDL_GetTicks();
        e9ui_transientMessage = e9ui_fullscreenMessage;
    }
}

void
e9ui_clearFullscreenComponent(void)
{
    e9ui_component_t *prev = debugger.ui.fullscreen;
    if (e9ui_transientMessage == e9ui_fullscreenMessage) {
        e9ui_fullscreenHintStart = 0;
        e9ui_transientMessage = NULL;
    }
    if (prev) {
        int w = 0;
        int h = 0;
        SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);
        e9k_transition_mode_t mode = transition_pickFullscreenMode(0);
        if (mode != e9k_transition_none) {
            debugger.inTransition = 1;
            if (mode == e9k_transition_slide) {
                transition_slide_run(prev, debugger.ui.root, w, h);
            } else if (mode == e9k_transition_explode) {
                transition_explode_run(prev, debugger.ui.root, w, h);
            } else if (mode == e9k_transition_doom) {
                transition_doom_runTo(prev, debugger.ui.root, w, h);
            } else if (mode == e9k_transition_flip) {
                transition_flip_run(prev, debugger.ui.root, w, h);
            } else if (mode == e9k_transition_rbar) {
                transition_rbar_run(prev, debugger.ui.root, w, h);
            }
        }
    }
    debugger.ui.fullscreen = NULL;
}

void
e9ui_showTransientMessage(const char *message)
{
    if (!message || !*message) {
        return;
    }
    e9ui_transientMessage = message;
    e9ui_fullscreenHintStart = SDL_GetTicks();
}

e9ui_component_t *
e9ui_getFullscreenComponent(void)
{
    return debugger.ui.fullscreen;
}

int
e9ui_isFullscreenComponent(const e9ui_component_t *comp)
{
    return comp && debugger.ui.fullscreen == comp;
}

void
e9ui_renderFrame(void)
{
  if (debugger.inTransition > 0) {
    return;
  }
  e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
  e9ui_updateState(root, &debugger.ui.ctx);
 
  e9ui_updateFontScale();
  SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 16, 16, 16, 255);
  SDL_RenderClear(debugger.ui.ctx.renderer);
  
  int w,h; SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);
  
  debugger.ui.ctx.winW = w;
  debugger.ui.ctx.winH = h;
  debugger.ui.ctx.mouseX = debugger.ui.mouseX;
  debugger.ui.ctx.mouseY = debugger.ui.mouseY;
  
  if (root && root->layout) {
    e9ui_rect_t full = (e9ui_rect_t){0,0,w,h};
    root->layout(root, &debugger.ui.ctx, full);
  }

  e9ui_updateAutoHide(root, &debugger.ui.ctx);

  if (root && root->render) {
    root->render(root, &debugger.ui.ctx);
  }

  e9ui_renderTransientMessage(&debugger.ui.ctx, w, h);
  e9ui_renderFpsOverlay(&debugger.ui.ctx, w, h);

  if (debugger.ui.ctx.font == NULL) {
    SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 220, 190, 190, 255);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 12, "MISSING FONT - EXPECTED", 2);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 28, "assets/RobotoMono-Regular.ttf", 2);
  }

  e9ui_renderTooltipOverlay();

  SDL_RenderPresent(debugger.ui.ctx.renderer);

  if (debugger.opts.debugLayout) {
    e9ui_debugDrawBounds(root, &debugger.ui.ctx, 0);
    SDL_RenderPresent(debugger.ui.ctx.renderer);
  }
}

void
e9ui_renderFrameNoLayout(void)
{
  e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
  e9ui_updateState(root, &debugger.ui.ctx);

  SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 16, 16, 16, 255);
  SDL_RenderClear(debugger.ui.ctx.renderer);

  int w,h; SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);

  debugger.ui.ctx.winW = w;
  debugger.ui.ctx.winH = h;
  debugger.ui.ctx.mouseX = debugger.ui.mouseX;
  debugger.ui.ctx.mouseY = debugger.ui.mouseY;

  e9ui_updateAutoHide(root, &debugger.ui.ctx);

  if (root && root->render) {
    root->render(root, &debugger.ui.ctx);
  }

  e9ui_renderTransientMessage(&debugger.ui.ctx, w, h);
  e9ui_renderFpsOverlay(&debugger.ui.ctx, w, h);

  if (debugger.ui.ctx.font == NULL) {
    SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 220, 190, 190, 255);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 12, "MISSING FONT - EXPECTED", 2);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 28, "assets/RobotoMono-Regular.ttf", 2);
  }

  e9ui_renderTooltipOverlay();

  SDL_RenderPresent(debugger.ui.ctx.renderer);

  if (debugger.opts.debugLayout) {
    e9ui_debugDrawBounds(root, &debugger.ui.ctx, 0);
    SDL_RenderPresent(debugger.ui.ctx.renderer);
  }
}

void
e9ui_renderFrameNoLayoutNoPresent(void)
{
  e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
  e9ui_updateState(root, &debugger.ui.ctx);

  SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 16, 16, 16, 255);
  SDL_RenderClear(debugger.ui.ctx.renderer);

  int w,h; SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);

  debugger.ui.ctx.winW = w;
  debugger.ui.ctx.winH = h;
  debugger.ui.ctx.mouseX = debugger.ui.mouseX;
  debugger.ui.ctx.mouseY = debugger.ui.mouseY;

  e9ui_updateAutoHide(root, &debugger.ui.ctx);

  if (root && root->render) {
    root->render(root, &debugger.ui.ctx);
  }

  e9ui_renderTransientMessage(&debugger.ui.ctx, w, h);
  e9ui_renderFpsOverlay(&debugger.ui.ctx, w, h);

  if (debugger.ui.ctx.font == NULL) {
    SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 220, 190, 190, 255);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 12, "MISSING FONT - EXPECTED", 2);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 28, "assets/RobotoMono-Regular.ttf", 2);
  }

  e9ui_renderTooltipOverlay();

  if (debugger.opts.debugLayout) {
    e9ui_debugDrawBounds(root, &debugger.ui.ctx, 0);
  }
}

void
e9ui_renderFrameNoLayoutNoPresentNoClear(void)
{
  e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
  e9ui_updateState(root, &debugger.ui.ctx);

  int w,h; SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);

  debugger.ui.ctx.winW = w;
  debugger.ui.ctx.winH = h;
  debugger.ui.ctx.mouseX = debugger.ui.mouseX;
  debugger.ui.ctx.mouseY = debugger.ui.mouseY;

  e9ui_updateAutoHide(root, &debugger.ui.ctx);

  if (root && root->render) {
    root->render(root, &debugger.ui.ctx);
  }

  e9ui_renderTransientMessage(&debugger.ui.ctx, w, h);
  e9ui_renderFpsOverlay(&debugger.ui.ctx, w, h);

  if (debugger.ui.ctx.font == NULL) {
    SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 220, 190, 190, 255);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 12, "MISSING FONT - EXPECTED", 2);
    debug_font_drawText(debugger.ui.ctx.renderer, 12, 28, "assets/RobotoMono-Regular.ttf", 2);
  }

  e9ui_renderTooltipOverlay();

  if (debugger.opts.debugLayout) {
    e9ui_debugDrawBounds(root, &debugger.ui.ctx, 0);
  }
}

void
e9ui_renderFrameNoLayoutNoPresentFade(int fadeAlpha)
{
  if (fadeAlpha < 0) {
    fadeAlpha = 0;
  }
  if (fadeAlpha > 255) {
    fadeAlpha = 255;
  }
  e9ui_renderFrameNoLayoutNoPresent();
  if (fadeAlpha < 255) {
    SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(debugger.ui.ctx.renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(debugger.ui.ctx.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(debugger.ui.ctx.renderer, 0, 0, 0, (Uint8)(255 - fadeAlpha));
    int w,h; SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);
    SDL_Rect r = { 0, 0, w, h };
    SDL_RenderFillRect(debugger.ui.ctx.renderer, &r);
    SDL_SetRenderDrawBlendMode(debugger.ui.ctx.renderer, prevBlend);
  }
}

static void
e9ui_loadWindowConfig(void)
{
  const char *p = debugger_configPath();
  if (!p) {
    return;
  }
  FILE *f = fopen(p, "r");
  if (!f) {
    return;
  }
  char key[128]; char val[128];
  while (fscanf(f, "%127[^=]=%127s\n", key, val) == 2) {
    if (strcmp(key, "win_x") == 0 || strcmp(key, "winX") == 0) {
      debugger.layout.winX = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "win_y") == 0 || strcmp(key, "winY") == 0) {
      debugger.layout.winY = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "win_w") == 0 || strcmp(key, "winW") == 0) {
      debugger.layout.winW = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "win_h") == 0 || strcmp(key, "winH") == 0) {
      debugger.layout.winH = (int)strtol(val, NULL, 10);
    }
  }
  fclose(f);
  if (debugger.cliWindowOverride) {
    debugger.layout.winW = debugger.cliWindowW;
    debugger.layout.winH = debugger.cliWindowH;
  }
}

int
e9ui_ctor(void)
{
  e9ui_theme_ctor();
  e9ui_loadWindowConfig();

    // Load persisted layout before creating window (for geometry)
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER) != 0) {
        debug_error("SDL_Init failed: %s", SDL_GetError());
        return 0;
    }
    if (TTF_Init() != 0) {
        debug_error("TTF_Init failed: %s", TTF_GetError());
        SDL_Quit();
        return 0;
    }
    {
        int flags = IMG_INIT_PNG;
        int initted = IMG_Init(flags);
        if ((initted & flags) != flags) {
            debug_error("IMG_Init failed to init PNG: %s", IMG_GetError());
            TTF_Quit();
            SDL_Quit();
            return 0;
        }
    }
    int wantW = (debugger.layout.winW > 0 ? debugger.layout.winW : 1000);
    int wantH = (debugger.layout.winH > 0 ? debugger.layout.winH : 700);
    #if defined(__APPLE__) || defined(_WIN32)
    if (debugger.glCompositeEnabled) {
      SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    }
    #endif
    SDL_Window *win = SDL_CreateWindow("ENGINE9000 DEBUGGER/PROFILER NEOGEO 68K", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, wantW, wantH,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL);
    if (!win) {
        debug_error("SDL_CreateWindow failed: %s", SDL_GetError());
        return 0;
    }
    e9ui_applyWindowIcon(win);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED |SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        debug_error("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        return 0;
    }
    debugger.ui.ctx.window = win;
    debugger.ui.ctx.renderer = ren;
    debugger.ui.ctx.dpiScale = e9ui_computeDpiScale();
    // Enable alpha blending for proper fade animations
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    // Apply saved window position if present
    if (debugger.layout.winX >= 0 && debugger.layout.winY >= 0) {
        SDL_SetWindowPosition(win, debugger.layout.winX, debugger.layout.winY);
    }
    if (debugger.glCompositeEnabled) {
      if (!gl_composite_init(win, ren)) {
        debug_error("gl-composite: disabled (init failed)");
      }
    }

  // Load default monospace font
  TTF_Font *font = e9ui_loadFont();
  if (!font) {
  }
  debugger.ui.ctx.font = font;
  // Initialize root event hooks
  debugger.ui.ctx.registerHotkey = e9ui_registerHotkey;
  debugger.ui.ctx.unregisterHotkey = e9ui_unregisterHotkey;
  debugger.ui.ctx.dispatchHotkey = e9ui_dispatchHotkey;
  debugger.ui.ctx.onSplitChanged = e9ui_onSplitChanged;
  // Enable layout debug overlay if requested
  const char *dl = getenv("E9K_DEBUG_LAYOUT");
  if (dl && *dl) debugger.opts.debugLayout = 1;
  // Load themed fonts (button + text fonts)
  e9ui_theme_loadFonts();

  e9ui_controllerInit();
    
    return 1;
}

static uint32_t
e9ui_eventWindowId(const SDL_Event *ev)
{
    if (!ev) {
        return 0;
    }
    switch (ev->type) {
    case SDL_MOUSEMOTION:
        return ev->motion.windowID;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        return ev->button.windowID;
    case SDL_MOUSEWHEEL:
        return ev->wheel.windowID;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        return ev->key.windowID;
    case SDL_TEXTINPUT:
        return ev->text.windowID;
    case SDL_WINDOWEVENT:
        return ev->window.windowID;
    default:
        return 0;
    }
}

int
e9ui_processEvents(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        uint32_t shaderWindowId = shader_ui_getWindowId();
        uint32_t memoryTrackWindowId = memory_track_ui_getWindowId();
        uint32_t evWindowId = e9ui_eventWindowId(&ev);
        if (shaderWindowId && evWindowId == shaderWindowId) {
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                shader_ui_handleEvent(&ev);
                continue;
            }
            shader_ui_handleEvent(&ev);
            continue;
        }
        if (memoryTrackWindowId && evWindowId == memoryTrackWindowId) {
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                memory_track_ui_handleEvent(&ev);
                continue;
            }
            memory_track_ui_handleEvent(&ev);
            continue;
        }
        debugger.ui.ctx.focusClickHandled = 0;
        debugger.ui.ctx.cursorOverride = 0;
        if (ev.type == SDL_QUIT) return 1;
        else if (ev.type == SDL_MOUSEMOTION) {
            if (sprite_debug_is_window_id(ev.motion.windowID)) {
                continue;
            }
            int prevX = debugger.ui.ctx.mouseX;
            int prevY = debugger.ui.ctx.mouseY;
            debugger.ui.ctx.mousePrevX = prevX;
            debugger.ui.ctx.mousePrevY = prevY;
            int scaledX = e9ui_scale_coord(&debugger.ui.ctx, ev.motion.x);
            int scaledY = e9ui_scale_coord(&debugger.ui.ctx, ev.motion.y);
            ev.motion.x = scaledX;
            ev.motion.y = scaledY;
            ev.motion.xrel = scaledX - prevX;
            ev.motion.yrel = scaledY - prevY;
            debugger.ui.ctx.mouseX = scaledX;
            debugger.ui.ctx.mouseY = scaledY;
            debugger.ui.mouseX = scaledX;
            debugger.ui.mouseY = scaledY;
        }
        else if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
            if (sprite_debug_is_window_id(ev.button.windowID)) {
                continue;
            }
            int scaledX = e9ui_scale_coord(&debugger.ui.ctx, ev.button.x);
            int scaledY = e9ui_scale_coord(&debugger.ui.ctx, ev.button.y);
            ev.button.x = scaledX;
            ev.button.y = scaledY;
            debugger.ui.ctx.mouseX = scaledX;
            debugger.ui.ctx.mouseY = scaledY;
            debugger.ui.mouseX = scaledX;
            debugger.ui.mouseY = scaledY;
        }
        else if (ev.type == SDL_MOUSEWHEEL) {
            if (sprite_debug_is_window_id(ev.wheel.windowID)) {
                continue;
            }
#ifdef _WIN32
            ev.wheel.y = -ev.wheel.y;
#endif
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            int scaledX = e9ui_scale_coord(&debugger.ui.ctx, mx);
            int scaledY = e9ui_scale_coord(&debugger.ui.ctx, my);
            debugger.ui.ctx.mouseX = scaledX;
            debugger.ui.ctx.mouseY = scaledY;
            debugger.ui.mouseX = scaledX;
            debugger.ui.mouseY = scaledY;
        }
        else if (ev.type == SDL_WINDOWEVENT) {
            sprite_debug_handleWindowEvent(&ev);
            if (ev.window.event == SDL_WINDOWEVENT_MOVED) {
                debugger.layout.winX = ev.window.data1; debugger.layout.winY = ev.window.data2; config_saveConfig();
            } else if (ev.window.event == SDL_WINDOWEVENT_RESIZED || ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                debugger.layout.winW = ev.window.data1; debugger.layout.winH = ev.window.data2; config_saveConfig();
                e9ui_updateFontScale();
            }
        }
        else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
            if (!e9ui_controller) {
                e9ui_controllerOpenIndex(ev.cdevice.which);
            }
            continue;
        }
        else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (e9ui_controller && ev.cdevice.which == e9ui_controllerId) {
                e9ui_controllerClose();
            }
            continue;
        }
        else if (ev.type == SDL_CONTROLLERAXISMOTION) {
            if (e9ui_controller && ev.caxis.which == e9ui_controllerId) {
                e9ui_controllerHandleAxis((SDL_GameControllerAxis)ev.caxis.axis, ev.caxis.value);
            }
            continue;
        }
        else if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
            if (e9ui_controller && ev.cbutton.which == e9ui_controllerId) {
                unsigned id = 0;
                if (e9ui_controllerMapButton((SDL_GameControllerButton)ev.cbutton.button, &id)) {
                    int pressed = (ev.type == SDL_CONTROLLERBUTTONDOWN) ? 1 : 0;
                    libretro_host_setJoypadState(0, id, pressed);
                }
            }
            continue;
        }
        else if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                if (sprite_debug_is_window_id(ev.key.windowID)) {
                    if (sprite_debug_is_open()) {
                        sprite_debug_toggle();
                    }
                    continue;
                }
                if (debugger.ui.helpModal) {
                    help_cancelModal();
                    continue;
                }
                if (debugger.ui.settingsModal) {
                    debugger_cancelSettingsModal();
                    continue;
                }
                if (debugger.ui.fullscreen) {
                    e9ui_clearFullscreenComponent();
                } else {
                    e9ui_component_t *geo_box = e9ui_findById(debugger.ui.root, "libretro_box");
                    if (geo_box) {
                        e9ui_setFullscreenComponent(geo_box);
                    } else {
                        e9ui_component_t *geo_view = e9ui_findById(debugger.ui.root, "geo_view");
                        if (geo_view) {
                            e9ui_setFullscreenComponent(geo_view);
                        }
                    }
                }
                continue;
            }
            if (ev.key.keysym.sym == SDLK_F1) {
                e9ui_setFocus(&debugger.ui.ctx, NULL);
                if (debugger.ui.helpModal) {
                    help_cancelModal();
                } else {
                    help_showModal(&debugger.ui.ctx);
                }
                continue;
            }
            if (ev.key.keysym.sym == SDLK_F2) {
                e9ui_setFocus(&debugger.ui.ctx, NULL);
                ui_copyFramebufferToClipboard();
                continue;
            }
            if (ev.key.keysym.sym == SDLK_F3) {
                e9ui_setFocus(&debugger.ui.ctx, NULL);
                crt_setEnabled(!crt_isEnabled());
                debugger.config.crtEnabled = crt_isEnabled() ? 1 : 0;
                continue;
            }
            if (ev.key.keysym.sym == SDLK_F4) {
                e9ui_fpsEnabled = !e9ui_fpsEnabled;
                e9ui_setFocus(&debugger.ui.ctx, NULL);
                e9ui_showTransientMessage(e9ui_fpsEnabled ? "FPS ON" : "FPS OFF");
                continue;
            }

            if (ev.key.keysym.sym == SDLK_COMMA || ev.key.keysym.sym == SDLK_PERIOD || ev.key.keysym.sym == SDLK_SLASH) {
                SDL_Keymod mods = (SDL_Keymod)(ev.key.keysym.mod & (KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT));
                int has_focus = (e9ui_getFocus(&debugger.ui.ctx) != NULL);
                if (mods == 0 && !has_focus) {
                    if (!input_record_isPlayback()) {
                        input_record_recordUiKey(debugger.frameCounter + 1, (unsigned)ev.key.keysym.sym, 1);
                        input_record_handleUiKey((unsigned)ev.key.keysym.sym, 1);
                    }
                    continue;
                }
            }
            // Focused component gets first crack at keydown
            int consumed = 0;
            if (debugger.ui.ctx.dispatchHotkey) {
                consumed = debugger.ui.ctx.dispatchHotkey(&debugger.ui.ctx, &ev.key);
            }
            if (!consumed && e9ui_getFocus(&debugger.ui.ctx) && e9ui_getFocus(&debugger.ui.ctx)->handleEvent) {
                consumed = e9ui_getFocus(&debugger.ui.ctx)->handleEvent(e9ui_getFocus(&debugger.ui.ctx), &debugger.ui.ctx, &ev);
            }
            e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
            if (!consumed && root && root->handleEvent) {
                root->handleEvent(root, &debugger.ui.ctx, &ev);
            }
            continue;
        } else if (ev.type == SDL_TEXTINPUT) {
            // Text input goes only to focused component
            if (e9ui_getFocus(&debugger.ui.ctx) && e9ui_getFocus(&debugger.ui.ctx)->handleEvent) {
                int consumed = e9ui_getFocus(&debugger.ui.ctx)->handleEvent(e9ui_getFocus(&debugger.ui.ctx), &debugger.ui.ctx, &ev);
                (void)consumed;
            }
            continue;
        }
        // For mouse and other events, bubble through tree for hit-testing and focus updates
        e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
        if (root) {
            e9ui_event_process(root, &debugger.ui.ctx, &ev);
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && !debugger.ui.ctx.focusClickHandled) {
            if (!sprite_debug_is_window_id(ev.button.windowID)) {
                e9ui_setFocus(&debugger.ui.ctx, NULL);
            }
        }
    }
    return 0;
}


void
e9ui_shutdown(void)
{  
  e9ui_controllerClose();
  gl_composite_shutdown();
  if (e9ui_fullscreenHintFont) {
    TTF_CloseFont(e9ui_fullscreenHintFont);
    e9ui_fullscreenHintFont = NULL;
  }
  if (e9ui_fpsFont) {
    TTF_CloseFont(e9ui_fpsFont);
    e9ui_fpsFont = NULL;
  }
  e9ui_split_resetCursors();
  e9ui_split_stack_resetCursors();
  e9ui_box_resetCursors();
  if (debugger.ui.hotkeys.entries) {
    alloc_free(debugger.ui.hotkeys.entries);
    debugger.ui.hotkeys.entries = NULL;
    debugger.ui.hotkeys.count = debugger.ui.hotkeys.cap =
      debugger.ui.hotkeys.next_id = 0;
  }
  
  if (debugger.ui.ctx.font) {
    TTF_CloseFont(debugger.ui.ctx.font);
    debugger.ui.ctx.font = NULL;
  }
  
  e9ui_theme_unloadFonts();
  e9ui_text_cache_clear();
  
  e9ui_childDestroy(debugger.ui.root, &debugger.ui.ctx);
  debugger.ui.root = NULL;

  if (debugger.ui.ctx.renderer)
    SDL_DestroyRenderer(debugger.ui.ctx.renderer);
  if (debugger.ui.ctx.window)
    SDL_DestroyWindow(debugger.ui.ctx.window);
  
  IMG_Quit();
  TTF_Quit();
  SDL_Quit();
}
