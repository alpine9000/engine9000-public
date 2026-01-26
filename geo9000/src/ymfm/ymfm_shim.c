/*
Copyright (c) 2022, Rupert Carmichael
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ymfm.h"
#include "ymfm_opn.h"
#include "ymfm_shim.h"

#define SIZE_YMBUF 2600

static int busytimer;
static int busyfrac;
static int timer[2];
static int divisor;
static int32_t output[2];

static int16_t ymbuf[SIZE_YMBUF]; // Buffer for raw output samples
static size_t ybufpos = 0; // Keep track of the position in the output buffer

// Grab the pointer to the YM2612's buffer
int16_t* ymfm_shim_get_buffer(void) {
    ybufpos = 0;
    return &ymbuf[0];
}

uint8_t ymfm_external_read(uint32_t type, uint32_t address) {
    if (type || address) { }
    return 0;
}

void ymfm_external_write(uint32_t type, uint32_t address, uint8_t data) {
    if (type || address || data) { }
}

void ymfm_sync_mode_write(uint8_t data) {
    fm_engine_mode_write(data);
}

void ymfm_sync_check_interrupts(void) {
    fm_engine_check_interrupts();
}

void ymfm_set_timer(uint32_t tnum, int32_t duration) {
    if (duration >= 0)
        timer[tnum] += (duration / divisor);
    else
        timer[tnum] = duration;
}

void ymfm_set_busy_end(uint32_t clocks) {
    busytimer += clocks / divisor;
    busyfrac += clocks % divisor;
    if (busyfrac >= divisor) {
        ++busytimer;
        busyfrac -= divisor;
    }
}

bool ymfm_is_busy(void) {
    return busytimer ? true : false;
}

void ymfm_update_irq(bool asserted) {
    if (asserted) { }
}

static inline void ymfm_shim_timer_tick(void) {
    if (busytimer) {
        --busytimer;
    }

    for (int i = 0; i < 2; ++i) {
        if (timer[i] < 0)
            continue;
        else if (--timer[i] == 0)
            fm_engine_timer_expired(i);
    }
}

void ymfm_shim_init(void) {
    divisor = 144;
    ym2612_init();
    fm_engine_init();
}

void ymfm_shim_deinit(void) {
}

size_t ymfm_shim_exec(void) {
    ymfm_shim_timer_tick();
    ym2612_generate(output);
    ymbuf[ybufpos++] = output[0];
    ymbuf[ybufpos++] = output[1];
    return 1;
}
