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

#ifndef GEO_CD_H
#define GEO_CD_H

void geo_cd_irq_set(uint8_t bits);
void geo_cd_irq_clear(uint8_t bits);
uint8_t geo_cd_irq_pending(void);
void geo_cd_frame_end(void);
void geo_cd_postload(void);
void geo_cd_init(void);
void geo_cd_deinit(void);
void geo_cd_reset(void);

// Called each frame to advance CD timing
void geo_cd_tick(unsigned mcycles);

// M68K memory map handlers for CD mode
unsigned geo_cd_m68k_read_8(unsigned address);
unsigned geo_cd_m68k_read_16(unsigned address);
void geo_cd_m68k_write_8(unsigned address, unsigned value);
void geo_cd_m68k_write_16(unsigned address, unsigned value);

// VBL masking (irqMask2 bits 4+5 must be set)
int geo_cd_vbl_enabled(void);
void geo_cd_set_vbl_pending(void);

// CDDA audio access for mixer
void geo_cd_read_cdda(int16_t *out, size_t nsamples);
void geo_cd_frame_start(void);

// State serialization
void geo_cd_state_load(uint8_t *st, unsigned ver);
void geo_cd_state_save(uint8_t *st);

// RAM access for save data and memory maps
const void* geo_cd_bram_ptr(void);
const void* geo_cd_pram_ptr(void);

int geo_cd_detect_bios(uint8_t *bios, size_t sz);
void geo_cd_set_speed_hack(int enabled);

// Loading skip — returns 1 if a CD sector was decoded this frame
int geo_cd_sector_decoded_this_frame(void);
void geo_cd_clear_sector_decoded(void);

#endif
