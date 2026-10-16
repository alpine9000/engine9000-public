/*
Copyright (c) 2022-2026 Rupert Carmichael
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
#ifdef E9K_HACK_AUDIO_VIS
#include <string.h>
#endif
#include <stddef.h>
#include <stdint.h>

#include "ymfm/ymfm.h"
#include "ymfm/ymfm_adpcm.h"
#include "ymfm/ymfm_opn.h"

#include "geo.h"
#include "geo_serial.h"
#include "geo_ymfm.h"
#include "geo_z80.h"

#define SIZE_YMBUF 2048
#define DIVISOR 144 // 144 for medium fidelity, 16 for high
#define GEO_YMFM_AUDIO_VIS_SAMPLE_RATE 56319u

static romdata_t *romdata = NULL;

static int16_t ymbuf[SIZE_YMBUF];
static size_t bufpos;
static int32_t busytimer;
static int32_t timer[2];
static int32_t output[3];

#ifdef E9K_HACK_AUDIO_VIS
typedef struct geo_ymfm_audio_vis_accum {
    int32_t peakL;
    int32_t peakR;
} geo_ymfm_audio_vis_accum_t;

static int geo_ymfm_audioVisEnabled;
static uint32_t geo_ymfm_audioMuteMask;
static uint64_t geo_ymfm_audioVisFrameNo;
static geo_ymfm_audio_vis_accum_t geo_ymfm_audioVisFm;
static geo_ymfm_audio_vis_accum_t geo_ymfm_audioVisSsg;
static geo_ymfm_audio_vis_accum_t geo_ymfm_audioVisAdpcmA[E9K_DEBUG_GEO_ADPCM_A_CHANNELS];
static geo_ymfm_audio_vis_accum_t geo_ymfm_audioVisAdpcmB;
static geo_ymfm_audio_vis_accum_t geo_ymfm_audioVisMixed;
static int16_t geo_ymfm_audioVisMixedSamples[E9K_DEBUG_GEO_AUDIO_SPECTRUM_SAMPLES];
static uint32_t geo_ymfm_audioVisMixedSampleCount;
#endif

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
    timer[tnum] = duration;
}

void ymfm_set_busy_end(uint32_t clocks) {
    busytimer += clocks;
}

bool ymfm_is_busy(void) {
    return busytimer > 0;
}

void ymfm_update_irq(bool asserted) {
    asserted ? geo_z80_assert_irq(0) : geo_z80_clear_irq();
}

static inline void geo_ymfm_timer_tick(void) {
    if (busytimer > 0) {
        busytimer -= DIVISOR;
        if (busytimer < 0) {
            busytimer = 0;
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (timer[i] < 0) {
            continue;
        }
        timer[i] -= DIVISOR;
        if (timer[i] <= 0) {
            fm_engine_timer_expired(i);
        }
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

#ifdef E9K_HACK_AUDIO_VIS
static int32_t
geo_ymfm_audioVisAbs32(int32_t value)
{
    if (value == INT32_MIN) {
        return INT32_MAX;
    }
    return value < 0 ? -value : value;
}

static void
geo_ymfm_audioVisResetAccum(geo_ymfm_audio_vis_accum_t *accum)
{
    if (!accum) {
        return;
    }
    memset(accum, 0, sizeof(*accum));
}

static void
geo_ymfm_audioVisResetAll(void)
{
    geo_ymfm_audioVisResetAccum(&geo_ymfm_audioVisFm);
    geo_ymfm_audioVisResetAccum(&geo_ymfm_audioVisSsg);
    for (int chnum = 0; chnum < E9K_DEBUG_GEO_ADPCM_A_CHANNELS; chnum++) {
        geo_ymfm_audioVisResetAccum(&geo_ymfm_audioVisAdpcmA[chnum]);
    }
    geo_ymfm_audioVisResetAccum(&geo_ymfm_audioVisAdpcmB);
    geo_ymfm_audioVisResetAccum(&geo_ymfm_audioVisMixed);
    geo_ymfm_audioVisMixedSampleCount = 0;
}

static void
geo_ymfm_audioVisAdd(geo_ymfm_audio_vis_accum_t *accum, int32_t left, int32_t right)
{
    if (!accum) {
        return;
    }

    int32_t absL = geo_ymfm_audioVisAbs32(left);
    int32_t absR = geo_ymfm_audioVisAbs32(right);
    if (absL > accum->peakL) {
        accum->peakL = absL;
    }
    if (absR > accum->peakR) {
        accum->peakR = absR;
    }
}

static void
geo_ymfm_audioVisAddMixedSample(int16_t left, int16_t right)
{
    if (geo_ymfm_audioVisMixedSampleCount >= E9K_DEBUG_GEO_AUDIO_SPECTRUM_SAMPLES) {
        return;
    }

    geo_ymfm_audioVisMixedSamples[geo_ymfm_audioVisMixedSampleCount++] = (int16_t)(((int32_t)left + (int32_t)right) / 2);
}

static void
geo_ymfm_audioVisFillSource(e9k_debug_audio_source_t *out, const geo_ymfm_audio_vis_accum_t *accum)
{
    if (!out || !accum) {
        return;
    }
    out->peakL = accum->peakL;
    out->peakR = accum->peakR;
}

static void
geo_ymfm_audioVisFillSourceVolume(e9k_debug_audio_source_t *out, uint32_t left, uint32_t right)
{
    if (!out) {
        return;
    }
    if (left > INT32_MAX) {
        left = INT32_MAX;
    }
    if (right > INT32_MAX) {
        right = INT32_MAX;
    }
    out->volumeL = (int32_t)left;
    out->volumeR = (int32_t)right;
    out->hasVolume = 1;
}

static uint32_t
geo_ymfm_audioVisAdpcmBPlaybackMilliHz(void)
{
    uint32_t deltaN = adpcm_b_engine_debug_delta_n();
    uint64_t milliHz = ((uint64_t)GEO_YMFM_AUDIO_VIS_SAMPLE_RATE * (uint64_t)deltaN * 1000u) >> 16;
    if (milliHz > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)milliHz;
}

void
geo_ymfm_setAudioVisEnabled(int enabled)
{
    geo_ymfm_audioVisEnabled = enabled ? 1 : 0;
    if (!geo_ymfm_audioVisEnabled) {
        geo_ymfm_audioMuteMask = 0;
    }
    geo_ymfm_audioVisFrameNo = 0;
    geo_ymfm_audioVisResetAll();
    ym2610_debug_set_audio_vis_enabled(geo_ymfm_audioVisEnabled);
    ym2610_debug_set_audio_mute_mask(geo_ymfm_audioMuteMask);
}

void
geo_ymfm_setAudioMuteMask(uint32_t mask)
{
    geo_ymfm_audioMuteMask = geo_ymfm_audioVisEnabled ? mask : 0;
    ym2610_debug_set_audio_mute_mask(geo_ymfm_audioMuteMask);
}

size_t
geo_ymfm_readAudioFrame(e9k_debug_audio_frame_t *out, size_t cap)
{
    if (!out || cap < sizeof(*out)) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->frameNo = geo_ymfm_audioVisFrameNo++;
    geo_ymfm_audioVisFillSource(&out->fm, &geo_ymfm_audioVisFm);
    geo_ymfm_audioVisFillSource(&out->ssg, &geo_ymfm_audioVisSsg);
    for (int chnum = 0; chnum < E9K_DEBUG_GEO_ADPCM_A_CHANNELS; chnum++) {
        geo_ymfm_audioVisFillSource(&out->adpcmA[chnum], &geo_ymfm_audioVisAdpcmA[chnum]);
    }
    geo_ymfm_audioVisFillSource(&out->adpcmB, &geo_ymfm_audioVisAdpcmB);
    geo_ymfm_audioVisFillSource(&out->mixed, &geo_ymfm_audioVisMixed);
    out->sampleRate = GEO_YMFM_AUDIO_VIS_SAMPLE_RATE;
    out->mixedSampleCount = geo_ymfm_audioVisMixedSampleCount;
    if (out->mixedSampleCount > E9K_DEBUG_GEO_AUDIO_SPECTRUM_SAMPLES) {
        out->mixedSampleCount = E9K_DEBUG_GEO_AUDIO_SPECTRUM_SAMPLES;
    }
    memcpy(out->mixedSamples, geo_ymfm_audioVisMixedSamples, out->mixedSampleCount * sizeof(out->mixedSamples[0]));
    uint32_t adpcmAVolumes[E9K_DEBUG_GEO_ADPCM_A_CHANNELS][2];
    uint32_t adpcmBVolumes[2];
    ym2610_debug_get_source_volumes(adpcmAVolumes, adpcmBVolumes);
    for (int chnum = 0; chnum < E9K_DEBUG_GEO_ADPCM_A_CHANNELS; chnum++) {
        geo_ymfm_audioVisFillSourceVolume(&out->adpcmA[chnum],
                                          adpcmAVolumes[chnum][0],
                                          adpcmAVolumes[chnum][1]);
    }
    geo_ymfm_audioVisFillSourceVolume(&out->adpcmB, adpcmBVolumes[0], adpcmBVolumes[1]);
    out->adpcmBPlaybackMilliHz = geo_ymfm_audioVisAdpcmBPlaybackMilliHz();
    geo_ymfm_audioVisResetAll();
    return sizeof(*out);
}
#endif

// Clock the YM2610
size_t geo_ymfm_exec(void) {
    geo_ymfm_timer_tick();
    ym2610_generate(output);

    // Avoid overflowing ymbuf if the caller breaks mid-frame repeatedly without draining audio.
    if (bufpos + 1 >= SIZE_YMBUF)
        bufpos = 0;

    // Mix stereo FM/ADPCM output (0,1) with mono SSG output (2)
#ifdef E9K_HACK_AUDIO_VIS
    int32_t ssgOutput = output[2];
    if (geo_ymfm_audioVisEnabled && (geo_ymfm_audioMuteMask & E9K_DEBUG_GEO_AUDIO_MUTE_SSG)) {
        output[2] = 0;
    }
#endif
    int16_t mixedL = mix(output[0], output[2]);
    int16_t mixedR = mix(output[1], output[2]);
#ifdef E9K_HACK_AUDIO_VIS
    if (geo_ymfm_audioVisEnabled) {
        int32_t fm[3];
        int32_t adpcmA[E9K_DEBUG_GEO_ADPCM_A_CHANNELS][3];
        int32_t adpcmB[3];
        ym2610_debug_get_source_outputs(fm, adpcmA, adpcmB);
        geo_ymfm_audioVisAdd(&geo_ymfm_audioVisFm, fm[0], fm[1]);
        geo_ymfm_audioVisAdd(&geo_ymfm_audioVisSsg, ssgOutput, ssgOutput);
        for (int chnum = 0; chnum < E9K_DEBUG_GEO_ADPCM_A_CHANNELS; chnum++) {
            geo_ymfm_audioVisAdd(&geo_ymfm_audioVisAdpcmA[chnum], adpcmA[chnum][0], adpcmA[chnum][1]);
        }
        geo_ymfm_audioVisAdd(&geo_ymfm_audioVisAdpcmB, adpcmB[0], adpcmB[1]);
        geo_ymfm_audioVisAdd(&geo_ymfm_audioVisMixed, mixedL, mixedR);
        geo_ymfm_audioVisAddMixedSample(mixedL, mixedR);
    }
#endif
    ymbuf[bufpos++] = mixedL;
    ymbuf[bufpos++] = mixedR;

    return 1;
}

// Hacks
void geo_ymfm_adpcm_wrap(int w) {
    adpcm_a_set_accum_wrap(w);
}

// States
void geo_ymfm_state_load(uint8_t *st, unsigned version) {
    busytimer = geo_serial_pop32(st);
    if (version == 3u) {
        uint32_t busyfrac = geo_serial_pop32(st);
        busytimer = (busytimer * DIVISOR) + busyfrac;
    }
    timer[0] = geo_serial_pop32(st);
    timer[1] = geo_serial_pop32(st);
    if (version == 3u) {
        for (unsigned i = 0; i < 2; ++i) {
            if (timer[i] >= 0) {
                timer[i] *= DIVISOR;
            }
        }
    }
    opn_state_load(st);
    adpcm_state_load(st);
    ssg_state_load(st);
}

void geo_ymfm_state_save(uint8_t *st) {
    geo_serial_push32(st, busytimer);
    geo_serial_push32(st, timer[0]);
    geo_serial_push32(st, timer[1]);
    opn_state_save(st);
    adpcm_state_save(st);
    ssg_state_save(st);
}
