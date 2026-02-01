/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "emu.h"
#include "runtime.h"
#include "gl_composite.h"
#include "alloc.h"
#include "libretro_host.h"
#include "seek_bar.h"
#include "debug.h"
#include "state_buffer.h"
#include "debugger.h"
#include "ui.h"
#include "e9ui_button.h"
#include "shader_ui.h"
#include "emu_geo.h"

typedef struct geo9000_state {
    int wasFocused;
    char *seekBarMeta;
    int histogramEnabled;
    char *shaderUiBtnMeta;
    char *buttonStackMeta;
} emu_state_t;

typedef struct geo9000_button_stack_state {
    int padding;
    int gap;
} emu_button_stack_state_t;

static void
emu_buttonStackMeasure(e9ui_component_t *self, e9ui_context_t *ctx, int *outW, int *outH)
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
    emu_button_stack_state_t *st = (emu_button_stack_state_t*)self->state;
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
emu_buttonStackPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    int h = 0;
    emu_buttonStackMeasure(self, ctx, NULL, &h);
    return h;
}

static void
emu_buttonStackLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    emu_button_stack_state_t *st = (emu_button_stack_state_t*)self->state;
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
emu_buttonStackRender(e9ui_component_t *self, e9ui_context_t *ctx)
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
emu_buttonStackMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    emu_button_stack_state_t *st = (emu_button_stack_state_t*)alloc_calloc(1, sizeof(*st));
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
    comp->name = "emu_button_stack";
    comp->state = st;
    comp->preferredHeight = emu_buttonStackPreferredHeight;
    comp->layout = emu_buttonStackLayout;
    comp->render = emu_buttonStackRender;
    return comp;
}

static void
emu_toggleShaderUi(e9ui_context_t *ctx, void *user)
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
emu_seekTooltip(float percent, char *out, size_t cap, void *user)
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


static int
emu_viewPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)ctx;
    (void)availW;
    return 0;
}

static void
emu_viewLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}


static void
emu_seekBarChanged(float percent, void *user)
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
emu_seekBarDragging(int dragging, float percent, void *user)
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

static unsigned
emu_joypadPort(void)
{
    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        return 0u;
    }
    return 0u;
}

static int
emu_pointInBounds(const e9ui_component_t *comp, int x, int y)
{
    if (!comp) {
        return 0;
    }
    return x >= comp->bounds.x && x < comp->bounds.x + comp->bounds.w &&
           y >= comp->bounds.y && y < comp->bounds.y + comp->bounds.h;
}

static int
emu_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)ctx;
    if (!self || !ev) {
        return 0;
    }
    emu_state_t *state = (emu_state_t *)self->state;
    if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEMOTION) {
        if (state && state->seekBarMeta) {
            e9ui_component_t *seek = e9ui_child_find(self, state->seekBarMeta);
            if (seek && seek->handleEvent && seek->handleEvent(seek, ctx, ev)) {
                return 1;
            }
        }
        if (state && state->buttonStackMeta) {
            e9ui_component_t *stack = e9ui_child_find(self, state->buttonStackMeta);
            int mx = (ev->type == SDL_MOUSEMOTION) ? ev->motion.x : ev->button.x;
            int my = (ev->type == SDL_MOUSEMOTION) ? ev->motion.y : ev->button.y;
            if (stack && emu_pointInBounds(stack, mx, my)) {
                return 0;
            }
        }
        int mx = (ev->type == SDL_MOUSEMOTION) ? ev->motion.x : ev->button.x;
        int my = (ev->type == SDL_MOUSEMOTION) ? ev->motion.y : ev->button.y;
        if (!emu_pointInBounds(self, mx, my)) {
            return 0;
        }
        unsigned port = libretro_host_getMousePort();
        if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
            port = LIBRETRO_HOST_MAX_PORTS;
        }
        if (port < LIBRETRO_HOST_MAX_PORTS || port == LIBRETRO_HOST_MAX_PORTS) {
            if (ev->type == SDL_MOUSEMOTION) {
                libretro_host_addMouseMotion(port, ev->motion.xrel, ev->motion.yrel);
                return 1;
            }
            if (ev->button.button == SDL_BUTTON_LEFT) {
                int pressed = (ev->type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
                libretro_host_setMouseButton(port, RETRO_DEVICE_ID_MOUSE_LEFT, pressed);
                return 1;
            }
            if (ev->button.button == SDL_BUTTON_RIGHT) {
                int pressed = (ev->type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
                libretro_host_setMouseButton(port, RETRO_DEVICE_ID_MOUSE_RIGHT, pressed);
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
    SDL_Keymod rawMods = ev->key.keysym.mod;
    SDL_Keymod mods = 0;
    if (rawMods & KMOD_CTRL) {
        mods = (SDL_Keymod)(mods | KMOD_CTRL);
    }
    if (rawMods & KMOD_SHIFT) {
        mods = (SDL_Keymod)(mods | KMOD_SHIFT);
    }
    if (rawMods & KMOD_ALT) {
        mods = (SDL_Keymod)(mods | KMOD_ALT);
    }
    if (rawMods & KMOD_GUI) {
        mods = (SDL_Keymod)(mods | KMOD_GUI);
    }
    if (ev->key.keysym.sym == SDLK_F5) {
        if (pressed) {
            debugger_toggleSpeed();
        }
        return 1;
    }
    if (ev->key.keysym.sym == SDLK_f && mods == (SDL_Keymod)(KMOD_CTRL|KMOD_ALT)) {
        if (pressed) {
            debugger.frameStepMode = 1;
            debugger.frameStepPending = 1;
        }
        return 1;
    }
    if (ev->key.keysym.sym == SDLK_b && mods == (SDL_Keymod)(KMOD_CTRL|KMOD_ALT)) {
        if (pressed) {
            debugger.frameStepMode = 1;
            debugger.frameStepPending = -1;
        }
        return 1;
    }    
    if (ev->key.keysym.sym == SDLK_g && mods == (SDL_Keymod)(KMOD_CTRL|KMOD_ALT)) {
        if (pressed) {
            debugger.frameStepMode = 0;
            debugger.frameStepPending = 0;
        }
        return 1;
    }
    if (debugger.emu->mapKeyToJoypad(ev->key.keysym.sym, &id)) {
        libretro_host_setJoypadState(emu_joypadPort(), id, pressed);
    } else {
        SDL_Keycode key = ev->key.keysym.sym;
        uint32_t character = debugger.emu->translateCharacter(key, ev->key.keysym.mod);
        unsigned retro_key = debugger.emu->translateKey(key);
        uint16_t mods = debugger.emu->translateModifiers(ev->key.keysym.mod);
        libretro_host_sendKeyEvent(retro_key, character, mods, pressed);
    }
    return 1;
}

static SDL_Rect
emu_fitRect(e9ui_rect_t bounds, int tex_w, int tex_h)
{
    SDL_Rect dst = { bounds.x, bounds.y, bounds.w, bounds.h };
    if (tex_w <= 0 || tex_h <= 0 || bounds.w <= 0 || bounds.h <= 0) {
        return dst;
    }
    double tex_aspect = (double)libretro_host_getDisplayAspect();
    if (tex_aspect <= 0.0) {
        tex_aspect = (double)tex_w / (double)tex_h;
    }
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
emu_viewRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    emu_state_t *state = (emu_state_t *)self->state;
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
    SDL_Rect dst = emu_fitRect(self->bounds, tex_w, tex_h);
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

    debugger.emu->render(ctx, &dst);    

    if (state && state->buttonStackMeta) {
        e9ui_component_t *stack = e9ui_child_find(self, state->buttonStackMeta);
        if (stack) {
            int margin = e9ui_scale_px(ctx, 8);
            int stackW = 0;
            int stackH = 0;
            emu_buttonStackMeasure(stack, ctx, &stackW, &stackH);
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
emu_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    (void)self;
}

e9ui_component_t *
emu_makeComponent(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    emu_state_t *state = (emu_state_t*)alloc_calloc(1, sizeof(*state));
    comp->name = "emu";
    comp->preferredHeight = emu_viewPreferredHeight;
    comp->layout = emu_viewLayout;
    comp->render = emu_viewRender;
    comp->handleEvent = emu_handleEvent;
    comp->dtor = emu_dtor;
    comp->focusable = 1;
    comp->state = state;

    state->histogramEnabled = 0;
    e9ui_component_t *button_stack = emu_buttonStackMake();
    if (button_stack) {
        e9ui_setAutoHide(button_stack, 1, 64);
        e9ui_setFocusTarget(button_stack, comp);
        state->buttonStackMeta = alloc_strdup("button_stack");
        e9ui_child_add(comp, button_stack, state->buttonStackMeta);
    }

    debugger.emu->createOverlays(comp, button_stack);
    
    e9ui_component_t *btn_shader = e9ui_button_make("CRT Settings", emu_toggleShaderUi, comp);
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
        seek_bar_setCallback(seek, emu_seekBarChanged, NULL);
        seek_bar_setDragCallback(seek, emu_seekBarDragging, seek);
        seek_bar_setTooltipCallback(seek, emu_seekTooltip, NULL);
        e9ui_setAutoHide(seek, 1, seek_bar_getHoverMargin(seek));
        state->seekBarMeta = alloc_strdup("seek_bar");
        e9ui_child_add(comp, seek, state->seekBarMeta);
    }
    return comp;
}
