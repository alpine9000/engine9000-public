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

#include <zlib.h>

#include "debugger.h"
#include "state_buffer.h"
#include "libretro_host.h"

typedef uint32_t zstate_buffer_u32_alias_t __attribute__((__may_alias__));

static void
zstate_buffer_writeU32Fast(uint8_t *dst, uint32_t v)
{
    *(zstate_buffer_u32_alias_t*)(void*)dst = (zstate_buffer_u32_alias_t)v;
}

static uint32_t
zstate_buffer_readU32Fast(const uint8_t *src)
{
    return (uint32_t)(*(const zstate_buffer_u32_alias_t*)(const void*)src);
}

typedef struct {
    state_frame_t *frames;
    size_t count;
    size_t cap;
    size_t start;
    size_t total_bytes;
    size_t max_bytes;
    uint64_t next_id;

    uint8_t *temp_state;
    size_t temp_size;

    uint8_t *comp_scratch;
    size_t comp_scratch_cap;

    uint8_t *recon_a;
    uint8_t *recon_b;
    size_t recon_size;

    uint8_t *dict_tail;
    size_t dict_tail_len;
    uint32_t frames_since_keyframe;
    size_t dict_state_size;

    int paused;
    uint64_t current_frame_no;
} zstate_buffer_t;

typedef struct {
    zstate_buffer_t current;
    zstate_buffer_t save;
} zstate_buffer_global_t;

static zstate_buffer_global_t zstate_buffer;

#define ZSTATE_DICT_TAIL_MAX (32768u)
#define ZSTATE_KEYFRAME_INTERVAL (120u)
#define ZSTATE_KEYFRAME_FORCE_RATIO_NUM (7u)
#define ZSTATE_KEYFRAME_FORCE_RATIO_DEN (8u)
#define ZSTATE_ZLIB_LEVEL (1)

static void
zstate_buffer_clearFrame(state_frame_t *f);

static void
zstate_buffer_reset(zstate_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    for (size_t i = 0; i < buf->count; ++i) {
        state_frame_t *f = &buf->frames[buf->start + i];
        zstate_buffer_clearFrame(f);
    }
    alloc_free(buf->frames);
    buf->frames = NULL;
    buf->count = 0;
    buf->cap = 0;
    buf->start = 0;
    buf->total_bytes = 0;
    buf->next_id = 0;

    alloc_free(buf->temp_state);
    buf->temp_state = NULL;
    buf->temp_size = 0;

    alloc_free(buf->comp_scratch);
    buf->comp_scratch = NULL;
    buf->comp_scratch_cap = 0;

    alloc_free(buf->recon_a);
    buf->recon_a = NULL;
    alloc_free(buf->recon_b);
    buf->recon_b = NULL;
    buf->recon_size = 0;

    alloc_free(buf->dict_tail);
    buf->dict_tail = NULL;
    buf->dict_tail_len = 0;
    buf->frames_since_keyframe = 0;
    buf->dict_state_size = 0;

    buf->paused = 0;
    buf->current_frame_no = 0;
}

static void
zstate_buffer_clearFrame(state_frame_t *f)
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
zstate_buffer_ensureRecon(zstate_buffer_t *buf, size_t size)
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
zstate_buffer_compact(zstate_buffer_t *buf)
{
    if (!buf || buf->start == 0 || buf->count == 0) {
        return;
    }
    memmove(buf->frames, buf->frames + buf->start, buf->count * sizeof(state_frame_t));
    buf->start = 0;
}

static void
zstate_buffer_updateDictTail(zstate_buffer_t *buf, const uint8_t *state, size_t stateSize)
{
    if (!buf || !state || stateSize == 0) {
        return;
    }
    if (!buf->dict_tail) {
        buf->dict_tail = (uint8_t*)alloc_alloc(ZSTATE_DICT_TAIL_MAX);
        if (!buf->dict_tail) {
            buf->dict_tail_len = 0;
            buf->dict_state_size = 0;
            return;
        }
    }
    size_t dictLen = stateSize;
    if (dictLen > ZSTATE_DICT_TAIL_MAX) {
        dictLen = ZSTATE_DICT_TAIL_MAX;
    }
    memcpy(buf->dict_tail, state + (stateSize - dictLen), dictLen);
    buf->dict_tail_len = dictLen;
    buf->dict_state_size = stateSize;
    buf->frames_since_keyframe = 0;
}

static int
zstate_buffer_ensureCompScratch(zstate_buffer_t *buf, size_t stateSize)
{
    if (!buf) {
        return 0;
    }
    uLong bound = compressBound((uLong)stateSize);
    if (bound == 0) {
        return 0;
    }
    if (buf->comp_scratch && buf->comp_scratch_cap >= (size_t)bound) {
        return 1;
    }
    uint8_t *tmp = (uint8_t*)alloc_realloc(buf->comp_scratch, (size_t)bound);
    if (!tmp) {
        return 0;
    }
    buf->comp_scratch = tmp;
    buf->comp_scratch_cap = (size_t)bound;
    return 1;
}

static int
zstate_buffer_deflateToScratch(zstate_buffer_t *buf, const uint8_t *src, size_t srcSize,
                               const uint8_t *dict, size_t dictSize, size_t *outSize)
{
    if (outSize) {
        *outSize = 0;
    }
    if (!buf || !src || srcSize == 0) {
        return 0;
    }
    if (!zstate_buffer_ensureCompScratch(buf, srcSize)) {
        return 0;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit(&strm, ZSTATE_ZLIB_LEVEL) != Z_OK) {
        return 0;
    }
    if (dict && dictSize > 0) {
        if (deflateSetDictionary(&strm, dict, (uInt)dictSize) != Z_OK) {
            deflateEnd(&strm);
            return 0;
        }
    }

    strm.next_in = (Bytef*)(void*)src;
    strm.avail_in = (uInt)srcSize;
    strm.next_out = (Bytef*)buf->comp_scratch;
    strm.avail_out = (uInt)buf->comp_scratch_cap;

    int r = deflate(&strm, Z_FINISH);
    if (r != Z_STREAM_END) {
        deflateEnd(&strm);
        return 0;
    }
    if (outSize) {
        *outSize = (size_t)strm.total_out;
    }
    deflateEnd(&strm);
    return 1;
}

static int
zstate_buffer_inflateFull(uint8_t *dst, size_t dstSize, const uint8_t *src, size_t srcSize,
                          const uint8_t *dict, size_t dictSize)
{
    if (!dst || dstSize == 0 || !src || srcSize == 0) {
        return 0;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit(&strm) != Z_OK) {
        return 0;
    }

    strm.next_in = (Bytef*)(void*)src;
    strm.avail_in = (uInt)srcSize;
    strm.next_out = (Bytef*)dst;
    strm.avail_out = (uInt)dstSize;

    for (;;) {
        int r = inflate(&strm, Z_FINISH);
        if (r == Z_STREAM_END) {
            break;
        }
        if (r == Z_NEED_DICT) {
            if (!dict || dictSize == 0) {
                inflateEnd(&strm);
                return 0;
            }
            if (inflateSetDictionary(&strm, dict, (uInt)dictSize) != Z_OK) {
                inflateEnd(&strm);
                return 0;
            }
            continue;
        }
        inflateEnd(&strm);
        return 0;
    }

    int ok = (strm.total_out == (uLong)dstSize) ? 1 : 0;
    inflateEnd(&strm);
    return ok;
}

static int
zstate_buffer_getKeyDictTail(const state_frame_t *key, const uint8_t **outDict, size_t *outLen, const uint8_t **outData, size_t *outDataLen, uint8_t *outCodec)
{
    if (outDict) {
        *outDict = NULL;
    }
    if (outLen) {
        *outLen = 0;
    }
    if (outData) {
        *outData = NULL;
    }
    if (outDataLen) {
        *outDataLen = 0;
    }
    if (outCodec) {
        *outCodec = 0;
    }
    if (!key || !key->is_keyframe || !key->payload || key->payload_size < 5) {
        return 0;
    }

    const uint8_t codec = key->payload[0];
    const uint32_t dictLen = zstate_buffer_readU32Fast(key->payload + 1);
    const size_t headerSize = 1 + 4 + (size_t)dictLen;
    if (headerSize > key->payload_size) {
        return 0;
    }
    if (dictLen > ZSTATE_DICT_TAIL_MAX) {
        return 0;
    }

    if (outCodec) {
        *outCodec = codec;
    }
    if (outDict) {
        *outDict = key->payload + 5;
    }
    if (outLen) {
        *outLen = (size_t)dictLen;
    }
    if (outData) {
        *outData = key->payload + headerSize;
    }
    if (outDataLen) {
        *outDataLen = key->payload_size - headerSize;
    }
    return 1;
}

static int
zstate_buffer_decodeKeyframe(zstate_buffer_t *buf, const state_frame_t *key, uint8_t *outState)
{
    if (!buf || !key || !key->is_keyframe || !outState || key->state_size == 0) {
        return 0;
    }
    const uint8_t *dict = NULL;
    size_t dictLen = 0;
    const uint8_t *data = NULL;
    size_t dataLen = 0;
    uint8_t codec = 0;
    if (!zstate_buffer_getKeyDictTail(key, &dict, &dictLen, &data, &dataLen, &codec)) {
        return 0;
    }

    if (codec == 0) {
        if (dataLen != key->state_size) {
            return 0;
        }
        memcpy(outState, data, key->state_size);
        return 1;
    } else if (codec == 1) {
        return zstate_buffer_inflateFull(outState, key->state_size, data, dataLen, NULL, 0);
    }
    (void)dict;
    (void)dictLen;
    return 0;
}

static int
zstate_buffer_decodeDelta(zstate_buffer_t *buf, const state_frame_t *delta, const uint8_t *dict, size_t dictLen, uint8_t *outState)
{
    if (!buf || !delta || delta->is_keyframe || !delta->payload || delta->payload_size < 1 || !outState) {
        return 0;
    }

    const uint8_t codec = delta->payload[0];
    const uint8_t *data = delta->payload + 1;
    const size_t dataLen = delta->payload_size - 1;

    if (codec == 0) {
        if (dataLen != delta->state_size) {
            return 0;
        }
        memcpy(outState, data, delta->state_size);
        return 1;
    } else if (codec == 1) {
        return zstate_buffer_inflateFull(outState, delta->state_size, data, dataLen, dict, dictLen);
    }
    return 0;
}

static int
zstate_buffer_buildKeyframePayload(zstate_buffer_t *buf, const uint8_t *state, size_t stateSize,
                                   uint8_t **outPayload, size_t *outPayloadSize)
{
    if (outPayload) {
        *outPayload = NULL;
    }
    if (outPayloadSize) {
        *outPayloadSize = 0;
    }
    if (!buf || !state || stateSize == 0 || !outPayload || !outPayloadSize) {
        return 0;
    }

    size_t dictLen = stateSize;
    if (dictLen > ZSTATE_DICT_TAIL_MAX) {
        dictLen = ZSTATE_DICT_TAIL_MAX;
    }
    const uint8_t *dictTail = state + (stateSize - dictLen);

    size_t compSize = 0;
    int compOk = zstate_buffer_deflateToScratch(buf, state, stateSize, NULL, 0, &compSize);

    uint8_t codec = 0;
    const uint8_t *dataSrc = state;
    size_t dataLen = stateSize;
    if (compOk && compSize > 0 && compSize < stateSize) {
        codec = 1;
        dataSrc = buf->comp_scratch;
        dataLen = compSize;
    }

    const size_t headerSize = 1 + 4 + dictLen;
    const size_t totalSize = headerSize + dataLen;
    uint8_t *payload = (uint8_t*)alloc_alloc(totalSize);
    if (!payload) {
        return 0;
    }

    payload[0] = codec;
    zstate_buffer_writeU32Fast(payload + 1, (uint32_t)dictLen);
    memcpy(payload + 5, dictTail, dictLen);
    memcpy(payload + headerSize, dataSrc, dataLen);

    *outPayload = payload;
    *outPayloadSize = totalSize;
    return 1;
}

static int
zstate_buffer_buildDeltaPayload(zstate_buffer_t *buf, const uint8_t *state, size_t stateSize,
                                const uint8_t *dict, size_t dictLen, uint8_t **outPayload, size_t *outPayloadSize)
{
    if (outPayload) {
        *outPayload = NULL;
    }
    if (outPayloadSize) {
        *outPayloadSize = 0;
    }
    if (!buf || !state || stateSize == 0 || !outPayload || !outPayloadSize) {
        return 0;
    }

    size_t compSize = 0;
    int compOk = zstate_buffer_deflateToScratch(buf, state, stateSize, dict, dictLen, &compSize);

    uint8_t codec = 0;
    const uint8_t *dataSrc = state;
    size_t dataLen = stateSize;
    if (compOk && compSize > 0 && compSize < stateSize) {
        codec = 1;
        dataSrc = buf->comp_scratch;
        dataLen = compSize;
    }

    const size_t totalSize = 1 + dataLen;
    uint8_t *payload = (uint8_t*)alloc_alloc(totalSize);
    if (!payload) {
        return 0;
    }
    payload[0] = codec;
    memcpy(payload + 1, dataSrc, dataLen);

    *outPayload = payload;
    *outPayloadSize = totalSize;
    return 1;
}

static void
zstate_buffer_rekey_next(zstate_buffer_t *buf)
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
    if (!zstate_buffer_ensureRecon(buf, first->state_size)) {
        return;
    }

    const uint8_t *dict = NULL;
    size_t dictLen = 0;
    if (!zstate_buffer_getKeyDictTail(first, &dict, &dictLen, NULL, NULL, NULL)) {
        return;
    }

    uint8_t *nextState = buf->recon_a;
    if (!zstate_buffer_decodeDelta(buf, next, dict, dictLen, nextState)) {
        return;
    }

    uint8_t *newPayload = NULL;
    size_t newPayloadSize = 0;
    if (!zstate_buffer_buildKeyframePayload(buf, nextState, next->state_size, &newPayload, &newPayloadSize)) {
        return;
    }

    buf->total_bytes -= next->payload_size;
    alloc_free(next->payload);
    next->payload = newPayload;
    next->payload_size = newPayloadSize;
    buf->total_bytes += next->payload_size;
    next->is_keyframe = 1;
}

static void
zstate_buffer_trim(zstate_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    while (buf->total_bytes > buf->max_bytes && buf->count > 0) {
        if (buf->count >= 2) {
            zstate_buffer_rekey_next(buf);
        }
        state_frame_t *oldest = &buf->frames[buf->start];
        buf->total_bytes -= oldest->payload_size;
        zstate_buffer_clearFrame(oldest);
        buf->start++;
        buf->count--;
        if (buf->start > 32 && buf->start > buf->cap / 2) {
            zstate_buffer_compact(buf);
        }
    }
}

static state_frame_t *
zstate_buffer_getFrameAt(zstate_buffer_t *buf, size_t idx)
{
    if (!buf || idx >= buf->count) {
        return NULL;
    }
    return &buf->frames[buf->start + idx];
}

static int
zstate_buffer_findIndexByFrameNo(zstate_buffer_t *buf, uint64_t frameNo, size_t *outIdx)
{
    if (outIdx) {
        *outIdx = 0;
    }
    if (!buf || buf->count == 0) {
        return 0;
    }
    for (size_t i = 0; i < buf->count; ++i) {
        state_frame_t *frame = zstate_buffer_getFrameAt(buf, i);
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
zstate_buffer_reconstructIndex(zstate_buffer_t *buf, size_t idx, uint8_t **outState, size_t *outSize)
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

    state_frame_t *target = zstate_buffer_getFrameAt(buf, idx);
    if (!target || target->state_size == 0) {
        return 0;
    }
    if (!zstate_buffer_ensureRecon(buf, target->state_size)) {
        return 0;
    }

    size_t keyIdx = idx;
    while (keyIdx > 0) {
        state_frame_t *frame = zstate_buffer_getFrameAt(buf, keyIdx);
        if (frame && frame->is_keyframe) {
            break;
        }
        keyIdx--;
    }
    state_frame_t *key = zstate_buffer_getFrameAt(buf, keyIdx);
    if (!key || !key->is_keyframe || !key->payload || key->state_size != target->state_size) {
        return 0;
    }

    if (target->is_keyframe) {
        if (!zstate_buffer_decodeKeyframe(buf, target, buf->recon_a)) {
            return 0;
        }
        if (outState) {
            *outState = buf->recon_a;
        }
        if (outSize) {
            *outSize = target->state_size;
        }
        return 1;
    }

    const uint8_t *dict = NULL;
    size_t dictLen = 0;
    if (!zstate_buffer_getKeyDictTail(key, &dict, &dictLen, NULL, NULL, NULL)) {
        return 0;
    }
    if (!zstate_buffer_decodeDelta(buf, target, dict, dictLen, buf->recon_b)) {
        return 0;
    }
    if (outState) {
        *outState = buf->recon_b;
    }
    if (outSize) {
        *outSize = target->state_size;
    }
    return 1;
}

static void
zstate_buffer_refreshDictFromNewestKeyframe(zstate_buffer_t *buf)
{
    if (!buf || buf->count == 0) {
        if (buf) {
            buf->dict_tail_len = 0;
            buf->dict_state_size = 0;
            buf->frames_since_keyframe = 0;
        }
        return;
    }
    for (size_t i = buf->count; i > 0; --i) {
        state_frame_t *f = zstate_buffer_getFrameAt(buf, i - 1);
        if (!f || !f->is_keyframe) {
            continue;
        }
        const uint8_t *dict = NULL;
        size_t dictLen = 0;
        if (!zstate_buffer_getKeyDictTail(f, &dict, &dictLen, NULL, NULL, NULL)) {
            break;
        }
        if (!buf->dict_tail) {
            buf->dict_tail = (uint8_t*)alloc_alloc(ZSTATE_DICT_TAIL_MAX);
            if (!buf->dict_tail) {
                break;
            }
        }
        memcpy(buf->dict_tail, dict, dictLen);
        buf->dict_tail_len = dictLen;
        buf->dict_state_size = f->state_size;
        buf->frames_since_keyframe = 0;
        return;
    }
    buf->dict_tail_len = 0;
    buf->dict_state_size = 0;
    buf->frames_since_keyframe = 0;
}

void
state_buffer_init(size_t max_bytes)
{
    memset(&zstate_buffer.current, 0, sizeof(zstate_buffer.current));
    zstate_buffer.current.max_bytes = max_bytes;
    memset(&zstate_buffer.save, 0, sizeof(zstate_buffer.save));
}

void
state_buffer_shutdown(void)
{
    zstate_buffer_reset(&zstate_buffer.current);
    zstate_buffer_reset(&zstate_buffer.save);
}

void
state_buffer_capture(void)
{
    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        // return;
    }
    if (zstate_buffer.current.paused) {
        return;
    }
    if (zstate_buffer.current.max_bytes == 0) {
        return;
    }

    size_t stateSize = 0;
    if (!libretro_host_getSerializeSize(&stateSize) || stateSize == 0) {
        return;
    }

    if (!zstate_buffer.current.temp_state || zstate_buffer.current.temp_size != stateSize) {
        uint8_t *tmp = (uint8_t*)alloc_realloc(zstate_buffer.current.temp_state, stateSize);
        if (!tmp) {
            return;
        }
        zstate_buffer.current.temp_state = tmp;
        zstate_buffer.current.temp_size = stateSize;
    }
    if (!libretro_host_serializeTo(zstate_buffer.current.temp_state, stateSize)) {
        return;
    }

    int canDelta = (zstate_buffer.current.dict_tail && zstate_buffer.current.dict_tail_len > 0 &&
                    zstate_buffer.current.dict_state_size == stateSize &&
                    zstate_buffer.current.frames_since_keyframe < ZSTATE_KEYFRAME_INTERVAL);

    int isKeyframe = 1;
    uint8_t *payload = NULL;
    size_t payloadSize = 0;

    if (canDelta) {
        const size_t limit = (stateSize * ZSTATE_KEYFRAME_FORCE_RATIO_NUM) / ZSTATE_KEYFRAME_FORCE_RATIO_DEN;
        if (zstate_buffer_buildDeltaPayload(&zstate_buffer.current, zstate_buffer.current.temp_state, stateSize,
                                            zstate_buffer.current.dict_tail, zstate_buffer.current.dict_tail_len,
                                            &payload, &payloadSize)) {
            size_t dataLen = payloadSize > 0 ? (payloadSize - 1) : 0;
            if (payload[0] == 0) {
                dataLen = stateSize;
            }
            if (dataLen <= limit) {
                isKeyframe = 0;
            } else {
                alloc_free(payload);
                payload = NULL;
                payloadSize = 0;
                isKeyframe = 1;
            }
        }
    }

    if (isKeyframe) {
        if (!zstate_buffer_buildKeyframePayload(&zstate_buffer.current, zstate_buffer.current.temp_state, stateSize,
                                                &payload, &payloadSize)) {
            return;
        }
        zstate_buffer_updateDictTail(&zstate_buffer.current, zstate_buffer.current.temp_state, stateSize);
    } else {
        zstate_buffer.current.frames_since_keyframe++;
    }

    if (zstate_buffer.current.count + zstate_buffer.current.start >= zstate_buffer.current.cap) {
        size_t newCap = zstate_buffer.current.cap ? zstate_buffer.current.cap * 2 : 64;
        state_frame_t *tmp = (state_frame_t*)alloc_realloc(zstate_buffer.current.frames, newCap * sizeof(state_frame_t));
        if (!tmp) {
            alloc_free(payload);
            return;
        }
        zstate_buffer.current.frames = tmp;
        zstate_buffer.current.cap = newCap;
    }

    state_frame_t *frame = &zstate_buffer.current.frames[zstate_buffer.current.start + zstate_buffer.current.count];
    memset(frame, 0, sizeof(*frame));
    frame->id = zstate_buffer.current.next_id++;
    frame->frame_no = zstate_buffer.current.current_frame_no;
    frame->state_size = stateSize;
    frame->is_keyframe = isKeyframe ? 1 : 0;
    frame->payload_size = payloadSize;
    frame->payload = payload;
    zstate_buffer.current.count++;
    zstate_buffer.current.total_bytes += frame->payload_size;

    if (zstate_buffer.current.count == 1) {
        zstate_buffer.current.frames[zstate_buffer.current.start].is_keyframe = 1;
        zstate_buffer_updateDictTail(&zstate_buffer.current, zstate_buffer.current.temp_state, stateSize);
    }

    zstate_buffer_trim(&zstate_buffer.current);
    if (zstate_buffer.current.count == 0) {
        zstate_buffer.current.dict_tail_len = 0;
        zstate_buffer.current.dict_state_size = 0;
        zstate_buffer.current.frames_since_keyframe = 0;
    }
}

size_t
state_buffer_getUsedBytes(void)
{
    return zstate_buffer.current.total_bytes;
}

size_t
state_buffer_getCount(void)
{
    return zstate_buffer.current.count;
}

void
state_buffer_setPaused(int paused)
{
    zstate_buffer.current.paused = paused ? 1 : 0;
}

int
state_buffer_isPaused(void)
{
    return zstate_buffer.current.paused ? 1 : 0;
}

size_t
state_buffer_getMaxBytes(void)
{
    return zstate_buffer.current.max_bytes;
}

void
state_buffer_setCurrentFrameNo(uint64_t frame_no)
{
    zstate_buffer.current.current_frame_no = frame_no;
}

uint64_t
state_buffer_getCurrentFrameNo(void)
{
    return zstate_buffer.current.current_frame_no;
}

state_frame_t*
state_buffer_getFrameAtPercent(float percent)
{
    if (zstate_buffer.current.count == 0) {
        return NULL;
    }
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    size_t idx = (size_t)((float)(zstate_buffer.current.count - 1) * percent + 0.5f);
    if (idx >= zstate_buffer.current.count) {
        idx = zstate_buffer.current.count - 1;
    }
    return zstate_buffer_getFrameAt(&zstate_buffer.current, idx);
}

int
state_buffer_hasFrameNo(uint64_t frame_no)
{
    return zstate_buffer_findIndexByFrameNo(&zstate_buffer.current, frame_no, NULL);
}

int
state_buffer_restoreFrameNo(uint64_t frame_no)
{
    size_t idx = 0;
    if (!zstate_buffer_findIndexByFrameNo(&zstate_buffer.current, frame_no, &idx)) {
        return 0;
    }
    state_frame_t *target = zstate_buffer_getFrameAt(&zstate_buffer.current, idx);
    if (!target || target->state_size == 0) {
        return 0;
    }
    uint8_t *state = NULL;
    size_t stateSize = 0;
    if (!zstate_buffer_reconstructIndex(&zstate_buffer.current, idx, &state, &stateSize)) {
        return 0;
    }
    if (!libretro_host_unserializeFrom(state, stateSize)) {
        return 0;
    }
    zstate_buffer.current.current_frame_no = target->frame_no;
    return 1;
}

int
state_buffer_trimAfterPercent(float percent)
{
    if (zstate_buffer.current.count == 0) {
        return 0;
    }
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    size_t idx = (size_t)((float)(zstate_buffer.current.count - 1) * percent + 0.5f);
    if (idx >= zstate_buffer.current.count) {
        idx = zstate_buffer.current.count - 1;
    }
    if (idx + 1 >= zstate_buffer.current.count) {
        return 1;
    }
    for (size_t i = idx + 1; i < zstate_buffer.current.count; ++i) {
        state_frame_t *f = zstate_buffer_getFrameAt(&zstate_buffer.current, i);
        if (f) {
            zstate_buffer.current.total_bytes -= f->payload_size;
            zstate_buffer_clearFrame(f);
        }
    }
    zstate_buffer.current.count = idx + 1;
    zstate_buffer_refreshDictFromNewestKeyframe(&zstate_buffer.current);
    return 1;
}

int
state_buffer_trimAfterFrameNo(uint64_t frame_no)
{
    size_t idx = 0;
    if (!zstate_buffer_findIndexByFrameNo(&zstate_buffer.current, frame_no, &idx)) {
        return 0;
    }
    if (idx + 1 >= zstate_buffer.current.count) {
        return 1;
    }
    for (size_t i = idx + 1; i < zstate_buffer.current.count; ++i) {
        state_frame_t *f = zstate_buffer_getFrameAt(&zstate_buffer.current, i);
        if (f) {
            zstate_buffer.current.total_bytes -= f->payload_size;
            zstate_buffer_clearFrame(f);
        }
    }
    zstate_buffer.current.count = idx + 1;
    zstate_buffer_refreshDictFromNewestKeyframe(&zstate_buffer.current);
    return 1;
}

static int
zstate_buffer_clone(zstate_buffer_t *dst, const zstate_buffer_t *src)
{
    if (!dst || !src) {
        return 0;
    }
    zstate_buffer_reset(dst);
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
            zstate_buffer_reset(dst);
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
                    zstate_buffer_reset(dst);
                    return 0;
                }
                memcpy(d->payload, s->payload, s->payload_size);
            }
        }
    }
    zstate_buffer_refreshDictFromNewestKeyframe(dst);
    return 1;
}

int
state_buffer_snapshot(void)
{
    return zstate_buffer_clone(&zstate_buffer.save, &zstate_buffer.current);
}

int
state_buffer_restoreSnapshot(void)
{
    if (zstate_buffer.save.count == 0) {
        return 0;
    }
    return zstate_buffer_clone(&zstate_buffer.current, &zstate_buffer.save);
}

int
state_buffer_setSaveKeyframe(const uint8_t *state, size_t state_size, uint64_t frame_no)
{
    if (!state || state_size == 0) {
        return 0;
    }
    zstate_buffer_reset(&zstate_buffer.save);

    zstate_buffer.save.frames = (state_frame_t*)alloc_calloc(1, sizeof(state_frame_t));
    if (!zstate_buffer.save.frames) {
        zstate_buffer_reset(&zstate_buffer.save);
        return 0;
    }

    uint8_t *payload = NULL;
    size_t payloadSize = 0;
    if (!zstate_buffer_buildKeyframePayload(&zstate_buffer.save, state, state_size, &payload, &payloadSize)) {
        zstate_buffer_reset(&zstate_buffer.save);
        return 0;
    }

    state_frame_t *frame = &zstate_buffer.save.frames[0];
    frame->id = 1;
    frame->frame_no = frame_no;
    frame->is_keyframe = 1;
    frame->payload_size = payloadSize;
    frame->payload = payload;
    frame->state_size = state_size;

    zstate_buffer.save.cap = 1;
    zstate_buffer.save.count = 1;
    zstate_buffer.save.start = 0;
    zstate_buffer.save.total_bytes = payloadSize;
    zstate_buffer.save.next_id = 2;
    zstate_buffer.save.current_frame_no = frame_no;
    zstate_buffer.save.max_bytes = zstate_buffer.current.max_bytes;
    zstate_buffer.save.paused = 0;
    zstate_buffer_refreshDictFromNewestKeyframe(&zstate_buffer.save);
    return 1;
}

int
state_buffer_saveSnapshotFile(const char *path, uint64_t rom_checksum)
{
    if (!path || !*path) {
        return 0;
    }
    zstate_buffer_t *buf = &zstate_buffer.save;
    if (buf->count == 0) {
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    const char magic[8] = { 'E', '9', 'K', 'S', 'N', 'A', 'P', '\0' };
    uint32_t version = 7;
    uint64_t count = (uint64_t)buf->count;
    uint64_t currentFrameNo = buf->current_frame_no;
    uint64_t romChecksum = rom_checksum;
    uint64_t prevSize = 0;

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
        state_frame_t *frame = zstate_buffer_getFrameAt(buf, i);
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
    if (memcmp(magic, "E9KSNAP", 7) != 0 || version != 7) {
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

    zstate_buffer_reset(&zstate_buffer.save);
    if (count > 0) {
        zstate_buffer.save.frames = (state_frame_t*)alloc_calloc((size_t)count, sizeof(state_frame_t));
        if (!zstate_buffer.save.frames) {
            fclose(f);
            return 0;
        }
        zstate_buffer.save.cap = (size_t)count;
        zstate_buffer.save.count = (size_t)count;
        zstate_buffer.save.start = 0;
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
            zstate_buffer_reset(&zstate_buffer.save);
            return 0;
        }
        state_frame_t *frame = &zstate_buffer.save.frames[i];
        frame->id = id;
        frame->frame_no = frameNo;
        frame->is_keyframe = isKeyframe ? 1 : 0;
        frame->state_size = (size_t)stateSize;
        frame->payload_size = (size_t)payloadSize;
        if (payloadSize > 0) {
            frame->payload = (uint8_t*)alloc_alloc((size_t)payloadSize);
            if (!frame->payload) {
                fclose(f);
                zstate_buffer_reset(&zstate_buffer.save);
                return 0;
            }
            if (fread(frame->payload, 1, (size_t)payloadSize, f) != payloadSize) {
                fclose(f);
                zstate_buffer_reset(&zstate_buffer.save);
                return 0;
            }
            totalBytes += frame->payload_size;
        }
        lastId = id;
    }

    (void)prevSize;
    zstate_buffer.save.total_bytes = totalBytes;
    zstate_buffer.save.next_id = lastId + 1;
    zstate_buffer.save.current_frame_no = currentFrameNo;
    zstate_buffer.save.max_bytes = zstate_buffer.current.max_bytes;
    zstate_buffer.save.paused = 0;
    zstate_buffer_refreshDictFromNewestKeyframe(&zstate_buffer.save);
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
    zstate_buffer_t *buf = &zstate_buffer.save;
    if (!buf || buf->count == 0) {
        return 0;
    }
    size_t idx = buf->count - 1;
    if (buf->current_frame_no) {
        size_t found = 0;
        if (zstate_buffer_findIndexByFrameNo(buf, buf->current_frame_no, &found)) {
            idx = found;
        }
    }
    uint8_t *state = NULL;
    size_t stateSize = 0;
    if (!zstate_buffer_reconstructIndex(buf, idx, &state, &stateSize)) {
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
        state_frame_t *frame = zstate_buffer_getFrameAt(buf, idx);
        *outFrameNo = frame ? frame->frame_no : 0;
    }
    return 1;
}
