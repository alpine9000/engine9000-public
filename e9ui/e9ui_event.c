/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include <string.h>

static int
e9ui_event_pointInBounds(const e9ui_component_t *comp, int x, int y)
{
    if (!comp) {
        return 0;
    }
    return x >= comp->bounds.x && x < comp->bounds.x + comp->bounds.w &&
           y >= comp->bounds.y && y < comp->bounds.y + comp->bounds.h;
}

int
e9ui_event_componentContainsBlockingWindow(e9ui_component_t *comp)
{
    if (!comp || e9ui_getHidden(comp)) {
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
        if (e9ui_event_componentContainsBlockingWindow(it->child)) {
            return 1;
        }
    }
    return 0;
}

static int
e9ui_event_componentTreeHasMousePress(e9ui_component_t *comp)
{
    if (!comp || e9ui_getHidden(comp)) {
        return 0;
    }
    if (comp->mousePressed) {
        return 1;
    }

    e9ui_child_iterator iter;
    if (!e9ui_child_iterateChildren(comp, &iter)) {
        return 0;
    }
    for (e9ui_child_iterator *it = e9ui_child_interateNext(&iter);
         it;
         it = e9ui_child_interateNext(&iter)) {
        if (!it->child) {
            continue;
        }
        if (e9ui_event_componentTreeHasMousePress(it->child)) {
            return 1;
        }
    }
    return 0;
}

static int
e9ui_event_pointerIsInsideComponent(const e9ui_component_t *comp, const e9ui_event_t *ev)
{
    if (!comp || !ev) {
        return 0;
    }
    switch (ev->type) {
    case SDL_MOUSEMOTION:
        return e9ui_event_pointInBounds(comp, ev->motion.x, ev->motion.y);
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        return e9ui_event_pointInBounds(comp, ev->button.x, ev->button.y);
    default:
        break;
    }
    return 0;
}

static e9ui_mouse_button_t
e9ui_event_translateMouseButton(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return E9UI_MOUSE_BUTTON_LEFT;
    case SDL_BUTTON_MIDDLE: return E9UI_MOUSE_BUTTON_MIDDLE;
    case SDL_BUTTON_RIGHT: return E9UI_MOUSE_BUTTON_RIGHT;
    default: return E9UI_MOUSE_BUTTON_OTHER;
    }
}

static int
e9ui_event_assignFocusForMouse(e9ui_component_t *comp, e9ui_context_t *ctx)
{
    if (!ctx || !comp) {
        return 0;
    }
    if (ctx->focusClickHandled) {
        return 0;
    }
    e9ui_component_t *before = e9ui_getFocus(ctx);
    e9ui_component_t *target = comp->focusTarget ? comp->focusTarget : comp;
    if (target && target->focusable) {
        ctx->focusClickHandled = 1;
        e9ui_setFocus(ctx, target);
        return before != e9ui_getFocus(ctx);
    } else if (comp->focusTarget) {
        ctx->focusClickHandled = 1;
        return 0;
    } else if (comp->focusable) {
        ctx->focusClickHandled = 1;
        e9ui_setFocus(ctx, comp);
        return before != e9ui_getFocus(ctx);
    }
    return 0;
}

static int
e9ui_event_processMouseCallbacks(e9ui_component_t *comp, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!comp || !ctx || !ev || (comp && comp->disabled)) {
        return 0;
    }

    e9ui_mouse_event_t mouse_ev = {0};
    int inside = 0;
    int consumed = 0;
    switch (ev->type) {
    case SDL_MOUSEMOTION:
        mouse_ev.action = E9UI_MOUSE_ACTION_MOVE;
        mouse_ev.x = ev->motion.x;
        mouse_ev.y = ev->motion.y;
        mouse_ev.dx = ev->motion.xrel;
        mouse_ev.dy = ev->motion.yrel;
        mouse_ev.button = E9UI_MOUSE_BUTTON_NONE;
        mouse_ev.clicks = 0;
        inside = e9ui_event_pointInBounds(comp, mouse_ev.x, mouse_ev.y);
        if (inside && !comp->mouseInside) {
            comp->mouseInside = 1;
            if (comp->onHover) {
                comp->onHover(comp, ctx, &mouse_ev);
            }
        } else if (!inside && comp->mouseInside) {
            comp->mouseInside = 0;
            if (comp->onLeave) {
                comp->onLeave(comp, ctx, &mouse_ev);
            }
        }
        if ((inside || comp->mousePressed) && comp->onMouseMove) {
            comp->onMouseMove(comp, ctx, &mouse_ev);
        }
        consumed = (inside || comp->mousePressed) ? 1 : 0;
        break;
    case SDL_MOUSEBUTTONDOWN:
        mouse_ev.action = E9UI_MOUSE_ACTION_DOWN;
        mouse_ev.x = ev->button.x;
        mouse_ev.y = ev->button.y;
        mouse_ev.button = e9ui_event_translateMouseButton(ev->button.button);
        mouse_ev.clicks = ev->button.clicks;
        inside = e9ui_event_pointInBounds(comp, mouse_ev.x, mouse_ev.y);
        if (inside) {
            comp->mouseInside = 1;
            int focusChanged = e9ui_event_assignFocusForMouse(comp, ctx);
            int skipMouseDown = 0;
            if (focusChanged && comp->name && strcmp(comp->name, "e9ui_textbox") == 0) {
                skipMouseDown = 1;
            }
            if (comp->onMouseDown && !skipMouseDown) {
                comp->onMouseDown(comp, ctx, &mouse_ev);
            }
            if ((comp->onClick || comp->onMouseMove || comp->onMouseUp) && !comp->mousePressed) {
                comp->mousePressed = 1;
                comp->mousePressedButton = mouse_ev.button;
                comp->mousePressedX = mouse_ev.x;
                comp->mousePressedY = mouse_ev.y;
            }
            consumed = 1;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        mouse_ev.action = E9UI_MOUSE_ACTION_UP;
        mouse_ev.x = ev->button.x;
        mouse_ev.y = ev->button.y;
        mouse_ev.button = e9ui_event_translateMouseButton(ev->button.button);
        mouse_ev.clicks = ev->button.clicks;
        inside = e9ui_event_pointInBounds(comp, mouse_ev.x, mouse_ev.y);
        int wasPressed = comp->mousePressed && comp->mousePressedButton == mouse_ev.button;
        if (wasPressed && comp->onMouseUp) {
            comp->onMouseUp(comp, ctx, &mouse_ev);
        }
        if (wasPressed) {
            comp->mousePressed = 0;
            comp->mousePressedButton = E9UI_MOUSE_BUTTON_NONE;
            if (inside && comp->onClick && mouse_ev.button == E9UI_MOUSE_BUTTON_LEFT) {
                comp->onClick(comp, ctx, &mouse_ev);
            }
            consumed = 1;
        }
        break;
    default:
        break;
    }
    return consumed;
}

static int
e9ui_event_processChildren(e9ui_component_t *comp, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!comp) {
        return 0;
    }
    if (comp->collapsed) {
        return 0;
    }
    if (ev &&
        ev->type == SDL_MOUSEBUTTONDOWN &&
        !e9ui_event_pointerIsInsideComponent(comp, ev)) {
        return 0;
    }
    int allow_multiple = 0;
    if (ev && (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONUP)) {
        allow_multiple = 1;
    }
    int hasPointer = 0;
    if (ev) {
        if (ev->type == SDL_MOUSEMOTION) {
            hasPointer = 1;
        } else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
            hasPointer = 1;
        } else if (ev->type == SDL_MOUSEWHEEL) {
            hasPointer = 1;
        }
    }
    int consumed = 0;
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
        if ((ev->type == SDL_MOUSEMOTION ||
             ev->type == SDL_MOUSEBUTTONDOWN ||
             ev->type == SDL_MOUSEBUTTONUP) &&
            it->child->name &&
            strcmp(it->child->name, "e9ui_scroll") == 0 &&
            !e9ui_event_pointerIsInsideComponent(it->child, ev) &&
            !e9ui_event_componentTreeHasMousePress(it->child)) {
            continue;
        }
        if (e9ui_event_process(it->child, ctx, ev)) {
            consumed = 1;
            if (hasPointer && e9ui_event_componentContainsBlockingWindow(it->child)) {
                return 1;
            }
            if (!allow_multiple) {
                return 1;
            }
        }
    }
    return consumed;
}

int
e9ui_event_process(e9ui_component_t *comp, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!comp || e9ui_getHidden(comp)) {
        return 0;
    }

    int consumed = 0;
    if (e9ui_event_processChildren(comp, ctx, ev)) {
        consumed = 1;
    }
    if (comp->handleEvent) {
        if (comp->handleEvent(comp, ctx, ev)) {
            consumed = 1;
        }
    }
    if (e9ui_event_processMouseCallbacks(comp, ctx, ev)) {
        consumed = 1;
    }
    return consumed;
}
