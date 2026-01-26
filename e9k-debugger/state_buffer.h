/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct state_frame {
    uint64_t id;
    uint64_t frame_no;
    int is_keyframe;
    size_t payload_size;
    uint8_t *payload;
    size_t state_size;
} state_frame_t;

void state_buffer_init(size_t max_bytes);
void state_buffer_shutdown(void);
void state_buffer_capture(void);
size_t state_buffer_getUsedBytes(void);
size_t state_buffer_getCount(void);
void state_buffer_setPaused(int paused);
int state_buffer_isPaused(void);
int state_buffer_trimAfterPercent(float percent);
int state_buffer_hasFrameNo(uint64_t frame_no);
int state_buffer_restoreFrameNo(uint64_t frame_no);
int state_buffer_trimAfterFrameNo(uint64_t frame_no);
int state_buffer_snapshot(void);
int state_buffer_restoreSnapshot(void);
int state_buffer_saveSnapshotFile(const char *path, uint64_t rom_checksum);
int state_buffer_loadSnapshotFile(const char *path, uint64_t *out_rom_checksum);
int state_buffer_getSnapshotState(uint8_t **out_state, size_t *out_size, uint64_t *out_frame_no);
size_t state_buffer_getMaxBytes(void);
void state_buffer_setCurrentFrameNo(uint64_t frame_no);
uint64_t state_buffer_getCurrentFrameNo(void);
state_frame_t*
state_buffer_getFrameAtPercent(float percent);

