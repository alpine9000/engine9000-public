/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "emu.h"
#include "e9ui_event.h"
#include "runtime.h"
#include "gl_composite.h"
#include "alloc.h"
#include "libretro_host.h"
#include "e9ui_range_bar.h"
#include "e9ui_seek_bar.h"
#include "debug.h"
#include "state_buffer.h"
#include "debugger.h"
#include "debugger_input_bindings.h"
#include "ui.h"
#include "hotkeys.h"
#include "e9ui_button.h"
#include "e9ui_scroll.h"
#include "shader_ui.h"

#define EMU_RANGE_BAR_MAX 4
#define EMU_OVERLAY_BUTTON_MICRO_THRESHOLD_W 480
#define EMU_OVERLAY_BUTTON_NANO_THRESHOLD_W 350
#define EMU_ZOOM_MIN_PERCENT 100
#define EMU_ZOOM_MAX_PERCENT 400
#define EMU_ZOOM_STEP_PERCENT 25

typedef struct emu_range_bar_binding {
    size_t index;
} emu_range_bar_binding_t;

typedef struct emu_zoom_binding {
    int deltaPercent;
    struct geo9000_state *state;
} emu_zoom_binding_t;

typedef struct geo9000_state {
    int wasFocused;
    int zoomPercent;
    int viewportWidth;
    int viewportHeight;
    int contentWidth;
    int contentHeight;
    e9ui_component_t *viewComp;
    e9ui_component_t *scrollComp;
    char *scrollMeta;
    char *seekBarMeta;
    int seekBarForcedHidden;
    int histogramEnabled;
    char *shaderUiBtnMeta;
    char *zoomOutBtnMeta;
    char *zoomInBtnMeta;
    char *buttonStackMeta;
    size_t rangeBarCount;
    uint32_t suppressedMouseButtons;
    char *rangeBarMeta[EMU_RANGE_BAR_MAX];
    emu_range_bar_binding_t rangeBarBindings[EMU_RANGE_BAR_MAX];
    emu_zoom_binding_t zoomOutBinding;
    emu_zoom_binding_t zoomInBinding;
} emu_state_t;

typedef struct geo9000_button_stack_state {
    int padding;
    int gap;
    int microPadding;
    int microGap;
    int nanoPadding;
    int nanoGap;
    int compactMode;
} emu_button_stack_state_t;

typedef enum emu_overlay_button_mode {
    emu_overlay_button_mode_mini = 0,
    emu_overlay_button_mode_micro = 1,
    emu_overlay_button_mode_nano = 2
} emu_overlay_button_mode_t;

static int emu_mouseCaptured = 0;
static e9ui_component_t *emu_mouseView;
static char emu_mouseCaptureMessage[160];

static int
emu_mouseButtonMask(Uint8 button)
{
    if (button >= 32u) {
        return 0;
    }
    return (int)(1u << button);
}

static int
emu_mouseCaptureBlocked(void)
{
    return e9ui_event_componentContainsBlockingWindow(e9ui->root) ||
           e9ui_event_componentContainsBlockingWindow(e9ui_getOverlayHost());
}

static int
emu_mouseCaptureSupportedForCurrentTarget(void)
{
    return target && target->emu && target->emu->mouseCaptureSupported;
}

static int
emu_mouseCaptureOnClickEnabledForCurrentTarget(void)
{
    if (!target || !target->emu || !target->emu->mouseCaptureOnClickEnabled) {
        return 0;
    }
    return target->emu->mouseCaptureOnClickEnabled() ? 1 : 0;
}

static int
emu_mouseCaptureEnable(e9ui_context_t *ctx)
{
    int relativeEnabled = 0;
    char releaseBinding[64];

    if (!ctx) {
        return 0;
    }
    if (emu_mouseCaptured) {
        return 1;
    }
    // Let the UI drag owner receive its button release before capturing.
    if (ctx->cursorCaptureOwner) {
        return 0;
    }
    if (!emu_mouseCaptureSupportedForCurrentTarget() || emu_mouseCaptureBlocked()) {
        return 0;
    }
    if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0) {
        relativeEnabled = 1;
    }
    libretro_host_clearMouseState();
    if (ctx->window) {
        SDL_SetWindowGrab(ctx->window, SDL_TRUE);
    }
    (void)SDL_CaptureMouse(SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    if (!relativeEnabled && ctx->window) {
        SDL_RaiseWindow(ctx->window);
    }
    emu_mouseCaptured = 1;
    if (hotkeys_formatActionBindingDisplay("mouse_release", releaseBinding, sizeof(releaseBinding)) &&
        releaseBinding[0]) {
        snprintf(emu_mouseCaptureMessage, sizeof(emu_mouseCaptureMessage),
                 "MOUSE CAPTURED (%s TO RELEASE)", releaseBinding);
        e9ui_showTransientMessage(emu_mouseCaptureMessage);
    } else {
        e9ui_showTransientMessage("MOUSE CAPTURED");
    }
    return 1;
}

int
emu_mouseCaptureRelease(e9ui_context_t *ctx)
{
    if (!emu_mouseCaptured) {
        return 0;
    }
    if (ctx && ctx->window) {
        SDL_SetWindowGrab(ctx->window, SDL_FALSE);
    }
    (void)SDL_CaptureMouse(SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    emu_mouseCaptured = 0;
    libretro_host_clearMouseState();
    e9ui_showTransientMessage("MOUSE RELEASED");
    return 1;
}

static void
emu_mouseCaptureValidate(e9ui_context_t *ctx)
{
    if (!emu_mouseCaptured) {
        return;
    }
    if (emu_mouseCaptureSupportedForCurrentTarget() && !emu_mouseCaptureBlocked()) {
        return;
    }
    (void)emu_mouseCaptureRelease(ctx);
}

int
emu_mouseCaptureStart(e9ui_context_t *ctx)
{
    if (emu_mouseCaptureEnable(ctx)) {
        e9ui_component_t *view = emu_mouseView;
        if (view) {
            e9ui_setFocus(ctx, view);
        }
        return 1;
    }
    return 0;
}

int
emu_handleCapturedMouseEvent(e9ui_context_t *ctx, const SDL_Event *ev)
{
    emu_mouseCaptureValidate(ctx);
    if (!emu_mouseCaptured ||
        (ev->type != SDL_MOUSEMOTION && ev->type != SDL_MOUSEBUTTONDOWN &&
         ev->type != SDL_MOUSEBUTTONUP && ev->type != SDL_MOUSEWHEEL)) {
        return 0;
    }
    unsigned port = target->mousePort >= 0 ? (unsigned)target->mousePort : libretro_host_getMousePort();
    if (ev->type == SDL_MOUSEMOTION) {
        libretro_host_addMouseMotion(port, ev->motion.xrel, ev->motion.yrel);
    } else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
        if (ev->button.button == SDL_BUTTON_LEFT || ev->button.button == SDL_BUTTON_RIGHT) {
            unsigned id = ev->button.button == SDL_BUTTON_LEFT ? RETRO_DEVICE_ID_MOUSE_LEFT : RETRO_DEVICE_ID_MOUSE_RIGHT;
            libretro_host_setMouseButton(port, id, ev->type == SDL_MOUSEBUTTONDOWN);
        }
    }
    return 1;
}

static void
emu_buttonStackSetCompactMode(e9ui_component_t *self, int compactMode)
{
    if (!self || !self->state) {
        return;
    }
    emu_button_stack_state_t *st = (emu_button_stack_state_t*)self->state;
    if (compactMode < emu_overlay_button_mode_mini) {
        compactMode = emu_overlay_button_mode_mini;
    }
    if (compactMode > emu_overlay_button_mode_nano) {
        compactMode = emu_overlay_button_mode_nano;
    }
    if (st->compactMode == compactMode) {
        return;
    }
    st->compactMode = compactMode;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child) {
            continue;
        }
        if (compactMode == emu_overlay_button_mode_nano) {
            e9ui_button_setNano(child, 1);
        } else if (compactMode == emu_overlay_button_mode_micro) {
            e9ui_button_setMicro(child, 1);
        } else {
            e9ui_button_setMini(child, 1);
        }
    }
}

static int
emu_buttonStackPushClip(e9ui_component_t *self, e9ui_context_t *ctx, SDL_Rect *prevClip, SDL_bool *hadPrevClip)
{
    if (!self || !ctx || !ctx->renderer || !prevClip || !hadPrevClip) {
        return 0;
    }
    SDL_Renderer *renderer = ctx->renderer;
    *hadPrevClip = SDL_RenderIsClipEnabled(renderer);
    if (*hadPrevClip) {
        SDL_RenderGetClipRect(renderer, prevClip);
    } else {
        prevClip->x = 0;
        prevClip->y = 0;
        prevClip->w = 0;
        prevClip->h = 0;
    }

    SDL_Rect stackClip = {
        self->bounds.x,
        self->bounds.y,
        self->bounds.w,
        self->bounds.h
    };
    if (self->autoHideHasClip) {
        SDL_Rect panelClip = {
            self->autoHideClip.x,
            self->autoHideClip.y,
            self->autoHideClip.w,
            self->autoHideClip.h
        };
        SDL_Rect mergedStackClip;
        if (!SDL_IntersectRect(&stackClip, &panelClip, &mergedStackClip)) {
            return 0;
        }
        stackClip = mergedStackClip;
    }
    if (stackClip.w <= 0 || stackClip.h <= 0) {
        return 0;
    }
    if (*hadPrevClip) {
        SDL_Rect merged;
        if (!SDL_IntersectRect(prevClip, &stackClip, &merged)) {
            return 0;
        }
        SDL_RenderSetClipRect(renderer, &merged);
        return 1;
    }
    SDL_RenderSetClipRect(renderer, &stackClip);
    return 1;
}

static void
emu_buttonStackPopClip(e9ui_context_t *ctx, SDL_Rect *prevClip, SDL_bool hadPrevClip)
{
    if (!ctx || !ctx->renderer) {
        return;
    }
    if (hadPrevClip) {
        SDL_RenderSetClipRect(ctx->renderer, prevClip);
    } else {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    }
}

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
    int basePad = st->padding;
    int baseGap = st->gap;
    if (st->compactMode == emu_overlay_button_mode_micro) {
        basePad = st->microPadding;
        baseGap = st->microGap;
    } else if (st->compactMode == emu_overlay_button_mode_nano) {
        basePad = st->nanoPadding;
        baseGap = st->nanoGap;
    }
    int pad = e9ui_scale_px(ctx, basePad);
    int gap = e9ui_scale_px(ctx, baseGap);
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
    int basePad = st->padding;
    int baseGap = st->gap;
    if (st->compactMode == emu_overlay_button_mode_micro) {
        basePad = st->microPadding;
        baseGap = st->microGap;
    } else if (st->compactMode == emu_overlay_button_mode_nano) {
        basePad = st->nanoPadding;
        baseGap = st->nanoGap;
    }
    int pad = e9ui_scale_px(ctx, basePad);
    int gap = e9ui_scale_px(ctx, baseGap);
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
    SDL_Rect prevClip;
    SDL_bool hadPrevClip = SDL_FALSE;
    if (!emu_buttonStackPushClip(self, ctx, &prevClip, &hadPrevClip)) {
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
    emu_buttonStackPopClip(ctx, &prevClip, hadPrevClip);
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
    st->microPadding = 2;
    st->microGap = 2;
    st->nanoPadding = 1;
    st->nanoGap = 1;
    st->compactMode = emu_overlay_button_mode_mini;
    comp->name = "emu_button_stack";
    comp->state = st;
    comp->preferredHeight = emu_buttonStackPreferredHeight;
    comp->layout = emu_buttonStackLayout;
    comp->render = emu_buttonStackRender;
    return comp;
}

static int
emu_zoomClampPercent(int zoomPercent)
{
    if (zoomPercent < EMU_ZOOM_MIN_PERCENT) {
        return EMU_ZOOM_MIN_PERCENT;
    }
    if (zoomPercent > EMU_ZOOM_MAX_PERCENT) {
        return EMU_ZOOM_MAX_PERCENT;
    }
    return zoomPercent;
}

static void
emu_updateScrollContentSize(emu_state_t *state, e9ui_component_t *scroll)
{
    int zoomPercent = 0;
    int contentWidth = 0;
    int contentHeight = 0;

    if (!state || !scroll) {
        return;
    }
    zoomPercent = emu_zoomClampPercent(state->zoomPercent);
    state->zoomPercent = zoomPercent;

    contentWidth = (state->viewportWidth * zoomPercent + 99) / 100;
    contentHeight = (state->viewportHeight * zoomPercent + 99) / 100;
    if (contentWidth < 1) {
        contentWidth = 1;
    }
    if (contentHeight < 1) {
        contentHeight = 1;
    }

    state->contentWidth = contentWidth;
    state->contentHeight = contentHeight;
    e9ui_scroll_setContentWidthPx(scroll, contentWidth);
    e9ui_scroll_setContentHeightPx(scroll, contentHeight);
}

static void
emu_adjustZoom(e9ui_context_t *ctx, void *user)
{
    emu_zoom_binding_t *binding = (emu_zoom_binding_t*)user;
    emu_state_t *state = NULL;
    e9ui_component_t *scroll = NULL;
    int oldZoom = 0;
    int newZoom = 0;
    int oldContentWidth = 0;
    int oldContentHeight = 0;
    int oldScrollX = 0;
    int oldScrollY = 0;
    int centerX = 0;
    int centerY = 0;
    int newScrollX = 0;
    int newScrollY = 0;

    (void)ctx;
    if (!binding || !binding->state) {
        return;
    }
    state = binding->state;
    scroll = state->scrollComp;
    if (!scroll) {
        return;
    }

    oldZoom = emu_zoomClampPercent(state->zoomPercent);
    newZoom = emu_zoomClampPercent(oldZoom + binding->deltaPercent);
    if (newZoom == oldZoom) {
        return;
    }

    oldContentWidth = state->contentWidth > 0 ? state->contentWidth : scroll->bounds.w;
    oldContentHeight = state->contentHeight > 0 ? state->contentHeight : scroll->bounds.h;
    e9ui_scroll_getScrollPx(scroll, &oldScrollX, &oldScrollY);
    centerX = oldScrollX + scroll->bounds.w / 2;
    centerY = oldScrollY + scroll->bounds.h / 2;

    state->zoomPercent = newZoom;
    emu_updateScrollContentSize(state, scroll);

    if (oldContentWidth > 0) {
        newScrollX = ((centerX * state->contentWidth) + oldContentWidth / 2) / oldContentWidth - scroll->bounds.w / 2;
    }
    if (oldContentHeight > 0) {
        newScrollY = ((centerY * state->contentHeight) + oldContentHeight / 2) / oldContentHeight - scroll->bounds.h / 2;
    }
    e9ui_scroll_setScrollPx(scroll, newScrollX, newScrollY);
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

static void
emu_rangeBarChanged(float startPercent, float endPercent, void *user)
{
    emu_range_bar_binding_t *binding = (emu_range_bar_binding_t*)user;
    if (!binding || !target || !target->emu || !target->emu->rangeBarChanged) {
        return;
    }
    target->emu->rangeBarChanged(binding->index, startPercent, endPercent);
}

static void
emu_rangeBarDragging(int dragging, float startPercent, float endPercent, void *user)
{
    emu_range_bar_binding_t *binding = (emu_range_bar_binding_t*)user;
    if (!binding || !target || !target->emu || !target->emu->rangeBarDragging) {
        return;
    }
    target->emu->rangeBarDragging(binding->index, dragging, startPercent, endPercent);
}

static void
emu_rangeBarTooltip(float startPercent, float endPercent, char *out, size_t cap, void *user)
{
    emu_range_bar_binding_t *binding = (emu_range_bar_binding_t*)user;
    if (!out || cap == 0 || !binding) {
        return;
    }
    if (target && target->emu && target->emu->rangeBarTooltip) {
        target->emu->rangeBarTooltip(binding->index, startPercent, endPercent, out, cap);
    }
}

static int
emu_rangeBarSync(e9ui_component_t *bar, size_t index)
{
    if (!bar || !target || !target->emu || !target->emu->rangeBarSync) {
        return 0;
    }
    return target->emu->rangeBarSync(index, bar) ? 1 : 0;
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
            e9ui_seek_bar_setPercent(seek, 1.0f);
        }
    }
}

static unsigned
emu_joypadPort(void)
{

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
emu_getVideoDst(e9ui_component_t *self, SDL_Rect *outDst);

static int
emu_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ev) {
        return 0;
    }
    emu_mouseCaptureValidate(ctx);
    emu_state_t *state = (emu_state_t *)self->state;
    if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEMOTION) {
        if (state) {
            for (size_t i = 0; i < state->rangeBarCount; ++i) {
                e9ui_component_t *rangeBar = NULL;
                if (!state->rangeBarMeta[i]) {
                    continue;
                }
                rangeBar = e9ui_child_find(self, state->rangeBarMeta[i]);
                if (rangeBar &&
                    (e9ui_range_bar_isDragging(rangeBar) || !e9ui_getHidden(rangeBar)) &&
                    rangeBar->handleEvent && rangeBar->handleEvent(rangeBar, ctx, ev)) {
                    return 1;
                }
            }
        }
        if (state && state->seekBarMeta) {
            e9ui_component_t *seek = e9ui_child_find(self, state->seekBarMeta);
            if (seek &&
                !state_buffer_isRollingPaused() &&
                !e9ui_getHidden(seek) &&
                seek->handleEvent &&
                seek->handleEvent(seek, ctx, ev)) {
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
        if (ev->type == SDL_MOUSEBUTTONUP && state) {
            int suppressMask = emu_mouseButtonMask(ev->button.button);

            if (suppressMask && (state->suppressedMouseButtons & (uint32_t)suppressMask)) {
                state->suppressedMouseButtons &= ~(uint32_t)suppressMask;
                return 1;
            }
        }
        if (ev->type == SDL_MOUSEBUTTONDOWN && ctx && e9ui_getFocus(ctx) != self) {
            if (state) {
                int suppressMask = emu_mouseButtonMask(ev->button.button);

                if (suppressMask) {
                    state->suppressedMouseButtons |= (uint32_t)suppressMask;
                }
            }
            return 1;
        }
        if (target && target->emu && target->emu->handleOverlayEvent &&
            (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP)) {
            SDL_Rect dst;
            if (emu_getVideoDst(self, &dst) && target->emu->handleOverlayEvent(ctx, &dst, ev)) {
                return 1;
            }
        }
        if (emu_mouseCaptureSupportedForCurrentTarget()) {
            if (ev->type == SDL_MOUSEBUTTONDOWN && emu_mouseCaptureOnClickEnabledForCurrentTarget()) {
                (void)emu_mouseCaptureEnable(ctx);
            }
            return 1;
        }
        unsigned port = libretro_host_getMousePort();
        if (target->mousePort >= 0) {
	  port = target->mousePort;
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
    unsigned port = 0;
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
    if (debugger_input_bindings_mapKeyToJoypadPort(target ? target->coreIndex : TARGET_NEOGEO,
                                                   (target && target->coreOptionGetValue) ? target->coreOptionGetValue : NULL,
                                                   ev->key.keysym.sym,
                                                   &port,
                                                   &id)) {
        libretro_host_setJoypadState(port, id, pressed);
    } else if (target->emu->mapKeyToJoypad(ev->key.keysym.sym, &id)) {
        libretro_host_setJoypadState(emu_joypadPort(), id, pressed);
    } else {
        SDL_Keycode key = ev->key.keysym.sym;
        uint32_t character = target->emu->translateCharacter(key, ev->key.keysym.mod);
        unsigned retro_key = target->emu->translateKey(key);
        uint16_t mods = target->emu->translateModifiers(ev->key.keysym.mod);
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

static int
emu_getVideoDst(e9ui_component_t *self, SDL_Rect *outDst)
{
    const uint8_t *data = NULL;
    int tex_w = 0;
    int tex_h = 0;
    size_t pitch = 0;

    if (outDst) {
        memset(outDst, 0, sizeof(*outDst));
    }
    if (!self || !outDst) {
        return 0;
    }
    if (!libretro_host_getFrame(&data, &tex_w, &tex_h, &pitch)) {
        return 0;
    }
    e9ui_rect_t fitBounds = self->bounds;
    if (target->emu->adjustVideoBounds) {
        target->emu->adjustVideoBounds(&fitBounds);
    }
    *outDst = emu_fitRect(fitBounds, tex_w, tex_h);
    if (target->emu->adjustVideoDst) {
        target->emu->adjustVideoDst(outDst);
    }
    return 1;
}

static void
emu_viewRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    SDL_Rect prevClip;
    SDL_Rect viewClip;
    SDL_bool clipEnabled = SDL_FALSE;
    int clipApplied = 0;

    if (!ctx || !ctx->renderer) {
        return;
    }
    if (!self) {
        return;
    }
    clipEnabled = SDL_RenderIsClipEnabled(ctx->renderer);
    SDL_RenderGetClipRect(ctx->renderer, &prevClip);
    viewClip.x = self->bounds.x;
    viewClip.y = self->bounds.y;
    viewClip.w = self->bounds.w;
    viewClip.h = self->bounds.h;
    if (viewClip.w > 0 && viewClip.h > 0) {
        if (clipEnabled) {
            SDL_Rect mergedClip;
            if (SDL_IntersectRect(&prevClip, &viewClip, &mergedClip)) {
                SDL_RenderSetClipRect(ctx->renderer, &mergedClip);
            } else {
                SDL_RenderSetClipRect(ctx->renderer, &viewClip);
            }
        } else {
            SDL_RenderSetClipRect(ctx->renderer, &viewClip);
        }
        clipApplied = 1;
    }
    emu_state_t *state = (emu_state_t *)self->state;
    emu_mouseCaptureValidate(ctx);
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
        goto restore_clip;
    }
    SDL_Rect dst;
    if (!emu_getVideoDst(self, &dst)) {
        goto restore_clip;
    }
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
            goto restore_clip;
        }
        SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
    }

    SDL_Rect componentClip = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    target->emu->render(ctx, &dst, &componentClip);    

    if (state && state->rangeBarCount > 0) {
        e9ui_rect_t vidBounds = { dst.x, dst.y, dst.w, dst.h };
        for (size_t i = 0; i < state->rangeBarCount; ++i) {
            e9ui_component_t *rangeBar = NULL;
            if (!state->rangeBarMeta[i]) {
                continue;
            }
            rangeBar = e9ui_child_find(self, state->rangeBarMeta[i]);
            if (rangeBar) {
                e9ui_range_bar_layoutInParent(rangeBar, ctx, vidBounds);
                e9ui_setAutoHideClip(rangeBar, &self->bounds);
                if (emu_rangeBarSync(rangeBar, i) &&
                    !e9ui_getHidden(rangeBar) && rangeBar->render) {
                    rangeBar->render(rangeBar, ctx);
                }
            }
        }
    }

    if (state && state->buttonStackMeta) {
        e9ui_component_t *stack = e9ui_child_find(self, state->buttonStackMeta);
        if (stack) {
            int overlayButtonMode = emu_overlay_button_mode_mini;
            int microThresholdW = e9ui_scale_px(ctx, EMU_OVERLAY_BUTTON_MICRO_THRESHOLD_W);
            int nanoThresholdW = e9ui_scale_px(ctx, EMU_OVERLAY_BUTTON_NANO_THRESHOLD_W);
            if (self->bounds.w <= nanoThresholdW) {
                overlayButtonMode = emu_overlay_button_mode_nano;
            } else if (self->bounds.w <= microThresholdW) {
                overlayButtonMode = emu_overlay_button_mode_micro;
            }
            emu_buttonStackSetCompactMode(stack, overlayButtonMode);
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
            seek->disabled = state_buffer_isRollingPaused() ? 1 : 0;
            if (seek->disabled) {
                if (!state->seekBarForcedHidden) {
                    e9ui_setHidden(seek, 1);
                    state->seekBarForcedHidden = 1;
                }
            } else {
                if (state->seekBarForcedHidden) {
                    e9ui_setHidden(seek, 0);
                    state->seekBarForcedHidden = 0;
                }
                e9ui_rect_t vid_bounds = { dst.x, dst.y, dst.w, dst.h };
                e9ui_seek_bar_layoutInParent(seek, ctx, vid_bounds);
                e9ui_setAutoHideClip(seek, &self->bounds);
                if (!e9ui_getHidden(seek) && seek->render) {
                    seek->render(seek, ctx);
                }
            }
        }
    }

restore_clip:
    if (clipApplied) {
        if (clipEnabled) {
            SDL_RenderSetClipRect(ctx->renderer, &prevClip);
        } else {
            SDL_RenderSetClipRect(ctx->renderer, NULL);
        }
    }
}

static void
emu_hostLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    emu_state_t *state = NULL;
    e9ui_component_t *scroll = NULL;

    if (!self) {
        return;
    }
    self->bounds = bounds;
    state = (emu_state_t*)self->state;
    if (!state) {
        return;
    }
    state->viewportWidth = bounds.w > 0 ? bounds.w : 1;
    state->viewportHeight = bounds.h > 0 ? bounds.h : 1;
    scroll = state->scrollComp;
    if (!scroll) {
        return;
    }
    emu_updateScrollContentSize(state, scroll);
    if (scroll->layout) {
        scroll->layout(scroll, ctx, bounds);
    }
}

static void
emu_hostRenderButtonStack(e9ui_component_t *self, e9ui_context_t *ctx)
{
    emu_state_t *state = NULL;
    e9ui_component_t *stack = NULL;
    int overlayButtonMode = emu_overlay_button_mode_mini;
    int microThresholdW = 0;
    int nanoThresholdW = 0;
    int margin = 0;
    int stackW = 0;
    int stackH = 0;

    if (!self || !ctx) {
        return;
    }
    state = (emu_state_t*)self->state;
    if (!state || !state->buttonStackMeta) {
        return;
    }
    stack = e9ui_child_find(self, state->buttonStackMeta);
    if (!stack) {
        return;
    }

    microThresholdW = e9ui_scale_px(ctx, EMU_OVERLAY_BUTTON_MICRO_THRESHOLD_W);
    nanoThresholdW = e9ui_scale_px(ctx, EMU_OVERLAY_BUTTON_NANO_THRESHOLD_W);
    if (self->bounds.w <= nanoThresholdW) {
        overlayButtonMode = emu_overlay_button_mode_nano;
    } else if (self->bounds.w <= microThresholdW) {
        overlayButtonMode = emu_overlay_button_mode_micro;
    }
    emu_buttonStackSetCompactMode(stack, overlayButtonMode);

    margin = e9ui_scale_px(ctx, 8);
    emu_buttonStackMeasure(stack, ctx, &stackW, &stackH);
    if (stackW <= 0 || stackH <= 0) {
        return;
    }
    stack->bounds.x = self->bounds.x + self->bounds.w - stackW - margin;
    stack->bounds.y = self->bounds.y + margin;
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

static void
emu_hostRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    emu_state_t *state = NULL;
    SDL_Rect dst;

    if (!self || !ctx) {
        return;
    }
    state = (emu_state_t*)self->state;
    if (state && state->scrollComp && state->scrollComp->render) {
        state->scrollComp->render(state->scrollComp, ctx);
    }
    emu_hostRenderButtonStack(self, ctx);
    if (state && state->viewComp &&
        target && target->emu && target->emu->renderForeground &&
        emu_getVideoDst(state->viewComp, &dst)) {
        target->emu->renderForeground(ctx, &dst);
    }
}

static int
emu_hostHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    emu_state_t *state = NULL;
    e9ui_component_t *stack = NULL;
    (void)ctx;

    if (!self || !ev) {
        return 0;
    }
    state = (emu_state_t*)self->state;
    if (state && state->buttonStackMeta &&
        (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEMOTION)) {
        int mx = (ev->type == SDL_MOUSEMOTION) ? ev->motion.x : ev->button.x;
        int my = (ev->type == SDL_MOUSEMOTION) ? ev->motion.y : ev->button.y;
        stack = e9ui_child_find(self, state->buttonStackMeta);
        if (stack && emu_pointInBounds(stack, mx, my)) {
            return 0;
        }
    }
    return 0;
}

static void
emu_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    (void)emu_mouseCaptureRelease(ctx);
    if (target && target->emu && target->emu->destroy) {
        target->emu->destroy();
    }
    (void)self;
}

static void
emu_viewDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)emu_mouseCaptureRelease(ctx);
    emu_mouseView = NULL;
    if (!self) {
        return;
    }
    self->state = NULL;
}

e9ui_component_t *
emu_makeComponent(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    e9ui_component_t *view = (e9ui_component_t*)alloc_calloc(1, sizeof(*view));
    emu_state_t *state = (emu_state_t*)alloc_calloc(1, sizeof(*state));
    e9ui_component_t *scroll = NULL;

    if (!comp || !view || !state) {
        alloc_free(comp);
        alloc_free(view);
        alloc_free(state);
        return NULL;
    }

    comp->name = "emu";
    comp->layout = emu_hostLayout;
    comp->render = emu_hostRender;
    comp->handleEvent = emu_hostHandleEvent;
    comp->dtor = emu_dtor;
    comp->state = state;

    emu_mouseView = view;
    view->name = "emu_view";
    view->layout = emu_viewLayout;
    view->render = emu_viewRender;
    view->handleEvent = emu_handleEvent;
    view->dtor = emu_viewDtor;
    view->focusable = 1;
    view->state = state;

    state->histogramEnabled = 0;
    state->zoomPercent = 100;
    state->viewportWidth = 1;
    state->viewportHeight = 1;
    state->viewComp = view;

    scroll = e9ui_scroll_make(view);
    state->scrollComp = scroll;
    state->scrollMeta = alloc_strdup("scroll");
    e9ui_child_add(comp, scroll, state->scrollMeta);

    e9ui_component_t *button_stack = emu_buttonStackMake();
    e9ui_setAutoHide(button_stack, 1, 64);
    e9ui_setFocusTarget(button_stack, view);
    state->buttonStackMeta = alloc_strdup("button_stack");
    e9ui_child_add(comp, button_stack, state->buttonStackMeta);

    state->zoomOutBinding.deltaPercent = -EMU_ZOOM_STEP_PERCENT;
    state->zoomOutBinding.state = state;
    state->zoomInBinding.deltaPercent = EMU_ZOOM_STEP_PERCENT;
    state->zoomInBinding.state = state;

    e9ui_component_t *btnZoomOut = e9ui_button_make("-", emu_adjustZoom, &state->zoomOutBinding);
    e9ui_button_setMini(btnZoomOut, 1);
    e9ui_setFocusTarget(btnZoomOut, view);
    state->zoomOutBtnMeta = alloc_strdup("zoom_out");
    e9ui_child_add(button_stack, btnZoomOut, state->zoomOutBtnMeta);

    e9ui_component_t *btnZoomIn = e9ui_button_make("+", emu_adjustZoom, &state->zoomInBinding);
    e9ui_button_setMini(btnZoomIn, 1);
    e9ui_setFocusTarget(btnZoomIn, view);
    state->zoomInBtnMeta = alloc_strdup("zoom_in");
    e9ui_child_add(button_stack, btnZoomIn, state->zoomInBtnMeta);

    target->emu->createOverlays(view, button_stack);
    
    e9ui_component_t *btn_shader = e9ui_button_make("CRT", emu_toggleShaderUi, view);
    e9ui_button_setMini(btn_shader, 1);
    e9ui_setFocusTarget(btn_shader, view);
    state->shaderUiBtnMeta = alloc_strdup("shader_ui");
    e9ui_child_add(button_stack, btn_shader, state->shaderUiBtnMeta);

    e9ui_component_t *seek = e9ui_seek_bar_make();
    e9ui_seek_bar_setMargins(seek, 30, 30, 10);
    e9ui_seek_bar_setHeight(seek, 14);
    e9ui_seek_bar_setHoverMargin(seek, 18);
    e9ui_seek_bar_setCallback(seek, emu_seekBarChanged, NULL);
    e9ui_seek_bar_setDragCallback(seek, emu_seekBarDragging, seek);
    e9ui_seek_bar_setTooltipCallback(seek, emu_seekTooltip, NULL);
    seek->disabled = state_buffer_isRollingPaused() ? 1 : 0;
    if (seek->disabled) {
        e9ui_setHidden(seek, 1);
        state->seekBarForcedHidden = 1;
    } else {
        state->seekBarForcedHidden = 0;
    }
    e9ui_setAutoHide(seek, 1, e9ui_seek_bar_getHoverMargin(seek));
    state->seekBarMeta = alloc_strdup("seek_bar");
    e9ui_child_add(view, seek, state->seekBarMeta);

    if (target && target->emu && target->emu->rangeBarCount && target->emu->rangeBarDescribe) {
        size_t count = target->emu->rangeBarCount();
        if (count > EMU_RANGE_BAR_MAX) {
            count = EMU_RANGE_BAR_MAX;
        }
        state->rangeBarCount = count;
        for (size_t i = 0; i < state->rangeBarCount; ++i) {
            emu_range_bar_desc_t desc;
            e9ui_component_t *rangeBar = NULL;
            memset(&desc, 0, sizeof(desc));
            if (!target->emu->rangeBarDescribe(i, &desc)) {
                continue;
            }
            if (!desc.metaKey || !*desc.metaKey) {
                continue;
            }
            rangeBar = e9ui_range_bar_make();
            e9ui_range_bar_setSide(rangeBar, (e9ui_range_bar_side_t)desc.side);
            e9ui_range_bar_setMargins(rangeBar, desc.marginTop, desc.marginBottom, desc.marginSide);
            e9ui_range_bar_setWidth(rangeBar, desc.width);
            e9ui_range_bar_setHoverMargin(rangeBar, desc.hoverMargin);
            state->rangeBarBindings[i].index = i;
            e9ui_range_bar_setCallback(rangeBar, emu_rangeBarChanged, &state->rangeBarBindings[i]);
            e9ui_range_bar_setDragCallback(rangeBar, emu_rangeBarDragging, &state->rangeBarBindings[i]);
            e9ui_range_bar_setTooltipCallback(rangeBar, emu_rangeBarTooltip, &state->rangeBarBindings[i]);
            e9ui_setAutoHide(rangeBar, 1, e9ui_range_bar_getHoverMargin(rangeBar));
            state->rangeBarMeta[i] = alloc_strdup(desc.metaKey);
            e9ui_child_add(view, rangeBar, state->rangeBarMeta[i]);
        }
    }
    return comp;
}
