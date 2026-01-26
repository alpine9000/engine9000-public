/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>

void
input_record_setRecordPath(const char *path);

void
input_record_setPlaybackPath(const char *path);

int
input_record_init(void);

void
input_record_shutdown(void);

int
input_record_isRecording(void);

int
input_record_isPlayback(void);

int
input_record_isInjecting(void);

void
input_record_recordJoypad(uint64_t frame, unsigned port, unsigned id, int pressed);

void
input_record_recordKey(uint64_t frame, unsigned keycode, uint32_t character,
                        uint16_t modifiers, int pressed);

void
input_record_recordClear(uint64_t frame);

void
input_record_recordUiKey(uint64_t frame, unsigned keycode, int pressed);

void
input_record_handleUiKey(unsigned keycode, int pressed);

void
input_record_applyFrame(uint64_t frame);


