/*
Copyright (c) 2022-2024 Rupert Carmichael
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Signed values are pushed and popped unsigned, but will retain their initial
 * value, as these functions simply record the bit pattern.
 */

#include <stddef.h>
#include <stdint.h>

#include "geo_serial.h"

static size_t pos = 0;

// Begin a Serialize or Deserialize operation
void geo_serial_begin(void) {
    pos = 0;
}

// Serially push a block of memory, one byte at a time
void geo_serial_pushblk(uint8_t *dst, uint8_t *src, size_t len) {
    for (size_t i = 0; i < len; ++i)
        dst[pos + i] = src[i];
    pos += len;
}

// Serially pop a block of memory, one byte at a time
void geo_serial_popblk(uint8_t *dst, uint8_t *src, size_t len) {
    for (size_t i = 0; i < len; ++i)
        dst[i] = src[pos + i];
    pos += len;
}

// Push an 8-bit integer
void geo_serial_push8(uint8_t *mem, uint8_t v) {
    mem[pos++] = v;
}

// Push a 16-bit integer
void geo_serial_push16(uint8_t *mem, uint16_t v) {
    mem[pos++] = v >> 8;
    mem[pos++] = v & 0xff;
}

// Push a 32-bit integer
void geo_serial_push32(uint8_t *mem, uint32_t v) {
    mem[pos++] = v >> 24;
    mem[pos++] = (v >> 16) & 0xff;
    mem[pos++] = (v >> 8) & 0xff;
    mem[pos++] = v & 0xff;
}

// Push a 64-bit integer
void geo_serial_push64(uint8_t *mem, uint64_t v) {
    mem[pos++] = v >> 56;
    mem[pos++] = (v >> 48) & 0xff;
    mem[pos++] = (v >> 40) & 0xff;
    mem[pos++] = (v >> 32) & 0xff;
    mem[pos++] = (v >> 24) & 0xff;
    mem[pos++] = (v >> 16) & 0xff;
    mem[pos++] = (v >> 8) & 0xff;
    mem[pos++] = v & 0xff;
}

// Pop an 8-bit integer
uint8_t geo_serial_pop8(uint8_t *mem) {
    return mem[pos++];
}

// Pop a 16-bit integer
uint16_t geo_serial_pop16(uint8_t *mem) {
    uint16_t ret = mem[pos++] << 8;
    ret |= mem[pos++];
    return ret;
}

// Pop a 32-bit integer
uint32_t geo_serial_pop32(uint8_t *mem) {
    uint32_t ret = mem[pos++] << 24;
    ret |= mem[pos++] << 16;
    ret |= mem[pos++] << 8;
    ret |= mem[pos++];
    return ret;
}

// Pop a 64-bit integer
uint64_t geo_serial_pop64(uint8_t *mem) {
    uint64_t ret = (uint64_t)(mem[pos++]) << 56;
    ret |= (uint64_t)(mem[pos++]) << 48;
    ret |= (uint64_t)(mem[pos++]) << 40;
    ret |= (uint64_t)(mem[pos++]) << 32;
    ret |= (uint64_t)(mem[pos++]) << 24;
    ret |= (uint64_t)(mem[pos++]) << 16;
    ret |= (uint64_t)(mem[pos++]) << 8;
    ret |= (uint64_t)(mem[pos++]);
    return ret;
}

// Return the size of the serialized data
size_t geo_serial_size(void) {
    return pos + 1;
}
