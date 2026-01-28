/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"

typedef struct {
  uint32_t (*translateCharacter)(SDL_Keycode key, SDL_Keymod mod);
  uint16_t (*translateModifiers)(SDL_Keymod mod);
  unsigned (*translateKey)(SDL_Keycode key);
  int (*mapKeyToJoypad)(SDL_Keycode key, unsigned *id);
  void (*createOverlays)(e9ui_component_t* comp, e9ui_component_t* button_stack);
  void (*render)(e9ui_context_t *ctx, SDL_Rect* dst);
} emu_system_iface_t;

e9ui_component_t *
emu_makeComponent(void);


