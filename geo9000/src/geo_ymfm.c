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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ymfm/ymfm.h"
#include "ymfm/ymfm_opn.h"

#include "geo.h"
#include "geo_serial.h"
#include "geo_ymfm.h"
#include "geo_z80.h"

#define SIZE_YMBUF 2048
#define DIVISOR 144 // 144 for medium fidelity, 16 for high

static romdata_t *romdata = NULL;

static int16_t ymbuf[SIZE_YMBUF];
static size_t bufpos;
static int32_t busytimer;
static int32_t busyfrac;
static int32_t timer[2];
static int32_t output[3];

uint8_t ymfm_external_read(uint32_t type, uint32_t address) {
    switch (type) {
        case ACCESS_ADPCM_A:
            return address > romdata->v1sz ? 0 : romdata->v1[address];
        case ACCESS_ADPCM_B:
            return address > romdata->v2sz ? 0 : romdata->v2[address];
        default:
            return 0;
    }
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
        timer[tnum] += (duration / DIVISOR);
    else // -1 means disabled
        timer[tnum] = duration;
}

void ymfm_set_busy_end(uint32_t clocks) {
    busytimer += clocks / DIVISOR;
    busyfrac += clocks % DIVISOR;
    if (busyfrac >= DIVISOR) {
        ++busytimer;
        busyfrac -= DIVISOR;
    }
}

bool ymfm_is_busy(void) {
    return busytimer ? true : false;
}

void ymfm_update_irq(bool asserted) {
    asserted ? geo_z80_assert_irq(0) : geo_z80_clear_irq();
}

static inline void geo_ymfm_timer_tick(void) {
    if (busytimer)
        --busytimer;

    for (int i = 0; i < 2; ++i) {
        if (timer[i] < 0)
            continue;
        else if (--timer[i] == 0)
            fm_engine_timer_expired(i);
    }
}

// Grab the pointer to the buffer
int16_t* geo_ymfm_get_buffer(void) {
    bufpos = 0;
    return &ymbuf[0];
}

// Initialize the YM2610
void geo_ymfm_init(void) {
    ym2610_init();
    ym2610_set_fidelity(OPN_FIDELITY_MED);
    fm_engine_init();
    romdata = geo_romdata_ptr();
}

// Perform a reset - required at least once before clocking
void geo_ymfm_reset(void) {
    ym2610_reset();
}

// Mix FM and SSG channels while maintaining a value within the int16_t range
static inline int16_t mix(int32_t samp0, int32_t samp1) {
    if (samp0 + samp1 >= 32767)
        return 32767;
    else if (samp0 + samp1 <= -32768)
        return -32768;
    return samp0 + samp1;
}

// Clock the YM2610
size_t geo_ymfm_exec(void) {
    geo_ymfm_timer_tick();
    ym2610_generate(output);

    // Avoid overflowing ymbuf if the caller breaks mid-frame repeatedly without draining audio.
    if (bufpos + 1 >= SIZE_YMBUF)
        bufpos = 0;

    // Mix stereo FM/ADPCM output (0,1) with mono SSG output (2)
    ymbuf[bufpos++] = mix(output[0], output[2]);
    ymbuf[bufpos++] = mix(output[1], output[2]);

    return 1;
}

// Hacks
void geo_ymfm_adpcm_wrap(int w) {
    adpcm_a_set_accum_wrap(w);
}

// States
void geo_ymfm_state_load(uint8_t *st) {
    busytimer = geo_serial_pop32(st);
    busyfrac = geo_serial_pop32(st);
    timer[0] = geo_serial_pop32(st);
    timer[1] = geo_serial_pop32(st);
    opn_state_load(st);
    adpcm_state_load(st);
    ssg_state_load(st);
}

void geo_ymfm_state_save(uint8_t *st) {
    geo_serial_push32(st, busytimer);
    geo_serial_push32(st, busyfrac);
    geo_serial_push32(st, timer[0]);
    geo_serial_push32(st, timer[1]);
    opn_state_save(st);
    adpcm_state_save(st);
    ssg_state_save(st);
}
