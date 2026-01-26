/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <stdint.h>

int
memory_track_ui_init(void);

void
memory_track_ui_shutdown(void);

int
memory_track_ui_isOpen(void);

uint32_t
memory_track_ui_getWindowId(void);

void
memory_track_ui_handleEvent(SDL_Event *ev);

void
memory_track_ui_render(void);

void
memory_track_ui_addFrameMarker(uint64_t frameNo);

void
memory_track_ui_clearMarkers(void);

size_t
memory_track_ui_getMarkerCount(void);
