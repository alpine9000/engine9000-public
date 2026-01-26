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

#include <stddef.h>
#include <stdint.h>

#include <speex/speex_resampler.h>

#include "geo.h"
#include "geo_mixer.h"
#include "geo_ymfm.h"

/* MVS samplerate is 55555Hz, AES samplerate is 55943.489Hz, but adjustments
   are needed to ensure clean resampling. Slight adjustments for accuracy may
   be done by a frontend with its own resampler, using the precise video
   framerate.
     samplerate * (60 / framerate) = ~56319.437
*/
#define SAMPLERATE_YM2610 56319

void (*geo_mixer_output)(size_t);

static int16_t *abuf = NULL; // Buffer to output resampled data into
static size_t samplerate = 48000; // Default sample rate is 48000Hz
static double framerate = FRAMERATE_AES; // Default to AES

static SpeexResamplerState *resampler = NULL;
static int err;

// Callback to notify the fronted that N samples are ready
static void (*geo_mixer_cb)(size_t);

// Resample audio and pass the samples back to the frontend
static void geo_mixer_resamp(size_t in_ym) {
    int16_t *ybuf = geo_ymfm_get_buffer();
    spx_uint32_t insamps = in_ym;
    spx_uint32_t outsamps = samplerate / framerate;

    err = speex_resampler_process_interleaved_int(resampler,
        (spx_int16_t*)ybuf, &insamps, (spx_int16_t*)abuf, &outsamps);

    geo_mixer_cb(outsamps << 1);
}

// Pass raw samples (no resampling) back to the frontend
static void geo_mixer_raw(size_t in_ym) {
    int16_t *ybuf = geo_ymfm_get_buffer();
    in_ym <<= 1; // Stereo

    for (size_t i = 0; i < in_ym; ++i)
        abuf[i] = ybuf[i];

    geo_mixer_cb(in_ym);
}

// Set the pointer to the output audio buffer
void geo_mixer_set_buffer(int16_t *ptr) {
    abuf = ptr;
}

// Set the callback that notifies the frontend that N audio samples are ready
void geo_mixer_set_callback(void (*cb)(size_t)) {
    geo_mixer_cb = cb;
}

// Set the output sample rate
void geo_mixer_set_rate(size_t rate) {
    samplerate = rate;
}

// Set output to raw samples
void geo_mixer_set_raw(void) {
    geo_mixer_output = &geo_mixer_raw;
}

// Deinitialize the resampler
void geo_mixer_deinit(void) {
    if (resampler) {
        speex_resampler_destroy(resampler);
        resampler = NULL;
    }
}

// Bring up the Speex resampler
void geo_mixer_init(void) {
    if (geo_get_system() != SYSTEM_AES)
        framerate = FRAMERATE_MVS;
    resampler = speex_resampler_init(2, SAMPLERATE_YM2610, samplerate, 3, &err);
    geo_mixer_output = &geo_mixer_resamp;
}
