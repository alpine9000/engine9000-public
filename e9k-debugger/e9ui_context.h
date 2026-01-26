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
    int           winW;
    int           winH;
    int           mouseX;
    int           mouseY;
    int           mousePrevX;
    int           mousePrevY;
    int           cursorOverride;
    float         dpiScale; 
    struct e9ui_component *_focus;
    int                  focusClickHandled;

    void (*sendLine)(const char *s);
    void (*sendInterrupt)(void);


    int  (*registerHotkey)(struct e9ui_context *ctx, SDL_Keycode key, SDL_Keymod modMask, SDL_Keymod modValue,
                           void (*cb)(struct e9ui_context *ctx, void *user), void *user);
    void (*unregisterHotkey)(struct e9ui_context *ctx, int id);
    int  (*dispatchHotkey)(struct e9ui_context *ctx, const SDL_KeyboardEvent *kev);
    void (*onSplitChanged)(struct e9ui_context *ctx, struct e9ui_component *split, float ratio);
    void (*applyCompletion)(struct e9ui_context *ctx, int prefixLen, const char *insert);
    void (*showCompletions)(struct e9ui_context *ctx, const char * const *cands, int count);
    void (*hideCompletions)(struct e9ui_context *ctx);
} e9ui_context_t;

#define e9ui_getFocus(ctx) (ctx)->_focus
void
e9ui_setFocus(struct e9ui_context *ctx, struct e9ui_component* comp);

