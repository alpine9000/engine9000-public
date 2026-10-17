/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"

typedef struct emu_range_bar_desc
{
  const char *metaKey;
  int side;
  int marginTop;
  int marginBottom;
  int marginSide;
  int width;
  int hoverMargin;
} emu_range_bar_desc_t;

typedef struct {
  uint32_t (*translateCharacter)(SDL_Keycode key, SDL_Keymod mod);
  uint16_t (*translateModifiers)(SDL_Keymod mod);
  unsigned (*translateKey)(SDL_Keycode key);
  int (*mapKeyToJoypad)(SDL_Keycode key, unsigned *id);
  int mouseCaptureSupported;
  int (*mouseCaptureOnClickEnabled)(void);
  size_t (*rangeBarCount)(void);
  int (*rangeBarDescribe)(size_t index, emu_range_bar_desc_t *outDesc);
  void (*rangeBarChanged)(size_t index, float startPercent, float endPercent);
  void (*rangeBarDragging)(size_t index, int dragging, float startPercent, float endPercent);
  void (*rangeBarTooltip)(size_t index, float startPercent, float endPercent, char *out, size_t cap);
  int (*rangeBarSync)(size_t index, e9ui_component_t *bar);
  int (*handleOverlayEvent)(e9ui_context_t *ctx, const SDL_Rect *dst, const e9ui_event_t *ev);
  void (*adjustVideoBounds)(e9ui_rect_t *bounds);
  void (*adjustVideoDst)(SDL_Rect *dst);
  void (*createOverlays)(e9ui_component_t* comp, e9ui_component_t* button_stack);
  void (*render)(e9ui_context_t *ctx, SDL_Rect* dst, const SDL_Rect *clipRect);
  void (*renderForeground)(e9ui_context_t *ctx, SDL_Rect* dst);
  void (*destroy)(void);
} emu_system_iface_t;

e9ui_component_t *
emu_makeComponent(void);

int
emu_handleCapturedMouseEvent(e9ui_context_t *ctx, const SDL_Event *ev);

int
emu_mouseCaptureStart(e9ui_context_t *ctx);

int
emu_mouseCaptureRelease(e9ui_context_t *ctx);
