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

#include "machine.h"

typedef struct state_wrap_info {
    uint32_t version;
    uint32_t textBaseAddr;
    uint32_t dataBaseAddr;
    uint32_t bssBaseAddr;
    const uint8_t *payload;
    size_t payloadSize;
} state_wrap_info_t;

size_t
state_wrap_headerSize(void);

size_t
state_wrap_wrappedSize(size_t payloadSize);

int
state_wrap_writeHeader(uint8_t *dst, size_t dstCap, size_t payloadSize, const machine_t *machine);

int
state_wrap_wrap(uint8_t *dst, size_t dstCap, const uint8_t *payload, size_t payloadSize, const machine_t *machine);

int
state_wrap_parse(const uint8_t *buf, size_t bufSize, state_wrap_info_t *out);
