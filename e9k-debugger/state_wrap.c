/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdint.h>
#include <string.h>

#include "state_wrap.h"

typedef struct state_wrap_header_v1 {
    char magic[8];
    uint32_t version;
    uint32_t headerSize;
    uint32_t payloadSize;
    uint32_t textBaseAddr;
    uint32_t dataBaseAddr;
    uint32_t bssBaseAddr;
} state_wrap_header_v1_t;

static const char state_wrap_magic[8] = { 'E', '9', 'K', 'S', 'T', 'A', 'T', 'E' };
static const uint32_t state_wrap_version = 1;

size_t
state_wrap_headerSize(void)
{
    return sizeof(state_wrap_header_v1_t);
}

size_t
state_wrap_wrappedSize(size_t payloadSize)
{
    return state_wrap_headerSize() + payloadSize;
}

int
state_wrap_writeHeader(uint8_t *dst, size_t dstCap, size_t payloadSize, const machine_t *machine)
{
    if (!dst || dstCap < state_wrap_wrappedSize(payloadSize)) {
        return 0;
    }

    state_wrap_header_v1_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, state_wrap_magic, sizeof(hdr.magic));
    hdr.version = state_wrap_version;
    hdr.headerSize = (uint32_t)sizeof(hdr);
    hdr.payloadSize = (uint32_t)payloadSize;
    hdr.textBaseAddr = machine ? machine->textBaseAddr : 0;
    hdr.dataBaseAddr = machine ? machine->dataBaseAddr : 0;
    hdr.bssBaseAddr = machine ? machine->bssBaseAddr : 0;

    memcpy(dst, &hdr, sizeof(hdr));
    return 1;
}

int
state_wrap_wrap(uint8_t *dst, size_t dstCap, const uint8_t *payload, size_t payloadSize, const machine_t *machine)
{
    if (!dst || !payload || payloadSize == 0) {
        return 0;
    }
    if (!state_wrap_writeHeader(dst, dstCap, payloadSize, machine)) {
        return 0;
    }
    memcpy(dst + state_wrap_headerSize(), payload, payloadSize);
    return 1;
}

int
state_wrap_parse(const uint8_t *buf, size_t bufSize, state_wrap_info_t *out)
{
    if (!buf || !out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));

    if (bufSize < sizeof(state_wrap_header_v1_t)) {
        return 0;
    }
    state_wrap_header_v1_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    if (memcmp(hdr.magic, state_wrap_magic, sizeof(state_wrap_magic)) != 0) {
        return 0;
    }
    if (hdr.headerSize < sizeof(state_wrap_header_v1_t)) {
        return 0;
    }
    if ((size_t)hdr.headerSize > bufSize) {
        return 0;
    }
    if ((size_t)hdr.headerSize + (size_t)hdr.payloadSize > bufSize) {
        return 0;
    }

    out->version = hdr.version;
    out->textBaseAddr = hdr.textBaseAddr;
    out->dataBaseAddr = hdr.dataBaseAddr;
    out->bssBaseAddr = hdr.bssBaseAddr;
    out->payload = buf + hdr.headerSize;
    out->payloadSize = (size_t)hdr.payloadSize;
    return 1;
}
