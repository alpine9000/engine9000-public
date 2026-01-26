/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_record.h"
#include "alloc.h"
#include "debug.h"
#include "libretro_host.h"
#include "geo_checkpoint.h"

typedef enum input_record_type {
    INPUT_RECORD_JOYPAD = 1,
    INPUT_RECORD_KEY,
    INPUT_RECORD_CLEAR,
    INPUT_RECORD_UIKEY
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
} input_record_event_t;

static char input_record_recordPath[4096];
static char input_record_playbackPath[4096];
static FILE *input_record_out = NULL;
static input_record_event_t *input_record_events = NULL;
static size_t input_record_eventCount = 0;
static size_t input_record_eventCap = 0;
static size_t input_record_eventIndex = 0;
static int input_record_mode = 0;
static int input_record_injecting = 0;

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

int
input_record_init(void)
{
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
        input_record_eventIndex = 0;
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
    return 1;
}

void
input_record_shutdown(void)
{
    if (input_record_out) {
        fclose(input_record_out);
        input_record_out = NULL;
    }
    if (input_record_events) {
        alloc_free(input_record_events);
        input_record_events = NULL;
    }
    input_record_eventCount = 0;
    input_record_eventCap = 0;
    input_record_eventIndex = 0;
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
input_record_isInjecting(void)
{
    return input_record_injecting ? 1 : 0;
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

static void
input_record_dumpCheckpoints(void)
{
    geo_debug_checkpoint_t entries[GEO_CHECKPOINT_COUNT];
    size_t bytes = libretro_host_debugReadCheckpoints(entries, sizeof(entries));
    size_t count = bytes / sizeof(entries[0]);
    if (count > GEO_CHECKPOINT_COUNT) {
        count = GEO_CHECKPOINT_COUNT;
    }
    printf("Profiler checkpoints (avg/min/max):\n");
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].count == 0) {
            continue;
        }
        printf("%02zu avg:%llu min:%llu max:%llu\n",
               i,
               (unsigned long long)entries[i].average,
               (unsigned long long)entries[i].minimum,
               (unsigned long long)entries[i].maximum);
    }
    fflush(stdout);
}

void
input_record_handleUiKey(unsigned keycode, int pressed)
{
    if (!pressed) {
        return;
    }
    if (keycode == (unsigned)SDLK_COMMA) {
        int enabled = 0;
        if (libretro_host_debugGetCheckpointEnabled(&enabled)) {
            libretro_host_debugSetCheckpointEnabled(enabled ? 0 : 1);
        }
    } else if (keycode == (unsigned)SDLK_PERIOD) {
        libretro_host_debugResetCheckpoints();
    } else if (keycode == (unsigned)SDLK_SLASH) {
        input_record_dumpCheckpoints();
    }
}

void
input_record_applyFrame(uint64_t frame)
{
    if (input_record_mode != 2) {
        return;
    }
    while (input_record_eventIndex < input_record_eventCount &&
           input_record_events[input_record_eventIndex].frame < frame) {
        input_record_eventIndex++;
    }
    if (input_record_eventIndex >= input_record_eventCount) {
        return;
    }
    input_record_injecting = 1;
    while (input_record_eventIndex < input_record_eventCount) {
        const input_record_event_t *ev = &input_record_events[input_record_eventIndex];
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
        }
        input_record_eventIndex++;
    }
    input_record_injecting = 0;
}
