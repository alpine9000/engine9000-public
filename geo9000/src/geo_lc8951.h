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

// Sanyo LC8951 CD-ROM Host Interface Controller

#ifndef GEO_LC8951_H
#define GEO_LC8951_H

// Buffer size (64K circular sector buffer, hardware-accurate)
#define LC8951_BUFSZ 0x10000

// IFSTAT bits (active-low: bit clear = pending)
#define LC_IFSTAT_CMDI  0x80
#define LC_IFSTAT_DTEI  0x40
#define LC_IFSTAT_DECI  0x20
#define LC_IFSTAT_DTBSY 0x08
#define LC_IFSTAT_STBSY 0x04
#define LC_IFSTAT_DTEN  0x02

// IFCTRL bits
#define LC_IFCTRL_CMDIEN  0x80
#define LC_IFCTRL_DTEIEN  0x40
#define LC_IFCTRL_DECIEN  0x20
#define LC_IFCTRL_DOUTEN  0x02

// CTRL0 bits
#define LC_CTRL0_DECEN  0x80
#define LC_CTRL0_AUTORQ 0x10

// CTRL1 bits
#define LC_CTRL1_MODRQ  0x08
#define LC_CTRL1_FORMRQ 0x04
#define LC_CTRL1_SHDREN 0x01

typedef struct _lc8951_t {
    uint8_t regs[16];           // Registers 0-15
    uint8_t regptr;             // Current register pointer

    uint8_t buffer[LC8951_BUFSZ]; // 64K circular sector buffer
    uint16_t wal;               // Write address (where next sector header goes)
    uint16_t ptl;               // Pointer (snapshot of WAL before sector write)
    uint16_t dacl;              // Data address counter (DMA read position)

    uint8_t ctrl0;
    uint8_t ctrl1;
    uint8_t ifstat;             // Interrupt flags (active-low)
    uint8_t ifctrl;             // Interrupt control

    uint16_t dbc;               // Data byte count (transfer length - 1)
    uint8_t head[4];            // Header (M, S, F, Mode)
    uint8_t stat0;              // Status 0 (CRCOK etc)
    uint8_t stat1;              // Status 1
    uint8_t stat2;              // Status 2 (from CTRL0/CTRL1)
    uint8_t stat3;              // Status 3

    uint8_t decoder_enabled;
    uint8_t protection_bypassed; // Neo Geo CD copy protection state
} lc8951_t;

void geo_lc8951_reset(lc8951_t* const lc);
uint8_t geo_lc8951_reg_read(lc8951_t* const lc);
void geo_lc8951_reg_write(lc8951_t* const lc, uint8_t val);

// Decode a sector at the given LBA into the circular buffer
void geo_lc8951_sector_decoded(lc8951_t* const lc, uint32_t lba);

void geo_lc8951_end_transfer(lc8951_t* const lc);

void geo_lc8951_state_load(lc8951_t* const lc, uint8_t *st);
void geo_lc8951_state_save(lc8951_t* const lc, uint8_t *st);

#endif
