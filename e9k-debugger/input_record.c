/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "input_record.h"
#include "alloc.h"
#include "debug.h"
#include "libretro_host.h"
#include "profile_checkpoints.h"

typedef enum input_record_type {
    INPUT_RECORD_JOYPAD = 1,
    INPUT_RECORD_KEY,
    INPUT_RECORD_CLEAR,
    INPUT_RECORD_UIKEY,
    INPUT_RECORD_UIKEY_EVENT,
    INPUT_RECORD_TEXT_INPUT,
    INPUT_RECORD_MOUSE_MOTION,
    INPUT_RECORD_MOUSE_BUTTON,
    INPUT_RECORD_MOUSE_WHEEL,
    INPUT_RECORD_CORE_MOUSE_MOTION,
    INPUT_RECORD_CORE_MOUSE_BUTTON
} input_record_type_t;

typedef struct input_record_event {
    uint64_t frame;
    input_record_type_t type;
    unsigned port;
    unsigned id;
    int pressed;
    unsigned keycode;
    uint32_t character;
    uint16_t modifiers;
    int repeat;
    int x;
    int y;
    int xrel;
    int yrel;
    int wheelX;
    int wheelY;
    int wheelDirection;
    int mousePort;
    uint8_t textLen;
    char text[32];
} input_record_event_t;

static char input_record_recordPath[4096];
static char input_record_playbackPath[4096];
static char input_record_uiEventPath[4096];
static FILE *input_record_out = NULL;
static FILE *input_record_uiEventOut = NULL;
static input_record_event_t *input_record_events = NULL;
static size_t input_record_eventCount = 0;
static size_t input_record_eventCap = 0;
static size_t input_record_eventIndexCore = 0;
static size_t input_record_eventIndexUi = 0;
static int input_record_mode = 0;
static int input_record_injecting = 0;
typedef struct input_record_ui_event {
    uint64_t frame;
    SDL_Event ev;
} input_record_ui_event_t;

static input_record_ui_event_t *input_record_uiEvents = NULL;
static size_t input_record_uiEventCount = 0;
static size_t input_record_uiEventCap = 0;
static size_t input_record_uiEventIndex = 0;
static int input_record_uiEventMode = 0;
static SDL_Event *input_record_uiQueue = NULL;
static size_t input_record_uiQueueCap = 0;
static size_t input_record_uiQueueCount = 0;
static size_t input_record_uiQueueHead = 0;
static int input_record_useUiEventQueue = 0;

static const char input_record_uiEventHeader[] = "E9K_UI_EVENT_V1\n";

static int
input_record_growUiQueue(size_t needed)
{
    if (input_record_uiQueueCap >= needed) {
        return 1;
    }
    size_t next = input_record_uiQueueCap ? input_record_uiQueueCap * 2 : 256;
    while (next < needed) {
        next *= 2;
    }
    SDL_Event *buf = (SDL_Event *)alloc_realloc(NULL, next * sizeof(*buf));
    if (!buf) {
        return 0;
    }
    if (input_record_uiQueueCount > 0 && input_record_uiQueueCap > 0) {
        for (size_t i = 0; i < input_record_uiQueueCount; ++i) {
            size_t index = (input_record_uiQueueHead + i) % input_record_uiQueueCap;
            buf[i] = input_record_uiQueue[index];
        }
    }
    if (input_record_uiQueue) {
        alloc_free(input_record_uiQueue);
    }
    input_record_uiQueue = buf;
    input_record_uiQueueCap = next;
    input_record_uiQueueHead = 0;
    return 1;
}

static void
input_record_queueUiEvent(const SDL_Event *ev)
{
    if (!ev) {
        return;
    }
    if (!input_record_growUiQueue(input_record_uiQueueCount + 1)) {
        return;
    }
    size_t index = (input_record_uiQueueHead + input_record_uiQueueCount) % input_record_uiQueueCap;
    input_record_uiQueue[index] = *ev;
    input_record_uiQueueCount++;
}

void
input_record_setUiEventQueueMode(int enabled)
{
    input_record_useUiEventQueue = enabled ? 1 : 0;
    input_record_uiQueueCount = 0;
    input_record_uiQueueHead = 0;
}

int
input_record_pollUiEvent(SDL_Event *ev)
{
    if (!ev) {
        return 0;
    }
    if (input_record_uiQueueCount == 0 || input_record_uiQueueCap == 0) {
        return 0;
    }
    *ev = input_record_uiQueue[input_record_uiQueueHead];
    input_record_uiQueueHead = (input_record_uiQueueHead + 1) % input_record_uiQueueCap;
    input_record_uiQueueCount--;
    if (input_record_uiQueueCount == 0) {
        input_record_uiQueueHead = 0;
    }
    return 1;
}

static void
input_record_pushEvent(const input_record_event_t *ev)
{
    if (!ev) {
        return;
    }
    if (input_record_eventCount == input_record_eventCap) {
        size_t next = input_record_eventCap ? input_record_eventCap * 2 : 256;
        input_record_event_t *buf =
            (input_record_event_t*)alloc_realloc(input_record_events, next * sizeof(*buf));
        if (!buf) {
            return;
        }
        input_record_events = buf;
        input_record_eventCap = next;
    }
    input_record_events[input_record_eventCount++] = *ev;
}

static void
input_record_parseLine(const char *line)
{
    if (!line || !*line) {
        return;
    }
    if (strncmp(line, "E9K_INPUT_V1", 12) == 0) {
        return;
    }
    unsigned long long frame = 0;
    char type = '\0';
    if (sscanf(line, "F %llu %c", &frame, &type) < 2) {
        return;
    }
    input_record_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.frame = (uint64_t)frame;
    if (type == 'J') {
        unsigned port = 0;
        unsigned id = 0;
        int pressed = 0;
        if (sscanf(line, "F %llu J %u %u %d", &frame, &port, &id, &pressed) == 4) {
            ev.type = INPUT_RECORD_JOYPAD;
            ev.port = port;
            ev.id = id;
            ev.pressed = pressed ? 1 : 0;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'K') {
        unsigned keycode = 0;
        unsigned character = 0;
        unsigned modifiers = 0;
        int pressed = 0;
        if (sscanf(line, "F %llu K %u %u %u %d", &frame, &keycode, &character, &modifiers, &pressed) == 5) {
            ev.type = INPUT_RECORD_KEY;
            ev.keycode = keycode;
            ev.character = (uint32_t)character;
            ev.modifiers = (uint16_t)modifiers;
            ev.pressed = pressed ? 1 : 0;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'C') {
        ev.type = INPUT_RECORD_CLEAR;
        input_record_pushEvent(&ev);
    } else if (type == 'U') {
        unsigned keycode = 0;
        int pressed = 0;
        if (sscanf(line, "F %llu U %u %d", &frame, &keycode, &pressed) == 3) {
            ev.type = INPUT_RECORD_UIKEY;
            ev.keycode = keycode;
            ev.pressed = pressed ? 1 : 0;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'S') {
        unsigned keycode = 0;
        unsigned modifiers = 0;
        unsigned repeat = 0;
        int pressed = 0;
        if (sscanf(line, "F %llu S %u %u %u %d", &frame, &keycode, &modifiers, &repeat, &pressed) == 5) {
            ev.type = INPUT_RECORD_UIKEY_EVENT;
            ev.keycode = keycode;
            ev.modifiers = (uint16_t)modifiers;
            ev.repeat = (int)repeat;
            ev.pressed = pressed ? 1 : 0;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'M') {
        int x = 0;
        int y = 0;
        int xrel = 0;
        int yrel = 0;
        if (sscanf(line, "F %llu M %d %d %d %d", &frame, &x, &y, &xrel, &yrel) == 5) {
            ev.type = INPUT_RECORD_MOUSE_MOTION;
            ev.x = x;
            ev.y = y;
            ev.xrel = xrel;
            ev.yrel = yrel;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'B') {
        unsigned button = 0;
        int pressed = 0;
        int x = 0;
        int y = 0;
        if (sscanf(line, "F %llu B %u %d %d %d", &frame, &button, &pressed, &x, &y) == 5) {
            ev.type = INPUT_RECORD_MOUSE_BUTTON;
            ev.id = button;
            ev.pressed = pressed ? 1 : 0;
            ev.x = x;
            ev.y = y;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'W') {
        int wheelX = 0;
        int wheelY = 0;
        int direction = 0;
        int mouseX = 0;
        int mouseY = 0;
        if (sscanf(line, "F %llu W %d %d %d %d %d", &frame, &wheelX, &wheelY, &direction, &mouseX, &mouseY) == 6) {
            ev.type = INPUT_RECORD_MOUSE_WHEEL;
            ev.wheelX = wheelX;
            ev.wheelY = wheelY;
            ev.wheelDirection = direction;
            ev.x = mouseX;
            ev.y = mouseY;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'm') {
        int port = 0;
        int dx = 0;
        int dy = 0;
        if (sscanf(line, "F %llu m %d %d %d", &frame, &port, &dx, &dy) == 4) {
            ev.type = INPUT_RECORD_CORE_MOUSE_MOTION;
            ev.mousePort = port;
            ev.xrel = dx;
            ev.yrel = dy;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'b') {
        int port = 0;
        unsigned button = 0;
        int pressed = 0;
        if (sscanf(line, "F %llu b %d %u %d", &frame, &port, &button, &pressed) == 4) {
            ev.type = INPUT_RECORD_CORE_MOUSE_BUTTON;
            ev.mousePort = port;
            ev.id = button;
            ev.pressed = pressed ? 1 : 0;
            input_record_pushEvent(&ev);
        }
    } else if (type == 'T') {
        const char *cursor = strstr(line, " T ");
        if (!cursor) {
            return;
        }
        cursor += 3;
        char *end = NULL;
        unsigned long len = strtoul(cursor, &end, 10);
        if (!end || end == cursor) {
            return;
        }
        while (*end == ' ') {
            end++;
        }
        if (len > sizeof(ev.text) - 1) {
            len = sizeof(ev.text) - 1;
        }
        size_t hexLen = len * 2;
        size_t available = strlen(end);
        if (available < hexLen) {
            return;
        }
        ev.type = INPUT_RECORD_TEXT_INPUT;
        ev.textLen = (uint8_t)len;
        for (size_t i = 0; i < len; ++i) {
            char hi = end[i * 2];
            char lo = end[i * 2 + 1];
            if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo)) {
                return;
            }
            int value = 0;
            value = (hi <= '9') ? (hi - '0') : ((toupper((unsigned char)hi) - 'A') + 10);
            value <<= 4;
            value |= (lo <= '9') ? (lo - '0') : ((toupper((unsigned char)lo) - 'A') + 10);
            ev.text[i] = (char)value;
        }
        ev.text[len] = '\0';
        input_record_pushEvent(&ev);
    }
}

void
input_record_setRecordPath(const char *path)
{
    if (!path || !*path) {
        input_record_recordPath[0] = '\0';
        return;
    }
    strncpy(input_record_recordPath, path, sizeof(input_record_recordPath) - 1);
    input_record_recordPath[sizeof(input_record_recordPath) - 1] = '\0';
}

void
input_record_setPlaybackPath(const char *path)
{
    if (!path || !*path) {
        input_record_playbackPath[0] = '\0';
        return;
    }
    strncpy(input_record_playbackPath, path, sizeof(input_record_playbackPath) - 1);
    input_record_playbackPath[sizeof(input_record_playbackPath) - 1] = '\0';
}

void
input_record_setUiEventPath(const char *path)
{
    if (!path || !*path) {
        input_record_uiEventPath[0] = '\0';
        return;
    }
    strncpy(input_record_uiEventPath, path, sizeof(input_record_uiEventPath) - 1);
    input_record_uiEventPath[sizeof(input_record_uiEventPath) - 1] = '\0';
}

static void
input_record_pushUiEvent(const input_record_ui_event_t *ev)
{
    if (!ev) {
        return;
    }
    if (input_record_uiEventCount == input_record_uiEventCap) {
        size_t next = input_record_uiEventCap ? input_record_uiEventCap * 2 : 256;
        input_record_ui_event_t *buf =
            (input_record_ui_event_t*)alloc_realloc(input_record_uiEvents, next * sizeof(*buf));
        if (!buf) {
            return;
        }
        input_record_uiEvents = buf;
        input_record_uiEventCap = next;
    }
    input_record_uiEvents[input_record_uiEventCount++] = *ev;
}

static int
input_record_loadUiEvents(void)
{
    if (!input_record_uiEventPath[0]) {
        return 1;
    }
    FILE *fp = fopen(input_record_uiEventPath, "rb");
    if (!fp) {
        debug_error("input: failed to open ui event file %s", input_record_uiEventPath);
        return 0;
    }
    char header[sizeof(input_record_uiEventHeader)];
    size_t headerLen = sizeof(input_record_uiEventHeader) - 1;
    if (fread(header, 1, headerLen, fp) != headerLen || memcmp(header, input_record_uiEventHeader, headerLen) != 0) {
        fclose(fp);
        debug_error("input: ui event file header mismatch");
        return 0;
    }
    input_record_ui_event_t ev;
    while (fread(&ev, 1, sizeof(ev), fp) == sizeof(ev)) {
        input_record_pushUiEvent(&ev);
    }
    fclose(fp);
    input_record_uiEventIndex = 0;
    input_record_uiEventMode = 2;
    return 1;
}

int
input_record_init(void)
{
    input_record_uiQueueCount = 0;
    input_record_uiQueueHead = 0;
    if (input_record_recordPath[0] && input_record_playbackPath[0]) {
        debug_error("input: --record and --playback are mutually exclusive");
        return 0;
    }
    if (input_record_playbackPath[0]) {
        FILE *fp = fopen(input_record_playbackPath, "r");
        if (!fp) {
            debug_error("input: failed to open playback file %s", input_record_playbackPath);
            return 0;
        }
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            input_record_parseLine(line);
        }
        fclose(fp);
        input_record_mode = 2;
        input_record_injecting = 1;
        libretro_host_clearJoypadState();
        input_record_injecting = 0;
        input_record_eventIndexCore = 0;
        input_record_eventIndexUi = 0;
        if (!input_record_loadUiEvents()) {
            return 0;
        }
        return 1;
    }
    if (input_record_recordPath[0]) {
        input_record_out = fopen(input_record_recordPath, "w");
        if (!input_record_out) {
            debug_error("input: failed to open record file %s", input_record_recordPath);
            return 0;
        }
        fprintf(input_record_out, "E9K_INPUT_V1\n");
        fflush(input_record_out);
        input_record_mode = 1;
    }
    if (input_record_recordPath[0] && input_record_uiEventPath[0]) {
        input_record_uiEventOut = fopen(input_record_uiEventPath, "wb");
        if (!input_record_uiEventOut) {
            debug_error("input: failed to open ui event file %s", input_record_uiEventPath);
            return 0;
        }
        fwrite(input_record_uiEventHeader, 1, sizeof(input_record_uiEventHeader) - 1, input_record_uiEventOut);
        fflush(input_record_uiEventOut);
        input_record_uiEventMode = 1;
    }
    return 1;
}

void
input_record_shutdown(void)
{
    input_record_recordPath[0] = '\0';
    input_record_playbackPath[0] = '\0';
    input_record_uiEventPath[0] = '\0';
    if (input_record_out) {
        fclose(input_record_out);
        input_record_out = NULL;
    }
    if (input_record_uiEventOut) {
        fclose(input_record_uiEventOut);
        input_record_uiEventOut = NULL;
    }
    if (input_record_events) {
        alloc_free(input_record_events);
        input_record_events = NULL;
    }
    if (input_record_uiEvents) {
        alloc_free(input_record_uiEvents);
        input_record_uiEvents = NULL;
    }
    if (input_record_uiQueue) {
        alloc_free(input_record_uiQueue);
        input_record_uiQueue = NULL;
    }
    input_record_eventCount = 0;
    input_record_eventCap = 0;
    input_record_eventIndexCore = 0;
    input_record_eventIndexUi = 0;
    input_record_uiEventCount = 0;
    input_record_uiEventCap = 0;
    input_record_uiEventIndex = 0;
    input_record_uiEventMode = 0;
    input_record_uiQueueCap = 0;
    input_record_uiQueueCount = 0;
    input_record_uiQueueHead = 0;
    input_record_useUiEventQueue = 0;
    input_record_mode = 0;
    input_record_injecting = 0;
}

int
input_record_isRecording(void)
{
    return input_record_mode == 1;
}

int
input_record_isPlayback(void)
{
    return input_record_mode == 2;
}

int
input_record_isPlaybackComplete(void)
{
    if (input_record_mode != 2) {
        return 0;
    }
    return input_record_eventIndexCore >= input_record_eventCount;
}

int
input_record_isInjecting(void)
{
    return input_record_injecting ? 1 : 0;
}

int
input_record_isUiEventRecording(void)
{
    return input_record_uiEventMode == 1;
}

int
input_record_isUiEventPlaybackComplete(void)
{
    if (input_record_uiEventMode != 2) {
        return 0;
    }
    return input_record_uiEventIndex >= input_record_uiEventCount;
}

void
input_record_recordJoypad(uint64_t frame, unsigned port, unsigned id, int pressed)
{
    if (input_record_mode != 1 || input_record_injecting || !input_record_out) {
        return;
    }
    fprintf(input_record_out, "F %llu J %u %u %d\n",
            (unsigned long long)frame, port, id, pressed ? 1 : 0);
    fflush(input_record_out);
}

void
input_record_recordKey(uint64_t frame, unsigned keycode, uint32_t character,
                        uint16_t modifiers, int pressed)
{
    if (input_record_mode != 1 || input_record_injecting || !input_record_out) {
        return;
    }
    fprintf(input_record_out, "F %llu K %u %u %u %d\n",
            (unsigned long long)frame,
            keycode,
            (unsigned)character,
            (unsigned)modifiers,
            pressed ? 1 : 0);
    fflush(input_record_out);
}

void
input_record_recordClear(uint64_t frame)
{
    if (input_record_mode != 1 || input_record_injecting || !input_record_out) {
        return;
    }
    fprintf(input_record_out, "F %llu C\n", (unsigned long long)frame);
    fflush(input_record_out);
}

void
input_record_recordUiKey(uint64_t frame, unsigned keycode, int pressed)
{
    if (input_record_mode != 1 || input_record_injecting || !input_record_out) {
        return;
    }
    fprintf(input_record_out, "F %llu U %u %d\n",
            (unsigned long long)frame,
            keycode,
            pressed ? 1 : 0);
    fflush(input_record_out);
}

void
input_record_recordUiKeyEvent(uint64_t frame, unsigned keycode, uint16_t modifiers, int repeat, int pressed)
{
    if (input_record_mode != 1 || input_record_injecting || !input_record_out) {
        return;
    }
    fprintf(input_record_out, "F %llu S %u %u %u %d\n",
            (unsigned long long)frame,
            keycode,
            (unsigned)modifiers,
            (unsigned)repeat,
            pressed ? 1 : 0);
    fflush(input_record_out);
}


void
input_record_recordUiEvent(uint64_t frame, const SDL_Event *ev)
{
    if (input_record_uiEventMode != 1 || input_record_injecting || !input_record_uiEventOut || !ev) {
        return;
    }
    if (ev->type == SDL_QUIT) {
        return;
    }
    input_record_ui_event_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.frame = frame;
    rec.ev = *ev;
    fwrite(&rec, 1, sizeof(rec), input_record_uiEventOut);
    fflush(input_record_uiEventOut);
}

void
input_record_recordCoreMouseMotion(uint64_t frame, int port, int dx, int dy)
{
    if (input_record_mode != 1 || input_record_injecting || !input_record_out) {
        return;
    }
    if (dx == 0 && dy == 0) {
        return;
    }
    fprintf(input_record_out, "F %llu m %d %d %d\n",
            (unsigned long long)frame,
            port,
            dx,
            dy);
    fflush(input_record_out);
}

void
input_record_recordCoreMouseButton(uint64_t frame, int port, unsigned id, int pressed)
{
    if (input_record_mode != 1 || input_record_injecting || !input_record_out) {
        return;
    }
    fprintf(input_record_out, "F %llu b %d %u %d\n",
            (unsigned long long)frame,
            port,
            id,
            pressed ? 1 : 0);
    fflush(input_record_out);
}

void
input_record_handleUiKey(unsigned keycode, int pressed)
{
    if (!pressed) {
        return;
    }
    if (keycode == (unsigned)SDLK_COMMA) {
        profile_checkpoints_toggle();
    } else if (keycode == (unsigned)SDLK_PERIOD) {
        profile_checkpoints_reset();
    } else if (keycode == (unsigned)SDLK_SLASH) {
        profile_checkpoints_dump();
    }
}

void
input_record_applyFrame(uint64_t frame)
{
    if (input_record_mode != 2) {
        return;
    }
    while (input_record_eventIndexCore < input_record_eventCount &&
           input_record_events[input_record_eventIndexCore].frame < frame) {
        input_record_eventIndexCore++;
    }
    if (input_record_eventIndexCore >= input_record_eventCount) {
        return;
    }
    input_record_injecting = 1;
    while (input_record_eventIndexCore < input_record_eventCount) {
        const input_record_event_t *ev = &input_record_events[input_record_eventIndexCore];
        if (ev->frame != frame) {
            break;
        }
        if (ev->type == INPUT_RECORD_JOYPAD) {
            libretro_host_setJoypadState(ev->port, ev->id, ev->pressed);
        } else if (ev->type == INPUT_RECORD_KEY) {
            libretro_host_sendKeyEvent(ev->keycode, ev->character, ev->modifiers, ev->pressed);
        } else if (ev->type == INPUT_RECORD_CLEAR) {
            libretro_host_clearJoypadState();
        } else if (ev->type == INPUT_RECORD_UIKEY) {
            input_record_handleUiKey(ev->keycode, ev->pressed);
        } else if (ev->type == INPUT_RECORD_CORE_MOUSE_MOTION) {
            libretro_host_addMouseMotion((unsigned)ev->mousePort, ev->xrel, ev->yrel);
        } else if (ev->type == INPUT_RECORD_CORE_MOUSE_BUTTON) {
            libretro_host_setMouseButton((unsigned)ev->mousePort, ev->id, ev->pressed);
        }
        input_record_eventIndexCore++;
    }
    input_record_injecting = 0;
}

void
input_record_applyUiFrame(uint64_t frame)
{
    if (input_record_mode != 2 && input_record_uiEventMode != 2) {
        return;
    }
    if (input_record_uiEventMode == 2 && input_record_uiEventCount > 0) {
        while (input_record_uiEventIndex < input_record_uiEventCount &&
               input_record_uiEvents[input_record_uiEventIndex].frame < frame) {
            input_record_uiEventIndex++;
        }
        if (input_record_uiEventIndex >= input_record_uiEventCount) {
            return;
        }
        input_record_injecting = 1;
        while (input_record_uiEventIndex < input_record_uiEventCount) {
            const input_record_ui_event_t *ev = &input_record_uiEvents[input_record_uiEventIndex];
            if (ev->frame != frame) {
                break;
            }
            if (input_record_useUiEventQueue) {
                input_record_queueUiEvent(&ev->ev);
            } else {
                SDL_PushEvent((SDL_Event *)&ev->ev);
            }
            input_record_uiEventIndex++;
        }
        input_record_injecting = 0;
        return;
    }
    while (input_record_eventIndexUi < input_record_eventCount &&
           input_record_events[input_record_eventIndexUi].frame < frame) {
        input_record_eventIndexUi++;
    }
    if (input_record_eventIndexUi >= input_record_eventCount) {
        return;
    }
    SDL_Window *window = SDL_GetMouseFocus();
    if (!window) {
        window = SDL_GetKeyboardFocus();
    }
    uint32_t windowId = window ? SDL_GetWindowID(window) : 0;
    input_record_injecting = 1;
    while (input_record_eventIndexUi < input_record_eventCount) {
        const input_record_event_t *ev = &input_record_events[input_record_eventIndexUi];
        if (ev->frame != frame) {
            break;
        }
        SDL_Event out;
        memset(&out, 0, sizeof(out));
        if (ev->type == INPUT_RECORD_MOUSE_MOTION) {
            out.type = SDL_MOUSEMOTION;
            out.motion.windowID = windowId;
            out.motion.x = ev->x;
            out.motion.y = ev->y;
            out.motion.xrel = ev->xrel;
            out.motion.yrel = ev->yrel;
            SDL_PushEvent(&out);
        } else if (ev->type == INPUT_RECORD_MOUSE_BUTTON) {
            out.type = ev->pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            out.button.windowID = windowId;
            out.button.button = (uint8_t)ev->id;
            out.button.state = ev->pressed ? SDL_PRESSED : SDL_RELEASED;
            out.button.clicks = 1;
            out.button.x = ev->x;
            out.button.y = ev->y;
            SDL_PushEvent(&out);
        } else if (ev->type == INPUT_RECORD_MOUSE_WHEEL) {
            if (window) {
                SDL_WarpMouseInWindow(window, ev->x, ev->y);
            }
            out.type = SDL_MOUSEWHEEL;
            out.wheel.windowID = windowId;
            out.wheel.x = ev->wheelX;
            out.wheel.y = ev->wheelY;
            out.wheel.direction = ev->wheelDirection;
            SDL_PushEvent(&out);
        } else if (ev->type == INPUT_RECORD_UIKEY_EVENT) {
            out.type = ev->pressed ? SDL_KEYDOWN : SDL_KEYUP;
            out.key.windowID = windowId;
            out.key.state = ev->pressed ? SDL_PRESSED : SDL_RELEASED;
            out.key.repeat = (uint8_t)ev->repeat;
            out.key.keysym.sym = (SDL_Keycode)ev->keycode;
            out.key.keysym.mod = (uint16_t)ev->modifiers;
            out.key.keysym.scancode = SDL_GetScancodeFromKey(out.key.keysym.sym);
            SDL_PushEvent(&out);
        } else if (ev->type == INPUT_RECORD_TEXT_INPUT) {
            out.type = SDL_TEXTINPUT;
            out.text.windowID = windowId;
            if (ev->textLen > 0) {
                size_t len = ev->textLen;
                if (len >= sizeof(out.text.text)) {
                    len = sizeof(out.text.text) - 1;
                }
                memcpy(out.text.text, ev->text, len);
                out.text.text[len] = '\0';
            } else {
                out.text.text[0] = '\0';
            }
            SDL_PushEvent(&out);
        }
        input_record_eventIndexUi++;
    }
    input_record_injecting = 0;
}
