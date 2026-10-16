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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#include "geo.h"
#include "geo_chd.h"

static chd_file *chd = NULL;
static const chd_header *header = NULL;

static geo_chd_track_t tracks[GEO_CHD_MAX_TRACKS];
static unsigned num_tracks = 0;
static uint32_t leadout_lba = 0;

// Hunk buffer for reading sectors
static uint8_t *hunkbuf = NULL;
static uint32_t hunksize = 0;
static int32_t cached_hunk = -1;

// Frame size: 2352 raw + 96 subcode = 2448
#define FRAME_SIZE CD_FRAME_SIZE // 2352 + 96 = 2448
static uint32_t frames_per_hunk = 1;

uint32_t geo_chd_msf_to_lba(uint8_t m, uint8_t s, uint8_t f) {
    return (m * 60 * 75) + (s * 75) + f;
}

void geo_chd_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f) {
    *m = lba / (60 * 75);
    *s = (lba / 75) % 60;
    *f = lba % 75;
}

static int geo_chd_parse_toc(void) {
    char metadata[256];
    uint32_t len;
    uint32_t tag;

    // Two position counters:
    // chdPos = position in CHD data (for reading sectors)
    // cdPos  = position on the virtual CD (what BIOS sees)
    uint32_t chdPos = 0;
    uint32_t cdPos = 0;
    int prev_was_data = 1;

    num_tracks = 0;
    memset(tracks, 0, sizeof(tracks));

    for (unsigned i = 0; i < GEO_CHD_MAX_TRACKS; ++i) {
        chd_error err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, i,
            metadata, sizeof(metadata), &len, &tag, NULL);

        if (err != CHDERR_NONE) {
            // Try the older format
            err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, i,
                metadata, sizeof(metadata), &len, &tag, NULL);

            if (err != CHDERR_NONE)
                break;
        }

        unsigned tracknum;
        char type[64];
        char subtype[64];
        unsigned frames = 0;
        unsigned pregap = 0;
        char pgtype[64];
        char pgsub[64];
        unsigned postgap = 0;

        type[0] = subtype[0] = pgtype[0] = pgsub[0] = '\0';

        // Try CHTR2 format first
        int parsed = sscanf(metadata,
            "TRACK:%u TYPE:%63s SUBTYPE:%63s FRAMES:%u PREGAP:%u PGTYPE:%63s "
                "PGSUB:%63s POSTGAP:%u",
                &tracknum, type, subtype, &frames, &pregap, pgtype, pgsub,
                &postgap);

        if (parsed < 4) {
            // Try CHTR format
            parsed = sscanf(metadata, "TRACK:%u TYPE:%63s SUBTYPE:%63s "
                "FRAMES:%u",
                &tracknum, type, subtype, &frames);

            if (parsed < 4)
                break;
        }

        int is_audio = !strcmp(type, "AUDIO");
        int is_raw = !strcmp(type, "MODE1_RAW");
        int is_vaudio = !strcmp(pgtype, "VAUDIO");

        // Align CHD position to 4-frame boundary (CHD requirement)
        if (chdPos % CD_TRACK_PADDING)
            chdPos += CD_TRACK_PADDING - (chdPos % CD_TRACK_PADDING);

        uint32_t track_len = frames;

        // Handle pregap (CHD pregap quirk)
        if (pregap) {
            // CHD WEIRDNESS: If previous track was data, the pregap is
            // NOT stored in the CHD data, so don't advance chdPos.
            // But if PGTYPE is VAUDIO (newer CHDs), it IS in the data.
            if (!prev_was_data || is_vaudio) {
                chdPos += pregap;
                track_len -= pregap;
            }
            cdPos += pregap;
        }

        tracks[i].start = cdPos;        // BIOS-facing position
        tracks[i].chd_start = chdPos;    // CHD data position
        tracks[i].frames = track_len;
        tracks[i].pregap = pregap;
        tracks[i].type = is_audio ? GEO_CHD_TRACK_AUDIO : GEO_CHD_TRACK_DATA;
        tracks[i].raw = is_raw;

        chdPos += track_len;
        cdPos += track_len;

        // Handle postgap
        cdPos += postgap;

        prev_was_data = !is_audio;
        num_tracks = i + 1;

        geo_log(GEO_LOG_DBG,
            "CHD Track %u: type=%s frames=%u cd_start=%u chd_start=%u "
            "pregap=%u\n",
            tracknum, type, track_len, tracks[i].start, tracks[i].chd_start,
            pregap);
    }

    leadout_lba = cdPos;
    return num_tracks > 0;
}

int geo_chd_open(const char *path) {
    chd_error err = chd_open(path, CHD_OPEN_READ, NULL, &chd);
    if (err != CHDERR_NONE) {
        geo_log(GEO_LOG_ERR, "Failed to open CHD: %s (error %d)\n", path, err);
        return 0;
    }

    header = chd_get_header(chd);
    hunksize = header->hunkbytes;
    frames_per_hunk = hunksize / FRAME_SIZE;
    if (frames_per_hunk == 0) frames_per_hunk = 1;

    hunkbuf = (uint8_t*)calloc(1, hunksize);
    if (!hunkbuf) {
        chd_close(chd);
        chd = NULL;
        return 0;
    }

    cached_hunk = -1;

    if (!geo_chd_parse_toc()) {
        geo_log(GEO_LOG_ERR, "Failed to parse CHD TOC\n");
        geo_chd_close();
        return 0;
    }

    geo_log(GEO_LOG_INF, "CHD opened: %u tracks, leadout at LBA %u\n",
        num_tracks, leadout_lba);

    return 1;
}

void geo_chd_close(void) {
    if (chd) {
        chd_close(chd);
        chd = NULL;
    }

    if (hunkbuf) {
        free(hunkbuf);
        hunkbuf = NULL;
    }

    header = NULL;
    num_tracks = 0;
    leadout_lba = 0;
    cached_hunk = -1;
}

static int geo_chd_read_frame(uint32_t lba, uint8_t *frame) {
    if (!chd || !hunkbuf)
        return 0;

    uint32_t hunknum = lba / frames_per_hunk;
    uint32_t frameoff = lba % frames_per_hunk;

    // Cache hunk reads
    if ((int32_t)hunknum != cached_hunk) {
        chd_error err = chd_read(chd, hunknum, hunkbuf);
        if (err != CHDERR_NONE) {
            geo_log(GEO_LOG_ERR, "CHD read error at hunk %u: %d\n",
                hunknum, err);
            cached_hunk = -1;
            return 0;
        }
        cached_hunk = hunknum;
    }

    memcpy(frame, hunkbuf + (frameoff * FRAME_SIZE), GEO_CHD_SECTOR_SIZE);
    return 1;
}

// Convert disc LBA (what the BIOS uses) to CHD LBA (for reading data)
// The disc has pregaps that may not exist in the CHD data.
static uint32_t disc_to_chd_lba(uint32_t disc_lba) {
    // Find which track this disc LBA falls in
    for (unsigned i = num_tracks; i > 0; --i) {
        if (disc_lba >= tracks[i - 1].start) {
            uint32_t offset = disc_lba - tracks[i - 1].start;
            return tracks[i - 1].chd_start + offset;
        }
    }
    // Before track 1 — for track 1, cd_start and chd_start are both 0
    return disc_lba;
}

int geo_chd_read_sector(uint32_t disc_lba, uint8_t *buf) {
    uint8_t frame[GEO_CHD_SECTOR_SIZE];
    uint32_t chd_lba = disc_to_chd_lba(disc_lba);

    if (!geo_chd_read_frame(chd_lba, frame))
        return 0;

    /* For MODE1_RAW, skip the 16-byte header to get 2048 data bytes.
       For MODE1, the frame already starts with user data.
    */
    unsigned offset = tracks[0].raw ? 16 : 0;
    memcpy(buf, frame + offset, GEO_CHD_DATA_SIZE);
    return 1;
}

int geo_chd_read_audio(uint32_t disc_lba, int16_t *buf) {
    uint8_t frame[GEO_CHD_SECTOR_SIZE];
    uint32_t chd_lba = disc_to_chd_lba(disc_lba);

    if (!geo_chd_read_frame(chd_lba, frame))
        return 0;

    // Audio frames are 2352 bytes = 588 stereo samples (16-bit interleaved)
    // CHD stores audio as big-endian, swap to native little-endian in one pass
    const uint8_t *src = frame;
    uint8_t *dst = (uint8_t*)buf;
    for (unsigned i = 0; i < GEO_CHD_SECTOR_SIZE; i += 2) {
        dst[i]     = src[i + 1];
        dst[i + 1] = src[i];
    }
    return 1;
}

unsigned geo_chd_num_tracks(void) {
    return num_tracks;
}

const geo_chd_track_t* geo_chd_get_track(unsigned track) {
    if (track == 0 || track > num_tracks)
        return NULL;
    return &tracks[track - 1]; // 1-based track numbers
}

int geo_chd_track_is_audio(unsigned track) {
    if (track == 0 || track > num_tracks)
        return 0;
    return tracks[track - 1].type == GEO_CHD_TRACK_AUDIO;
}

uint32_t geo_chd_track_start(unsigned track) {
    if (track == 0 || track > num_tracks)
        return 0;
    return tracks[track - 1].start;
}

uint32_t geo_chd_track_frames(unsigned track) {
    if (track == 0 || track > num_tracks)
        return 0;
    return tracks[track - 1].frames;
}

uint32_t geo_chd_leadout(void) {
    return leadout_lba;
}
