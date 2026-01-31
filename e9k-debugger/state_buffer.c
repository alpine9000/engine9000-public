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
#include "state_wrap.h"

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

#define STATE_BUFFER_LEVEL_COUNT 6

typedef struct {
    state_frame_t *frames;
    size_t count;
    size_t cap;
    size_t start;
    size_t total_bytes;
    size_t max_bytes;
    uint8_t *prev_state;
    size_t prev_size;
} state_buffer_level_t;

typedef struct {
    state_buffer_level_t levels[STATE_BUFFER_LEVEL_COUNT];
    size_t total_bytes;
    size_t max_bytes;
    uint64_t next_id;
    uint8_t *temp_state;
    size_t temp_size;
    uint8_t *diff_scratch;
    size_t diff_scratch_cap;
    uint8_t *recon_a;
    uint8_t *recon_b;
    size_t recon_size;
    int paused;
    int rollingPaused;
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
state_buffer_resetLevel(state_buffer_level_t *lvl)
{
    if (!lvl) {
        return;
    }
    for (size_t i = 0; i < lvl->count; ++i) {
        state_frame_t *f = &lvl->frames[lvl->start + i];
        state_buffer_clearFrame(f);
    }
    alloc_free(lvl->frames);
    lvl->frames = NULL;
    lvl->count = 0;
    lvl->cap = 0;
    lvl->start = 0;
    lvl->total_bytes = 0;
    lvl->max_bytes = 0;
    alloc_free(lvl->prev_state);
    lvl->prev_state = NULL;
    lvl->prev_size = 0;
}

static void
state_buffer_reset(state_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    for (int i = 0; i < STATE_BUFFER_LEVEL_COUNT; ++i) {
        state_buffer_resetLevel(&buf->levels[i]);
    }
    buf->total_bytes = 0;
    buf->max_bytes = 0;
    buf->next_id = 0;
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
    buf->rollingPaused = 0;
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
state_buffer_compactLevel(state_buffer_level_t *lvl)
{
    if (!lvl || lvl->start == 0 || lvl->count == 0) {
        return;
    }
    memmove(lvl->frames, lvl->frames + lvl->start, lvl->count * sizeof(state_frame_t));
    lvl->start = 0;
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

static state_frame_t *
state_buffer_level_getFrameAt(state_buffer_level_t *lvl, size_t idx)
{
    if (!lvl || idx >= lvl->count) {
        return NULL;
    }
    return &lvl->frames[lvl->start + idx];
}

static int
state_buffer_level_ensureSlots(state_buffer_level_t *lvl, size_t additional)
{
    if (!lvl) {
        return 0;
    }
    if (lvl->start + lvl->count + additional <= lvl->cap) {
        return 1;
    }
    if (lvl->start > 0) {
        state_buffer_compactLevel(lvl);
        if (lvl->start + lvl->count + additional <= lvl->cap) {
            return 1;
        }
    }
    size_t newCap = lvl->cap ? lvl->cap * 2 : 64;
    while (newCap < (lvl->count + additional)) {
        newCap *= 2;
    }
    state_frame_t *tmp = (state_frame_t*)alloc_realloc(lvl->frames, newCap * sizeof(state_frame_t));
    if (!tmp) {
        return 0;
    }
    lvl->frames = tmp;
    lvl->cap = newCap;
    return 1;
}

static void
state_buffer_level_dropPrefix(state_buffer_t *buf, state_buffer_level_t *lvl, size_t count)
{
    if (!buf || !lvl || count == 0 || lvl->count == 0) {
        return;
    }
    if (count > lvl->count) {
        count = lvl->count;
    }
    for (size_t i = 0; i < count; ++i) {
        state_frame_t *f = &lvl->frames[lvl->start + i];
        lvl->total_bytes -= f->payload_size;
        buf->total_bytes -= f->payload_size;
        state_buffer_clearFrame(f);
    }
    lvl->start += count;
    lvl->count -= count;
    if (lvl->count == 0) {
        lvl->start = 0;
    } else if (lvl->start > 32 && lvl->start > lvl->cap / 2) {
        state_buffer_compactLevel(lvl);
    }
}

static void
state_buffer_level_dropTailFromIndex(state_buffer_t *buf, state_buffer_level_t *lvl, size_t idx)
{
    if (!buf || !lvl || lvl->count == 0) {
        return;
    }
    if (idx >= lvl->count) {
        return;
    }
    for (size_t i = idx; i < lvl->count; ++i) {
        state_frame_t *f = state_buffer_level_getFrameAt(lvl, i);
        if (f) {
            lvl->total_bytes -= f->payload_size;
            buf->total_bytes -= f->payload_size;
            state_buffer_clearFrame(f);
        }
    }
    lvl->count = idx;
    if (lvl->count == 0) {
        lvl->start = 0;
    }
}

static int
state_buffer_level_convertToKeyframe(state_buffer_t *buf, state_buffer_level_t *lvl, size_t idx,
                                     const uint8_t *state, size_t stateSize)
{
    if (!buf || !lvl || !state || stateSize == 0) {
        return 0;
    }
    state_frame_t *f = state_buffer_level_getFrameAt(lvl, idx);
    if (!f) {
        return 0;
    }
    if (f->is_keyframe) {
        return 1;
    }
    uint8_t *payload = (uint8_t*)alloc_alloc(stateSize);
    if (!payload) {
        return 0;
    }
    memcpy(payload, state, stateSize);

    lvl->total_bytes -= f->payload_size;
    buf->total_bytes -= f->payload_size;
    alloc_free(f->payload);
    f->payload = payload;
    f->payload_size = stateSize;
    f->is_keyframe = 1;
    lvl->total_bytes += f->payload_size;
    buf->total_bytes += f->payload_size;
    return 1;
}

static int
state_buffer_level_appendState(state_buffer_t *buf, state_buffer_level_t *lvl,
                               const uint8_t *state, size_t stateSize, uint64_t frameNo)
{
    if (!buf || !lvl || !state || stateSize == 0) {
        return 0;
    }
    if (!state_buffer_level_ensureSlots(lvl, 1)) {
        return 0;
    }

    int havePrev = (lvl->count > 0 && lvl->prev_state && lvl->prev_size == stateSize);
    int isKeyframe = havePrev ? 0 : 1;
    size_t payloadSize = stateSize;
    const uint8_t *payloadSrc = state;

    if (havePrev) {
        const size_t diffCap = diff_payload_max_size(stateSize);
        if (!buf->diff_scratch || buf->diff_scratch_cap < diffCap) {
            uint8_t *tmp = (uint8_t*)alloc_realloc(buf->diff_scratch, diffCap);
            if (!tmp) {
                return 0;
            }
            buf->diff_scratch = tmp;
            buf->diff_scratch_cap = diffCap;
        }
        size_t diffSize = write_diff_payload(buf->diff_scratch, buf->diff_scratch_cap, lvl->prev_state, state, stateSize);
        if (diffSize > 0 && diffSize < stateSize) {
            isKeyframe = 0;
            payloadSize = diffSize;
            payloadSrc = buf->diff_scratch;
        } else {
            isKeyframe = 1;
            payloadSize = stateSize;
            payloadSrc = state;
        }
    }

    state_frame_t *frame = &lvl->frames[lvl->start + lvl->count];
    memset(frame, 0, sizeof(*frame));
    frame->id = buf->next_id++;
    frame->frame_no = frameNo;
    frame->state_size = stateSize;
    frame->is_keyframe = isKeyframe ? 1 : 0;
    frame->payload_size = payloadSize;
    frame->payload = (uint8_t*)alloc_alloc(payloadSize);
    if (!frame->payload) {
        return 0;
    }
    memcpy(frame->payload, payloadSrc, payloadSize);

    lvl->count++;
    lvl->total_bytes += payloadSize;
    buf->total_bytes += payloadSize;

    if (!lvl->prev_state || lvl->prev_size != stateSize) {
        uint8_t *prev = (uint8_t*)alloc_realloc(lvl->prev_state, stateSize);
        if (!prev) {
            return 0;
        }
        lvl->prev_state = prev;
        lvl->prev_size = stateSize;
    }
    memcpy(lvl->prev_state, state, stateSize);

    if (lvl->count == 1) {
        lvl->frames[lvl->start].is_keyframe = 1;
    }
    return 1;
}

static int
state_buffer_level_reconstructIndex(state_buffer_t *buf, state_buffer_level_t *lvl, size_t idx,
                                    uint8_t **outState, size_t *outSize)
{
    if (outState) {
        *outState = NULL;
    }
    if (outSize) {
        *outSize = 0;
    }
    if (!buf || !lvl || lvl->count == 0 || idx >= lvl->count) {
        return 0;
    }

    state_frame_t *target = state_buffer_level_getFrameAt(lvl, idx);
    if (!target || !target->payload || target->state_size == 0) {
        return 0;
    }
    size_t keyIdx = idx;
    while (keyIdx > 0) {
        state_frame_t *f = state_buffer_level_getFrameAt(lvl, keyIdx);
        if (f && f->is_keyframe) {
            break;
        }
        keyIdx--;
    }
    state_frame_t *key = state_buffer_level_getFrameAt(lvl, keyIdx);
    if (!key || !key->is_keyframe || !key->payload || key->state_size == 0) {
        return 0;
    }

    const size_t stateSize = key->state_size;
    if (!state_buffer_ensureRecon(buf, stateSize)) {
        return 0;
    }
    uint8_t *cur = buf->recon_a;
    memcpy(cur, key->payload, stateSize);

    for (size_t i = keyIdx + 1; i <= idx; ++i) {
        state_frame_t *f = state_buffer_level_getFrameAt(lvl, i);
        if (!f || !f->payload || f->state_size != stateSize) {
            return 0;
        }
        if (f->is_keyframe) {
            memcpy(cur, f->payload, stateSize);
            continue;
        }
        if (!apply_diff_inplace(cur, stateSize, f->payload, f->payload_size)) {
            return 0;
        }
    }

    if (outState) {
        *outState = cur;
    }
    if (outSize) {
        *outSize = stateSize;
    }
    return 1;
}

static void
state_buffer_dropOldestToFit(state_buffer_t *buf, int levelIndex)
{
    state_buffer_level_t *lvl = &buf->levels[levelIndex];
    if (!buf || !lvl || lvl->count == 0) {
        return;
    }
    state_frame_t *first = state_buffer_level_getFrameAt(lvl, 0);
    if (!first || !first->is_keyframe) {
        return;
    }
    if (lvl->total_bytes <= lvl->max_bytes) {
        return;
    }

    size_t bytesToFree = lvl->total_bytes - lvl->max_bytes;
    size_t n = 0;
    size_t bytes = 0;
    const size_t stateSize = first->state_size;
    while (n < lvl->count && bytes < bytesToFree) {
        state_frame_t *f = state_buffer_level_getFrameAt(lvl, n);
        if (!f || f->state_size != stateSize) {
            break;
        }
        bytes += f->payload_size;
        n++;
    }
    if (n == 0) {
        return;
    }
    if (n >= lvl->count) {
        state_buffer_level_dropPrefix(buf, lvl, lvl->count);
        alloc_free(lvl->prev_state);
        lvl->prev_state = NULL;
        lvl->prev_size = 0;
        return;
    }

    state_frame_t *newOldest = state_buffer_level_getFrameAt(lvl, n);
    if (newOldest && newOldest->state_size == stateSize && !newOldest->is_keyframe) {
        if (state_buffer_ensureRecon(buf, stateSize)) {
            uint8_t *work = buf->recon_a;
            memcpy(work, first->payload, stateSize);
            for (size_t i = 1; i <= n; ++i) {
                state_frame_t *f = state_buffer_level_getFrameAt(lvl, i);
                if (!f || !f->payload || f->state_size != stateSize) {
                    break;
                }
                if (f->is_keyframe) {
                    memcpy(work, f->payload, stateSize);
                } else {
                    apply_diff_inplace_fast(work, stateSize, f->payload);
                }
            }
            state_buffer_level_convertToKeyframe(buf, lvl, n, work, stateSize);
        }
    }

    state_buffer_level_dropPrefix(buf, lvl, n);
}

#define STATE_BUFFER_PROMOTE_MAX_FRAMES 512u

static void
state_buffer_promoteOldest(state_buffer_t *buf, int levelIndex)
{
    state_buffer_level_t *src = &buf->levels[levelIndex];
    state_buffer_level_t *dst = &buf->levels[levelIndex + 1];
    if (!buf || !src || !dst || src->count == 0) {
        return;
    }
    state_frame_t *first = state_buffer_level_getFrameAt(src, 0);
    if (!first || !first->is_keyframe || !first->payload || first->state_size == 0) {
        return;
    }
    const size_t stateSize = first->state_size;

    size_t bytesToFree = 0;
    if (src->total_bytes > src->max_bytes) {
        bytesToFree = src->total_bytes - src->max_bytes;
    }

    size_t n = 0;
    size_t bytes = 0;
    const size_t maxFrames = (size_t)STATE_BUFFER_PROMOTE_MAX_FRAMES;
    while (n < src->count && bytes < bytesToFree && n < maxFrames) {
        state_frame_t *f = state_buffer_level_getFrameAt(src, n);
        if (!f || f->state_size != stateSize) {
            break;
        }
        bytes += f->payload_size;
        n++;
    }
    if (n < 2 && src->count >= 2) {
        n = 2;
    }
    if (n == 0) {
        return;
    }
    if (n > src->count) {
        n = src->count;
    }

    if (!state_buffer_ensureRecon(buf, stateSize)) {
        return;
    }
    uint8_t *work = buf->recon_a;
    memcpy(work, first->payload, stateSize);

    // Keep every 2nd frame from the promoted range (0, 2, 4, ...).
    state_buffer_level_appendState(buf, dst, work, stateSize, first->frame_no);
    for (size_t i = 1; i < n; ++i) {
        state_frame_t *f = state_buffer_level_getFrameAt(src, i);
        if (!f || !f->payload || f->state_size != stateSize) {
            break;
        }
        if (f->is_keyframe) {
            memcpy(work, f->payload, stateSize);
        } else {
            apply_diff_inplace_fast(work, stateSize, f->payload);
        }
        if ((i & 1u) == 0u) {
            state_buffer_level_appendState(buf, dst, work, stateSize, f->frame_no);
        }
    }

    // Ensure the next oldest frame in the source level remains reconstructable after dropping the prefix.
    if (n < src->count) {
        state_frame_t *newOldest = state_buffer_level_getFrameAt(src, n);
        if (newOldest && newOldest->state_size == stateSize && !newOldest->is_keyframe) {
            if (newOldest->is_keyframe) {
                memcpy(work, newOldest->payload, stateSize);
            } else {
                apply_diff_inplace_fast(work, stateSize, newOldest->payload);
            }
            state_buffer_level_convertToKeyframe(buf, src, n, work, stateSize);
        }
    }

    state_buffer_level_dropPrefix(buf, src, n);
}

static void
state_buffer_trimLevels(state_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    for (int i = 0; i < STATE_BUFFER_LEVEL_COUNT; ++i) {
        state_buffer_level_t *lvl = &buf->levels[i];
        while (lvl->max_bytes > 0 && lvl->total_bytes > lvl->max_bytes && lvl->count > 0) {
            if (i == STATE_BUFFER_LEVEL_COUNT - 1) {
                state_buffer_dropOldestToFit(buf, i);
            } else {
                state_buffer_promoteOldest(buf, i);
            }
        }
    }
}

static void
state_buffer_configureLevelBudgets(state_buffer_t *buf, size_t maxBytes)
{
    if (!buf) {
        return;
    }
    buf->max_bytes = maxBytes;
    size_t remaining = maxBytes;
    for (int i = 0; i < STATE_BUFFER_LEVEL_COUNT; ++i) {
        size_t b = 0;
        if (i == STATE_BUFFER_LEVEL_COUNT - 1) {
            b = remaining;
        } else {
            b = remaining / 2;
            remaining -= b;
        }
        buf->levels[i].max_bytes = b;
    }
}

static int
state_buffer_findFrameByFrameNo(state_buffer_t *buf, uint64_t frameNo, int *outLevel, size_t *outIdx)
{
    if (outLevel) {
        *outLevel = 0;
    }
    if (outIdx) {
        *outIdx = 0;
    }
    if (!buf) {
        return 0;
    }
    for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
        state_buffer_level_t *lvl = &buf->levels[li];
        for (size_t i = 0; i < lvl->count; ++i) {
            state_frame_t *f = state_buffer_level_getFrameAt(lvl, i);
            if (f && f->frame_no == frameNo) {
                if (outLevel) {
                    *outLevel = li;
                }
                if (outIdx) {
                    *outIdx = i;
                }
                return 1;
            }
        }
    }
    return 0;
}

void
state_buffer_init(size_t max_bytes)
{
    memset(&state_buffer.current, 0, sizeof(state_buffer.current));
    state_buffer_configureLevelBudgets(&state_buffer.current, max_bytes);
    memset(&state_buffer.save, 0, sizeof(state_buffer.save));
    state_buffer_configureLevelBudgets(&state_buffer.save, max_bytes);
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
        // return;
    }
    if (state_buffer.current.paused || state_buffer.current.rollingPaused) {
        return;
    }
    if (state_buffer.current.max_bytes == 0) {
        return;
    }

    size_t stateSize = 0;
    if (!libretro_host_getSerializeSize(&stateSize) || stateSize == 0) {
        return;
    }
    size_t headerSize = state_wrap_headerSize();
    size_t wrappedSize = headerSize + stateSize;
    if (!state_buffer.current.temp_state || state_buffer.current.temp_size != wrappedSize) {
        uint8_t *tmp = (uint8_t*)alloc_realloc(state_buffer.current.temp_state, wrappedSize);
        if (!tmp) {
            return;
        }
        state_buffer.current.temp_state = tmp;
        state_buffer.current.temp_size = wrappedSize;
    }
    if (!libretro_host_serializeTo(state_buffer.current.temp_state + headerSize, stateSize)) {
        return;
    }
    if (!state_wrap_writeHeader(state_buffer.current.temp_state, state_buffer.current.temp_size, stateSize, &debugger.machine)) {
        return;
    }

    state_buffer_level_t *lvl0 = &state_buffer.current.levels[0];
    if (!state_buffer_level_appendState(&state_buffer.current, lvl0, state_buffer.current.temp_state, wrappedSize,
                                        state_buffer.current.current_frame_no)) {
        return;
    }
    state_buffer_trimLevels(&state_buffer.current);
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

void
state_buffer_setRollingPaused(int paused)
{
    state_buffer.current.rollingPaused = paused ? 1 : 0;
}

int
state_buffer_isRollingPaused(void)
{
    return state_buffer.current.rollingPaused ? 1 : 0;
}

size_t
state_buffer_getUsedBytes(void)
{
    return state_buffer.current.total_bytes;
}

size_t
state_buffer_getCount(void)
{
    size_t count = 0;
    for (int i = 0; i < STATE_BUFFER_LEVEL_COUNT; ++i) {
        count += state_buffer.current.levels[i].count;
    }
    return count;
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

state_frame_t*
state_buffer_getFrameAtPercent(float percent)
{
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;

    // Mipmap-aware mapping: percent corresponds to position in the overall
    // frame_no range (oldest..newest), not "index in stored frames".
    // This keeps the slider feeling time-linear even as older history is thinned.

    state_frame_t *oldest = NULL;
    state_frame_t *newest = NULL;

    for (int li = STATE_BUFFER_LEVEL_COUNT - 1; li >= 0; --li) {
        state_buffer_level_t *lvl = &state_buffer.current.levels[li];
        if (lvl->count > 0) {
            oldest = state_buffer_level_getFrameAt(lvl, 0);
            break;
        }
    }
    for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
        state_buffer_level_t *lvl = &state_buffer.current.levels[li];
        if (lvl->count > 0) {
            newest = state_buffer_level_getFrameAt(lvl, lvl->count - 1);
            break;
        }
    }
    if (!oldest || !newest) {
        return NULL;
    }

    const uint64_t minFrameNo = oldest->frame_no;
    const uint64_t maxFrameNo = newest->frame_no;
    if (minFrameNo >= maxFrameNo) {
        return newest;
    }

    const uint64_t span = maxFrameNo - minFrameNo;
    const uint64_t off = (uint64_t)((long double)span * (long double)percent + 0.5L);
    const uint64_t targetFrameNo = minFrameNo + off;

    // Find the tier that contains targetFrameNo, then binary search within it.
    for (int li = STATE_BUFFER_LEVEL_COUNT - 1; li >= 0; --li) {
        state_buffer_level_t *lvl = &state_buffer.current.levels[li];
        if (lvl->count == 0) {
            continue;
        }
        state_frame_t *first = state_buffer_level_getFrameAt(lvl, 0);
        state_frame_t *last = state_buffer_level_getFrameAt(lvl, lvl->count - 1);
        if (!first || !last) {
            continue;
        }
        if (targetFrameNo > last->frame_no) {
            continue;
        }

        if (targetFrameNo <= first->frame_no) {
            return first;
        }

        size_t lo = 0;
        size_t hi = lvl->count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            state_frame_t *m = state_buffer_level_getFrameAt(lvl, mid);
            if (!m) {
                break;
            }
            if (m->frame_no <= targetFrameNo) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        size_t idx = (lo == 0) ? 0 : (lo - 1);
        return state_buffer_level_getFrameAt(lvl, idx);
    }

    return newest;
}


int
state_buffer_hasFrameNo(uint64_t frame_no)
{
    return state_buffer_findFrameByFrameNo(&state_buffer.current, frame_no, NULL, NULL);
}

int
state_buffer_restoreFrameNo(uint64_t frame_no)
{
    int levelIndex = 0;
    size_t idx = 0;
    if (!state_buffer_findFrameByFrameNo(&state_buffer.current, frame_no, &levelIndex, &idx)) {
        return 0;
    }
    state_buffer_level_t *lvl = &state_buffer.current.levels[levelIndex];
    state_frame_t *target = state_buffer_level_getFrameAt(lvl, idx);
    if (!target || target->state_size == 0) {
        return 0;
    }
    uint8_t *state = NULL;
    size_t state_size = 0;
    if (!state_buffer_level_reconstructIndex(&state_buffer.current, lvl, idx, &state, &state_size)) {
        return 0;
    }
    state_wrap_info_t info;
    if (!state_wrap_parse(state, state_size, &info)) {
        return 0;
    }
    debugger.machine.textBaseAddr = info.textBaseAddr;
    debugger.machine.dataBaseAddr = info.dataBaseAddr;
    debugger.machine.bssBaseAddr = info.bssBaseAddr;
    if (!libretro_host_unserializeFrom(info.payload, info.payloadSize)) {
        return 0;
    }
    state_buffer.current.current_frame_no = target->frame_no;
    return 1;
}

int
state_buffer_trimAfterPercent(float percent)
{
    state_frame_t *f = state_buffer_getFrameAtPercent(percent);
    if (!f) {
        return 0;
    }
    return state_buffer_trimAfterFrameNo(f->frame_no);
}

int
state_buffer_trimAfterFrameNo(uint64_t frame_no)
{
    // Drop any frames newer than `frame_no` across all tiers.
    for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
        state_buffer_level_t *lvl = &state_buffer.current.levels[li];
        if (lvl->count == 0) {
            alloc_free(lvl->prev_state);
            lvl->prev_state = NULL;
            lvl->prev_size = 0;
            continue;
        }
        size_t cut = lvl->count;
        for (size_t i = 0; i < lvl->count; ++i) {
            state_frame_t *f = state_buffer_level_getFrameAt(lvl, i);
            if (f && f->frame_no > frame_no) {
                cut = i;
                break;
            }
        }
        if (cut < lvl->count) {
            state_buffer_level_dropTailFromIndex(&state_buffer.current, lvl, cut);
        }
        if (lvl->count == 0) {
            alloc_free(lvl->prev_state);
            lvl->prev_state = NULL;
            lvl->prev_size = 0;
        }
    }

    // Refresh per-level prev_state from the last remaining frame so future diffs remain valid.
    for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
        state_buffer_level_t *lvl = &state_buffer.current.levels[li];
        if (lvl->count == 0) {
            continue;
        }
        size_t last = lvl->count - 1;
        uint8_t *state = NULL;
        size_t stateSize = 0;
        if (!state_buffer_level_reconstructIndex(&state_buffer.current, lvl, last, &state, &stateSize)) {
            return 0;
        }
        if (!lvl->prev_state || lvl->prev_size != stateSize) {
            uint8_t *prev = (uint8_t*)alloc_realloc(lvl->prev_state, stateSize);
            if (!prev) {
                return 0;
            }
            lvl->prev_state = prev;
            lvl->prev_size = stateSize;
        }
        memcpy(lvl->prev_state, state, stateSize);
    }

    // Seed level 0's prev_state from the exact restored frame, even if it lives in an older tier.
    int foundLevel = 0;
    size_t foundIdx = 0;
    if (!state_buffer_findFrameByFrameNo(&state_buffer.current, frame_no, &foundLevel, &foundIdx)) {
        return 0;
    }
    state_buffer_level_t *foundLvl = &state_buffer.current.levels[foundLevel];
    uint8_t *exact = NULL;
    size_t exactSize = 0;
    if (!state_buffer_level_reconstructIndex(&state_buffer.current, foundLvl, foundIdx, &exact, &exactSize)) {
        return 0;
    }
    state_buffer_level_t *lvl0 = &state_buffer.current.levels[0];
    if (!lvl0->prev_state || lvl0->prev_size != exactSize) {
        uint8_t *prev = (uint8_t*)alloc_realloc(lvl0->prev_state, exactSize);
        if (!prev) {
            return 0;
        }
        lvl0->prev_state = prev;
        lvl0->prev_size = exactSize;
    }
    memcpy(lvl0->prev_state, exact, exactSize);
    state_buffer.current.current_frame_no = frame_no;
    return 1;
}

static int
state_buffer_clone(state_buffer_t *dst, const state_buffer_t *src)
{
    if (!dst || !src) {
        return 0;
    }
    state_buffer_reset(dst);
    state_buffer_configureLevelBudgets(dst, src->max_bytes);
    dst->total_bytes = 0;
    dst->next_id = src->next_id;
    dst->paused = src->paused;
    dst->current_frame_no = src->current_frame_no;

    for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
        const state_buffer_level_t *sLvl = &src->levels[li];
        state_buffer_level_t *dLvl = &dst->levels[li];

        dLvl->max_bytes = sLvl->max_bytes;
        dLvl->count = sLvl->count;
        dLvl->cap = sLvl->count;
        dLvl->start = 0;
        dLvl->total_bytes = 0;

        if (sLvl->count > 0) {
            dLvl->frames = (state_frame_t*)alloc_calloc(sLvl->count, sizeof(state_frame_t));
            if (!dLvl->frames) {
                state_buffer_reset(dst);
                return 0;
            }
            for (size_t i = 0; i < sLvl->count; ++i) {
                const state_frame_t *s = &sLvl->frames[sLvl->start + i];
                state_frame_t *d = &dLvl->frames[i];
                d->id = s->id;
                d->frame_no = s->frame_no;
                d->is_keyframe = s->is_keyframe;
                d->state_size = s->state_size;
                d->payload_size = s->payload_size;
                if (s->payload_size > 0 && s->payload) {
                    d->payload = (uint8_t*)alloc_alloc(s->payload_size);
                    if (!d->payload) {
                        state_buffer_reset(dst);
                        return 0;
                    }
                    memcpy(d->payload, s->payload, s->payload_size);
                    dLvl->total_bytes += s->payload_size;
                    dst->total_bytes += s->payload_size;
                }
            }
        }

        if (sLvl->prev_state && sLvl->prev_size > 0) {
            dLvl->prev_state = (uint8_t*)alloc_alloc(sLvl->prev_size);
            if (!dLvl->prev_state) {
                state_buffer_reset(dst);
                return 0;
            }
            memcpy(dLvl->prev_state, sLvl->prev_state, sLvl->prev_size);
            dLvl->prev_size = sLvl->prev_size;
        }
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
    size_t saveCount = 0;
    for (int i = 0; i < STATE_BUFFER_LEVEL_COUNT; ++i) {
        saveCount += state_buffer.save.levels[i].count;
    }
    if (saveCount == 0) {
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
    state_buffer_configureLevelBudgets(&state_buffer.save, state_buffer.current.max_bytes);

    state_buffer_level_t *lvl0 = &state_buffer.save.levels[0];
    lvl0->frames = (state_frame_t*)alloc_calloc(1, sizeof(state_frame_t));
    if (!lvl0->frames) {
        state_buffer_reset(&state_buffer.save);
        return 0;
    }

    state_wrap_info_t info;
    if (!state_wrap_parse(state, state_size, &info)) {
        state_buffer_reset(&state_buffer.save);
        return 0;
    }
    size_t storeSize = state_size;
    uint8_t *payload = (uint8_t*)alloc_alloc(storeSize);
    if (!payload) {
        state_buffer_reset(&state_buffer.save);
        return 0;
    }
    memcpy(payload, state, state_size);

    state_frame_t *frame = &lvl0->frames[0];
    frame->id = 1;
    frame->frame_no = frame_no;
    frame->is_keyframe = 1;
    frame->payload_size = storeSize;
    frame->payload = payload;
    frame->state_size = storeSize;

    lvl0->cap = 1;
    lvl0->count = 1;
    lvl0->start = 0;
    lvl0->total_bytes = storeSize;
    state_buffer.save.total_bytes = storeSize;
    state_buffer.save.next_id = 2;
    state_buffer.save.current_frame_no = frame_no;
    state_buffer.save.paused = 0;
    lvl0->prev_state = (uint8_t*)alloc_alloc(storeSize);
    if (!lvl0->prev_state) {
        state_buffer_reset(&state_buffer.save);
        return 0;
    }
    memcpy(lvl0->prev_state, payload, storeSize);
    lvl0->prev_size = storeSize;
    return 1;
}

int
state_buffer_saveSnapshotFile(const char *path, uint64_t rom_checksum)
{
    if (!path || !*path) {
        return 0;
    }
    state_buffer_t *buf = &state_buffer.save;
    size_t totalCount = 0;
    for (int i = 0; i < STATE_BUFFER_LEVEL_COUNT; ++i) {
        totalCount += buf->levels[i].count;
    }
    if (totalCount == 0) {
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    const char magic[8] = { 'E', '9', 'K', 'S', 'N', 'A', 'P', '\0' };
    uint32_t version = 8;
    uint32_t levelCount = (uint32_t)STATE_BUFFER_LEVEL_COUNT;
    uint64_t count = (uint64_t)totalCount;
    uint64_t currentFrameNo = buf->current_frame_no;
    uint64_t romChecksum = rom_checksum;
    if (fwrite(magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&currentFrameNo, sizeof(currentFrameNo), 1, f) != 1 ||
        fwrite(&romChecksum, sizeof(romChecksum), 1, f) != 1 ||
        fwrite(&levelCount, sizeof(levelCount), 1, f) != 1 ||
        fwrite(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
        state_buffer_level_t *lvl = &buf->levels[li];
        uint64_t lvlCount = (uint64_t)lvl->count;
        uint64_t lvlPrevSize = (uint64_t)lvl->prev_size;
        if (fwrite(&lvlCount, sizeof(lvlCount), 1, f) != 1 ||
            fwrite(&lvlPrevSize, sizeof(lvlPrevSize), 1, f) != 1) {
            fclose(f);
            return 0;
        }
        for (size_t i = 0; i < lvl->count; ++i) {
            state_frame_t *frame = state_buffer_level_getFrameAt(lvl, i);
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
        if (lvlPrevSize > 0 && lvl->prev_state) {
            if (fwrite(lvl->prev_state, 1, (size_t)lvlPrevSize, f) != lvlPrevSize) {
                fclose(f);
                return 0;
            }
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
    uint32_t levelCount = 0;
    uint64_t count = 0;
    if (fread(magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&currentFrameNo, sizeof(currentFrameNo), 1, f) != 1 ||
        fread(&romChecksum, sizeof(romChecksum), 1, f) != 1 ||
        fread(&levelCount, sizeof(levelCount), 1, f) != 1 ||
        fread(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    if (memcmp(magic, "E9KSNAP", 7) != 0 || version != 8 || levelCount != (uint32_t)STATE_BUFFER_LEVEL_COUNT) {
        fclose(f);
        return 0;
    }
    if (out_rom_checksum) {
        *out_rom_checksum = romChecksum;
    }

    state_buffer_reset(&state_buffer.save);
    state_buffer_configureLevelBudgets(&state_buffer.save, state_buffer.current.max_bytes);
    state_buffer.save.current_frame_no = currentFrameNo;
    state_buffer.save.paused = 0;

    size_t totalBytes = 0;
    uint64_t lastId = 0;
    for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
        state_buffer_level_t *lvl = &state_buffer.save.levels[li];
        uint64_t lvlCount = 0;
        uint64_t lvlPrevSize = 0;
        if (fread(&lvlCount, sizeof(lvlCount), 1, f) != 1 ||
            fread(&lvlPrevSize, sizeof(lvlPrevSize), 1, f) != 1) {
            fclose(f);
            state_buffer_reset(&state_buffer.save);
            return 0;
        }
        if (lvlCount > 0) {
            lvl->frames = (state_frame_t*)alloc_calloc((size_t)lvlCount, sizeof(state_frame_t));
            if (!lvl->frames) {
                fclose(f);
                state_buffer_reset(&state_buffer.save);
                return 0;
            }
            lvl->cap = (size_t)lvlCount;
            lvl->count = (size_t)lvlCount;
            lvl->start = 0;
        }
        for (size_t i = 0; i < (size_t)lvlCount; ++i) {
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
            state_frame_t *frame = &lvl->frames[i];
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
                lvl->total_bytes += frame->payload_size;
            }
            lastId = id;
        }
        if (lvlPrevSize > 0) {
            lvl->prev_state = (uint8_t*)alloc_alloc((size_t)lvlPrevSize);
            if (!lvl->prev_state) {
                fclose(f);
                state_buffer_reset(&state_buffer.save);
                return 0;
            }
            if (fread(lvl->prev_state, 1, (size_t)lvlPrevSize, f) != lvlPrevSize) {
                fclose(f);
                state_buffer_reset(&state_buffer.save);
                return 0;
            }
            lvl->prev_size = (size_t)lvlPrevSize;
        }
    }
    state_buffer.save.total_bytes = totalBytes;
    state_buffer.save.next_id = lastId + 1;
    state_buffer.save.current_frame_no = currentFrameNo;
    (void)count;
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
    int levelIndex = 0;
    size_t idx = 0;
    if (buf->current_frame_no && state_buffer_findFrameByFrameNo(buf, buf->current_frame_no, &levelIndex, &idx)) {
        // ok
    } else {
        // newest frame in the lowest non-empty tier
        int foundLevel = -1;
        for (int li = 0; li < STATE_BUFFER_LEVEL_COUNT; ++li) {
            if (buf->levels[li].count > 0) {
                foundLevel = li;
                break;
            }
        }
        if (foundLevel < 0) {
            return 0;
        }
        levelIndex = foundLevel;
        idx = buf->levels[levelIndex].count - 1;
    }

    state_buffer_level_t *lvl = &buf->levels[levelIndex];
    uint8_t *state = NULL;
    size_t stateSize = 0;
    if (!state_buffer_level_reconstructIndex(buf, lvl, idx, &state, &stateSize)) {
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
        state_frame_t *frame = state_buffer_level_getFrameAt(lvl, idx);
        *outFrameNo = frame ? frame->frame_no : 0;
    }
    return 1;
}
