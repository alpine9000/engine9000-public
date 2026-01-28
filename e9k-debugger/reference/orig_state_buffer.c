/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "debugger.h"
#include "state_buffer.h"
#include "libretro_host.h"

typedef uint32_t state_buffer_u32_alias_t __attribute__((__may_alias__));

static void
state_buffer_writeU32Fast(uint8_t *dst, uint32_t v)
{
    *(state_buffer_u32_alias_t*)(void*)dst = (state_buffer_u32_alias_t)v;
}

static uint32_t
state_buffer_readU32Fast(const uint8_t *src)
{
    return (uint32_t)(*(const state_buffer_u32_alias_t*)(const void*)src);
}

typedef struct {
    state_frame_t *frames;
    size_t count;
    size_t cap;
    size_t start;
    size_t total_bytes;
    size_t max_bytes;
    uint64_t next_id;
    uint8_t *prev_state;
    size_t prev_size;
    uint8_t *temp_state;
    size_t temp_size;
    uint8_t *diff_scratch;
    size_t diff_scratch_cap;
    uint8_t *recon_a;
    uint8_t *recon_b;
    size_t recon_size;
    int paused;
    uint64_t current_frame_no;
} state_buffer_t;

typedef struct {
  state_buffer_t current;
  state_buffer_t save;
} state_buffer_global_t;

state_buffer_global_t state_buffer;

static void
state_buffer_clearFrame(state_frame_t *f);

static void
state_buffer_reset(state_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < buf->count; ++i) {
        state_frame_t *f = &buf->frames[buf->start + i];
        state_buffer_clearFrame(f);
    }
    alloc_free(buf->frames);
    buf->frames = NULL;
    buf->count = 0;
    buf->cap = 0;
    buf->start = 0;
    buf->total_bytes = 0;
    buf->next_id = 0;
    alloc_free(buf->prev_state);
    buf->prev_state = NULL;
    buf->prev_size = 0;
    alloc_free(buf->temp_state);
    buf->temp_state = NULL;
    buf->temp_size = 0;
    alloc_free(buf->diff_scratch);
    buf->diff_scratch = NULL;
    buf->diff_scratch_cap = 0;
    alloc_free(buf->recon_a);
    buf->recon_a = NULL;
    alloc_free(buf->recon_b);
    buf->recon_b = NULL;
    buf->recon_size = 0;
    buf->paused = 0;
    buf->current_frame_no = 0;
}

static void
state_buffer_clearFrame(state_frame_t *f)
{
    if (!f) {
        return;
    }
    alloc_free(f->payload);
    f->payload = NULL;
    f->payload_size = 0;
    f->state_size = 0;
    f->is_keyframe = 0;
    f->id = 0;
    f->frame_no = 0;
}

static int
state_buffer_ensureRecon(state_buffer_t *buf, size_t size)
{
    if (!buf) {
        return 0;
    }
    if (buf->recon_size == size && buf->recon_a && buf->recon_b) {
        return 1;
    }
    uint8_t *a = (uint8_t*)alloc_realloc(buf->recon_a, size);
    if (!a) {
        return 0;
    }
    uint8_t *b = (uint8_t*)alloc_realloc(buf->recon_b, size);
    if (!b) {
        buf->recon_a = a;
        return 0;
    }
    buf->recon_a = a;
    buf->recon_b = b;
    buf->recon_size = size;
    return 1;
}

static void
state_buffer_compact(state_buffer_t *buf)
{
    if (!buf || buf->start == 0 || buf->count == 0) {
        return;
    }
    memmove(buf->frames, buf->frames + buf->start, buf->count * sizeof(state_frame_t));
    buf->start = 0;
}

#define STATE_BUFFER_DIFF_BLOCK_SIZE 64u

static size_t
diff_payload_max_size(size_t size)
{
    const uint32_t blockSize = STATE_BUFFER_DIFF_BLOCK_SIZE;
    const size_t blockCount = size / blockSize;
    const size_t tailLen = size - blockCount * blockSize;
    // Payload format:
    // u32 block_size, u32 block_count, u32 tail_len, u32 changed_count
    // repeated changed_count times:
    //   u32 block_index, u8 data[block_size]
    // tail bytes: absolute bytes (tail_len)
    return 16 + blockCount * (4 + blockSize) + tailLen;
}

static size_t
write_diff_payload(uint8_t *dst, size_t cap, const uint8_t *prev, const uint8_t *cur, size_t size)
{
    if (!dst || !prev || !cur) {
        return 0;
    }

    const uint32_t blockSize = STATE_BUFFER_DIFF_BLOCK_SIZE;
    const uint32_t blockCount = (uint32_t)(size / blockSize);
    const uint32_t tailLen = (uint32_t)(size - (size_t)blockCount * blockSize);
    const size_t maxSize = diff_payload_max_size(size);
    if (cap < maxSize) {
        return 0;
    }

    size_t pos = 0;
    const size_t changedCountPos = 12;
    state_buffer_writeU32Fast(dst + pos, blockSize);
    pos += 4;
    state_buffer_writeU32Fast(dst + pos, blockCount);
    pos += 4;
    state_buffer_writeU32Fast(dst + pos, tailLen);
    pos += 4;
    state_buffer_writeU32Fast(dst + pos, 0);
    pos += 4;

    uint32_t changedCount = 0;
    for (uint32_t i = 0; i < blockCount; ++i) {
        const size_t off = (size_t)i * blockSize;
        if ((i & 31u) == 0u) {
            __builtin_prefetch(prev + off + 256, 0, 1);
            __builtin_prefetch(cur + off + 256, 0, 1);
        }
        if (memcmp(prev + off, cur + off, blockSize) == 0) {
            continue;
        }
        state_buffer_writeU32Fast(dst + pos, i);
        pos += 4;
        memcpy(dst + pos, cur + off, blockSize);
        pos += blockSize;
        changedCount++;
    }

    if (tailLen) {
        memcpy(dst + pos, cur + (size_t)blockCount * blockSize, tailLen);
        pos += tailLen;
    }

    state_buffer_writeU32Fast(dst + changedCountPos, changedCount);
    return pos;
}

static int
apply_diff_inplace(uint8_t *io, size_t io_size, const uint8_t *payload, size_t payload_size)
{
    if (!io || !payload || io_size == 0) {
        return 0;
    }
    if (payload_size < 16) {
        return 0;
    }

    size_t pos = 0;
    uint32_t blockSize = state_buffer_readU32Fast(payload + pos);
    pos += 4;
    uint32_t blockCount = state_buffer_readU32Fast(payload + pos);
    pos += 4;
    uint32_t tailLen = state_buffer_readU32Fast(payload + pos);
    pos += 4;
    uint32_t changedCount = state_buffer_readU32Fast(payload + pos);
    pos += 4;
    if (blockSize != STATE_BUFFER_DIFF_BLOCK_SIZE) {
        return 0;
    }
    if ((size_t)blockCount * blockSize + (size_t)tailLen != io_size) {
        return 0;
    }
    for (uint32_t i = 0; i < changedCount; ++i) {
        if (pos + 4 + blockSize > payload_size) {
            return 0;
        }
        uint32_t index = state_buffer_readU32Fast(payload + pos);
        pos += 4;
        if (index >= blockCount) {
            return 0;
        }
        memcpy(io + (size_t)index * blockSize, payload + pos, blockSize);
        pos += blockSize;
    }
    if (pos + tailLen > payload_size) {
        return 0;
    }
    if (tailLen) {
        memcpy(io + (size_t)blockCount * blockSize, payload + pos, tailLen);
    }
    return 1;
}

static void
apply_diff_inplace_fast(uint8_t *io, size_t io_size, const uint8_t *payload)
{
    const uint32_t blockSize = state_buffer_readU32Fast(payload + 0);
    const uint32_t blockCount = state_buffer_readU32Fast(payload + 4);
    const uint32_t tailLen = state_buffer_readU32Fast(payload + 8);
    const uint32_t changedCount = state_buffer_readU32Fast(payload + 12);

    size_t pos = 16;
    for (uint32_t i = 0; i < changedCount; ++i) {
        uint32_t index = state_buffer_readU32Fast(payload + pos);
        pos += 4;
        memcpy(io + (size_t)index * blockSize, payload + pos, blockSize);
        pos += blockSize;
    }
    if (tailLen) {
        memcpy(io + (size_t)blockCount * blockSize, payload + pos, tailLen);
    }

    (void)io_size;
}

static int
apply_diff(uint8_t *out, size_t out_size, const uint8_t *base, size_t base_size,
           const uint8_t *payload, size_t payload_size)
{
    if (!out || !base || !payload || out_size == 0 || out_size != base_size) {
        return 0;
    }
    memcpy(out, base, out_size);
    return apply_diff_inplace(out, out_size, payload, payload_size);
}

static void
state_buffer_rekey_next(state_buffer_t *buf)
{
    if (!buf || buf->count < 2) {
        return;
    }
    state_frame_t *first = &buf->frames[buf->start];
    state_frame_t *next = &buf->frames[buf->start + 1];
    if (next->is_keyframe) {
        return;
    }
    if (!first->is_keyframe || first->state_size == 0 || next->state_size != first->state_size) {
        return;
    }
    uint8_t *full = (uint8_t*)alloc_alloc(first->state_size);
    if (!full) {
        return;
    }
    if (!apply_diff(full, first->state_size, first->payload, first->state_size,
                    next->payload, next->payload_size)) {
        alloc_free(full);
        return;
    }
    alloc_free(next->payload);
    next->payload = full;
    buf->total_bytes -= next->payload_size;
    next->payload_size = first->state_size;
    buf->total_bytes += next->payload_size;
    next->is_keyframe = 1;
}

static void
state_buffer_trim(state_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    while (buf->total_bytes > buf->max_bytes && buf->count > 0) {
        if (buf->count >= 2) {
            state_buffer_rekey_next(buf);
        }
        state_frame_t *oldest = &buf->frames[buf->start];
        buf->total_bytes -= oldest->payload_size;
        state_buffer_clearFrame(oldest);
        buf->start++;
        buf->count--;
        if (buf->start > 32 && buf->start > buf->cap / 2) {
            state_buffer_compact(buf);
        }
    }
}

void
state_buffer_init(size_t max_bytes)
{
  memset(&state_buffer.current, 0, sizeof(state_buffer.current));
  state_buffer.current.max_bytes = max_bytes;
  memset(&state_buffer.save, 0, sizeof(state_buffer.save));
}

void
state_buffer_shutdown(void)
{
  state_buffer_reset(&state_buffer.current);
  state_buffer_reset(&state_buffer.save);
}

void
state_buffer_capture(void)
{
  if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
    //     return;
  }
  if (state_buffer.current.paused) {
    return;
  }
  if (state_buffer.current.max_bytes == 0) {
    return;
  }
  size_t state_size = 0;
  if (!libretro_host_getSerializeSize(&state_size) || state_size == 0) {
    return;
  }
  if (!state_buffer.current.temp_state || state_buffer.current.temp_size != state_size) {
    uint8_t *buf = (uint8_t*)alloc_realloc(state_buffer.current.temp_state, state_size);
    if (!buf) {
      return;
    }
    state_buffer.current.temp_state = buf;
    state_buffer.current.temp_size = state_size;
  }
  if (!libretro_host_serializeTo(state_buffer.current.temp_state, state_size)) {
    return;
  }
  
  int have_prev = (state_buffer.current.prev_state && state_buffer.current.prev_size == state_size);
  int is_keyframe = 1;
  size_t payload_size = state_size;
  const uint8_t *payload_src = state_buffer.current.temp_state;
  if (have_prev) {
    const size_t diff_cap = diff_payload_max_size(state_size);
    if (!state_buffer.current.diff_scratch || state_buffer.current.diff_scratch_cap < diff_cap) {
      uint8_t *tmp = (uint8_t*)alloc_realloc(state_buffer.current.diff_scratch, diff_cap);
      if (!tmp) {
        return;
      }
      state_buffer.current.diff_scratch = tmp;
      state_buffer.current.diff_scratch_cap = diff_cap;
    }
    size_t diff_size = write_diff_payload(state_buffer.current.diff_scratch, state_buffer.current.diff_scratch_cap,
                                          state_buffer.current.prev_state, state_buffer.current.temp_state, state_size);
    if (diff_size > 0 && diff_size < state_size) {
      is_keyframe = 0;
      payload_size = diff_size;
      payload_src = state_buffer.current.diff_scratch;
    }
  }
  
  if (state_buffer.current.count + state_buffer.current.start >= state_buffer.current.cap) {
    size_t new_cap = state_buffer.current.cap ? state_buffer.current.cap * 2 : 64;
    state_frame_t *tmp = (state_frame_t*)alloc_realloc(state_buffer.current.frames, new_cap * sizeof(state_frame_t));
    if (!tmp) {
      return;
    }
    state_buffer.current.frames = tmp;
    state_buffer.current.cap = new_cap;
  }
  
  state_frame_t *frame = &state_buffer.current.frames[state_buffer.current.start + state_buffer.current.count];
  memset(frame, 0, sizeof(*frame));
  frame->id = state_buffer.current.next_id++;
  frame->frame_no = state_buffer.current.current_frame_no;
  frame->state_size = state_size;
  frame->is_keyframe = is_keyframe ? 1 : 0;
  frame->payload_size = payload_size;
  frame->payload = (uint8_t*)alloc_alloc(payload_size);
  if (!frame->payload) {
    return;
  }
  memcpy(frame->payload, payload_src, payload_size);
  state_buffer.current.count++;
  state_buffer.current.total_bytes += frame->payload_size;
  
  if (!state_buffer.current.prev_state || state_buffer.current.prev_size != state_size) {
    uint8_t *prev = (uint8_t*)alloc_realloc(state_buffer.current.prev_state, state_size);
    if (!prev) {
      return;
    }
    state_buffer.current.prev_state = prev;
    state_buffer.current.prev_size = state_size;
  }
  memcpy(state_buffer.current.prev_state, state_buffer.current.temp_state, state_size);
  
  if (state_buffer.current.count == 1) {
    state_buffer.current.frames[state_buffer.current.start].is_keyframe = 1;
  }
  
  state_buffer_trim(&state_buffer.current);
}

void
state_buffer_setPaused(int paused)
{
  state_buffer.current.paused = paused ? 1 : 0;
}

int
state_buffer_isPaused(void)
{
    return state_buffer.current.paused ? 1 : 0;
}

size_t
state_buffer_getUsedBytes(void)
{
    return state_buffer.current.total_bytes;
}

size_t
state_buffer_getCount(void)
{
    return state_buffer.current.count;
}

size_t
state_buffer_getMaxBytes(void)
{
    return state_buffer.current.max_bytes;
}

void
state_buffer_setCurrentFrameNo(uint64_t frame_no)
{
    state_buffer.current.current_frame_no = frame_no;
}

uint64_t
state_buffer_getCurrentFrameNo(void)
{
    return state_buffer.current.current_frame_no;
}

static state_frame_t *
state_buffer_getFrameAtBuffer(state_buffer_t *buf, size_t idx)
{
    if (!buf || idx >= buf->count) {
        return NULL;
    }
    return &buf->frames[buf->start + idx];
}

static int
state_buffer_findIndexByFrameNoBuffer(state_buffer_t *buf, uint64_t frameNo, size_t *outIdx)
{
    if (outIdx) {
        *outIdx = 0;
    }
    if (!buf || buf->count == 0) {
        return 0;
    }
    for (size_t i = 0; i < buf->count; ++i) {
        state_frame_t *frame = state_buffer_getFrameAtBuffer(buf, i);
        if (frame && frame->frame_no == frameNo) {
            if (outIdx) {
                *outIdx = i;
            }
            return 1;
        }
    }
    return 0;
}

static int
state_buffer_reconstructIndexBuffer(state_buffer_t *buf, size_t idx, uint8_t **outState, size_t *outSize)
{
    if (outState) {
        *outState = NULL;
    }
    if (outSize) {
        *outSize = 0;
    }
    if (!buf || buf->count == 0 || idx >= buf->count) {
        return 0;
    }
    state_frame_t *target = state_buffer_getFrameAtBuffer(buf, idx);
    if (!target || target->state_size == 0) {
        return 0;
    }
    size_t keyIdx = idx;
    while (keyIdx > 0) {
        state_frame_t *frame = state_buffer_getFrameAtBuffer(buf, keyIdx);
        if (frame && frame->is_keyframe) {
            break;
        }
        keyIdx--;
    }
    state_frame_t *key = state_buffer_getFrameAtBuffer(buf, keyIdx);
    if (!key || !key->is_keyframe || key->state_size == 0 || !key->payload) {
        return 0;
    }
    size_t stateSize = key->state_size;
    if (!state_buffer_ensureRecon(buf, stateSize)) {
        return 0;
    }
    uint8_t *cur = buf->recon_a;
    memcpy(cur, key->payload, stateSize);
    for (size_t i = keyIdx + 1; i <= idx; ++i) {
        state_frame_t *frame = state_buffer_getFrameAtBuffer(buf, i);
        if (!frame || frame->state_size != stateSize || !frame->payload) {
            return 0;
        }
        if (frame->is_keyframe) {
            memcpy(cur, frame->payload, stateSize);
            continue;
        }
        apply_diff_inplace_fast(cur, stateSize, frame->payload);
    }
    if (outState) {
        *outState = cur;
    }
    if (outSize) {
        *outSize = stateSize;
    }
    return 1;
}

static state_frame_t *
state_buffer_getFrameAt(state_buffer_t *buf, size_t idx)
{
    if (!buf || idx >= buf->count) {
        return NULL;
    }
    return &buf->frames[buf->start + idx];
}

static int
state_buffer_findIndexByFrameNo(uint64_t frame_no, size_t *out_idx)
{
    if (out_idx) {
        *out_idx = 0;
    }
    if (state_buffer.current.count == 0) {
        return 0;
    }
    for (size_t i = 0; i < state_buffer.current.count; ++i) {
        state_frame_t *f = state_buffer_getFrameAt(&state_buffer.current, i);
        if (f && f->frame_no == frame_no) {
            if (out_idx) {
                *out_idx = i;
            }
            return 1;
        }
    }
    return 0;
}

static int
state_buffer_reconstructIndex(size_t idx, uint8_t **out_state, size_t *out_size)
{
    if (state_buffer.current.count == 0 || idx >= state_buffer.current.count) {
        return 0;
    }
    state_frame_t *target = state_buffer_getFrameAt(&state_buffer.current, idx);
    if (!target || target->state_size == 0) {
        return 0;
    }
    size_t key_idx = idx;
    while (key_idx > 0) {
        state_frame_t *f = state_buffer_getFrameAt(&state_buffer.current, key_idx);
        if (f && f->is_keyframe) {
            break;
        }
        key_idx--;
    }
    state_frame_t *key = state_buffer_getFrameAt(&state_buffer.current, key_idx);
    if (!key || !key->is_keyframe || key->state_size == 0 || !key->payload) {
        return 0;
    }
    size_t state_size = key->state_size;
    if (!state_buffer_ensureRecon(&state_buffer.current, state_size)) {
        return 0;
    }
    uint8_t *cur = state_buffer.current.recon_a;
    memcpy(cur, key->payload, state_size);
    for (size_t i = key_idx + 1; i <= idx; ++i) {
        state_frame_t *f = state_buffer_getFrameAt(&state_buffer.current, i);
        if (!f || f->state_size != state_size || !f->payload) {
            return 0;
        }
        if (f->is_keyframe) {
            memcpy(cur, f->payload, state_size);
            continue;
        }
        apply_diff_inplace_fast(cur, state_size, f->payload);
    }
    if (out_state) {
        *out_state = cur;
    }
    if (out_size) {
        *out_size = state_size;
    }
    return 1;
}

state_frame_t*
state_buffer_getFrameAtPercent(float percent)
{
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 1.0f) percent = 1.0f;
  size_t idx = (size_t)((float)(state_buffer.current.count - 1) * percent + 0.5f);
  if (idx >= state_buffer.current.count) {
    idx = state_buffer.current.count - 1;
  }
  state_frame_t *target = state_buffer_getFrameAt(&state_buffer.current, idx);
  return target;
}


int
state_buffer_hasFrameNo(uint64_t frame_no)
{
    return state_buffer_findIndexByFrameNo(frame_no, NULL);
}

int
state_buffer_restoreFrameNo(uint64_t frame_no)
{
    size_t idx = 0;
    if (!state_buffer_findIndexByFrameNo(frame_no, &idx)) {
        return 0;
    }
    state_frame_t *target = state_buffer_getFrameAt(&state_buffer.current, idx);
    if (!target || target->state_size == 0) {
        return 0;
    }
    uint8_t *state = NULL;
    size_t state_size = 0;
    if (!state_buffer_reconstructIndex(idx, &state, &state_size)) {
        return 0;
    }
    if (!libretro_host_unserializeFrom(state, state_size)) {
        return 0;
    }
    state_buffer.current.current_frame_no = target->frame_no;
    return 1;
}

int
state_buffer_trimAfterPercent(float percent)
{
    if (state_buffer.current.count == 0) {
        return 0;
    }
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    size_t idx = (size_t)((float)(state_buffer.current.count - 1) * percent + 0.5f);
    if (idx >= state_buffer.current.count) {
        idx = state_buffer.current.count - 1;
    }
    if (idx + 1 >= state_buffer.current.count) {
        return 1;
    }
    uint8_t *state = NULL;
    size_t state_size = 0;
    if (!state_buffer_reconstructIndex(idx, &state, &state_size)) {
        return 0;
    }
    for (size_t i = idx + 1; i < state_buffer.current.count; ++i) {
        state_frame_t *f = state_buffer_getFrameAt(&state_buffer.current, i);
        if (f) {
            state_buffer.current.total_bytes -= f->payload_size;
            state_buffer_clearFrame(f);
        }
    }
    state_buffer.current.count = idx + 1;
    if (!state_buffer.current.prev_state || state_buffer.current.prev_size != state_size) {
        uint8_t *prev = (uint8_t*)alloc_realloc(state_buffer.current.prev_state, state_size);
        if (!prev) {
            return 0;
        }
        state_buffer.current.prev_state = prev;
        state_buffer.current.prev_size = state_size;
    }
    memcpy(state_buffer.current.prev_state, state, state_size);
    return 1;
}

int
state_buffer_trimAfterFrameNo(uint64_t frame_no)
{
    size_t idx = 0;
    if (!state_buffer_findIndexByFrameNo(frame_no, &idx)) {
        return 0;
    }
    if (idx + 1 >= state_buffer.current.count) {
        return 1;
    }
    uint8_t *state = NULL;
    size_t state_size = 0;
    if (!state_buffer_reconstructIndex(idx, &state, &state_size)) {
        return 0;
    }
    for (size_t i = idx + 1; i < state_buffer.current.count; ++i) {
        state_frame_t *f = state_buffer_getFrameAt(&state_buffer.current, i);
        if (f) {
            state_buffer.current.total_bytes -= f->payload_size;
            state_buffer_clearFrame(f);
        }
    }
    state_buffer.current.count = idx + 1;
    if (!state_buffer.current.prev_state || state_buffer.current.prev_size != state_size) {
        uint8_t *prev = (uint8_t*)alloc_realloc(state_buffer.current.prev_state, state_size);
        if (!prev) {
            return 0;
        }
        state_buffer.current.prev_state = prev;
        state_buffer.current.prev_size = state_size;
    }
    memcpy(state_buffer.current.prev_state, state, state_size);
    return 1;
}

static int
state_buffer_clone(state_buffer_t *dst, const state_buffer_t *src)
{
    if (!dst || !src) {
        return 0;
    }
    state_buffer_reset(dst);
    dst->max_bytes = src->max_bytes;
    dst->count = src->count;
    dst->cap = src->count;
    dst->start = 0;
    dst->total_bytes = src->total_bytes;
    dst->next_id = src->next_id;
    dst->paused = src->paused;
    dst->current_frame_no = src->current_frame_no;

    if (src->count > 0) {
        dst->frames = (state_frame_t*)alloc_calloc(src->count, sizeof(state_frame_t));
        if (!dst->frames) {
            state_buffer_reset(dst);
            return 0;
        }
        for (size_t i = 0; i < src->count; ++i) {
            const state_frame_t *s = &src->frames[src->start + i];
            state_frame_t *d = &dst->frames[i];
            d->id = s->id;
            d->frame_no = s->frame_no;
            d->is_keyframe = s->is_keyframe;
            d->state_size = s->state_size;
            d->payload_size = s->payload_size;
            if (s->payload_size > 0) {
                d->payload = (uint8_t*)alloc_alloc(s->payload_size);
                if (!d->payload) {
                    state_buffer_reset(dst);
                    return 0;
                }
                memcpy(d->payload, s->payload, s->payload_size);
            }
        }
    }
    if (src->prev_state && src->prev_size > 0) {
        dst->prev_state = (uint8_t*)alloc_alloc(src->prev_size);
        if (!dst->prev_state) {
            state_buffer_reset(dst);
            return 0;
        }
        memcpy(dst->prev_state, src->prev_state, src->prev_size);
        dst->prev_size = src->prev_size;
    }
    return 1;
}

int
state_buffer_snapshot(void)
{
    return state_buffer_clone(&state_buffer.save, &state_buffer.current);
}

int
state_buffer_restoreSnapshot(void)
{
    if (state_buffer.save.count == 0 && state_buffer.save.prev_size == 0) {
        return 0;
    }
    return state_buffer_clone(&state_buffer.current, &state_buffer.save);
}

int
state_buffer_setSaveKeyframe(const uint8_t *state, size_t state_size, uint64_t frame_no)
{
    if (!state || state_size == 0) {
        return 0;
    }
    state_buffer_reset(&state_buffer.save);

    state_buffer.save.frames = (state_frame_t*)alloc_calloc(1, sizeof(state_frame_t));
    if (!state_buffer.save.frames) {
        state_buffer_reset(&state_buffer.save);
        return 0;
    }

    uint8_t *payload = (uint8_t*)alloc_alloc(state_size);
    if (!payload) {
        state_buffer_reset(&state_buffer.save);
        return 0;
    }
    memcpy(payload, state, state_size);

    state_frame_t *frame = &state_buffer.save.frames[0];
    frame->id = 1;
    frame->frame_no = frame_no;
    frame->is_keyframe = 1;
    frame->payload_size = state_size;
    frame->payload = payload;
    frame->state_size = state_size;

    state_buffer.save.cap = 1;
    state_buffer.save.count = 1;
    state_buffer.save.start = 0;
    state_buffer.save.total_bytes = state_size;
    state_buffer.save.next_id = 2;
    state_buffer.save.current_frame_no = frame_no;
    state_buffer.save.max_bytes = state_buffer.current.max_bytes;
    state_buffer.save.paused = 0;
    return 1;
}

int
state_buffer_saveSnapshotFile(const char *path, uint64_t rom_checksum)
{
    if (!path || !*path) {
        return 0;
    }
    state_buffer_t *buf = &state_buffer.save;
    if (buf->count == 0 && buf->prev_size == 0) {
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    const char magic[8] = { 'E', '9', 'K', 'S', 'N', 'A', 'P', '\0' };
    uint32_t version = 6;
    uint64_t count = (uint64_t)buf->count;
    uint64_t currentFrameNo = buf->current_frame_no;
    uint64_t romChecksum = rom_checksum;
    uint64_t prevSize = (uint64_t)buf->prev_size;
    if (fwrite(magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&currentFrameNo, sizeof(currentFrameNo), 1, f) != 1 ||
        fwrite(&romChecksum, sizeof(romChecksum), 1, f) != 1 ||
        fwrite(&count, sizeof(count), 1, f) != 1 ||
        fwrite(&prevSize, sizeof(prevSize), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    for (size_t i = 0; i < buf->count; ++i) {
        state_frame_t *frame = state_buffer_getFrameAtBuffer(buf, i);
        if (!frame) {
            fclose(f);
            return 0;
        }
        uint64_t id = frame->id;
        uint64_t frameNo = frame->frame_no;
        uint32_t isKeyframe = frame->is_keyframe ? 1u : 0u;
        uint64_t stateSize = (uint64_t)frame->state_size;
        uint64_t payloadSize = (uint64_t)frame->payload_size;
        if (fwrite(&id, sizeof(id), 1, f) != 1 ||
            fwrite(&frameNo, sizeof(frameNo), 1, f) != 1 ||
            fwrite(&isKeyframe, sizeof(isKeyframe), 1, f) != 1 ||
            fwrite(&stateSize, sizeof(stateSize), 1, f) != 1 ||
            fwrite(&payloadSize, sizeof(payloadSize), 1, f) != 1) {
            fclose(f);
            return 0;
        }
        if (payloadSize > 0 && frame->payload) {
            if (fwrite(frame->payload, 1, (size_t)payloadSize, f) != payloadSize) {
                fclose(f);
                return 0;
            }
        }
    }
    if (prevSize > 0 && buf->prev_state) {
        if (fwrite(buf->prev_state, 1, (size_t)prevSize, f) != prevSize) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

int
state_buffer_loadSnapshotFile(const char *path, uint64_t *out_rom_checksum)
{
    if (!path || !*path) {
        return 0;
    }
    if (out_rom_checksum) {
        *out_rom_checksum = 0;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    char magic[8] = {0};
    uint32_t version = 0;
    uint64_t currentFrameNo = 0;
    uint64_t romChecksum = 0;
    uint64_t count = 0;
    uint64_t prevSize = 0;
    if (fread(magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&currentFrameNo, sizeof(currentFrameNo), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    if (memcmp(magic, "E9KSNAP", 7) != 0 || version != 6) {
        fclose(f);
        return 0;
    }
    if (fread(&romChecksum, sizeof(romChecksum), 1, f) != 1 ||
        fread(&count, sizeof(count), 1, f) != 1 ||
        fread(&prevSize, sizeof(prevSize), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    if (out_rom_checksum) {
        *out_rom_checksum = romChecksum;
    }
    state_buffer_reset(&state_buffer.save);
    if (count > 0) {
        state_buffer.save.frames = (state_frame_t*)alloc_calloc((size_t)count, sizeof(state_frame_t));
        if (!state_buffer.save.frames) {
            fclose(f);
            return 0;
        }
        state_buffer.save.cap = (size_t)count;
        state_buffer.save.count = (size_t)count;
        state_buffer.save.start = 0;
    }
    size_t totalBytes = 0;
    uint64_t lastId = 0;
    for (size_t i = 0; i < (size_t)count; ++i) {
        uint64_t id = 0;
        uint64_t frameNo = 0;
        uint32_t isKeyframe = 0;
        uint64_t stateSize = 0;
        uint64_t payloadSize = 0;
        if (fread(&id, sizeof(id), 1, f) != 1 ||
            fread(&frameNo, sizeof(frameNo), 1, f) != 1 ||
            fread(&isKeyframe, sizeof(isKeyframe), 1, f) != 1 ||
            fread(&stateSize, sizeof(stateSize), 1, f) != 1 ||
            fread(&payloadSize, sizeof(payloadSize), 1, f) != 1) {
            fclose(f);
            state_buffer_reset(&state_buffer.save);
            return 0;
        }
        state_frame_t *frame = &state_buffer.save.frames[i];
        frame->id = id;
        frame->frame_no = frameNo;
        frame->is_keyframe = isKeyframe ? 1 : 0;
        frame->state_size = (size_t)stateSize;
        frame->payload_size = (size_t)payloadSize;
        if (payloadSize > 0) {
            frame->payload = (uint8_t*)alloc_alloc((size_t)payloadSize);
            if (!frame->payload) {
                fclose(f);
                state_buffer_reset(&state_buffer.save);
                return 0;
            }
            if (fread(frame->payload, 1, (size_t)payloadSize, f) != payloadSize) {
                fclose(f);
                state_buffer_reset(&state_buffer.save);
                return 0;
            }
            totalBytes += frame->payload_size;
        }
        lastId = id;
    }
    if (prevSize > 0) {
        state_buffer.save.prev_state = (uint8_t*)alloc_alloc((size_t)prevSize);
        if (!state_buffer.save.prev_state) {
            fclose(f);
            state_buffer_reset(&state_buffer.save);
            return 0;
        }
        if (fread(state_buffer.save.prev_state, 1, (size_t)prevSize, f) != prevSize) {
            fclose(f);
            state_buffer_reset(&state_buffer.save);
            return 0;
        }
        state_buffer.save.prev_size = (size_t)prevSize;
    }
    state_buffer.save.total_bytes = totalBytes;
    state_buffer.save.next_id = lastId + 1;
    state_buffer.save.current_frame_no = currentFrameNo;
    state_buffer.save.max_bytes = state_buffer.current.max_bytes;
    state_buffer.save.paused = 0;
    fclose(f);
    return 1;
}

int
state_buffer_getSnapshotState(uint8_t **outState, size_t *outSize, uint64_t *outFrameNo)
{
    if (outState) {
        *outState = NULL;
    }
    if (outSize) {
        *outSize = 0;
    }
    if (outFrameNo) {
        *outFrameNo = 0;
    }
    state_buffer_t *buf = &state_buffer.save;
    if (!buf || buf->count == 0) {
        return 0;
    }
    size_t idx = buf->count - 1;
    if (buf->current_frame_no) {
        size_t found = 0;
        if (state_buffer_findIndexByFrameNoBuffer(buf, buf->current_frame_no, &found)) {
            idx = found;
        }
    }
    uint8_t *state = NULL;
    size_t stateSize = 0;
    if (!state_buffer_reconstructIndexBuffer(buf, idx, &state, &stateSize)) {
        return 0;
    }
    uint8_t *copy = (uint8_t*)alloc_alloc(stateSize);
    if (!copy) {
        return 0;
    }
    memcpy(copy, state, stateSize);
    if (outState) {
        *outState = copy;
    } else {
        alloc_free(copy);
    }
    if (outSize) {
        *outSize = stateSize;
    }
    if (outFrameNo) {
        state_frame_t *frame = state_buffer_getFrameAtBuffer(buf, idx);
        *outFrameNo = frame ? frame->frame_no : 0;
    }
    return 1;
}
