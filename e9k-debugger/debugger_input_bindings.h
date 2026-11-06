/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <SDL.h>

#define DEBUGGER_INPUT_BINDINGS_AMIGA_JOYSTICK_OPTION "e9k_debugger_amiga_keyboard_joystick"
#define DEBUGGER_INPUT_BINDINGS_UNBOUND_VALUE "sdlk:0"

typedef struct debugger_input_bindings_spec
{
    const char *optionKey;
    const char *label;
    const char *info;
    unsigned port;
    unsigned joypadId;
} debugger_input_bindings_spec_t;

size_t
debugger_input_bindings_specCount(void);

const debugger_input_bindings_spec_t *
debugger_input_bindings_specAt(size_t index);

const debugger_input_bindings_spec_t *
debugger_input_bindings_findSpec(const char *optionKey);

int
debugger_input_bindings_isOptionKey(const char *optionKey);

const char *
debugger_input_bindings_categoryKey(void);

const char *
debugger_input_bindings_categoryLabel(void);

const char *
debugger_input_bindings_categoryInfo(void);

SDL_Keycode
debugger_input_bindings_defaultKeyForTarget(int targetIndex, const char *optionKey);

int
debugger_input_bindings_buildStoredValue(SDL_Keycode key, char *out, size_t outCap);

int
debugger_input_bindings_parseStoredValue(const char *value, SDL_Keycode *outKey);

void
debugger_input_bindings_formatDisplayValue(const char *value, char *out, size_t outCap);

int
debugger_input_bindings_mapKeyToJoypad(int targetIndex,
                                       const char *(*getValue)(const char *key),
                                       SDL_Keycode key,
                                       unsigned *outId);

int
debugger_input_bindings_mapKeyToJoypadPort(int targetIndex,
                                           const char *(*getValue)(const char *key),
                                           SDL_Keycode key,
                                           unsigned *outPort,
                                           unsigned *outId);
