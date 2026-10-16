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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "geo.h"
#include "geo_disc.h"
#include "geo_lc8951.h"
#include "geo_serial.h"

// BCD conversion helper
static inline uint8_t to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

// Write into the 64K circular buffer with 16-bit position wrapping
static void lc_buffer_write(lc8951_t* const lc, size_t pos,
                            const uint8_t *data, size_t len) {
    size_t end = pos + len;
    if (end <= LC8951_BUFSZ) {
        memcpy(&lc->buffer[pos], data, len);
    }
    else {
        size_t first = LC8951_BUFSZ - pos;
        memcpy(&lc->buffer[pos], data, first);
        memcpy(&lc->buffer[0], data + first, len - first);
    }
}

/* Copy protection bypass for Neo Geo CD
   https://wiki.neogeodev.org/index.php/Copy_protection
   This hijacks the read of CPY.TXT, which starts with the text "Copyright
   by SNK". The character change corrupts the checksum, which passes the
   first check, and also converts the BEQ instruction to a BNE to pass
   the second check.
*/
static int protection_bypass(uint8_t *sector) {
    if (sector[64] == 'g' && !memcmp(sector, "Copyright by SNK", 16)) {
        sector[64] = 'f';
        return 1;
    }
    return 0;
}

void geo_lc8951_sector_decoded(lc8951_t* const lc, uint32_t lba) {
    if (!lc->decoder_enabled)
        return;

    uint8_t sector[GEO_DISC_DATA_SIZE];
    if (!geo_disc_read_sector(lba, sector)) {
        lc->stat0 = 0;
        lc->stat1 = 0;
        if (lc->ctrl0 & LC_CTRL0_AUTORQ)
            lc->stat2 = lc->ctrl1 & LC_CTRL1_MODRQ;
        else
            lc->stat2 = lc->ctrl1 & (LC_CTRL1_MODRQ | LC_CTRL1_FORMRQ);
        lc->stat3 = 0;
        lc->ifstat &= ~LC_IFSTAT_DECI;
        return;
    }

    // Update header with current MSF position
    uint8_t m, s, f;
    geo_disc_lba_to_msf(lba + 150, &m, &s, &f);
    lc->head[0] = to_bcd(m);
    lc->head[1] = to_bcd(s);
    lc->head[2] = to_bcd(f);
    lc->head[3] = 0x01; // Mode 1

    // Write 4-byte header + 2048-byte sector data into circular buffer at WAL
    // BIOS protocol: reads PTL, sets DAC = PTL + 4 (skip header), DBC = 0x7FF
    lc_buffer_write(lc, lc->wal, lc->head, 4);

    if (!lc->protection_bypassed)
        lc->protection_bypassed = protection_bypass(sector);
    lc_buffer_write(lc, (uint16_t)(lc->wal + 4), sector, GEO_DISC_DATA_SIZE);

    // PTL = WAL (snapshot before advancing — tells BIOS where sector starts)
    lc->ptl = lc->wal;

    // Advance WAL by one raw sector (uint16_t wraps naturally at 64K boundary)
    lc->wal += GEO_DISC_SECTOR_SIZE;

    // Set status registers
    lc->stat0 = 0x80; // CRCOK
    lc->stat1 = 0;

    // STAT2: reflects CTRL1 mode flags filtered by AUTORQ
    if (lc->ctrl0 & LC_CTRL0_AUTORQ)
        lc->stat2 = lc->ctrl1 & LC_CTRL1_MODRQ;
    else
        lc->stat2 = lc->ctrl1 & (LC_CTRL1_MODRQ | LC_CTRL1_FORMRQ);

    lc->stat3 = 0;

    // DECI: sector decoded interrupt pending (active-low: clear bit)
    lc->ifstat &= ~LC_IFSTAT_DECI;
}

void geo_lc8951_reset(lc8951_t* const lc) {
    memset(lc, 0, sizeof(*lc));
    lc->ifstat = 0xff; // All interrupt flags cleared (active low)
    // WAL/WAH initialized to 0x0930 (2352 = one raw sector size)
    lc->wal = 0x0930;
}

uint8_t geo_lc8951_reg_read(lc8951_t* const lc) {
    uint8_t reg = lc->regptr;
    uint8_t val = 0;

    // Auto-increment except register 0
    if (lc->regptr > 0) {
        lc->regptr = (lc->regptr + 1) & 0x0f;
        if (lc->regptr == 0)
            lc->regptr = 1; // Skip 0 on wrap
    }

    switch (reg) {
        case 0x00: val = 0; break; // COMIN - not readable
        case 0x01: val = lc->ifstat; break;
        case 0x02: val = lc->dbc & 0xff; break;
        case 0x03: val = (lc->dbc >> 8) & 0x0f; break;
        case 0x04: // HEAD0 (returns 0 when SHDREN is set)
            val = (lc->ctrl1 & LC_CTRL1_SHDREN) ? 0 : lc->head[0];
            break;
        case 0x05: // HEAD1
            val = (lc->ctrl1 & LC_CTRL1_SHDREN) ? 0 : lc->head[1];
            break;
        case 0x06: // HEAD2
            val = (lc->ctrl1 & LC_CTRL1_SHDREN) ? 0 : lc->head[2];
            break;
        case 0x07: // HEAD3
            val = (lc->ctrl1 & LC_CTRL1_SHDREN) ? 0 : lc->head[3];
            break;
        case 0x08: val = lc->ptl & 0xff; break;
        case 0x09: val = (lc->ptl >> 8) & 0xff; break;
        case 0x0a: val = lc->wal & 0xff; break;
        case 0x0b: val = (lc->wal >> 8) & 0xff; break;
        case 0x0c: val = lc->stat0; break;
        case 0x0d: val = lc->stat1; break;
        case 0x0e: val = lc->stat2; break;
        case 0x0f: // STAT3 — reading acknowledges DECI (set bit = no interrupt)
            val = lc->stat3;
            lc->ifstat |= LC_IFSTAT_DECI;
            break;
    }

    return val;
}

void geo_lc8951_reg_write(lc8951_t* const lc, uint8_t val) {
    uint8_t reg = lc->regptr;

    // Auto-increment except register 0
    if (lc->regptr > 0) {
        lc->regptr = (lc->regptr + 1) & 0x0f;
        if (lc->regptr == 0)
            lc->regptr = 1;
    }

    switch (reg) {
        case 0x00: // SBOUT - not used
            break;
        case 0x01: // IFCTRL
            lc->ifctrl = val;
            break;
        case 0x02: // DBCL
            lc->dbc = (lc->dbc & 0xff00) | val;
            break;
        case 0x03: // DBCH
            lc->dbc = (lc->dbc & 0x00ff) | ((val & 0x0f) << 8);
            break;
        case 0x04: // DACL
            lc->dacl = (lc->dacl & 0xff00) | val;
            break;
        case 0x05: // DACH
            lc->dacl = (lc->dacl & 0x00ff) | (val << 8);
            break;
        case 0x06: // DTRG — data transfer trigger
            if (lc->ifctrl & LC_IFCTRL_DOUTEN)
                lc->ifstat &= ~LC_IFSTAT_DTBSY; // Clear DTBSY = transfer ready
            break;
        case 0x07: // DTACK - acknowledge transfer completion
            lc->ifstat |= LC_IFSTAT_DTEI; // Clear DTEI flag
            break;
        // NOTE: LC8951 write registers 0x08-0x0E differ from read registers!
        // Write: WAL(8) WAH(9) CTRL0(A) CTRL1(B) PTL(C) PTH(D) CTRL2(E)
        // Read:  PTL(8) PTH(9) WAL(A)   WAH(B)   STAT0-3(C-F)
        case 0x08: // WAL (write side)
            lc->wal = (lc->wal & 0xff00) | val;
            break;
        case 0x09: // WAH (write side)
            lc->wal = (lc->wal & 0x00ff) | (val << 8);
            break;
        case 0x0a: // CTRL0 (write side)
            lc->ctrl0 = val;
            lc->decoder_enabled = (val & LC_CTRL0_DECEN) ? 1 : 0;
            break;
        case 0x0b: // CTRL1 (write side)
            lc->ctrl1 = val;
            break;
        case 0x0c: // PTL (write side)
            lc->ptl = (lc->ptl & 0xff00) | val;
            break;
        case 0x0d: // PTH (write side)
            lc->ptl = (lc->ptl & 0x00ff) | (val << 8);
            break;
        case 0x0e: // CTRL2 (write side, no effect)
            break;
        case 0x0f: // Reset
            geo_lc8951_reset(lc);
            break;
    }
}

void geo_lc8951_end_transfer(lc8951_t* const lc) {
    // Transfer complete — set DTBSY (active-low: bit set = not busy)
    lc->ifstat |= LC_IFSTAT_DTBSY;

    // Advance DAC by (DBC + 1), uint16_t wraps naturally
    lc->dacl += lc->dbc + 1;

    // Clear DBC
    lc->dbc = 0;

    // Set DTEI pending (active-low: clear bit = interrupt pending)
    lc->ifstat &= ~LC_IFSTAT_DTEI;
}

void geo_lc8951_state_save(lc8951_t* const lc, uint8_t *st) {
    for (size_t i = 0; i < 16; ++i) geo_serial_push8(st, lc->regs[i]);
    geo_serial_push8(st, lc->regptr);
    geo_serial_pushblk(st, lc->buffer, LC8951_BUFSZ);
    geo_serial_push16(st, lc->wal);
    geo_serial_push16(st, lc->ptl);
    geo_serial_push16(st, lc->dacl);
    geo_serial_push8(st, lc->ctrl0);
    geo_serial_push8(st, lc->ctrl1);
    geo_serial_push8(st, lc->ifstat);
    geo_serial_push8(st, lc->ifctrl);
    geo_serial_push16(st, lc->dbc);
    for (size_t i = 0; i < 4; ++i) geo_serial_push8(st, lc->head[i]);
    geo_serial_push8(st, lc->stat0);
    geo_serial_push8(st, lc->stat1);
    geo_serial_push8(st, lc->stat2);
    geo_serial_push8(st, lc->stat3);
    geo_serial_push8(st, lc->decoder_enabled);
    geo_serial_push8(st, lc->protection_bypassed);
}

void geo_lc8951_state_load(lc8951_t* const lc, uint8_t *st) {
    for (size_t i = 0; i < 16; ++i) lc->regs[i] = geo_serial_pop8(st);
    lc->regptr = geo_serial_pop8(st);
    geo_serial_popblk(lc->buffer, st, LC8951_BUFSZ);
    lc->wal = geo_serial_pop16(st);
    lc->ptl = geo_serial_pop16(st);
    lc->dacl = geo_serial_pop16(st);
    lc->ctrl0 = geo_serial_pop8(st);
    lc->ctrl1 = geo_serial_pop8(st);
    lc->ifstat = geo_serial_pop8(st);
    lc->ifctrl = geo_serial_pop8(st);
    lc->dbc = geo_serial_pop16(st);
    for (size_t i = 0; i < 4; ++i) lc->head[i] = geo_serial_pop8(st);
    lc->stat0 = geo_serial_pop8(st);
    lc->stat1 = geo_serial_pop8(st);
    lc->stat2 = geo_serial_pop8(st);
    lc->stat3 = geo_serial_pop8(st);
    lc->decoder_enabled = geo_serial_pop8(st);
    lc->protection_bypassed = geo_serial_pop8(st);
}
