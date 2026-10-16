/*
Copyright (c) 2026 Romain Tisserand
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

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "geo.h"
#include "geo_disc.h"
#include "geo_cue.h"

#ifdef HAVE_CHDR
#include "geo_chd.h"
#endif

// Function pointer dispatch — set once at open, zero overhead per call
static int (*fn_read_sector)(uint32_t, uint8_t*);
static int (*fn_read_audio)(uint32_t, int16_t*);
static unsigned (*fn_num_tracks)(void);
static int (*fn_track_is_audio)(unsigned);
static uint32_t (*fn_track_start)(unsigned);
static uint32_t (*fn_track_frames)(unsigned);
static uint32_t (*fn_leadout)(void);
static void (*fn_close)(void);

static int detect_backend(const char *path) {
#ifdef HAVE_CHDR
    // Compare the four-character extension case-insensitively
    const char *extptr = strrchr(path, '.');
    if (!extptr)
        return 0;

    if (strlen(extptr) != 4)
        return 0;
    if (tolower((unsigned char)extptr[1]) == 'c' &&
        tolower((unsigned char)extptr[2]) == 'h' &&
        tolower((unsigned char)extptr[3]) == 'd')
        return 1;
    if (tolower((unsigned char)extptr[1]) == 'c' &&
        tolower((unsigned char)extptr[2]) == 'u' &&
        tolower((unsigned char)extptr[3]) == 'e')
        return 2;
    return 0;
#else
    (void)path;
    return 2; // BIN/CUE only
#endif
}

// Reset all function pointers to NULL
static void clear_dispatch(void) {
    fn_read_sector = NULL;
    fn_read_audio = NULL;
    fn_num_tracks = NULL;
    fn_track_is_audio = NULL;
    fn_track_start = NULL;
    fn_track_frames = NULL;
    fn_leadout = NULL;
    fn_close = NULL;
}

static unsigned stub_num_tracks(void) {
    return 0;
}

static int stub_read(uint32_t lba, uint8_t *buf) {
    (void)lba; (void)buf; return 0;
}

static int stub_read_audio(uint32_t lba, int16_t *buf) {
    (void)lba; (void)buf; return 0;
}

static int stub_track_is_audio(unsigned t) {
    (void)t; return 0;
}

static uint32_t stub_track_u32(unsigned t) {
    (void)t; return 0;
}

static uint32_t stub_leadout(void) {
    return 0;
}

int geo_disc_open(const char *path) {
    int ok = 0;
    int backend = detect_backend(path);

#ifdef HAVE_CHDR
    switch (backend) {
        case 1: // CHD
            ok = geo_chd_open(path);
            if (ok) {
                fn_read_sector = geo_chd_read_sector;
                fn_read_audio = geo_chd_read_audio;
                fn_num_tracks = geo_chd_num_tracks;
                fn_track_is_audio = geo_chd_track_is_audio;
                fn_track_start = geo_chd_track_start;
                fn_track_frames = geo_chd_track_frames;
                fn_leadout = geo_chd_leadout;
                fn_close = geo_chd_close;
            }
            break;
        case 2: // CUE
            ok = geo_cue_open(path);
            if (ok) {
                fn_read_sector = geo_cue_read_sector;
                fn_read_audio = geo_cue_read_audio;
                fn_num_tracks = geo_cue_num_tracks;
                fn_track_is_audio = geo_cue_track_is_audio;
                fn_track_start = geo_cue_track_start;
                fn_track_frames = geo_cue_track_frames;
                fn_leadout = geo_cue_leadout;
                fn_close = geo_cue_close;
            }
            break;
        default:
            geo_log(GEO_LOG_ERR, "Unsupported disc format: %s\n", path);
            break;
    }
#else
    (void)backend;
    ok = geo_cue_open(path);
    if (ok) {
        fn_read_sector = geo_cue_read_sector;
        fn_read_audio = geo_cue_read_audio;
        fn_num_tracks = geo_cue_num_tracks;
        fn_track_is_audio = geo_cue_track_is_audio;
        fn_track_start = geo_cue_track_start;
        fn_track_frames = geo_cue_track_frames;
        fn_leadout = geo_cue_leadout;
        fn_close = geo_cue_close;
    }
#endif

    if (!ok) { // Set stubs so callers don't need NULL checks
        fn_read_sector = stub_read;
        fn_read_audio = stub_read_audio;
        fn_num_tracks = stub_num_tracks;
        fn_track_is_audio = stub_track_is_audio;
        fn_track_start = stub_track_u32;
        fn_track_frames = stub_track_u32;
        fn_leadout = stub_leadout;
        fn_close = NULL;
    }

    return ok;
}

void geo_disc_close(void) {
    if (fn_close)
        fn_close();
    clear_dispatch();
}

int geo_disc_read_sector(uint32_t disc_lba, uint8_t *buf) {
    return fn_read_sector(disc_lba, buf);
}

int geo_disc_read_audio(uint32_t disc_lba, int16_t *buf) {
    return fn_read_audio(disc_lba, buf);
}

unsigned geo_disc_num_tracks(void) {
    return fn_num_tracks();
}

int geo_disc_track_is_audio(unsigned track) {
    return fn_track_is_audio(track);
}

uint32_t geo_disc_track_start(unsigned track) {
    return fn_track_start(track);
}

uint32_t geo_disc_track_frames(unsigned track) {
    return fn_track_frames(track);
}

uint32_t geo_disc_leadout(void) {
    return fn_leadout();
}

void geo_disc_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f) {
    *m = lba / (60 * 75);
    *s = (lba / 75) % 60;
    *f = lba % 75;
}

uint32_t geo_disc_msf_to_lba(uint8_t m, uint8_t s, uint8_t f) {
    return (m * 60 * 75) + (s * 75) + f;
}
