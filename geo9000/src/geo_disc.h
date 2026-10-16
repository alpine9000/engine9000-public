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

#ifndef GEO_DISC_H
#define GEO_DISC_H

#define GEO_DISC_MAX_TRACKS  99
#define GEO_DISC_SECTOR_SIZE 2352
#define GEO_DISC_DATA_SIZE   2048

#define GEO_DISC_TRACK_DATA  0
#define GEO_DISC_TRACK_AUDIO 1

int geo_disc_open(const char *path);
void geo_disc_close(void);

int geo_disc_read_sector(uint32_t disc_lba, uint8_t *buf);
int geo_disc_read_audio(uint32_t disc_lba, int16_t *buf);

unsigned geo_disc_num_tracks(void);
int geo_disc_track_is_audio(unsigned track);
uint32_t geo_disc_track_start(unsigned track);
uint32_t geo_disc_track_frames(unsigned track);
uint32_t geo_disc_leadout(void);

void geo_disc_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);
uint32_t geo_disc_msf_to_lba(uint8_t m, uint8_t s, uint8_t f);

#endif
