/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <math.h>

#include "config.h"
#include "hotkeys.h"
#include "help.h"
#include "file.h"
#include "e9ui_theme.h"
#include "ui_test.h"
#include "e9ui_text_cache.h"
#include "e9ui_theme_defaults.h"
#include "strutil.h"

#ifdef E9UI_ENABLE_GAMEPAD
#include "debugger.h"
#include "libretro_host.h"
#include "libretro.h"
#endif

#ifdef E9UI_ENABLE_DEBUG_FONT
#include "debug_font.h"
#endif

static int
e9ui_componentTreeContainsPointer(const e9ui_component_t *root, const void *target);

#ifdef E9UI_ENABLE_GAMEPAD
typedef enum e9ui_controller_device_type
{
  e9ui_controllerDeviceNone = 0,
  e9ui_controllerDeviceGameController,
  e9ui_controllerDeviceJoystick
} e9ui_controller_device_type_t;

static SDL_GameController *e9ui_controller = NULL;
static SDL_Joystick *e9ui_joystick = NULL;
static SDL_JoystickID e9ui_controllerId = -1;
static e9ui_controller_device_type_t e9ui_controllerDeviceType = e9ui_controllerDeviceNone;
static int e9ui_controllerLeft = 0;
static int e9ui_controllerRight = 0;
static int e9ui_controllerUp = 0;
static int e9ui_controllerDown = 0;
static int e9ui_controllerAxisLeft = 0;
static int e9ui_controllerAxisRight = 0;
static int e9ui_controllerAxisUp = 0;
static int e9ui_controllerAxisDown = 0;
static int e9ui_controllerHatLeft = 0;
static int e9ui_controllerHatRight = 0;
static int e9ui_controllerHatUp = 0;
static int e9ui_controllerHatDown = 0;
static const int e9ui_controllerDeadzone = 8000;
static char e9ui_controllerPreferredGuid[E9UI_GAMEPAD_GUID_CAP];
#endif
static uint32_t e9ui_fullscreenHintStart = 0;
static TTF_Font *e9ui_fullscreenHintFont = NULL;
static int e9ui_fullscreenHintSize = 0;
static const char *e9ui_transientMessage = NULL;
static char e9ui_fullscreenMessage[160] = "PRESS F12 TO EXIT FULLSCREEN";
static int e9ui_loadingLayout = 0;
static int e9ui_fpsEnabled = 0;
static uint32_t e9ui_fpsLastTick = 0;
static int e9ui_fpsFrames = 0;
static float e9ui_fpsValue = 0.0f;
static TTF_Font *e9ui_fpsFont = NULL;
static int e9ui_fpsFontSize = 0;
static void e9ui_updateFontScale(void);
static void e9ui_updateRefreshRate(SDL_Window *win);

static void
e9ui_runDeferred(e9ui_context_t *ctx)
{
    if (!e9ui || !e9ui->deferred || e9ui->deferredCount <= 0) {
        return;
    }
    while (e9ui->deferred && e9ui->deferredCount > 0) {
        e9ui_defer_entry_t *entries = e9ui->deferred;
        int count = e9ui->deferredCount;
        e9ui->deferred = NULL;
        e9ui->deferredCount = 0;
        e9ui->deferredCap = 0;
        for (int i = 0; i < count; ++i) {
            if (entries[i].fn) {
                entries[i].fn(ctx, entries[i].user);
            }
        }
        alloc_free(entries);
    }
}

static void
e9ui_updateState(e9ui_component_t *comp, e9ui_context_t *ctx);

static void
e9ui_updateAutoHide(e9ui_component_t *comp, e9ui_context_t *ctx);

static void
e9ui_updateFullscreenHintMessage(void)
{
#ifdef E9UI_ENABLE_DEBUG_FONT
    char binding[64];
    if (hotkeys_formatActionBindingDisplay("fullscreen", binding, sizeof(binding)) && binding[0]) {
        snprintf(e9ui_fullscreenMessage, sizeof(e9ui_fullscreenMessage),
                 "PRESS %s TO EXIT FULLSCREEN", binding);
        return;
    }
#endif
    snprintf(e9ui_fullscreenMessage, sizeof(e9ui_fullscreenMessage),
             "PRESS FULLSCREEN HOTKEY TO EXIT FULLSCREEN");
}

int
e9ui_getFpsEnabled(void)
{
    return e9ui_fpsEnabled ? 1 : 0;
}

void
e9ui_setFpsEnabled(int enabled)
{
    e9ui_fpsEnabled = enabled ? 1 : 0;
}

uint32_t
e9ui_getTicks(e9ui_context_t *ctx)
{
    if (ctx && ctx->getTicks) {
        return ctx->getTicks(ctx);
    }
    return SDL_GetTicks();
}

void
e9ui_setRefreshHz(e9ui_context_t *ctx, int refreshHz)
{
    if (!ctx || !ctx->setRefreshHz || refreshHz <= 0) {
        return;
    }
    ctx->setRefreshHz(ctx, refreshHz);
}

const char *
e9ui_getWindowIconAssetPath(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->getWindowIconAssetPath) {
        return NULL;
    }
    return ctx->getWindowIconAssetPath(ctx);
}

uint64_t
e9ui_getNextUiFrameId(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->getNextUiFrameId) {
        return 0;
    }
    return ctx->getNextUiFrameId(ctx);
}

void
e9ui_commitUiFrameId(e9ui_context_t *ctx, uint64_t frameId)
{
    if (!ctx || !ctx->commitUiFrameId) {
        return;
    }
    ctx->commitUiFrameId(ctx, frameId);
}

int
e9ui_captureUiFrame(e9ui_context_t *ctx, uint64_t frameId, SDL_Renderer *renderer)
{
    if (!ctx || !ctx->captureUiFrame) {
        return 0;
    }
    return ctx->captureUiFrame(ctx, frameId, renderer);
}

int
e9ui_transformTextboxSelection(e9ui_context_t *ctx,
                               const char *actionId,
                               const char *input,
                               char *out,
                               size_t outCap)
{
    if (!ctx || !ctx->transformTextboxSelection || !actionId || !input || !out || outCap == 0) {
        return 0;
    }
    return ctx->transformTextboxSelection(ctx, actionId, input, out, outCap);
}

int
e9ui_shouldPresentFrame(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->shouldPresentFrame) {
        return 1;
    }
    return ctx->shouldPresentFrame(ctx);
}

int
e9ui_pollInjectedUiEvent(e9ui_context_t *ctx, uint64_t frameId, SDL_Event *eventValue)
{
    if (!ctx || !ctx->pollInjectedUiEvent) {
        return 0;
    }
    return ctx->pollInjectedUiEvent(ctx, frameId, eventValue);
}

void
e9ui_recordUiEvent(e9ui_context_t *ctx, uint64_t frameId, const SDL_Event *eventValue)
{
    if (!ctx || !ctx->recordUiEvent || !eventValue) {
        return;
    }
    ctx->recordUiEvent(ctx, frameId, eventValue);
}

int
e9ui_runFullscreenTransition(e9ui_context_t *ctx,
                             int entering,
                             e9ui_component_t *from,
                             e9ui_component_t *to,
                             int width,
                             int height)
{
    if (!ctx || !ctx->runFullscreenTransition) {
        return 0;
    }
    return ctx->runFullscreenTransition(ctx, entering, from, to, width, height);
}

void
e9ui_setMainWindowFocused(e9ui_context_t *ctx, int focused)
{
    if (!ctx || !ctx->setMainWindowFocused) {
        return;
    }
    ctx->setMainWindowFocused(ctx, focused);
}

int
e9ui_handleGlobalKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
    if (!ctx || !ctx->handleGlobalKeydown || !kev) {
        return 0;
    }
    return ctx->handleGlobalKeydown(ctx, kev);
}

void
e9ui_shutdownHostUi(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->shutdownHostUi) {
        return;
    }
    ctx->shutdownHostUi(ctx);
}

void
e9ui_prepareMainWindow(e9ui_context_t *ctx,
                       int cliOverrideWindowSize,
                       int startHidden,
                       int *wantX,
                       int *wantY,
                       int *wantW,
                       int *wantH,
                       Uint32 *winFlags)
{
    if (!ctx || !ctx->prepareMainWindow) {
        (void)cliOverrideWindowSize;
        (void)startHidden;
        return;
    }
    ctx->prepareMainWindow(ctx, cliOverrideWindowSize, startHidden, wantX, wantY, wantW, wantH, winFlags);
}

int
e9ui_shouldUseVsync(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->shouldUseVsync) {
        return 1;
    }
    return ctx->shouldUseVsync(ctx);
}

void
e9ui_finalizeMainWindow(e9ui_context_t *ctx,
                        SDL_Window *window,
                        SDL_Renderer *renderer,
                        int wantW,
                        int wantH)
{
    if (!ctx || !ctx->finalizeMainWindow) {
        (void)window;
        (void)renderer;
        (void)wantW;
        (void)wantH;
        return;
    }
    ctx->finalizeMainWindow(ctx, window, renderer, wantW, wantH);
}

int
e9ui_defer(e9ui_context_t *ctx, e9ui_defer_fn_t fn, void *user)
{
    (void)ctx;
    if (!e9ui || !fn) {
        return 0;
    }
    if (e9ui->deferredCount >= e9ui->deferredCap) {
        int nextCap = e9ui->deferredCap ? e9ui->deferredCap * 2 : 8;
        e9ui_defer_entry_t *next = (e9ui_defer_entry_t *)alloc_realloc(
            e9ui->deferred, (size_t)nextCap * sizeof(*next));
        if (!next) {
            return 0;
        }
        e9ui->deferred = next;
        e9ui->deferredCap = nextCap;
    }
    e9ui->deferred[e9ui->deferredCount].fn = fn;
    e9ui->deferred[e9ui->deferredCount].user = user;
    e9ui->deferredCount++;
    return 1;
}

static void
e9ui_overlayHost_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self) {
        return;
    }
    self->bounds = bounds;
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(self, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (!it->child || e9ui_getHidden(it->child) || !it->child->layout) {
            continue;
        }
        it->child->layout(it->child, ctx, bounds);
    }
}

static void
e9ui_overlayHost_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self) {
        return;
    }
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(self, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (!it->child || e9ui_getHidden(it->child) || !it->child->render) {
            continue;
        }
        it->child->render(it->child, ctx);
    }
}

static e9ui_component_t *
e9ui_makeOverlayHost(const char *name)
{
    e9ui_component_t *host = (e9ui_component_t *)alloc_calloc(1, sizeof(*host));
    if (!host) {
        return NULL;
    }
    host->name = name;
    host->layout = e9ui_overlayHost_layout;
    host->render = e9ui_overlayHost_render;
    return host;
}

static e9ui_component_t *
e9ui_activeContentRoot(void)
{
    if (!e9ui) {
        return NULL;
    }
    return e9ui->fullscreen ? e9ui->fullscreen : e9ui->root;
}

e9ui_component_t *
e9ui_getOverlayHost(void)
{
    if (!e9ui) {
        return NULL;
    }
    return e9ui->overlayRoot;
}

e9ui_component_t *
e9ui_getSelectionScopeForOwner(const e9ui_component_t *owner)
{
    e9ui_child_reverse_iterator iter;
    e9ui_component_t *contentRoot = NULL;

    if (!e9ui || !owner) {
        return NULL;
    }

    if (e9ui->overlayRoot && e9ui_child_iterateChildrenReverse(e9ui->overlayRoot, &iter)) {
        for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
             it;
             it = e9ui_child_iteratePrev(&iter)) {
            if (!it->child) {
                continue;
            }
            if (e9ui_componentTreeContainsPointer(it->child, owner)) {
                return it->child;
            }
        }
    }
    contentRoot = e9ui_activeContentRoot();
    if (e9ui_componentTreeContainsPointer(contentRoot, owner)) {
        return contentRoot;
    }
    return NULL;
}

static void
e9ui_sceneUpdateState(void)
{
    if (e9ui->root) {
        e9ui_updateState(e9ui->root, &e9ui->ctx);
    }
    if (e9ui->overlayRoot) {
        e9ui_updateState(e9ui->overlayRoot, &e9ui->ctx);
    }
}

static void
e9ui_sceneLayout(int width, int height)
{
    e9ui_rect_t full = (e9ui_rect_t){ 0, 0, width, height };
    e9ui_component_t *contentRoot = e9ui_activeContentRoot();
    if (contentRoot && contentRoot->layout) {
        contentRoot->layout(contentRoot, &e9ui->ctx, full);
    }
    if (e9ui->overlayRoot && e9ui->overlayRoot->layout) {
        e9ui->overlayRoot->layout(e9ui->overlayRoot, &e9ui->ctx, full);
    }
}

static void
e9ui_sceneUpdateAutoHide(void)
{
    e9ui_component_t *contentRoot = e9ui_activeContentRoot();
    if (contentRoot) {
        e9ui_updateAutoHide(contentRoot, &e9ui->ctx);
    }
    if (e9ui->overlayRoot) {
        e9ui_updateAutoHide(e9ui->overlayRoot, &e9ui->ctx);
    }
}

static void
e9ui_sceneRender(void)
{
    e9ui_component_t *contentRoot = e9ui_activeContentRoot();
    if (contentRoot && contentRoot->render) {
        contentRoot->render(contentRoot, &e9ui->ctx);
    }
    if (e9ui->overlayRoot && e9ui->overlayRoot->render) {
        e9ui->overlayRoot->render(e9ui->overlayRoot, &e9ui->ctx);
    }
}

static void
e9ui_resetRendererOverlayState(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    SDL_RenderSetClipRect(ctx->renderer, NULL);
    SDL_RenderSetViewport(ctx->renderer, NULL);
    SDL_RenderSetScale(ctx->renderer, 1.0f, 1.0f);
}

static int
e9ui_pointInBounds(const e9ui_component_t *comp, int x, int y);

static void
e9ui_pointerOwnerClearIfStale(e9ui_context_t *ctx);

static e9ui_component_t *
e9ui_pointerOwnerForPoint(int x, int y)
{
    if (!e9ui) {
        return NULL;
    }
    e9ui_child_reverse_iterator iter;
    if (e9ui->overlayRoot && e9ui_child_iterateChildrenReverse(e9ui->overlayRoot, &iter)) {
        for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
             it;
             it = e9ui_child_iteratePrev(&iter)) {
            if (!it->child || e9ui_getHidden(it->child)) {
                continue;
            }
            if (e9ui_pointInBounds(it->child, x, y)) {
                return it->child;
            }
        }
    }
    return e9ui_activeContentRoot();
}

static int
e9ui_sceneProcessEvent(const e9ui_event_t *ev)
{
    e9ui_pointerOwnerClearIfStale(&e9ui->ctx);
    if (ev &&
        (ev->type == SDL_MOUSEMOTION ||
         ev->type == SDL_MOUSEBUTTONUP) &&
        e9ui->ctx.pointerOwner) {
        int consumed = e9ui_event_process(e9ui->ctx.pointerOwner, &e9ui->ctx, ev);
        if (ev->type == SDL_MOUSEBUTTONUP &&
            ev->button.button == e9ui->ctx.pointerOwnerButton) {
            e9ui->ctx.pointerOwner = NULL;
            e9ui->ctx.pointerOwnerButton = 0;
        }
        return consumed;
    }
    if (ev && ev->type == SDL_MOUSEBUTTONDOWN) {
        e9ui_component_t *pointerOwner = e9ui_pointerOwnerForPoint(ev->button.x, ev->button.y);
        if (pointerOwner) {
            e9ui->ctx.pointerOwner = pointerOwner;
            e9ui->ctx.pointerOwnerButton = ev->button.button;
            return e9ui_event_process(pointerOwner, &e9ui->ctx, ev);
        }
        return 0;
    }
    if (e9ui->overlayRoot) {
        int allow_multiple = 0;
        int consumed = 0;
        if (ev && (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONUP)) {
            allow_multiple = 1;
        }
        e9ui_child_reverse_iterator iter;
        if (e9ui_child_iterateChildrenReverse(e9ui->overlayRoot, &iter)) {
            for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
                 it;
                 it = e9ui_child_iteratePrev(&iter)) {
                if (!it->child) {
                    continue;
                }
                if (e9ui_event_process(it->child, &e9ui->ctx, ev)) {
                    consumed = 1;
                    if (!allow_multiple) {
                        return 1;
                    }
                }
            }
        }
        if (consumed) {
            return 1;
        }
    }
    e9ui_component_t *contentRoot = e9ui_activeContentRoot();
    if (contentRoot && e9ui_event_process(contentRoot, &e9ui->ctx, ev)) {
        return 1;
    }
    return 0;
}

static int
e9ui_eventIsPointer(const e9ui_event_t *ev)
{
    if (!ev) {
        return 0;
    }
    return ev->type == SDL_MOUSEMOTION ||
           ev->type == SDL_MOUSEBUTTONDOWN ||
           ev->type == SDL_MOUSEBUTTONUP ||
           ev->type == SDL_MOUSEWHEEL;
}

static int
e9ui_componentTreeContainsPointer(const e9ui_component_t *root, const void *target)
{
    if (!root || !target) {
        return 0;
    }
    if ((const void *)root == target) {
        return 1;
    }
    for (list_t *ptr = root->children; ptr; ptr = ptr->next) {
        e9ui_component_child_t *container = (e9ui_component_child_t *)ptr->data;
        if (!container || !container->component) {
            continue;
        }
        if ((const void *)container->component == target) {
            return 1;
        }
        if (e9ui_componentTreeContainsPointer(container->component, target)) {
            return 1;
        }
    }
    return 0;
}

static int
e9ui_captureOwnerIsLive(const e9ui_context_t *ctx, const void *owner)
{
    if (!ctx || !owner || !e9ui) {
        return 0;
    }
    if (e9ui_componentTreeContainsPointer(e9ui->overlayRoot, owner)) {
        return 1;
    }
    if (e9ui_componentTreeContainsPointer(e9ui_activeContentRoot(), owner)) {
        return 1;
    }
    if (e9ui_componentTreeContainsPointer(e9ui->root, owner)) {
        return 1;
    }
    return 0;
}

static void
e9ui_focusClearIfStale(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->_focus) {
        return;
    }
    if (!e9ui_captureOwnerIsLive(ctx, ctx->_focus)) {
        ctx->_focus = NULL;
    }
}

static void
e9ui_pointerOwnerClearIfStale(e9ui_context_t *ctx)
{
    if (!ctx || !ctx->pointerOwner) {
        return;
    }
    if (!e9ui_captureOwnerIsLive(ctx, ctx->pointerOwner)) {
        ctx->pointerOwner = NULL;
        ctx->pointerOwnerButton = 0;
    }
}

static int
e9ui_dispatchCapturedPointerEvent(e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!ctx || !ev) {
        return 0;
    }
    if (!e9ui_eventIsPointer(ev) || !ctx->cursorCaptureOwner) {
        return 0;
    }
    void *ownerPtr = ctx->cursorCaptureOwner;
    if (!e9ui_captureOwnerIsLive(ctx, ownerPtr)) {
        ctx->cursorCaptureOwner = NULL;
        ctx->cursorCapture = NULL;
        return 1;
    }
    e9ui_component_t *owner = (e9ui_component_t *)ownerPtr;
    if (!owner || !owner->handleEvent) {
        ctx->cursorCaptureOwner = NULL;
        ctx->cursorCapture = NULL;
        return 1;
    }
    (void)owner->handleEvent(owner, ctx, ev);
    if (ev->type == SDL_MOUSEBUTTONUP) {
        (void)e9ui_text_select_handleEvent(ctx, ev);
    }
    return 1;
}

static void
e9ui_closeOverlaysBeforeFullscreenTransition(void)
{
    e9ui_windowCloseAllOverlay();
    e9ui_modal_closeAll(&e9ui->ctx);
}

static int
e9ui_getDisplayRefreshRate(int displayIndex)
{
    int refresh = 0;
    if (displayIndex < 0) {
        return 0;
    }
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(displayIndex, &mode) == 0) {
        refresh = mode.refresh_rate;
    }
    if (refresh <= 0 && SDL_GetDesktopDisplayMode(displayIndex, &mode) == 0) {
        refresh = mode.refresh_rate;
    }
    return refresh;
}

static void
e9ui_syncWindowDisplayState(SDL_Window *win, int forceFontScale)
{
    if (!win) {
        return;
    }
    int displayIndex = SDL_GetWindowDisplayIndex(win);
    int displayChanged = 0;
    if (displayIndex >= 0 && displayIndex != e9ui->currentDisplayIndex) {
        e9ui->currentDisplayIndex = displayIndex;
        displayChanged = 1;
    }
    e9ui_updateRefreshRate(win);
    if (forceFontScale || displayChanged) {
        e9ui_updateFontScale();
    }
}

static void
e9ui_updateRefreshRate(SDL_Window *win)
{
    int displayIndex = SDL_GetWindowDisplayIndex(win);
    int refresh = e9ui_getDisplayRefreshRate(displayIndex);
    e9ui_setRefreshHz(&e9ui->ctx, refresh);
}

static void
e9ui_applyWindowIcon(SDL_Window *win)
{
  if (!win) {
    return;
  }
  const char *icon_asset = e9ui_getWindowIconAssetPath(&e9ui->ctx);
  if (!icon_asset || !icon_asset[0]) {
    return;
  }
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

void
e9ui_drawFocusRingRect(e9ui_context_t *ctx, SDL_Rect rect, int padPx)
{
  if (!ctx || !ctx->renderer || rect.w <= 0 || rect.h <= 0) {
    return;
  }

  int pad = e9ui_scale_px(ctx, padPx);
  if (pad <= 0) {
    pad = padPx;
  }
  if (pad < 1) {
    pad = 1;
  }

  SDL_Rect inner = {
    rect.x - pad,
    rect.y - pad,
    rect.w + pad * 2,
    rect.h + pad * 2
  };
  SDL_Rect outer = {
    inner.x - 1,
    inner.y - 1,
    inner.w + 2,
    inner.h + 2
  };

  SDL_SetRenderDrawColor(ctx->renderer, 36, 84, 150, 255);
  SDL_RenderDrawRect(ctx->renderer, &outer);
  SDL_SetRenderDrawColor(ctx->renderer, 120, 180, 255, 255);
  SDL_RenderDrawRect(ctx->renderer, &inner);
}

static void
e9ui_renderMissingFontNotice(void)
{
#ifdef E9UI_ENABLE_DEBUG_FONT
  SDL_SetRenderDrawColor(e9ui->ctx.renderer, 220, 190, 190, 255);
  debug_font_drawText(e9ui->ctx.renderer, 12, 12, "MISSING FONT - EXPECTED", 2);
  debug_font_drawText(e9ui->ctx.renderer, 12, 28, "assets/RobotoMono-Regular.ttf", 2);
#endif
}

static void
e9ui_renderTransientMessage(e9ui_context_t *ctx, int w, int h)
{
  if (!ctx || !ctx->renderer || !e9ui_transientMessage) {
    return;
  }
  uint32_t now = e9ui_getTicks(ctx);
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
  if (size != e9ui_fullscreenHintSize || !e9ui_fullscreenHintFont) {
    if (e9ui_fullscreenHintFont) {
      TTF_CloseFont(e9ui_fullscreenHintFont);
      e9ui_fullscreenHintFont = NULL;
    }
    e9ui_fullscreenHintSize = size;
    const char *asset = e9ui->theme.text.fontAsset ? e9ui->theme.text.fontAsset : E9UI_THEME_TEXT_FONT_ASSET;
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
  if (!ctx || !ctx->renderer || !e9ui_fpsEnabled || !e9ui->fullscreen) {
    return;
  }
  uint32_t now = e9ui_getTicks(ctx);
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
  if (size != e9ui_fpsFontSize || !e9ui_fpsFont) {
    if (e9ui_fpsFont) {
      TTF_CloseFont(e9ui_fpsFont);
      e9ui_fpsFont = NULL;
    }
    e9ui_fpsFontSize = size;
    const char *asset = e9ui->theme.text.fontAsset ? e9ui->theme.text.fontAsset : E9UI_THEME_TEXT_FONT_ASSET;
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

#ifdef E9UI_ENABLE_GAMEPAD
static void
e9ui_controllerNormalizeGuid(const char *guid, char *out, size_t outCap)
{
  if (!out || outCap == 0) {
    return;
  }
  out[0] = '\0';
  if (!guid || !guid[0]) {
    return;
  }
  if (strcmp(guid, "auto") == 0) {
    return;
  }
  strutil_strlcpy(out, outCap, guid);
}

static void
e9ui_controllerGetGuidString(SDL_JoystickGUID guid, char *out, size_t outCap)
{
  if (!out || outCap == 0) {
    return;
  }
  out[0] = '\0';
  char tmp[E9UI_GAMEPAD_GUID_CAP];
  SDL_JoystickGetGUIDString(guid, tmp, (int)sizeof(tmp));
  strutil_strlcpy(out, outCap, tmp);
}

static int
e9ui_controllerReadGuidForIndex(int index, char *out, size_t outCap)
{
  if (!out || outCap == 0 || index < 0) {
    return 0;
  }
  e9ui_controllerGetGuidString(SDL_JoystickGetDeviceGUID(index), out, outCap);
  return out[0] ? 1 : 0;
}

static int
e9ui_controllerReadActiveGuid(char *out, size_t outCap)
{
  SDL_Joystick *joy = NULL;
  if (!out || outCap == 0) {
    return 0;
  }
  if (e9ui_controller) {
    joy = SDL_GameControllerGetJoystick(e9ui_controller);
  } else if (e9ui_joystick) {
    joy = e9ui_joystick;
  } else {
    return 0;
  }
  if (!joy) {
    out[0] = '\0';
    return 0;
  }
  e9ui_controllerGetGuidString(SDL_JoystickGetGUID(joy), out, outCap);
  return out[0] ? 1 : 0;
}

static int
e9ui_controllerPreferredGuidSet(void)
{
  return e9ui_controllerPreferredGuid[0] ? 1 : 0;
}

static void
e9ui_controllerClose(void)
{
  if (e9ui_controller) {
    SDL_GameControllerClose(e9ui_controller);
    e9ui_controller = NULL;
  }
  if (e9ui_joystick) {
    SDL_JoystickClose(e9ui_joystick);
    e9ui_joystick = NULL;
  }
  e9ui_controllerId = -1;
  e9ui_controllerDeviceType = e9ui_controllerDeviceNone;
  e9ui_controllerLeft = 0;
  e9ui_controllerRight = 0;
  e9ui_controllerUp = 0;
  e9ui_controllerDown = 0;
  e9ui_controllerAxisLeft = 0;
  e9ui_controllerAxisRight = 0;
  e9ui_controllerAxisUp = 0;
  e9ui_controllerAxisDown = 0;
  e9ui_controllerHatLeft = 0;
  e9ui_controllerHatRight = 0;
  e9ui_controllerHatUp = 0;
  e9ui_controllerHatDown = 0;
  libretro_host_clearJoypadState();
}

static void
e9ui_controllerOpenIndex(int index)
{
  if (e9ui_controller || e9ui_joystick || index < 0) {
    return;
  }
  if (SDL_IsGameController(index)) {
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
    e9ui_controllerDeviceType = e9ui_controllerDeviceGameController;
    return;
  }
  SDL_Joystick *joy = SDL_JoystickOpen(index);
  if (!joy) {
    return;
  }
  e9ui_joystick = joy;
  e9ui_controllerId = SDL_JoystickInstanceID(joy);
  e9ui_controllerDeviceType = e9ui_controllerDeviceJoystick;
}

static void
e9ui_controllerTryOpenPreferred(void)
{
  if (e9ui_controller || e9ui_joystick || !e9ui_controllerPreferredGuidSet()) {
    return;
  }
  int count = SDL_NumJoysticks();
  for (int i = 0; i < count; ++i) {
    char guid[E9UI_GAMEPAD_GUID_CAP];
    if (!e9ui_controllerReadGuidForIndex(i, guid, sizeof(guid))) {
      continue;
    }
    if (strcmp(guid, e9ui_controllerPreferredGuid) != 0) {
      continue;
    }
    e9ui_controllerOpenIndex(i);
    if (e9ui_controller || e9ui_joystick) {
      return;
    }
  }
}

static void
e9ui_controllerOpenPreferredOrAuto(void)
{
  if (e9ui_controller || e9ui_joystick) {
    return;
  }
  if (e9ui_controllerPreferredGuidSet()) {
    e9ui_controllerTryOpenPreferred();
    return;
  }
  int count = SDL_NumJoysticks();
  for (int i = 0; i < count; ++i) {
    if (!SDL_IsGameController(i)) {
      continue;
    }
    e9ui_controllerOpenIndex(i);
    if (e9ui_controller || e9ui_joystick) {
      return;
    }
  }
  for (int i = 0; i < count; ++i) {
    if (SDL_IsGameController(i)) {
      continue;
    }
    e9ui_controllerOpenIndex(i);
    if (e9ui_controller || e9ui_joystick) {
      return;
    }
  }
}

static void
e9ui_controllerInit(void)
{
  e9ui_controllerNormalizeGuid(debugger.preferredControllerGuid,
                               e9ui_controllerPreferredGuid,
                               sizeof(e9ui_controllerPreferredGuid));
  e9ui_controllerOpenPreferredOrAuto();
}

static int
e9ui_controllerMapButton(SDL_GameControllerButton button, unsigned *outId)
{
  if (!outId) {
    return 0;
  }

  return target->controllerMapButton(button, outId);  
}

static void
e9ui_controllerSetDir(unsigned port, unsigned id, int *state, int pressed)
{
  if (!state) {
    return;
  }
  if (*state == pressed) {
    return;
  }
  *state = pressed;
  libretro_host_setJoypadState(port, id, pressed);
}

static void
e9ui_controllerSyncAxesAndHat(void)
{
  unsigned port = 0u;
  e9ui_controllerSetDir(port,
                        RETRO_DEVICE_ID_JOYPAD_LEFT,
                        &e9ui_controllerLeft,
                        e9ui_controllerAxisLeft || e9ui_controllerHatLeft);
  e9ui_controllerSetDir(port,
                        RETRO_DEVICE_ID_JOYPAD_RIGHT,
                        &e9ui_controllerRight,
                        e9ui_controllerAxisRight || e9ui_controllerHatRight);
  e9ui_controllerSetDir(port,
                        RETRO_DEVICE_ID_JOYPAD_UP,
                        &e9ui_controllerUp,
                        e9ui_controllerAxisUp || e9ui_controllerHatUp);
  e9ui_controllerSetDir(port,
                        RETRO_DEVICE_ID_JOYPAD_DOWN,
                        &e9ui_controllerDown,
                        e9ui_controllerAxisDown || e9ui_controllerHatDown);
}

static void
e9ui_controllerHandleAxis(SDL_GameControllerAxis axis, int value)
{
  if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
    e9ui_controllerAxisLeft = (value < -e9ui_controllerDeadzone) ? 1 : 0;
    e9ui_controllerAxisRight = (value > e9ui_controllerDeadzone) ? 1 : 0;
  } else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
    e9ui_controllerAxisUp = (value < -e9ui_controllerDeadzone) ? 1 : 0;
    e9ui_controllerAxisDown = (value > e9ui_controllerDeadzone) ? 1 : 0;
  }
  e9ui_controllerSyncAxesAndHat();
}

static int
e9ui_controllerMapGenericButton(Uint8 button, SDL_GameControllerButton *outButton)
{
  SDL_GameControllerButton mapped = SDL_CONTROLLER_BUTTON_INVALID;
  switch (button) {
  case 0:
    mapped = SDL_CONTROLLER_BUTTON_A;
    break;
  case 1:
    mapped = SDL_CONTROLLER_BUTTON_B;
    break;
  case 2:
    mapped = SDL_CONTROLLER_BUTTON_X;
    break;
  case 3:
    mapped = SDL_CONTROLLER_BUTTON_Y;
    break;
  case 4:
    mapped = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    break;
  case 5:
    mapped = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    break;
  case 6:
  case 8:
    mapped = SDL_CONTROLLER_BUTTON_BACK;
    break;
  case 7:
  case 9:
    mapped = SDL_CONTROLLER_BUTTON_START;
    break;
  default:
    return 0;
  }
  if (outButton) {
    *outButton = mapped;
  }
  return 1;
}

static void
e9ui_controllerHandleJoystickButton(Uint8 button, int pressed)
{
  SDL_GameControllerButton mappedButton = SDL_CONTROLLER_BUTTON_INVALID;
  unsigned id = 0u;
  unsigned port = 0u;
  if (!e9ui_controllerMapGenericButton(button, &mappedButton)) {
    return;
  }
  if (!e9ui_controllerMapButton(mappedButton, &id)) {
    return;
  }
  libretro_host_setJoypadState(port, id, pressed);
}

static void
e9ui_controllerHandleHat(Uint8 value)
{
  e9ui_controllerHatLeft = (value & SDL_HAT_LEFT) ? 1 : 0;
  e9ui_controllerHatRight = (value & SDL_HAT_RIGHT) ? 1 : 0;
  e9ui_controllerHatUp = (value & SDL_HAT_UP) ? 1 : 0;
  e9ui_controllerHatDown = (value & SDL_HAT_DOWN) ? 1 : 0;
  e9ui_controllerSyncAxesAndHat();
}
#endif

size_t
e9ui_gamepadReadAvailable(e9ui_gamepad_info_t *out, size_t cap)
{
#ifdef E9UI_ENABLE_GAMEPAD
  size_t count = 0;
  int total = SDL_NumJoysticks();
  for (int i = 0; i < total; ++i) {
    if (out && count < cap) {
      e9ui_gamepad_info_t *dst = &out[count];
      memset(dst, 0, sizeof(*dst));
      e9ui_controllerReadGuidForIndex(i, dst->guid, sizeof(dst->guid));
      const char *name = SDL_IsGameController(i)
        ? SDL_GameControllerNameForIndex(i)
        : SDL_JoystickNameForIndex(i);
      strutil_strlcpy(dst->name, sizeof(dst->name), name ? name : dst->guid);
    }
    count++;
  }
  return count;
#else
  (void)out;
  (void)cap;
  return 0;
#endif
}

const char *
e9ui_gamepadGetPreferredGuid(void)
{
#ifdef E9UI_ENABLE_GAMEPAD
  return e9ui_controllerPreferredGuid[0] ? e9ui_controllerPreferredGuid : NULL;
#else
  return NULL;
#endif
}

void
e9ui_gamepadSetPreferredGuid(const char *guid)
{
#ifdef E9UI_ENABLE_GAMEPAD
  char normalized[E9UI_GAMEPAD_GUID_CAP];
  e9ui_controllerNormalizeGuid(guid, normalized, sizeof(normalized));
  if (strcmp(normalized, e9ui_controllerPreferredGuid) == 0) {
    if (!e9ui_controller && !e9ui_joystick) {
      e9ui_controllerOpenPreferredOrAuto();
    }
    return;
  }

  strutil_strlcpy(e9ui_controllerPreferredGuid, sizeof(e9ui_controllerPreferredGuid), normalized);

  char activeGuid[E9UI_GAMEPAD_GUID_CAP];
  int keepActive = 0;
  if (e9ui_controllerReadActiveGuid(activeGuid, sizeof(activeGuid))) {
    if (e9ui_controllerPreferredGuidSet() &&
        strcmp(activeGuid, e9ui_controllerPreferredGuid) == 0) {
      keepActive = 1;
    }
  }
  if (!keepActive && (e9ui_controller || e9ui_joystick)) {
    e9ui_controllerClose();
  }
  if (!e9ui_controller && !e9ui_joystick) {
    e9ui_controllerOpenPreferredOrAuto();
  }
#else
  (void)guid;
#endif
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
e9ui_updateStateTree(e9ui_component_t *root)
{
  e9ui_updateState(root, &e9ui->ctx);
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
  if (!ctx) {
    return;
  }
  e9ui_focusClearIfStale(ctx);
  e9ui_component_t *prev = ctx->_focus;
  if (prev && prev != comp && prev->name && strcmp(prev->name, "e9ui_textbox") == 0) {
    e9ui_textbox_clearSelectionExternal(prev);
  }
  ctx->_focus = comp;
  if (comp && prev != comp && comp->name && strcmp(comp->name, "e9ui_textbox") == 0) {
    e9ui_textbox_selectAllExternal(comp);
  }
}

int
e9ui_cursorIsCapturedBy(const e9ui_context_t *ctx, const void *owner)
{
    if (!ctx || !owner) {
        return 0;
    }
    return ctx->cursorCaptureOwner == owner ? 1 : 0;
}

void
e9ui_cursorRequest(e9ui_context_t *ctx, void *owner, SDL_Cursor *cursor)
{
    if (!ctx || !cursor) {
        return;
    }
    if (ctx->cursorCaptureOwner && ctx->cursorCaptureOwner != owner) {
        return;
    }
    if (ctx->cursorCaptureOwner == owner) {
        ctx->cursorCapture = cursor;
    }
    ctx->cursorOverride = 1;
    SDL_SetCursor(cursor);
}

void
e9ui_cursorCapture(e9ui_context_t *ctx, void *owner, SDL_Cursor *cursor)
{
    if (!ctx || !owner || !cursor) {
        return;
    }
    ctx->cursorCaptureOwner = owner;
    ctx->cursorCapture = cursor;
    e9ui_cursorRequest(ctx, owner, cursor);
}

void
e9ui_cursorRelease(e9ui_context_t *ctx, void *owner)
{
    if (!ctx || !owner) {
        return;
    }
    if (ctx->cursorCaptureOwner != owner) {
        return;
    }
    ctx->cursorCaptureOwner = NULL;
    ctx->cursorCapture = NULL;
}

static void
e9ui_focusCollectFocusable(e9ui_component_t *comp,
                           e9ui_component_t **out,
                           int cap,
                           int *count)
{
    if (!comp || !out || !count || *count >= cap) {
        return;
    }
    if (e9ui_getHidden(comp) || comp->disabled || comp->collapsed) {
        return;
    }
    if (comp->focusable && *count < cap) {
        out[*count] = comp;
        (*count)++;
    }
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (!it->child) {
            continue;
        }
        e9ui_focusCollectFocusable(it->child, out, cap, count);
    }
}

static int
e9ui_focusComponentContains(e9ui_component_t *root, e9ui_component_t *target)
{
    if (!root || !target) {
        return 0;
    }
    if (root == target) {
        return 1;
    }
    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(root, &iter)) {
        return 0;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (!it->child) {
            continue;
        }
        if (e9ui_focusComponentContains(it->child, target)) {
            return 1;
        }
    }
    return 0;
}

static int
e9ui_focusIsOverlayContainer(const e9ui_component_t *comp)
{
    if (!comp || !comp->name) {
        return 0;
    }
    return strcmp(comp->name, "e9ui_modal") == 0;
}

static e9ui_component_t *
e9ui_focusTopOverlayRoot(e9ui_component_t *overlayRoot)
{
    if (!overlayRoot) {
        return NULL;
    }
    e9ui_child_reverse_iterator iter;
    if (!e9ui_child_iterateChildrenReverse(overlayRoot, &iter)) {
        return NULL;
    }
    for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
         it;
         it = e9ui_child_iteratePrev(&iter)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child) || !e9ui_focusIsOverlayContainer(child)) {
            continue;
        }
        return child;
    }
    return NULL;
}

e9ui_component_t *
e9ui_focusTraversalRoot(e9ui_context_t *ctx, e9ui_component_t *current)
{
    if (!e9ui && !ctx) {
        return NULL;
    }
    e9ui_component_t *focusRoot = NULL;
    e9ui_component_t *focusFullscreen = NULL;
    e9ui_component_t *focusOverlayRoot = NULL;
    if (ctx) {
        focusRoot = ctx->focusRoot;
        focusFullscreen = ctx->focusFullscreen;
        if (e9ui && ctx == &e9ui->ctx) {
            focusOverlayRoot = e9ui->overlayRoot;
        }
    }
    if (!focusRoot && e9ui && (!ctx || ctx == &e9ui->ctx)) {
        focusRoot = e9ui->root;
    }
    if (!focusFullscreen && e9ui && (!ctx || ctx == &e9ui->ctx)) {
        focusFullscreen = e9ui->fullscreen;
    }
    if (e9ui && (!ctx || ctx == &e9ui->ctx)) {
        focusOverlayRoot = e9ui->overlayRoot;
    }
    e9ui_component_t *topOverlay = e9ui_focusTopOverlayRoot(focusOverlayRoot);
    if (!current) {
        if (topOverlay) {
            return topOverlay;
        }
        return focusFullscreen ? focusFullscreen : focusRoot;
    }
    if (topOverlay && e9ui_focusComponentContains(topOverlay, current)) {
        return topOverlay;
    }
    if (focusOverlayRoot) {
        e9ui_child_reverse_iterator iter;
        if (e9ui_child_iterateChildrenReverse(focusOverlayRoot, &iter)) {
            for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
                 it;
                 it = e9ui_child_iteratePrev(&iter)) {
                e9ui_component_t *child = it->child;
                if (!child || e9ui_getHidden(child) || !e9ui_focusIsOverlayContainer(child)) {
                    continue;
                }
                if (e9ui_focusComponentContains(child, current)) {
                    return child;
                }
            }
        }
    }
    return focusFullscreen ? focusFullscreen : focusRoot;
}

e9ui_component_t *
e9ui_focusFindNext(e9ui_component_t *root, e9ui_component_t *current, int reverse)
{
    if (!root) {
        return NULL;
    }
    e9ui_component_t *focusables[2048];
    int count = 0;
    e9ui_focusCollectFocusable(root, focusables, (int)(sizeof(focusables) / sizeof(focusables[0])), &count);
    if (count <= 0) {
        return NULL;
    }
    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (focusables[i] == current) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return focusables[0];
    }
    if (reverse) {
        return focusables[(idx + count - 1) % count];
    }
    return focusables[(idx + 1) % count];
}

void
e9ui_focusAdvance(e9ui_context_t *ctx, e9ui_component_t *current, int reverse)
{
    if (!ctx || !current) {
        return;
    }
    e9ui_component_t *root = e9ui_focusTraversalRoot(ctx, current);
    e9ui_component_t *next = e9ui_focusFindNext(root, current, reverse ? 1 : 0);
    if (next) {
        e9ui_setFocus(ctx, next);
    }
}

void
e9ui_setTooltip(e9ui_component_t *comp, const char *tooltip)
{
    if (!comp) {
        return;
    }
    comp->tooltip = tooltip;
    comp->tooltipSerial += 1u;
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
e9ui_saveLayout(const char* configPath)
{
    if (!configPath) {
        return;
    }
    FILE *f = fopen(configPath, "w");
    if (!f) {
        return;
    }
    // Save component state by traversing the tree (even if root itself has no persistSave)
    if (e9ui->root) {
        e9ui_saveLayoutRecursive(e9ui->root, &e9ui->ctx, f);
    }
    // Save window geometry (last known)
    int wx = e9ui->layout.winX, wy = e9ui->layout.winY, ww = e9ui->layout.winW, wh = e9ui->layout.winH;
    if (e9ui->ctx.window) {
        SDL_GetWindowPosition(e9ui->ctx.window, &wx, &wy);
        SDL_GetWindowSize(e9ui->ctx.window, &ww, &wh);
    }
    fprintf(f, "win_x=%d\nwin_y=%d\nwin_w=%d\nwin_h=%d\n", wx, wy, ww, wh);
    if (e9ui_windowHasSavedPosition(e9ui->layout.memTrackWinX, e9ui->layout.memTrackWinY) &&
        e9ui_windowHasSavedSize(e9ui->layout.memTrackWinW, e9ui->layout.memTrackWinH)) {
        fprintf(f, "memtrack_win_x=%d\nmemtrack_win_y=%d\nmemtrack_win_w=%d\nmemtrack_win_h=%d\n",
                e9ui->layout.memTrackWinX, e9ui->layout.memTrackWinY,
                e9ui->layout.memTrackWinW, e9ui->layout.memTrackWinH);
    }
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
e9ui_loadLayoutComponents(const char* configPath)
{
    if (!configPath) {
        return;
    }
    FILE *f = fopen(configPath, "r");
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
        e9ui_component_t *c = e9ui_findById(e9ui->root, id);
        if (c && c->persistLoad) {
            c->persistLoad(c, &e9ui->ctx, prop, val);
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
    if (!e9ui->ctx.window || !e9ui->ctx.renderer) {
        return 1.0f;
    }
    int winW = 0, winH = 0;
    int renW = 0, renH = 0;
    SDL_GetWindowSize(e9ui->ctx.window, &winW, &winH);
    SDL_GetRendererOutputSize(e9ui->ctx.renderer, &renW, &renH);
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
    float scale = e9ui->ctx.dpiScale;
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
    float prevScale = e9ui->ctx.dpiScale;
    if (fabsf(newScale - prevScale) < 0.01f) {
        e9ui->ctx.dpiScale = newScale;
        return;
    }
    e9ui->ctx.dpiScale = newScale;
    if (e9ui->ctx.font) {
        TTF_CloseFont(e9ui->ctx.font);
        e9ui->ctx.font = NULL;
    }
    e9ui->ctx.font = e9ui_loadFont();
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
    (void)ctx;
    e9ui_tooltip_result_t best = { NULL, -1, NULL };
    if (!comp || !e9ui_pointInBounds(comp, x, y)) {
        return best;
    }
    e9ui_child_reverse_iterator iter;
    if (e9ui_child_iterateChildrenReverse(comp, &iter)) {
        for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
             it;
             it = e9ui_child_iteratePrev(&iter)) {
            if (!it->child || e9ui_getHidden(it->child) || !e9ui_pointInBounds(it->child, x, y)) {
                continue;
            }
            e9ui_tooltip_result_t candidate =
                e9ui_findTooltipRecursive(it->child, ctx, x, y, depth + 1);
            if (!e9ui_getHidden(candidate.comp) && candidate.depth > best.depth) {
                best = candidate;
            }
            if (it->child->name &&
                (strcmp(it->child->name, "e9ui_modal") == 0 ||
                 strcmp(it->child->name, "e9ui_window_overlay") == 0)) {
                return best;
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

static int
e9ui_pointerBlockedByOverlayRecursive(e9ui_component_t *comp, int x, int y)
{
    if (!comp || e9ui_getHidden(comp) || !e9ui_pointInBounds(comp, x, y)) {
        return 0;
    }
    if (comp->name &&
        (strcmp(comp->name, "e9ui_modal") == 0 ||
         strcmp(comp->name, "e9ui_window_overlay") == 0)) {
        return 1;
    }
    e9ui_child_reverse_iterator iter;
    if (!e9ui_child_iterateChildrenReverse(comp, &iter)) {
        return 0;
    }
    for (e9ui_child_reverse_iterator *it = e9ui_child_iteratePrev(&iter);
         it;
         it = e9ui_child_iteratePrev(&iter)) {
        if (!it->child) {
            continue;
        }
        if (e9ui_pointerBlockedByOverlayRecursive(it->child, x, y)) {
            return 1;
        }
    }
    return 0;
}

void
e9ui_drawTooltip(const e9ui_context_t *ctx, const char *text, int baseX, int baseY)
{
    if (!ctx || !ctx->renderer || !ctx->font || !text || !*text) {
        return;
    }

    const int maxLines = 32;
    const char *lineStarts[maxLines];
    int lineLens[maxLines];
    int lineCount = 0;
    const char *lineStart = text;
    for (const char *p = text;; ++p) {
        if (*p == '\n' || *p == '\0') {
            if (lineCount < maxLines) {
                lineStarts[lineCount] = lineStart;
                lineLens[lineCount] = (int)(p - lineStart);
                lineCount++;
            }
            if (*p == '\0') {
                break;
            }
            lineStart = p + 1;
        }
    }
    if (lineCount <= 0) {
        return;
    }

    int lineHeight = TTF_FontLineSkip(ctx->font);
    if (lineHeight <= 0) {
        lineHeight = TTF_FontHeight(ctx->font);
    }
    if (lineHeight <= 0) {
        return;
    }

    int textW = 0;
    char lineBuf[1024];
    for (int i = 0; i < lineCount; ++i) {
        int len = lineLens[i];
        while (len > 0 && lineStarts[i][len - 1] == '\r') {
            len--;
        }
        if (len <= 0) {
            continue;
        }
        if (len >= (int)sizeof(lineBuf)) {
            len = (int)sizeof(lineBuf) - 1;
        }
        memcpy(lineBuf, lineStarts[i], (size_t)len);
        lineBuf[len] = '\0';
        int lineW = 0;
        int lineH = 0;
        if (TTF_SizeText(ctx->font, lineBuf, &lineW, &lineH) == 0 && lineW > textW) {
            textW = lineW;
        }
    }
    if (textW <= 0) {
        textW = e9ui_scale_px(ctx, 8);
    }
    int textH = lineHeight * lineCount;

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
    for (int i = 0; i < lineCount; ++i) {
        int len = lineLens[i];
        while (len > 0 && lineStarts[i][len - 1] == '\r') {
            len--;
        }
        if (len < 0) {
            len = 0;
        }
        if (len >= (int)sizeof(lineBuf)) {
            len = (int)sizeof(lineBuf) - 1;
        }
        memcpy(lineBuf, lineStarts[i], (size_t)len);
        lineBuf[len] = '\0';
        if (lineBuf[0] == '\0') {
            continue;
        }
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, ctx->font, lineBuf, textColor, &tw, &th);
        if (tex) {
            int lineY = y + pad + i * lineHeight + ((lineHeight - th) / 2);
            SDL_Rect textRect = { x + pad, lineY, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &textRect);
        }
    }
}

static void
e9ui_renderTooltipOverlay(void)
{
    e9ui_component_t *contentRoot = e9ui_activeContentRoot();
    e9ui_component_t *overlayRoot = e9ui_getOverlayHost();
    if (!contentRoot && !overlayRoot) {
        return;
    }
    static struct {
        const char *text;
        const e9ui_component_t *comp;
        unsigned serial;
        int x;
        int y;
        int active;
    } tooltip_state = {0};

    e9ui_tooltip_result_t tooltip = { NULL, -1, NULL };
    if (overlayRoot) {
        tooltip = e9ui_findTooltipRecursive(overlayRoot, &e9ui->ctx,
                                            e9ui->ctx.mouseX, e9ui->ctx.mouseY, 0);
    }
    if (!tooltip.text && overlayRoot &&
        e9ui_pointerBlockedByOverlayRecursive(overlayRoot, e9ui->ctx.mouseX, e9ui->ctx.mouseY)) {
        tooltip_state.active = 0;
        tooltip_state.text = NULL;
        tooltip_state.comp = NULL;
        tooltip_state.serial = 0u;
        return;
    }
    if (!tooltip.text && contentRoot) {
        tooltip = e9ui_findTooltipRecursive(contentRoot, &e9ui->ctx,
                                            e9ui->ctx.mouseX, e9ui->ctx.mouseY, 0);
    }
    if (!tooltip.text) {
        tooltip_state.active = 0;
        tooltip_state.text = NULL;
        tooltip_state.comp = NULL;
        tooltip_state.serial = 0u;
        return;
    }

    if (!tooltip_state.active ||
        tooltip_state.comp != tooltip.comp ||
        tooltip_state.text != tooltip.text ||
        tooltip_state.serial != tooltip.comp->tooltipSerial) {
        tooltip_state.active = 1;
        tooltip_state.comp = tooltip.comp;
        tooltip_state.text = tooltip.text;
        tooltip_state.serial = tooltip.comp->tooltipSerial;
        tooltip_state.x = e9ui->ctx.mouseX;
        tooltip_state.y = e9ui->ctx.mouseY;
    }

    e9ui_drawTooltip(&e9ui->ctx, tooltip_state.text, tooltip_state.x, tooltip_state.y);
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
    if (comp != e9ui->fullscreen) {
        e9ui_closeOverlaysBeforeFullscreenTransition();
    }
    e9ui_component_t *prev = e9ui->fullscreen ? e9ui->fullscreen : e9ui->root;
    if (comp) {
    } else {
        if (e9ui_transientMessage == e9ui_fullscreenMessage) {
            e9ui_fullscreenHintStart = 0;
            e9ui_transientMessage = NULL;
        }
    }
    if (comp) {
        e9ui_component_t *focus = e9ui_findFocusable(comp, &e9ui->ctx);
        if (focus) {
            e9ui_setFocus(&e9ui->ctx, focus);
        }
    }
    if (comp && prev) {
        int w = 0;
        int h = 0;
        SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);
        if (!e9ui_loadingLayout) {
            (void)e9ui_runFullscreenTransition(&e9ui->ctx, 1, prev, comp, w, h);
        }
    }
    e9ui->fullscreen = comp;
    if (comp) {
        e9ui_updateFullscreenHintMessage();
        e9ui_fullscreenHintStart = e9ui_getTicks(&e9ui->ctx);
        e9ui_transientMessage = e9ui_fullscreenMessage;
    }
}

void
e9ui_clearFullscreenComponent(void)
{
    e9ui_closeOverlaysBeforeFullscreenTransition();
    e9ui_component_t *prev = e9ui->fullscreen;
    if (e9ui_transientMessage == e9ui_fullscreenMessage) {
        e9ui_fullscreenHintStart = 0;
        e9ui_transientMessage = NULL;
    }
    if (prev) {
        int w = 0;
        int h = 0;
        SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);
        (void)e9ui_runFullscreenTransition(&e9ui->ctx, 0, prev, e9ui->root, w, h);
    }
    e9ui->fullscreen = NULL;
}

void
e9ui_showTransientMessage(const char *message)
{
    if (!message || !*message) {
        return;
    }
    e9ui_transientMessage = message;
    e9ui_fullscreenHintStart = e9ui_getTicks(&e9ui->ctx);
}

void
e9ui_drawSelectableText(e9ui_context_t *ctx,
                        e9ui_component_t *owner,
                        TTF_Font *font,
                        const char *text,
                        SDL_Color color,
                        int x,
                        int y,
                        int lineHeight,
                        int hitW,
                        void *bucket,
                        int dragOnly,
                        int selectable)
{
    e9ui_text_select_drawText(ctx, owner, font, text, color, x, y, lineHeight,
                              hitW, bucket, dragOnly, selectable);
}

e9ui_component_t *
e9ui_getFullscreenComponent(void)
{
    return e9ui->fullscreen;
}

int
e9ui_isFullscreenComponent(const e9ui_component_t *comp)
{
    return comp && e9ui->fullscreen == comp;
}

void
e9ui_renderFrame(void)
{
  if (e9ui->transition.inTransition > 0) {
    return;
  }
  e9ui_sceneUpdateState();

  SDL_SetRenderDrawColor(e9ui->ctx.renderer, 16, 16, 16, 255);
  SDL_RenderClear(e9ui->ctx.renderer);
  
  int w,h; SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);
  
  e9ui->ctx.winW = w;
  e9ui->ctx.winH = h;
  e9ui->ctx.mouseX = e9ui->mouseX;
  e9ui->ctx.mouseY = e9ui->mouseY;

  e9ui_sceneLayout(w, h);
  e9ui_sceneUpdateAutoHide();
  e9ui_text_select_beginFrame(&e9ui->ctx);
  e9ui_sceneRender();
  e9ui_text_select_endFrame(&e9ui->ctx);

  e9ui_textbox_selectOverlayRender(&e9ui->ctx);
  e9ui_resetRendererOverlayState(&e9ui->ctx);

  e9ui_renderTransientMessage(&e9ui->ctx, w, h);
  e9ui_renderFpsOverlay(&e9ui->ctx, w, h);

  if (e9ui->ctx.font == NULL) {
    e9ui_renderMissingFontNotice();
  }

  e9ui_renderTooltipOverlay();

  uint64_t uiFrameId = e9ui_getNextUiFrameId(&e9ui->ctx);
  (void)e9ui_captureUiFrame(&e9ui->ctx, uiFrameId, e9ui->ctx.renderer);
  if (e9ui_shouldPresentFrame(&e9ui->ctx)) {
    SDL_RenderPresent(e9ui->ctx.renderer);
  }
  e9ui_commitUiFrameId(&e9ui->ctx, uiFrameId);
}

void
e9ui_renderFrameNoLayout(void)
{
  e9ui_sceneUpdateState();

  SDL_SetRenderDrawColor(e9ui->ctx.renderer, 16, 16, 16, 255);
  SDL_RenderClear(e9ui->ctx.renderer);

  int w,h; SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);

  e9ui->ctx.winW = w;
  e9ui->ctx.winH = h;
  e9ui->ctx.mouseX = e9ui->mouseX;
  e9ui->ctx.mouseY = e9ui->mouseY;

  e9ui_sceneUpdateAutoHide();
  e9ui_text_select_beginFrame(&e9ui->ctx);
  e9ui_sceneRender();
  e9ui_text_select_endFrame(&e9ui->ctx);

  e9ui_textbox_selectOverlayRender(&e9ui->ctx);
  e9ui_resetRendererOverlayState(&e9ui->ctx);

  e9ui_renderTransientMessage(&e9ui->ctx, w, h);
  e9ui_renderFpsOverlay(&e9ui->ctx, w, h);

  if (e9ui->ctx.font == NULL) {
    e9ui_renderMissingFontNotice();
  }

  e9ui_renderTooltipOverlay();

  SDL_RenderPresent(e9ui->ctx.renderer);

}

void
e9ui_renderFrameNoLayoutNoPresent(void)
{
  e9ui->glCompositeCapture = 1;

  e9ui_sceneUpdateState();

  SDL_SetRenderDrawColor(e9ui->ctx.renderer, 16, 16, 16, 255);
  SDL_RenderClear(e9ui->ctx.renderer);

  int w,h; SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);

  e9ui->ctx.winW = w;
  e9ui->ctx.winH = h;
  e9ui->ctx.mouseX = e9ui->mouseX;
  e9ui->ctx.mouseY = e9ui->mouseY;

  e9ui_sceneUpdateAutoHide();
  e9ui_text_select_beginFrame(&e9ui->ctx);
  e9ui_sceneRender();
  e9ui_text_select_endFrame(&e9ui->ctx);

  e9ui_textbox_selectOverlayRender(&e9ui->ctx);
  e9ui_resetRendererOverlayState(&e9ui->ctx);

  e9ui_renderTransientMessage(&e9ui->ctx, w, h);
  e9ui_renderFpsOverlay(&e9ui->ctx, w, h);

  if (e9ui->ctx.font == NULL) {
    e9ui_renderMissingFontNotice();
  }

  e9ui_renderTooltipOverlay();

  e9ui->glCompositeCapture = 0;
  
}

void
e9ui_renderFrameNoLayoutNoPresentNoClear(void)
{
  e9ui_sceneUpdateState();

  int w,h; SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);

  e9ui->ctx.winW = w;
  e9ui->ctx.winH = h;
  e9ui->ctx.mouseX = e9ui->mouseX;
  e9ui->ctx.mouseY = e9ui->mouseY;

  e9ui_sceneUpdateAutoHide();
  e9ui_text_select_beginFrame(&e9ui->ctx);
  e9ui_sceneRender();
  e9ui_text_select_endFrame(&e9ui->ctx);

  e9ui_textbox_selectOverlayRender(&e9ui->ctx);
  e9ui_resetRendererOverlayState(&e9ui->ctx);

  e9ui_renderTransientMessage(&e9ui->ctx, w, h);
  e9ui_renderFpsOverlay(&e9ui->ctx, w, h);

  if (e9ui->ctx.font == NULL) {
    e9ui_renderMissingFontNotice();
  }

  e9ui_renderTooltipOverlay();
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
    SDL_GetRenderDrawBlendMode(e9ui->ctx.renderer, &prevBlend);
    SDL_SetRenderDrawBlendMode(e9ui->ctx.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, (Uint8)(255 - fadeAlpha));
    int w,h; SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);
    SDL_Rect r = { 0, 0, w, h };
    SDL_RenderFillRect(e9ui->ctx.renderer, &r);
    SDL_SetRenderDrawBlendMode(e9ui->ctx.renderer, prevBlend);
  }
}

static void
e9ui_loadWindowConfig(const char* configPath)
{
  if (!configPath) {
    return;
  }
  FILE *f = fopen(configPath, "r");
  if (!f) {
    return;
  }
  char key[128]; char val[128];
  while (fscanf(f, "%127[^=]=%127s\n", key, val) == 2) {
    if (strcmp(key, "win_x") == 0 || strcmp(key, "winX") == 0) {
      e9ui->layout.winX = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "win_y") == 0 || strcmp(key, "winY") == 0) {
      e9ui->layout.winY = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "win_w") == 0 || strcmp(key, "winW") == 0) {
      e9ui->layout.winW = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "win_h") == 0 || strcmp(key, "winH") == 0) {
      e9ui->layout.winH = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "memtrack_win_x") == 0) {
      e9ui->layout.memTrackWinX = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "memtrack_win_y") == 0) {
      e9ui->layout.memTrackWinY = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "memtrack_win_w") == 0) {
      e9ui->layout.memTrackWinW = (int)strtol(val, NULL, 10);
    } else if (strcmp(key, "memtrack_win_h") == 0) {
      e9ui->layout.memTrackWinH = (int)strtol(val, NULL, 10);
    }
  }
  fclose(f);
}

int
e9ui_ctor(const char* configPath, int cliOverrideWindowSize, int cliWinW, int cliWinH, int startHidden)
{
  e9ui_theme_ctor();
  e9ui_loadWindowConfig(configPath);

  if (cliOverrideWindowSize) {
    e9ui->layout.winW = cliWinW;
    e9ui->layout.winH = cliWinH;
  }    
  
    // Load persisted layout before creating window (for geometry)
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER|SDL_INIT_JOYSTICK) != 0) {
        debug_error("SDL_Init with audio failed: %s", SDL_GetError());
        if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER|SDL_INIT_JOYSTICK) != 0) {
            debug_error("SDL_Init failed: %s", SDL_GetError());
            return 0;
        }
        debug_error("SDL audio disabled (continuing without SDL_INIT_AUDIO)");
    }
    {
        int refresh = e9ui_getDisplayRefreshRate(0);
        e9ui_setRefreshHz(&e9ui->ctx, refresh);
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
    int wantW = (e9ui->layout.winW > 0 ? e9ui->layout.winW : 1000);
    int wantH = (e9ui->layout.winH > 0 ? e9ui->layout.winH : 700);
    int wantX = SDL_WINDOWPOS_CENTERED;
    int wantY = SDL_WINDOWPOS_CENTERED;
    Uint32 winFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (startHidden) {
      winFlags |= SDL_WINDOW_HIDDEN;
    }
    e9ui_prepareMainWindow(&e9ui->ctx, cliOverrideWindowSize, startHidden, &wantX, &wantY, &wantW, &wantH, &winFlags);
    SDL_Window *win = SDL_CreateWindow("ENGINE9000 DEBUGGER/PROFILER 68K", wantX, wantY, wantW, wantH,
                                       winFlags);
    if (!win) {
        debug_error("SDL_CreateWindow failed: %s", SDL_GetError());
        return 0;
    }
    e9ui_applyWindowIcon(win);
    e9ui_updateRefreshRate(win);
    uint32_t rendererFlags = SDL_RENDERER_ACCELERATED;
    if (e9ui_shouldUseVsync(&e9ui->ctx)) {
        rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, rendererFlags);
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, 0);
    }
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        debug_error("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        return 0;
    }
    e9ui->ctx.window = win;
    e9ui_context_setRenderer(&e9ui->ctx, ren);
    e9ui->ctx.dpiScale = e9ui_computeDpiScale();
    e9ui->currentDisplayIndex = SDL_GetWindowDisplayIndex(win);
    // Enable alpha blending for proper fade animations
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (e9ui->layout.winX >= 0 && e9ui->layout.winY >= 0) {
        SDL_SetWindowPosition(win, e9ui->layout.winX, e9ui->layout.winY);
    }
    e9ui_finalizeMainWindow(&e9ui->ctx, win, ren, wantW, wantH);

  // Load default monospace font
  TTF_Font *font = e9ui_loadFont();
  if (!font) {
  }
  e9ui->ctx.font = font;
  e9ui->ctx.onSplitChanged = e9ui_onSplitChanged;
  // Load themed fonts (button + text fonts)
  e9ui_theme_loadFonts();

  if (!e9ui->overlayRoot) {
    e9ui->overlayRoot = e9ui_makeOverlayHost("e9ui_overlay_host");
  }

#ifdef E9UI_ENABLE_GAMEPAD
  e9ui_controllerInit();
#endif
    
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

static void
e9ui_forceDefaultCursorOnNonMainHover(const SDL_Event *ev)
{
    if (!ev) {
        return;
    }
    if (ev->type != SDL_MOUSEMOTION) {
        return;
    }
    SDL_Cursor *defaultCursor = SDL_GetDefaultCursor();
    if (!defaultCursor) {
        return;
    }
    SDL_SetCursor(defaultCursor);
}

int
e9ui_processEvents(void)
{
    SDL_Event ev;
    while (1) {
        e9ui_runDeferred(&e9ui->ctx);
        int hasEvent = 0;
        uint64_t uiFrameId = e9ui_getNextUiFrameId(&e9ui->ctx);
        int pollResult = e9ui_pollInjectedUiEvent(&e9ui->ctx, uiFrameId, &ev);
        if (pollResult > 0) {
            hasEvent = 1;
        } else if (pollResult == 0) {
	    hasEvent = SDL_PollEvent(&ev);
        }
        if (!hasEvent) {
            break;
        }
        e9ui_recordUiEvent(&e9ui->ctx, uiFrameId, &ev);

        uint32_t evWindowId = e9ui_eventWindowId(&ev);
        uint32_t mainWindowId = 0;
        if (e9ui->ctx.window) {
            mainWindowId = SDL_GetWindowID(e9ui->ctx.window);
        }
        if (evWindowId == mainWindowId && e9ui->ctx.handleHostEvent &&
            e9ui->ctx.handleHostEvent(&e9ui->ctx, &ev)) {
            continue;
        }
        if (ev.type == SDL_MOUSEMOTION &&
            evWindowId &&
            mainWindowId &&
            evWindowId != mainWindowId) {
            e9ui_forceDefaultCursorOnNonMainHover(&ev);
        }
        e9ui->ctx.focusClickHandled = 0;
        e9ui->ctx.cursorOverride = 0;
        e9ui->ctx.focusRoot = e9ui->root;
        e9ui->ctx.focusFullscreen = e9ui->fullscreen;
        e9ui_focusClearIfStale(&e9ui->ctx);
        if (ev.type == SDL_QUIT) {
            e9ui_runDeferred(&e9ui->ctx);
            return 1;
        }
        else if (ev.type == SDL_MOUSEMOTION) {
            if (!mainWindowId || ev.motion.windowID != mainWindowId) {
                continue;
            }
            int rawXrel = ev.motion.xrel;
            int rawYrel = ev.motion.yrel;
            int prevX = e9ui->ctx.mouseX;
            int prevY = e9ui->ctx.mouseY;
            e9ui->ctx.mousePrevX = prevX;
            e9ui->ctx.mousePrevY = prevY;
            int scaledX = e9ui_scale_coord(&e9ui->ctx, ev.motion.x);
            int scaledY = e9ui_scale_coord(&e9ui->ctx, ev.motion.y);
            ev.motion.x = scaledX;
            ev.motion.y = scaledY;
            if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
                ev.motion.xrel = e9ui_scale_coord(&e9ui->ctx, rawXrel);
                ev.motion.yrel = e9ui_scale_coord(&e9ui->ctx, rawYrel);
            } else {
                ev.motion.xrel = scaledX - prevX;
                ev.motion.yrel = scaledY - prevY;
            }
            e9ui->ctx.mouseX = scaledX;
            e9ui->ctx.mouseY = scaledY;
            e9ui->mouseX = scaledX;
            e9ui->mouseY = scaledY;
            if (e9ui_dispatchCapturedPointerEvent(&e9ui->ctx, &ev)) {
                continue;
            }
            if (e9ui_textbox_selectOverlayHandleEvent(&e9ui->ctx, &ev)) {
                continue;
            }
        }
        else if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
            if (!mainWindowId || ev.button.windowID != mainWindowId) {
                continue;
            }
            int scaledX = e9ui_scale_coord(&e9ui->ctx, ev.button.x);
            int scaledY = e9ui_scale_coord(&e9ui->ctx, ev.button.y);
            ev.button.x = scaledX;
            ev.button.y = scaledY;
            e9ui->ctx.mouseX = scaledX;
            e9ui->ctx.mouseY = scaledY;
            e9ui->mouseX = scaledX;
            e9ui->mouseY = scaledY;
            if (e9ui_dispatchCapturedPointerEvent(&e9ui->ctx, &ev)) {
                continue;
            }
            if (e9ui_textbox_selectOverlayHandleEvent(&e9ui->ctx, &ev)) {
                continue;
            }
        }
        else if (ev.type == SDL_MOUSEWHEEL) {
            if (!mainWindowId || ev.wheel.windowID != mainWindowId) {
                continue;
            }
            ev.wheel.x = -ev.wheel.x;
            ev.wheel.y = -ev.wheel.y;
            int scaledX = e9ui->ctx.mouseX;
            int scaledY = e9ui->ctx.mouseY;
#if SDL_VERSION_ATLEAST(2, 0, 18)
            if (ev.wheel.mouseX != 0 || ev.wheel.mouseY != 0) {
                scaledX = e9ui_scale_coord(&e9ui->ctx, ev.wheel.mouseX);
                scaledY = e9ui_scale_coord(&e9ui->ctx, ev.wheel.mouseY);
            }
#endif
            e9ui->ctx.mouseX = scaledX;
            e9ui->ctx.mouseY = scaledY;
            e9ui->mouseX = scaledX;
            e9ui->mouseY = scaledY;
            if (e9ui_dispatchCapturedPointerEvent(&e9ui->ctx, &ev)) {
                continue;
            }
            if (e9ui_textbox_selectOverlayHandleEvent(&e9ui->ctx, &ev)) {
                continue;
            }
        }
        else if (ev.type == SDL_WINDOWEVENT) {
            if (!mainWindowId || ev.window.windowID != mainWindowId) {
                continue;
            }
            if (ev.window.event == SDL_WINDOWEVENT_MOVED) {
                e9ui->layout.winX = ev.window.data1;
                e9ui->layout.winY = ev.window.data2;
                config_saveConfig();
                e9ui_syncWindowDisplayState(e9ui->ctx.window, 0);
            } else if (ev.window.event == SDL_WINDOWEVENT_RESIZED || ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                e9ui->layout.winW = ev.window.data1;
                e9ui->layout.winH = ev.window.data2;
                config_saveConfig();
                e9ui_syncWindowDisplayState(e9ui->ctx.window, 1);
            } else if (ev.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                e9ui_syncWindowDisplayState(e9ui->ctx.window, 1);
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                e9ui_setMainWindowFocused(&e9ui->ctx, 1);
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                e9ui_setMainWindowFocused(&e9ui->ctx, 0);
            }
        }
        
#ifdef E9UI_ENABLE_GAMEPAD
        else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
            if (!e9ui_controller && !e9ui_joystick) {
                e9ui_controllerOpenPreferredOrAuto();
            }
            continue;
        }
        else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (ev.cdevice.which == e9ui_controllerId) {
                e9ui_controllerClose();
            }
            continue;
        }
        else if (ev.type == SDL_CONTROLLERAXISMOTION) {
            if (e9ui_controllerDeviceType == e9ui_controllerDeviceGameController &&
                e9ui_controller &&
                ev.caxis.which == e9ui_controllerId) {
                e9ui_controllerHandleAxis((SDL_GameControllerAxis)ev.caxis.axis, ev.caxis.value);
            }
            continue;
        }
        else if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
            if (e9ui_controllerDeviceType == e9ui_controllerDeviceGameController &&
                e9ui_controller &&
                ev.cbutton.which == e9ui_controllerId) {
                unsigned id = 0;
                if (e9ui_controllerMapButton((SDL_GameControllerButton)ev.cbutton.button, &id)) {
                    int pressed = (ev.type == SDL_CONTROLLERBUTTONDOWN) ? 1 : 0;
                    unsigned port = 0u;
                    libretro_host_setJoypadState(port, id, pressed);
                }
            }
            continue;
        }
        else if (ev.type == SDL_JOYDEVICEADDED) {
            if (!e9ui_controller && !e9ui_joystick) {
                e9ui_controllerOpenPreferredOrAuto();
            }
            continue;
        }
        else if (ev.type == SDL_JOYDEVICEREMOVED) {
            if (ev.jdevice.which == e9ui_controllerId) {
                e9ui_controllerClose();
            }
            continue;
        }
        else if (ev.type == SDL_JOYAXISMOTION) {
            if (e9ui_controllerDeviceType == e9ui_controllerDeviceJoystick &&
                e9ui_joystick &&
                ev.jaxis.which == e9ui_controllerId) {
                if (ev.jaxis.axis == 0) {
                    e9ui_controllerHandleAxis(SDL_CONTROLLER_AXIS_LEFTX, ev.jaxis.value);
                } else if (ev.jaxis.axis == 1) {
                    e9ui_controllerHandleAxis(SDL_CONTROLLER_AXIS_LEFTY, ev.jaxis.value);
                }
            }
            continue;
        }
        else if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
            if (e9ui_controllerDeviceType == e9ui_controllerDeviceJoystick &&
                e9ui_joystick &&
                ev.jbutton.which == e9ui_controllerId) {
                int pressed = (ev.type == SDL_JOYBUTTONDOWN) ? 1 : 0;
                e9ui_controllerHandleJoystickButton(ev.jbutton.button, pressed);
            }
            continue;
        }
        else if (ev.type == SDL_JOYHATMOTION) {
            if (e9ui_controllerDeviceType == e9ui_controllerDeviceJoystick &&
                e9ui_joystick &&
                ev.jhat.which == e9ui_controllerId &&
                ev.jhat.hat == 0) {
                e9ui_controllerHandleHat(ev.jhat.value);
            }
            continue;
        }
#endif
        else if (ev.type == SDL_KEYDOWN) {
            e9ui->ctx.keyMods = ev.key.keysym.mod;
            if (e9ui_textbox_selectOverlayHandleEvent(&e9ui->ctx, &ev)) {
                continue;
            }
            e9ui_component_t *overlayRoot = e9ui_getOverlayHost();
            e9ui_component_t *contentRoot = e9ui_activeContentRoot();
            if (overlayRoot && e9ui_windowDispatchKeydown(overlayRoot, &e9ui->ctx, &ev)) {
                continue;
            }
            if (contentRoot && e9ui_windowDispatchKeydown(contentRoot, &e9ui->ctx, &ev)) {
                continue;
            }
            SDL_Keycode key = ev.key.keysym.sym;
            SDL_Keymod mods = ev.key.keysym.mod;
            int accel = (mods & KMOD_GUI) || (mods & KMOD_CTRL);
            int reverseTab = (mods & KMOD_SHIFT) ? 1 : 0;
            if (e9ui_handleGlobalKeydown(&e9ui->ctx, &ev.key)) {
                continue;
            }
            if (!accel && key == SDLK_TAB && !e9ui_getFocus(&e9ui->ctx)) {
                e9ui_component_t *focusRoot = e9ui_focusTraversalRoot(&e9ui->ctx, NULL);
                e9ui_component_t *next = e9ui_focusFindNext(focusRoot, NULL, reverseTab);
                if (next) {
                    e9ui_setFocus(&e9ui->ctx, next);
                    continue;
                }
            }
            // Focused component gets first crack at keydown
            int consumed = 0;
            e9ui_component_t *focused = e9ui_getFocus(&e9ui->ctx);
            if (!consumed && focused && focused->handleEvent) {
                consumed = focused->handleEvent(focused, &e9ui->ctx, &ev);
            }
            if (!consumed && contentRoot && contentRoot->handleEvent) {
                (void)contentRoot->handleEvent(contentRoot, &e9ui->ctx, &ev);
            }
            continue;
        } else if (ev.type == SDL_KEYUP) {
            e9ui->ctx.keyMods = ev.key.keysym.mod;
        } else if (ev.type == SDL_TEXTINPUT) {
            if (e9ui_textbox_selectOverlayHandleEvent(&e9ui->ctx, &ev)) {
                continue;
            }
            // Text input goes only to focused component
            if (e9ui_getFocus(&e9ui->ctx) && e9ui_getFocus(&e9ui->ctx)->handleEvent) {
                int consumed = e9ui_getFocus(&e9ui->ctx)->handleEvent(e9ui_getFocus(&e9ui->ctx), &e9ui->ctx, &ev);
                (void)consumed;
            }
            continue;
        }
        // For mouse and other events, bubble through tree for hit-testing and focus updates
        (void)e9ui_sceneProcessEvent(&ev);
        if (ev.type == SDL_MOUSEMOTION ||
            ev.type == SDL_MOUSEBUTTONDOWN ||
            ev.type == SDL_MOUSEBUTTONUP) {
            (void)e9ui_text_select_handleEvent(&e9ui->ctx, &ev);
        }
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && !e9ui->ctx.focusClickHandled) {
            e9ui_setFocus(&e9ui->ctx, NULL);
        }
    }
    e9ui_runDeferred(&e9ui->ctx);
    return 0;
}


void
e9ui_shutdown(void)
{  
#ifdef E9UI_ENABLE_GAMEPAD
  e9ui_controllerClose();
#endif
  if (e9ui_fullscreenHintFont) {
    TTF_CloseFont(e9ui_fullscreenHintFont);
    e9ui_fullscreenHintFont = NULL;
  }
  e9ui_fullscreenHintSize = 0;
  e9ui_fullscreenHintStart = 0;
  e9ui_transientMessage = NULL;
  if (e9ui_fpsFont) {
    TTF_CloseFont(e9ui_fpsFont);
    e9ui_fpsFont = NULL;
  }
  e9ui_fpsFontSize = 0;
  e9ui_fpsLastTick = 0;
  e9ui_fpsFrames = 0;
  e9ui_fpsValue = 0.0f;
  e9ui_split_resetCursors();
  e9ui_split_stack_resetCursors();
  e9ui_box_resetCursors();
  e9ui_shutdownHostUi(&e9ui->ctx);
  if (e9ui->deferred) {
    alloc_free(e9ui->deferred);
    e9ui->deferred = NULL;
  }
  e9ui->deferredCount = 0;
  e9ui->deferredCap = 0;
  
  if (e9ui->ctx.font) {
    TTF_CloseFont(e9ui->ctx.font);
    e9ui->ctx.font = NULL;
  }
  
  e9ui_theme_unloadFonts();
  e9ui_text_cache_clear();
  e9ui_text_select_shutdown();
  e9ui_window_resetOverlayResources();
  e9ui_modal_resetResources();

  e9ui_childDestroy(e9ui->overlayRoot, &e9ui->ctx);
  e9ui->overlayRoot = NULL;
  e9ui_childDestroy(e9ui->root, &e9ui->ctx);
  e9ui->root = NULL;
  e9ui->toolbar = NULL;
  e9ui->profileButton = NULL;
  e9ui->analyseButton = NULL;
  e9ui->speedButton = NULL;
  e9ui->restartButton = NULL;
  e9ui->resetButton = NULL;
  e9ui->audioButton = NULL;
  e9ui->settingsButton = NULL;
  e9ui->settingsModal = NULL;
  e9ui->settingsSaveButton = NULL;
  e9ui->coreOptionsModal = NULL;
  e9ui->helpModal = NULL;
  e9ui->prompt = NULL;
  e9ui->pendingRemove = NULL;
  e9ui->sourceBox = NULL;
  e9ui->fullscreen = NULL;
  e9ui->ctx._focus = NULL;

  if (e9ui->ctx.renderer)
    SDL_DestroyRenderer(e9ui->ctx.renderer);
  e9ui_context_setRenderer(&e9ui->ctx, NULL);
  if (e9ui->ctx.window)
    SDL_DestroyWindow(e9ui->ctx.window);
  e9ui->ctx.window = NULL;
  
  IMG_Quit();
  TTF_Quit();
  SDL_Quit();
}
