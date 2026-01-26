/*
Copyright (c) 2022-2025 Rupert Carmichael
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

#ifndef GEO_LSPC_H
#define GEO_LSPC_H

// HBlank, Back Porch, Active, Front Porch: 28 + 28 + 320 + 8
#define LSPC_PIXELS     384

// VBlank, Top Border, Active, Bottom Border: 8 + 16 + 224 + 16
#define LSPC_SCANLINES  264

// First active line is drawn after 8 VBlank lines, buffering starts after 6
#define LSPC_LINE_BUFSTART      6
#define LSPC_LINE_BORDER_TOP    8
#define LSPC_LINE_BORDER_BOTTOM 248

#define LSPC_WIDTH          320 // Active drawing
#define LSPC_WIDTH_VISIBLE  304 // 8 pixel horizontal overscan on left/right
#define LSPC_HEIGHT         256 // Active drawing including top/bottom border
#define LSPC_HEIGHT_VISIBLE 224 // 16 pixel vertical overscan on top/bottom

#define LSPC_FIXTILES_H     40
#define LSPC_FIXTILES_V     32
#define LSPC_FIX_BOARD      0
#define LSPC_FIX_CART       1

#define FIX_BANKSW_NONE 0
#define FIX_BANKSW_LINE 1
#define FIX_BANKSW_TILE 2

typedef struct _lspc_t {
    // 64K + 4K = 68K, broken into Lower and Upper segments of 16-bit values
    uint16_t vram[(SIZE_64K + SIZE_4K) >> 1];

    // 2 banks of 256 16-entry palettes, 16-bit colours
    uint16_t palram[SIZE_16K >> 1];

    // Palette bank offset currently in use
    uint8_t palbank;

    // VRAM Address
    uint16_t vramaddr;
    uint16_t vrambank;

    // Signed 16-bit "modulo" value applied to VRAM address after writes
    int16_t vrammod;

    // Auto animation
    uint8_t aa_counter;
    uint8_t aa_disable;
    uint8_t aa_reload;
    uint8_t aa_timer;

    // Shadow mode
    uint8_t shadow;

    uint32_t scanline;
    uint32_t cyc;
} lspc_t;

void geo_lspc_set_buffer(uint32_t*);
void geo_lspc_set_fix_banksw(unsigned);
void geo_lspc_set_fix(unsigned);
void geo_lspc_set_sprlimit(unsigned);
unsigned geo_lspc_get_sprlimit(void);
void geo_lspc_set_palette(unsigned);

void geo_lspc_postload(void);

uint8_t geo_lspc_palram_rd08(uint32_t);
uint16_t geo_lspc_palram_rd16(uint32_t);
void geo_lspc_palram_wr08(uint32_t, uint8_t);
void geo_lspc_palram_wr16(uint32_t, uint16_t);
void geo_lspc_palram_bank(unsigned);

void geo_lspc_vramaddr_wr(uint16_t);

uint16_t geo_lspc_vram_rd(void);
void geo_lspc_vram_wr(uint16_t);

uint16_t geo_lspc_vrammod_rd(void);
void geo_lspc_vrammod_wr(int16_t);

uint16_t geo_lspc_mode_rd(void);
void geo_lspc_mode_wr(uint16_t);

void geo_lspc_init(void);

void geo_lspc_shadow_wr(unsigned);

void geo_lspc_run(unsigned);

void geo_lspc_state_load(uint8_t*);
void geo_lspc_state_save(uint8_t*);

const void* geo_lspc_vram_ptr(void);
const void* geo_lspc_palram_ptr(void);

#endif
