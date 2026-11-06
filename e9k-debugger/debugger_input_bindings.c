/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debugger_input_bindings.h"
#include "libretro.h"
#include "rom_config.h"
#include "target.h"

#define DEBUGGER_INPUT_BINDINGS_VALUE_PREFIX "sdlk:"

static const char *debugger_input_bindings_categoryKeyValue = "input";
static const char *debugger_input_bindings_categoryLabelValue = "Input";
static const char *debugger_input_bindings_categoryInfoValue =
    "e9k-debugger keyboard bindings for the emulated controller. Click a binding and press a key to set it.";

static const debugger_input_bindings_spec_t debugger_input_bindings_specs[] = {
    {
        .optionKey = "e9k_debugger_input_dpad_up",
        .label = "D-Pad Up",
        .info = "Debugger keyboard binding for controller Up. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_UP
    },
    {
        .optionKey = "e9k_debugger_input_dpad_down",
        .label = "D-Pad Down",
        .info = "Debugger keyboard binding for controller Down. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_DOWN
    },
    {
        .optionKey = "e9k_debugger_input_dpad_left",
        .label = "D-Pad Left",
        .info = "Debugger keyboard binding for controller Left. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_LEFT
    },
    {
        .optionKey = "e9k_debugger_input_dpad_right",
        .label = "D-Pad Right",
        .info = "Debugger keyboard binding for controller Right. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_RIGHT
    },
    {
        .optionKey = "e9k_debugger_input_button_a",
        .label = "Button A",
        .info = "Debugger keyboard binding for controller A. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_A
    },
    {
        .optionKey = "e9k_debugger_input_button_b",
        .label = "Button B",
        .info = "Debugger keyboard binding for controller B. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_B
    },
    {
        .optionKey = "e9k_debugger_input_button_x",
        .label = "Button X",
        .info = "Debugger keyboard binding for controller X. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_X
    },
    {
        .optionKey = "e9k_debugger_input_button_y",
        .label = "Button Y",
        .info = "Debugger keyboard binding for controller Y. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_Y
    },
    {
        .optionKey = "e9k_debugger_input_button_l",
        .label = "Button L",
        .info = "Debugger keyboard binding for controller L. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_L
    },
    {
        .optionKey = "e9k_debugger_input_button_r",
        .label = "Button R",
        .info = "Debugger keyboard binding for controller R. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_R
    },
    {
        .optionKey = "e9k_debugger_input_start",
        .label = "Start",
        .info = "Debugger keyboard binding for controller Start. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_START
    },
    {
        .optionKey = "e9k_debugger_input_select",
        .label = "Select",
        .info = "Debugger keyboard binding for controller Select. Click the binding and press a key to set it.",
        .port = 0,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_SELECT
    },
    {
        .optionKey = "e9k_debugger_input_p2_dpad_up",
        .label = "P2 D-Pad Up",
        .info = "Debugger keyboard binding for player 2 controller Up. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_UP
    },
    {
        .optionKey = "e9k_debugger_input_p2_dpad_down",
        .label = "P2 D-Pad Down",
        .info = "Debugger keyboard binding for player 2 controller Down. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_DOWN
    },
    {
        .optionKey = "e9k_debugger_input_p2_dpad_left",
        .label = "P2 D-Pad Left",
        .info = "Debugger keyboard binding for player 2 controller Left. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_LEFT
    },
    {
        .optionKey = "e9k_debugger_input_p2_dpad_right",
        .label = "P2 D-Pad Right",
        .info = "Debugger keyboard binding for player 2 controller Right. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_RIGHT
    },
    {
        .optionKey = "e9k_debugger_input_p2_button_a",
        .label = "P2 Button A",
        .info = "Debugger keyboard binding for player 2 controller A. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_A
    },
    {
        .optionKey = "e9k_debugger_input_p2_button_b",
        .label = "P2 Button B",
        .info = "Debugger keyboard binding for player 2 controller B. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_B
    },
    {
        .optionKey = "e9k_debugger_input_p2_button_x",
        .label = "P2 Button X",
        .info = "Debugger keyboard binding for player 2 controller X. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_X
    },
    {
        .optionKey = "e9k_debugger_input_p2_button_y",
        .label = "P2 Button Y",
        .info = "Debugger keyboard binding for player 2 controller Y. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_Y
    },
    {
        .optionKey = "e9k_debugger_input_p2_button_l",
        .label = "P2 Button L",
        .info = "Debugger keyboard binding for player 2 controller L. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_L
    },
    {
        .optionKey = "e9k_debugger_input_p2_button_r",
        .label = "P2 Button R",
        .info = "Debugger keyboard binding for player 2 controller R. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_R
    },
    {
        .optionKey = "e9k_debugger_input_p2_start",
        .label = "P2 Start",
        .info = "Debugger keyboard binding for player 2 controller Start. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_START
    },
    {
        .optionKey = "e9k_debugger_input_p2_select",
        .label = "P2 Select",
        .info = "Debugger keyboard binding for player 2 controller Select. Click the binding and press a key to set it.",
        .port = 1,
        .joypadId = RETRO_DEVICE_ID_JOYPAD_SELECT
    }
};

static void
debugger_input_bindings_applyDisplayCase(char *s)
{
    if (!s) {
        return;
    }
    char *tokenStart = s;
    while (*tokenStart) {
        char *tokenEnd = tokenStart;
        while (*tokenEnd && *tokenEnd != '+') {
            ++tokenEnd;
        }
        size_t tokenLen = (size_t)(tokenEnd - tokenStart);
        int isSingleAlpha = (tokenLen == 1 && isalpha((unsigned char)tokenStart[0])) ? 1 : 0;
        for (char *p = tokenStart; p < tokenEnd; ++p) {
            if (!isalpha((unsigned char)*p)) {
                continue;
            }
            if (isSingleAlpha) {
                *p = (char)tolower((unsigned char)*p);
            } else {
                *p = (char)toupper((unsigned char)*p);
            }
        }
        if (*tokenEnd == '+') {
            tokenStart = tokenEnd + 1;
        } else {
            break;
        }
    }
}

static int
debugger_input_bindings_keysMatch(SDL_Keycode configuredKey, SDL_Keycode pressedKey)
{
    if (configuredKey == pressedKey) {
        return 1;
    }
    if ((configuredKey == SDLK_LGUI || configuredKey == SDLK_RGUI) &&
        (pressedKey == SDLK_LGUI || pressedKey == SDLK_RGUI)) {
        return 1;
    }
    return 0;
}

static SDL_Keycode
debugger_input_bindings_defaultKeyForJoypad(int targetIndex, unsigned joypadId)
{
    switch (joypadId) {
    case RETRO_DEVICE_ID_JOYPAD_UP:
        return SDLK_UP;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:
        return SDLK_DOWN;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:
        return SDLK_LEFT;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:
        return SDLK_RIGHT;
    case RETRO_DEVICE_ID_JOYPAD_A:
        return SDLK_LALT;
    case RETRO_DEVICE_ID_JOYPAD_B:
        return SDLK_LCTRL;
    case RETRO_DEVICE_ID_JOYPAD_X:
        if (targetIndex == TARGET_AMIGA) {
            return SDLK_UNKNOWN;
        }
        return SDLK_LSHIFT;
    case RETRO_DEVICE_ID_JOYPAD_Y:
        if (targetIndex == TARGET_AMIGA) {
            return SDLK_UNKNOWN;
        }
        return SDLK_SPACE;
    case RETRO_DEVICE_ID_JOYPAD_START:
        if (targetIndex == TARGET_AMIGA) {
            return SDLK_UNKNOWN;
        }
        return SDLK_1;
    case RETRO_DEVICE_ID_JOYPAD_SELECT:
        if (targetIndex == TARGET_AMIGA) {
            return SDLK_UNKNOWN;
        }
        return SDLK_5;
    case RETRO_DEVICE_ID_JOYPAD_L:
    case RETRO_DEVICE_ID_JOYPAD_R:
    default:
        break;
    }
    return SDLK_UNKNOWN;
}

size_t
debugger_input_bindings_specCount(void)
{
    return sizeof(debugger_input_bindings_specs) / sizeof(debugger_input_bindings_specs[0]);
}

const debugger_input_bindings_spec_t *
debugger_input_bindings_specAt(size_t index)
{
    if (index >= debugger_input_bindings_specCount()) {
        return NULL;
    }
    return &debugger_input_bindings_specs[index];
}

const debugger_input_bindings_spec_t *
debugger_input_bindings_findSpec(const char *optionKey)
{
    if (!optionKey || !*optionKey) {
        return NULL;
    }
    for (size_t i = 0; i < debugger_input_bindings_specCount(); ++i) {
        const debugger_input_bindings_spec_t *spec = &debugger_input_bindings_specs[i];
        if (spec->optionKey && strcmp(spec->optionKey, optionKey) == 0) {
            return spec;
        }
    }
    return NULL;
}

int
debugger_input_bindings_isOptionKey(const char *optionKey)
{
    return debugger_input_bindings_findSpec(optionKey) ? 1 : 0;
}

const char *
debugger_input_bindings_categoryKey(void)
{
    return debugger_input_bindings_categoryKeyValue;
}

const char *
debugger_input_bindings_categoryLabel(void)
{
    return debugger_input_bindings_categoryLabelValue;
}

const char *
debugger_input_bindings_categoryInfo(void)
{
    return debugger_input_bindings_categoryInfoValue;
}

SDL_Keycode
debugger_input_bindings_defaultKeyForTarget(int targetIndex, const char *optionKey)
{
    const debugger_input_bindings_spec_t *spec = debugger_input_bindings_findSpec(optionKey);
    if (!spec) {
        return SDLK_UNKNOWN;
    }
    if (spec->port != 0) {
        return SDLK_UNKNOWN;
    }
    return debugger_input_bindings_defaultKeyForJoypad(targetIndex, spec->joypadId);
}

int
debugger_input_bindings_buildStoredValue(SDL_Keycode key, char *out, size_t outCap)
{
    if (!out || outCap == 0) {
        return 0;
    }
    if (key == SDLK_UNKNOWN || key == 0) {
        out[0] = '\0';
        return 1;
    }
    int written = snprintf(out, outCap, "%s%d", DEBUGGER_INPUT_BINDINGS_VALUE_PREFIX, (int)key);
    if (written < 0 || (size_t)written >= outCap) {
        out[outCap - 1] = '\0';
        return 0;
    }
    return 1;
}

int
debugger_input_bindings_parseStoredValue(const char *value, SDL_Keycode *outKey)
{
    if (outKey) {
        *outKey = SDLK_UNKNOWN;
    }
    if (!value || !*value) {
        return 0;
    }

    size_t prefixLen = strlen(DEBUGGER_INPUT_BINDINGS_VALUE_PREFIX);
    if (strncmp(value, DEBUGGER_INPUT_BINDINGS_VALUE_PREFIX, prefixLen) == 0) {
        char *end = NULL;
        long parsed = strtol(value + prefixLen, &end, 10);
        if (end && *end == '\0') {
            if (outKey) {
                *outKey = (SDL_Keycode)parsed;
            }
            return 1;
        }
        return 0;
    }

    SDL_Keycode fromName = SDL_GetKeyFromName(value);
    if (fromName == SDLK_UNKNOWN) {
        return 0;
    }
    if (outKey) {
        *outKey = fromName;
    }
    return 1;
}

void
debugger_input_bindings_formatDisplayValue(const char *value, char *out, size_t outCap)
{
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';

    SDL_Keycode key = SDLK_UNKNOWN;
    if (!debugger_input_bindings_parseStoredValue(value, &key) || key == SDLK_UNKNOWN) {
        return;
    }

    const char *name = SDL_GetKeyName(key);
    if (!name || !*name) {
        snprintf(out, outCap, "Key %d", (int)key);
        out[outCap - 1] = '\0';
        return;
    }
    snprintf(out, outCap, "%s", name);
    out[outCap - 1] = '\0';
    debugger_input_bindings_applyDisplayCase(out);
}

int
debugger_input_bindings_mapKeyToJoypadPort(int targetIndex,
                                           const char *(*getValue)(const char *key),
                                           SDL_Keycode key,
                                           unsigned *outPort,
                                           unsigned *outId)
{
    if (!outId || !outPort) {
        return 0;
    }

    if (targetIndex == TARGET_AMIGA && getValue) {
        const char *enabled = getValue(DEBUGGER_INPUT_BINDINGS_AMIGA_JOYSTICK_OPTION);
        if (enabled && strcmp(enabled, "disabled") == 0) {
            return 0;
        }
    }

    for (size_t i = 0; i < debugger_input_bindings_specCount(); ++i) {
        const debugger_input_bindings_spec_t *spec = &debugger_input_bindings_specs[i];
        SDL_Keycode boundKey = SDLK_UNKNOWN;
        const char *storedValue = rom_config_getActiveInputBindingValue(spec->optionKey);
        if ((!storedValue || !*storedValue) && getValue) {
            storedValue = getValue(spec->optionKey);
        }
        if (!debugger_input_bindings_parseStoredValue(storedValue, &boundKey)) {
            boundKey = debugger_input_bindings_defaultKeyForTarget(targetIndex, spec->optionKey);
        }
        if (boundKey == SDLK_UNKNOWN || boundKey == 0) {
            continue;
        }
        if (!debugger_input_bindings_keysMatch(boundKey, key)) {
            continue;
        }

        *outPort = spec->port;
        *outId = spec->joypadId;
        return 1;
    }

    return 0;
}

int
debugger_input_bindings_mapKeyToJoypad(int targetIndex,
                                       const char *(*getValue)(const char *key),
                                       SDL_Keycode key,
                                       unsigned *outId)
{
    unsigned port = 0u;
    return debugger_input_bindings_mapKeyToJoypadPort(targetIndex, getValue, key, &port, outId);
}
