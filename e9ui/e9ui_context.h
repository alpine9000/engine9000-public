/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

struct e9ui_component;

typedef struct e9ui_context {
    SDL_Window  *window;
    SDL_Renderer *renderer;
    TTF_Font     *font;
    int           rendererSupportsTargetTexture;
    int           winW;
    int           winH;
    int           mouseX;
    int           mouseY;
    int           mousePrevX;
    int           mousePrevY;
    SDL_Keymod    keyMods;
    int           cursorOverride;
    void         *cursorCaptureOwner;
    SDL_Cursor   *cursorCapture;
    struct e9ui_component *pointerOwner;
    Uint8         pointerOwnerButton;
    float         dpiScale; 
    struct e9ui_component *_focus;
    int                  focusClickHandled;
    struct e9ui_component *focusRoot;
    struct e9ui_component *focusFullscreen;

    void (*sendLine)(const char *s);
    void (*sendInterrupt)(void);
    uint32_t (*getTicks)(struct e9ui_context *ctx);
    void (*setRefreshHz)(struct e9ui_context *ctx, int refreshHz);
    const char *(*getWindowIconAssetPath)(struct e9ui_context *ctx);
    uint64_t (*getNextUiFrameId)(struct e9ui_context *ctx);
    void (*commitUiFrameId)(struct e9ui_context *ctx, uint64_t frameId);
    int (*captureUiFrame)(struct e9ui_context *ctx, uint64_t frameId, SDL_Renderer *renderer);
    int (*transformTextboxSelection)(struct e9ui_context *ctx,
                                     const char *actionId,
                                     const char *input,
                                     char *out,
                                     size_t outCap);
    int (*shouldPresentFrame)(struct e9ui_context *ctx);
    int (*pollInjectedUiEvent)(struct e9ui_context *ctx, uint64_t frameId, SDL_Event *eventValue);
    void (*recordUiEvent)(struct e9ui_context *ctx, uint64_t frameId, const SDL_Event *eventValue);
    int (*runFullscreenTransition)(struct e9ui_context *ctx,
                                   int entering,
                                   struct e9ui_component *from,
                                   struct e9ui_component *to,
                                   int width,
                                   int height);
    void (*setMainWindowFocused)(struct e9ui_context *ctx, int focused);
    void (*blockingWindowOpened)(struct e9ui_context *ctx);
    int (*handleHostEvent)(struct e9ui_context *ctx, const SDL_Event *ev);
    int (*handleGlobalKeydown)(struct e9ui_context *ctx, const SDL_KeyboardEvent *kev);
    void (*shutdownHostUi)(struct e9ui_context *ctx);
    void (*prepareMainWindow)(struct e9ui_context *ctx,
                              int cliOverrideWindowSize,
                              int startHidden,
                              int *wantX,
                              int *wantY,
                              int *wantW,
                              int *wantH,
                              Uint32 *winFlags);
    int (*shouldUseVsync)(struct e9ui_context *ctx);
    void (*finalizeMainWindow)(struct e9ui_context *ctx,
                               SDL_Window *window,
                               SDL_Renderer *renderer,
                               int wantW,
                               int wantH);


    int  (*registerHotkey)(struct e9ui_context *ctx, SDL_Keycode key, SDL_Keymod modMask, SDL_Keymod modValue,
                           void (*cb)(struct e9ui_context *ctx, void *user), void *user);
    void (*unregisterHotkey)(struct e9ui_context *ctx, int id);
    int  (*dispatchHotkey)(struct e9ui_context *ctx, const SDL_KeyboardEvent *kev);
    void (*onSplitChanged)(struct e9ui_context *ctx, struct e9ui_component *split, float ratio);
    void (*applyCompletion)(struct e9ui_context *ctx, int prefixLen, const char *insert);
    void (*showCompletions)(struct e9ui_context *ctx, const char * const *cands, int count);
    void (*hideCompletions)(struct e9ui_context *ctx);
} e9ui_context_t;

static inline void
e9ui_context_setRenderer(e9ui_context_t *ctx, SDL_Renderer *renderer)
{
    if (!ctx) {
        return;
    }
    ctx->renderer = renderer;
    ctx->rendererSupportsTargetTexture = renderer ? SDL_RenderTargetSupported(renderer) : 0;
}

static inline int
e9ui_context_supportsTargetTexture(const e9ui_context_t *ctx)
{
    return ctx && ctx->renderer && ctx->rendererSupportsTargetTexture;
}

#define e9ui_getFocus(ctx) (ctx)->_focus
void
e9ui_setFocus(struct e9ui_context *ctx, struct e9ui_component* comp);
