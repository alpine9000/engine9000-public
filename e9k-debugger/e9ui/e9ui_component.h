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
#include <stdio.h>
#include "e9ui_context.h"
#include "e9ui_types.h"
#include "list.h"

typedef enum e9ui_mouse_action {
    E9UI_MOUSE_ACTION_MOVE,
    E9UI_MOUSE_ACTION_DOWN,
    E9UI_MOUSE_ACTION_UP
} e9ui_mouse_action_t;

typedef enum e9ui_mouse_button {
    E9UI_MOUSE_BUTTON_NONE = 0,
    E9UI_MOUSE_BUTTON_LEFT = 1,
    E9UI_MOUSE_BUTTON_MIDDLE = 2,
    E9UI_MOUSE_BUTTON_RIGHT = 3,
    E9UI_MOUSE_BUTTON_OTHER = 4
} e9ui_mouse_button_t;

typedef struct e9ui_mouse_event {
    e9ui_mouse_action_t action;
    e9ui_mouse_button_t button;
    int x;
    int y;
    int dx;
    int dy;
} e9ui_mouse_event_t;

typedef void (*e9ui_mouse_callback_t)(struct e9ui_component *self, e9ui_context_t *ctx,
                                       const e9ui_mouse_event_t *mouse_ev);


typedef struct e9ui_component {
  const char *name;
  void       *state;
  e9ui_rect_t bounds;
  const char *persist_id;
  const char *tooltip;
  int focusable;
  
  list_t* children;
  int  (*init)(struct e9ui_component *self, e9ui_context_t *ctx);
  int  (*preferredHeight)(struct e9ui_component *self, e9ui_context_t *ctx, int availW);
  void (*layout)(struct e9ui_component *self, e9ui_context_t *ctx, e9ui_rect_t bounds);
  void (*render)(struct e9ui_component *self, e9ui_context_t *ctx);
  int  (*handleEvent)(struct e9ui_component *self, e9ui_context_t *ctx, const e9ui_event_t *ev);
  void (*dtor)(struct e9ui_component *self, e9ui_context_t *ctx);
  void (*persistSave)(struct e9ui_component *self, e9ui_context_t *ctx, FILE *f);
  void (*persistLoad)(struct e9ui_component *self, e9ui_context_t *ctx, const char *key, const char *value);
  e9ui_mouse_callback_t onHover;
  e9ui_mouse_callback_t onLeave;
  e9ui_mouse_callback_t onClick;
  e9ui_mouse_callback_t onMouseMove;
  e9ui_mouse_callback_t onMouseDown;
  e9ui_mouse_callback_t onMouseUp;
  int mouseInside;
  int mousePressed;
  int collapsed;
  int collapsedHeight;

  int autoHide;
  int autoHideMargin;
  int autoHideHasClip;
  e9ui_rect_t autoHideClip;

  struct e9ui_component *focusTarget;
  
  int disabled;
  const int *disabledVariable;
  int disableWhenTrue;
  
  int _hidden;  
  const int  *hiddenVariable;
  int  hiddenWhenTrue;    
} e9ui_component_t;

typedef struct {
  e9ui_component_t* component;
  void* meta;
} e9ui_component_child_t;




