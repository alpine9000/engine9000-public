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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "m68k/m68k.h"
#include "m68k/m68kcpu.h"

#include "geo.h"
#include "geo_cd.h"
#include "geo_disc.h"
#include "geo_lc8951.h"
#include "geo_lspc.h"
#include "geo_m68k.h"
#include "geo_serial.h"
#include "geo_z80.h"

// BIOS family detection
#define CD_BIOS_UNKNOWN     0
#define CD_BIOS_FRONT       1
#define CD_BIOS_TOP         2
#define CD_BIOS_CDZ         3
#define CD_BIOS_UNI         4

// Neo Geo CD RAM Buffers
static uint8_t pram[SIZE_2M];          // Program RAM (replaces P ROM/main RAM)
static uint8_t spr_dram[SIZE_4M];      // Sprite DRAM (replaces C ROM)
static uint8_t pcm_dram[SIZE_1M];      // PCM DRAM (replaces V ROMs)
static uint8_t z80_ram_cd[SIZE_64K];   // Z80 RAM (replaces M1 ROM + banking)
static uint8_t fix_ram[SIZE_128K];     // FIX layer RAM (replaces S ROM)
static uint8_t bram[SIZE_8K];          // Backup RAM (replaces memory card)

// ROM data pointer
static romdata_t *romdata = NULL;

// Vector table source: 0 = BIOS, 1 = Program RAM
static uint8_t vectable = 0;

// Transfer area (0xE00000) configuration
static uint8_t reg_transarea = 0;      // 0 = SPR, 1 = PCM, 4 = Z80, 5 = FIX
static uint8_t spr_bank = 0;           // SPR bank (0-3, 1MB each)
static uint8_t pcm_bank = 0;           // PCM bank (0-1, 512K each)

// Video control registers
static uint8_t reg_disblspr = 0;
static uint8_t reg_disblfix = 0;
static uint8_t reg_envideo = 0;

// Bus request states
static uint8_t busreq_spr = 0;
static uint8_t busreq_pcm = 0;
static uint8_t busreq_z80 = 0;
static uint8_t busreq_fix = 0;

// Z80 reset/enable control
static uint8_t z80_enabled = 0;

// SRAM lock
static uint8_t reg_sramlock = 0;

// LC8951 CD Controller instance
static lc8951_t lc;

/* CD Communication Protocol
   The 68K communicates with the CD drive controller through a nybble-based
   serial protocol using registers REG_CDDOUTPUT ($FF0163), REG_CDDCTRL
   ($FF0165), and REG_CDDINPUT ($FF0161). Commands and responses are each
   5 bytes (10 nybbles), with the last nybble of the 5th byte being a
   checksum. The protocol is clocked by writing to REG_CDDCTRL to advance
   the nybble pointer and toggle the strobe signal.

   Command byte 0 (high nybble) selects the operation:
     0x00 = Status          0x10 = Stop           0x20 = Query TOC
     0x30 = Play (MSF)      0x40 = Seek           0x50 = Unknown (CDZ)
     0x60 = Pause           0x70 = Resume
     0x80 = Scan forward    0x90 = Scan backward   0xB0 = Move to track

   Drive status codes returned in the high nybble of response byte 0:
     0x00 = Idle            0x10 = Playing         0x20 = Seeking
     0x30 = Scanning        0x40 = Paused
     0x90 = Stopped         0xC0 = End of disc
*/

#define CD_STATUS_IDLE      0x00
#define CD_STATUS_PLAY      0x10
#define CD_STATUS_SEEK      0x20
#define CD_STATUS_SCAN      0x30
#define CD_STATUS_PAUSE     0x40
#define CD_STATUS_STOP      0x90
#define CD_STATUS_END       0xC0

typedef struct _cdcomm_t {
    uint8_t cmd[5];         // Command packet bytes
    uint8_t status[5];      // Response packet bytes
    uint8_t cmd_nybble;     // Current nybble in command (0-9)
    uint8_t stat_nybble;    // Current nybble in response (0-9)
    uint8_t strobe;         // Clock strobe state

    uint8_t drive_status;   // Current drive state
    uint32_t play_lba;      // Current play position (LBA)
    uint32_t target_lba;    // Target position
    uint8_t playing_audio;  // Is an audio track playing?
    uint8_t playing_data;   // Is a data track being read?
} cdcomm_t;

static cdcomm_t cd;

// DMA Engine
typedef struct _cddma_t {
    uint32_t src;
    uint32_t dst;
    uint32_t len;           // Transfer length (in words)
    uint16_t val;           // Fill value
    uint16_t config;        // DMA mode
    uint8_t enabled;
} cddma_t;

static cddma_t dma;

/* CD IRQ State
   The Neo Geo CD uses IRQ level 2 (shared with cartridge timer interrupt)
   for CD-related interrupts. Two interrupt sources are multiplexed:
     - Decoder interrupt (Vector 21, bit 5): fired when the LC8951 decodes
       a sector and DECI is pending. Acknowledged by writing 0x20 to $FF000F.
     - Communication interrupt (Vector 22, bit 4): fired at the sector rate
       (~75Hz playing, ~64Hz idle). Acknowledged by writing 0x10 to $FF000F.
   Both sources require irq_mask1 ($FF0002) to have the appropriate bits set
   and cd_irq_enabled ($FF0181) to be nonzero.
*/
static uint8_t cd_irq_mask = 0;        // FF000F acknowledge bits
static uint8_t cd_irq_enabled = 0;     // FF0181
static uint16_t irq_mask1 = 0;         // FF0002: CDROM interrupt mask
static uint16_t irq_mask2 = 0;         // FF0004: VBL interrupt mask (VITAL)
static int vbl_pending = 0;            // Latched VBL if irq_mask2 wasn't ready

// Interrupt handling
static uint8_t cd_pending_irq = 0;

// Re-evaluate CD IRQ level and vector based on pending sources
static void cd_update_interrupts(void) {
    if (cd_pending_irq)
        geo_m68k_interrupt(IRQ_CD);
    else
        m68k_set_virq(IRQ_CD, 0);
}

void geo_cd_irq_set(uint8_t bits) {
    cd_pending_irq |= bits;
    cd_update_interrupts();
}

void geo_cd_irq_clear(uint8_t bits) {
    cd_pending_irq &= ~bits;
    cd_update_interrupts();
}

uint8_t geo_cd_irq_pending(void) {
    return cd_pending_irq;
}

// Bit-reverse table for CDDA sample registers
static const uint8_t bitrev[256] = {
    0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0,
    0x10,0x90,0x50,0xd0,0x30,0xb0,0x70,0xf0,
    0x08,0x88,0x48,0xc8,0x28,0xa8,0x68,0xe8,
    0x18,0x98,0x58,0xd8,0x38,0xb8,0x78,0xf8,
    0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4,
    0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4,
    0x0c,0x8c,0x4c,0xcc,0x2c,0xac,0x6c,0xec,
    0x1c,0x9c,0x5c,0xdc,0x3c,0xbc,0x7c,0xfc,
    0x02,0x82,0x42,0xc2,0x22,0xa2,0x62,0xe2,
    0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2,
    0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea,
    0x1a,0x9a,0x5a,0xda,0x3a,0xba,0x7a,0xfa,
    0x06,0x86,0x46,0xc6,0x26,0xa6,0x66,0xe6,
    0x16,0x96,0x56,0xd6,0x36,0xb6,0x76,0xf6,
    0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee,
    0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe,
    0x01,0x81,0x41,0xc1,0x21,0xa1,0x61,0xe1,
    0x11,0x91,0x51,0xd1,0x31,0xb1,0x71,0xf1,
    0x09,0x89,0x49,0xc9,0x29,0xa9,0x69,0xe9,
    0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9,
    0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5,
    0x15,0x95,0x55,0xd5,0x35,0xb5,0x75,0xf5,
    0x0d,0x8d,0x4d,0xcd,0x2d,0xad,0x6d,0xed,
    0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,0x7d,0xfd,
    0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3,
    0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3,
    0x0b,0x8b,0x4b,0xcb,0x2b,0xab,0x6b,0xeb,
    0x1b,0x9b,0x5b,0xdb,0x3b,0xbb,0x7b,0xfb,
    0x07,0x87,0x47,0xc7,0x27,0xa7,0x67,0xe7,
    0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7,
    0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef,
    0x1f,0x9f,0x5f,0xdf,0x3f,0xbf,0x7f,0xff,
};

static inline uint16_t reverse_bits_16(uint16_t val) {
    return (bitrev[val & 0xff] << 8) | bitrev[val >> 8];
}

// CD timing
#define CD_SECTOR_RATE_IDLE (24000000 / 64)  // ~375000 master cycles (idle)
#define CD_SECTOR_RATE_1X   (24000000 / 75)  // ~320000 master cycles per sector
#define CD_SECTOR_RATE_2X   (24000000 / 150) // ~160000 master cycles per sector

static uint32_t cd_sector_counter = 0;
static uint32_t cd_sector_rate = CD_SECTOR_RATE_1X;

static int speed_hack = 0;
static int bios_family = CD_BIOS_UNKNOWN;
static int sector_decoded_this_frame = 0;

// Cached track lookup - avoids repeated forward scans through all tracks
static unsigned cached_track = 1;
static uint32_t cached_track_start = 0;
static uint32_t cached_track_end = 0;

// Find which 1-based track a disc LBA belongs to.
// Uses backward search (first match wins) and caches the result.
static unsigned find_track_for_lba(uint32_t lba) {
    // Fast path: check cached track
    if (lba >= cached_track_start && lba < cached_track_end)
        return cached_track;

    unsigned ntracks = geo_disc_num_tracks();
    unsigned track = 1;
    for (unsigned i = ntracks; i > 0; --i) {
        if (lba >= geo_disc_track_start(i)) {
            track = i;
            break;
        }
    }

    // Update cache
    cached_track = track;
    cached_track_start = geo_disc_track_start(track);
    if (track < ntracks)
        cached_track_end = geo_disc_track_start(track + 1);
    else
        cached_track_end = geo_disc_leadout();

    return track;
}

/* CDDA audio - demand-driven by the mixer (not timed according to the master
   clock based tick function). The mixer calls geo_cd_read_cdda() to pull
   exactly the samples it needs. A cached sector bridges the 588-sample sector
   boundary.
*/
#define CDDA_SAMPS_PER_SECTOR 588
static uint8_t cdda_playing = 0;
static int16_t cdda_sector_cache[CDDA_SAMPS_PER_SECTOR << 1]; // Current sector
static size_t cdda_sector_pos = CDDA_SAMPS_PER_SECTOR; // Position in cache

// Audio stream LBA (independent of cd.play_lba)
static uint32_t cdda_audio_lba = 0;

// Master cycle counter within frame for cycle-accurate CDDA sample indexing
static uint32_t cd_frame_mcycs = 0;

// Helper functions
static inline uint8_t read08(uint8_t *ptr, uint32_t addr) {
    return ptr[addr];
}

static inline uint16_t read16(uint8_t *ptr, uint32_t addr) {
    return (ptr[addr] << 8) | ptr[addr + 1];
}

static inline void write16(uint8_t *ptr, uint32_t addr, uint16_t data) {
    ptr[addr + 1] = data & 0xff;
    ptr[addr] = data >> 8;
}

// BCD conversion helpers
static inline uint8_t to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

static inline uint8_t from_bcd(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0f);
}

// CD Communication Protocol
static void cd_comm_build_response(void);

static void cd_comm_reset(void) {
    memset(&cd, 0, sizeof(cd));
    cd.drive_status = CD_STATUS_IDLE;
    cd.cmd_nybble = 0;
    cd.stat_nybble = 9;
    cd.strobe = 1;
    // Set valid checksum on initial response packet
    cd.status[0] = cd.drive_status;
    cd_comm_build_response();
}

static uint8_t cd_comm_checksum(uint8_t *pkt) {
    uint8_t sum = 0;
    for (int i = 0; i < 4; ++i) {
        sum += (pkt[i] >> 4);
        sum += (pkt[i] & 0x0f);
    }
    sum += (pkt[4] >> 4);
    sum += 5;
    return (~sum) & 0x0f;
}

static void cd_comm_build_response(void) {
    cd.status[4] = (cd.status[4] & 0xf0) | cd_comm_checksum(cd.status);
}

static void cd_comm_process_command(void) {
    // Verify command checksum
    uint8_t expected = cd_comm_checksum(cd.cmd);
    if ((cd.cmd[4] & 0x0f) != expected) {
        geo_log(GEO_LOG_WRN, "CD command checksum mismatch\n");
        cd.status[0] = cd.drive_status;
        cd.status[1] = 0;
        cd.status[2] = 0;
        cd.status[3] = 0;
        cd.status[4] = 0;
        cd_comm_build_response();
        return;
    }

    uint8_t m, s, f;

    switch (cd.cmd[0]) {
        case 0x00: // Status
            cd.status[0] = (cd.status[0] & 0x0f) | cd.drive_status;
            break;

        case 0x10: // Stop
            cd.playing_audio = 0;
            cd.playing_data = 0;
            cdda_playing = 0;
            cd.drive_status = CD_STATUS_IDLE;
            cd.status[0] = cd.drive_status;
            cd.status[1] = 0;
            cd.status[2] = 0;
            cd.status[3] = 0;
            cd.status[4] = 0;
            break;

        case 0x20: { // Query TOC/position info
            uint8_t subcmd = cd.cmd[1] & 0x0f;
            if ((cd.drive_status == CD_STATUS_IDLE) &&
                geo_disc_num_tracks() > 0)
                cd.drive_status = CD_STATUS_STOP;

            switch (subcmd) {
                case 0x00: { // Current absolute position
                    geo_disc_lba_to_msf(cd.play_lba + 150, &m, &s, &f);
                    cd.status[0] = cd.drive_status;
                    cd.status[1] = to_bcd(m);
                    cd.status[2] = to_bcd(s);
                    cd.status[3] = to_bcd(f);
                    unsigned trk = find_track_for_lba(cd.play_lba);
                    cd.status[4] = !geo_disc_track_is_audio(trk) ? 0x40 : 0x00;
                    break;
                }
                case 0x01: { // Current relative position
                    geo_disc_lba_to_msf(cd.play_lba + 150, &m, &s, &f);
                    cd.status[0] = cd.drive_status | 0x01;
                    cd.status[1] = to_bcd(m);
                    cd.status[2] = to_bcd(s);
                    cd.status[3] = to_bcd(f);
                    unsigned trk = find_track_for_lba(cd.play_lba);
                    cd.status[4] = !geo_disc_track_is_audio(trk) ? 0x40 : 0x00;
                    break;
                }
                case 0x02: { // Current track
                    unsigned track = find_track_for_lba(cd.play_lba);
                    int is_data = !geo_disc_track_is_audio(track);
                    cd.status[0] = cd.drive_status | 0x02;
                    cd.status[1] = to_bcd(track);

                    /* Return a computed index when on the data track. The
                       index is derived from the current position using the raw
                       internal LBA, matching what the CD drive's sub-Q channel
                       would report. This satisfies in-game protection checks
                       performed during gameplay.
                    */
                    if (is_data) {
                        uint8_t pm, ps, pf;
                        geo_disc_lba_to_msf(cd.play_lba, &pm, &ps, &pf);
                        unsigned idx = ((pm * 60) + (ps + 4)) / 4;
                        if (idx > 99)
                            idx = 99;
                        cd.status[2] = to_bcd(idx);
                    }
                    else {
                        cd.status[2] = to_bcd(1);
                    }

                    cd.status[3] = 0;
                    cd.status[4] = is_data ? 0x40 : 0x00;
                    break;
                }
                case 0x03: { // Leadout address
                    uint32_t lo = geo_disc_leadout();
                    geo_disc_lba_to_msf(lo + 150, &m, &s, &f);
                    cd.status[0] = cd.drive_status | 0x03;
                    cd.status[1] = to_bcd(m);
                    cd.status[2] = to_bcd(s);
                    cd.status[3] = to_bcd(f);
                    cd.status[4] = 0;
                    break;
                }
                case 0x04: { // First and last track
                    cd.status[0] = cd.drive_status | 0x04;
                    cd.status[1] = to_bcd(1);
                    cd.status[2] = to_bcd(geo_disc_num_tracks());
                    cd.status[3] = 0;
                    cd.status[4] = 0;
                    break;
                }
                case 0x05: { // Track N start time
                    uint8_t track = from_bcd(cd.cmd[2]);
                    uint32_t start = geo_disc_track_start(track);
                    geo_disc_lba_to_msf(start + 150, &m, &s, &f);
                    uint8_t is_data = !geo_disc_track_is_audio(track);
                    cd.status[0] = cd.drive_status | 0x05;
                    cd.status[1] = to_bcd(m);
                    cd.status[2] = to_bcd(s);
                    cd.status[3] = to_bcd(f) | (is_data ? 0x80 : 0x00);
                    cd.status[4] = cd.cmd[2] << 4;
                    break;
                }
                case 0x06: { // End of disc check
                    uint32_t lo = geo_disc_leadout();
                    if (cd.play_lba >= lo)
                        cd.drive_status = CD_STATUS_END;
                    cd.status[0] = cd.drive_status | 0x06;
                    cd.status[1] = 0;
                    cd.status[2] = 0;
                    cd.status[3] = 0;
                    unsigned trk = find_track_for_lba(cd.play_lba);
                    cd.status[4] = !geo_disc_track_is_audio(trk) ? 0x40 : 0x00;
                    break;
                }
                case 0x07: // CDZ disc recognition (copy protection)
                    cd.status[0] = cd.drive_status | 0x07;
                    cd.status[1] = 0x02;
                    cd.status[2] = 0;
                    cd.status[3] = 0;
                    cd.status[4] = 0;
                    break;
                default:
                    cd.status[0] = cd.drive_status;
                    cd.status[1] = 0;
                    cd.status[2] = 0;
                    cd.status[3] = 0;
                    cd.status[4] = 0;
                    break;
            }
            break;
        }

        case 0x30: { // Play (from MSF)
            m = from_bcd(cd.cmd[1]);
            s = from_bcd(cd.cmd[2]);
            f = from_bcd(cd.cmd[3]);
            uint32_t lba = geo_disc_msf_to_lba(m, s, f);
            if (lba >= 150)
                lba -= 150;

            cd.play_lba = lba;
            cd.target_lba = lba;

            unsigned track = find_track_for_lba(lba);

            if (geo_disc_track_is_audio(track)) {
                cd.playing_audio = 1;
                cd.playing_data = 0;
                cdda_playing = 1;
                cdda_audio_lba = lba;
                // Force new sector read
                cdda_sector_pos = CDDA_SAMPS_PER_SECTOR;
            } else {
                cd.playing_audio = 0;
                cd.playing_data = 1;
                cdda_playing = 0;
            }

            cd.drive_status = CD_STATUS_PLAY;
            cd.status[0] = cd.drive_status | 0x02;
            cd.status[1] = to_bcd(track);
            cd.status[2] = 0;
            cd.status[3] = 0;
            cd.status[4] = 0;
            break;
        }

        case 0x40: { // Seek
            m = from_bcd(cd.cmd[1]);
            s = from_bcd(cd.cmd[2]);
            f = from_bcd(cd.cmd[3]);
            uint32_t lba = geo_disc_msf_to_lba(m, s, f);
            if (lba >= 150)
                lba -= 150;
            cd.play_lba = lba;
            cd.playing_audio = 0;
            cd.playing_data = 0;
            cdda_playing = 0;
            cd.drive_status = CD_STATUS_PAUSE;
            cd.status[0] = CD_STATUS_SEEK;
            cd.status[1] = 0;
            cd.status[2] = 0;
            cd.status[3] = 0;
            cd.status[4] = 0;
            break;
        }

        case 0x50: // Unknown (CDZ only)
            cd.status[0] = cd.drive_status;
            break;

        case 0x60: // Pause
            cdda_playing = 0;
            cd.drive_status = CD_STATUS_PAUSE;
            cd.status[0] = cd.drive_status;
            break;

        case 0x70: // Resume
            if (cd.playing_audio || cd.playing_data) {
                cd.drive_status = CD_STATUS_PLAY;
                if (cd.playing_audio) {
                    cdda_playing = 1;
                    cdda_audio_lba = cd.play_lba;
                    cdda_sector_pos = CDDA_SAMPS_PER_SECTOR;
                }
            }
            cd.status[0] = cd.drive_status;
            break;

        case 0x80: // Scan forward
            cd.play_lba += 75;
            if (cd.play_lba >= geo_disc_leadout()) {
                cd.play_lba = geo_disc_leadout() - 1;
                cd.drive_status = CD_STATUS_END;
            } else {
                cd.drive_status = CD_STATUS_PLAY;
            }
            cd.status[0] = CD_STATUS_SCAN;
            break;

        case 0x90: // Scan backward
            if (cd.play_lba >= 75)
                cd.play_lba -= 75;
            else
                cd.play_lba = 0;
            cd.drive_status = CD_STATUS_PLAY;
            cd.status[0] = CD_STATUS_SCAN;
            break;

        case 0xB0: { // Move to track
            uint8_t track = from_bcd(cd.cmd[1]);
            if (track >= 1 && track <= geo_disc_num_tracks()) {
                cd.play_lba = geo_disc_track_start(track);
            }

            if (geo_disc_track_is_audio(track)) {
                cd.playing_audio = 1;
                cd.playing_data = 0;
                cdda_playing = 1;
                cdda_audio_lba = cd.play_lba;
                cdda_sector_pos = CDDA_SAMPS_PER_SECTOR;
            }
            else {
                cd.playing_audio = 0;
                cd.playing_data = 1;
                cdda_playing = 0;
            }

            cd.drive_status = CD_STATUS_PLAY;
            cd.status[0] = cd.drive_status | 0x02;
            cd.status[1] = to_bcd(track);
            cd.status[2] = 0;
            cd.status[3] = 0;
            cd.status[4] = 0;
            break;
        }

        // Protection-related commands (return status only)
        case 0x02: case 0x13: case 0x23: case 0x33:
        case 0x43: case 0x53: case 0x63: case 0xE2:
            cd.status[0] = cd.drive_status;
            cd.status[1] = 0;
            cd.status[2] = 0;
            cd.status[3] = 0;
            cd.status[4] = 0;
            break;

        default:
            geo_log(GEO_LOG_WRN, "Unknown CD command: %02x\n", cd.cmd[0]);
            cd.status[0] = cd.drive_status;
            cd.status[1] = 0;
            cd.status[2] = 0;
            cd.status[3] = 0;
            cd.status[4] = 0;
            break;
    }

    cd_comm_build_response();
}

// DMA Implementation
#define DMA_DEST_RAM     0
#define DMA_DEST_MAPPED  1
#define DMA_DEST_PALETTE 2

// Transfer Areas
#define TRANSAREA_SPR    0
#define TRANSAREA_PCM    1
#define TRANSAREA_Z80    4
#define TRANSAREA_FIX    5

static int cd_dma_resolve_dest(uint32_t address, uint8_t **dst_ptr,
                               uint32_t *dst_mask) {
    address &= 0xffffff;
    if (address < 0x200000) {
        // Program RAM
        *dst_ptr = pram;
        *dst_mask = SIZE_2M - 1;
        return DMA_DEST_RAM;
    }
    else if (address >= 0x400000 && address < 0x800000) {
        // Palette RAM - handled via geo_lspc_palram_wr16 for colour conversion
        *dst_ptr = (uint8_t*)1; // non-NULL sentinel
        *dst_mask = 0x1fff; // 8K palette entries (16KB / 2)
        return DMA_DEST_PALETTE;
    }
    else if (address >= 0xe00000 && address <= 0xefffff) {
        switch (reg_transarea) {
            case TRANSAREA_SPR: {
                *dst_ptr = spr_dram + (spr_bank * SIZE_1M);
                *dst_mask = SIZE_1M - 1;
                break;
            }
            case TRANSAREA_PCM: {
                *dst_ptr = pcm_dram + (pcm_bank * SIZE_512K);
                *dst_mask = SIZE_512K - 1;
                break;
            }
            case TRANSAREA_Z80: {
                *dst_ptr = z80_ram_cd;
                *dst_mask = SIZE_64K - 1;
                break;
            }
            case TRANSAREA_FIX: {
                *dst_ptr = fix_ram;
                *dst_mask = SIZE_128K - 1;
                break;
            }
            default: {
                *dst_ptr = NULL;
                return DMA_DEST_RAM;
            }
        }
        return DMA_DEST_MAPPED;
    }
    else {
        *dst_ptr = NULL;
        return DMA_DEST_RAM;
    }
}

/* Write a 16-bit word to the DMA destination, advancing offset by 2. Handles
   mapped area address transformations for FIX/Z80/PCM (addr >> 1, low byte).
*/
static void dma_write_word(uint8_t *ptr, uint32_t mask, uint32_t *offset,
                           uint16_t data, int dest_type) {
    switch (dest_type) {
        case DMA_DEST_MAPPED: {
            switch (reg_transarea) {
                case TRANSAREA_SPR: { // SPR - big-endian word write
                    uint32_t addr = *offset & (mask & ~1u);
                    ptr[addr] = (data >> 8) & 0xff;
                    ptr[addr + 1] = data & 0xff;
                    break;
                }
                case TRANSAREA_PCM: // PCM - address >> 1, low byte
                case TRANSAREA_Z80: // Z80 - address >> 1, low byte
                case TRANSAREA_FIX: { // FIX - address >> 1, low byte
                    uint32_t addr = (*offset >> 1) & mask;
                    ptr[addr] = data & 0xff;
                    break;
                }
            }
            break;
        }
        case DMA_DEST_PALETTE: {
            // Route through LSPC palette write for proper colour conversion
            geo_lspc_palram_wr16(*offset, data);
            break;
        }
        default: {
            // Program RAM - big-endian word write
            ptr[*offset & mask] = (data >> 8) & 0xff;
            ptr[(*offset + 1) & mask] = data & 0xff;
            break;
        }
    }
    *offset += 2;
}

static void cd_dma_execute(void) {
    if (!dma.enabled || dma.len == 0)
        return;

    uint8_t *dst_ptr = NULL;
    uint32_t dst_mask = 0;

    int dest_type = cd_dma_resolve_dest(dma.dst, &dst_ptr, &dst_mask);
    if (!dst_ptr) {
        geo_log(GEO_LOG_WRN, "DMA to unknown address: %06x area=%u\n",
            dma.dst, reg_transarea);
        dma.enabled = 0;
        return;
    }

    /* DMA Microcode Execution
       The LC8953 DMA controller uses a programmable microcode architecture.
       The 16-bit config word (REG_DMA_MODE) determines the transfer mode.
       Known microcode configurations used by the Neo Geo CD BIOS:

       0xFFC5, 0xFF89: LC8951 -> destination (sector data copy)
       0xFC2D:         LC8951 -> destination, odd byte expansion
       0xFE3D, 0xFE6D: RAM -> RAM copy (addr registers are swapped)
       0xFEF5:         Fill with incrementing addresses
       0xFFCD, 0xFFDD: Pattern fill
       0xE2DD, 0xF2DD: RAM -> RAM copy with byte swap (odd byte expansion)
       0xCFFD:         Fill odd bytes with incrementing addresses
    */
    switch (dma.config) {
        case 0xffc5:
        case 0xff89: {
            /* LC8951 -> Destination: copy decoded sector data
               Reads big-endian words from LC8951 buffer[DAC] with 16-bit
               wrapping.
            */
            if (lc.ifstat & LC_IFSTAT_DTBSY) {
                geo_log(GEO_LOG_WRN,
                    "DMA %04x: DTRG not written (DTBSY set)\n", dma.config);
                break;
            }
            uint32_t dst = dma.dst;
            uint32_t len = dma.len;
            uint32_t lc_words = (lc.dbc + 1) / 2;
            if (lc_words > 0 && len > lc_words)
                len = lc_words;
            uint16_t dac = lc.dacl;
            for (uint32_t i = 0; i < len; ++i) {
                uint16_t data = ((uint16_t)lc.buffer[dac] << 8) |
                    lc.buffer[(uint16_t)(dac + 1)];
                dma_write_word(dst_ptr, dst_mask, &dst, data, dest_type);
                dac += 2;
            }
            geo_lc8951_end_transfer(&lc);
            break;
        }
        case 0xfc2d: {
            /* LC8951 -> Destination, odd byte expansion
               Splits each source word into two destination words (high byte,
               then full word). Used for PCM/Z80/FIX transfers where the
               destination only accepts the low byte of each word write.
            */
            if (lc.ifstat & LC_IFSTAT_DTBSY) {
                geo_log(GEO_LOG_WRN,
                    "DMA fc2d: DTRG not written (DTBSY set)\n");
                break;
            }
            uint32_t dst = dma.dst;
            uint32_t len = dma.len;
            uint32_t lc_words = (lc.dbc + 1) / 2;
            if (lc_words > 0 && len > lc_words)
                len = lc_words;
            uint16_t dac = lc.dacl;
            for (uint32_t i = 0; i < len; ++i) {
                uint16_t data = ((uint16_t)lc.buffer[dac] << 8) |
                    lc.buffer[(uint16_t)(dac + 1)];
                dma_write_word(dst_ptr, dst_mask, &dst, data >> 8, dest_type);
                dma_write_word(dst_ptr, dst_mask, &dst, data, dest_type);
                dac += 2;
            }
            geo_lc8951_end_transfer(&lc);
            break;
        }
        case 0xfe3d:
        case 0xfe6d: { // RAM -> RAM copy (address registers are swapped)
            uint32_t src = dma.dst; // ADDR1 is actually the source
            uint32_t dst = dma.src; // ADDR2 is actually the destination

            dest_type = cd_dma_resolve_dest(dst, &dst_ptr, &dst_mask);
            if (!dst_ptr) {
                geo_log(GEO_LOG_WRN,
                    "DMA fe3d to unknown address: %06x\n", dst);
                break;
            }

            /* Inhibit blank vector table writes (Double Dragon fix):
               Some games zero the vector table before loading real vectors.
               If we write the zeros, the next interrupt hits address 0 (crash).
            */
            if ((dst & 0xffffff) < 0x80) {
                int blank = 1;
                for (uint32_t i = 0; i < dma.len && blank; ++i) {
                    if (read16(pram, (src + i * 2) & (SIZE_2M - 1)))
                        blank = 0;
                }
                if (blank)
                    break;
            }

            for (uint32_t i = 0; i < dma.len; ++i) {
                uint16_t w = read16(pram, src & (SIZE_2M - 1));
                dma_write_word(dst_ptr, dst_mask, &dst, w, dest_type);
                src += 2;
            }
            break;
        }
        case 0xfef5: { // Fill with incrementing addresses
            /* Writes 2 words per iteration (addr >> 16, addr), address
               increments by 4
            */
            uint32_t address = dma.dst;
            uint32_t dst = dma.dst;
            for (uint32_t i = 0; i < dma.len; ++i) {
                dma_write_word(dst_ptr, dst_mask, &dst, (address >> 16),
                    dest_type);
                dma_write_word(dst_ptr, dst_mask, &dst, address, dest_type);
                address += 4;
            }
            break;
        }
        case 0xffcd:
        case 0xffdd: {
            /* Pattern fill - Writes pattern word per iteration via mapped
               handler.
            */
            uint32_t dst = dma.dst;
            for (uint32_t i = 0; i < dma.len; ++i) {
                dma_write_word(dst_ptr, dst_mask, &dst, dma.val, dest_type);
            }
            break;
        }
        case 0xe2dd:
        case 0xf2dd: { // RAM -> RAM copy with byte swap (odd byte expansion)
            /* Reads each source word, writes BYTE_SWAP_16(data) then data
               (2 destination words per source word). Used for sprite tile
               uploads where the tile format requires byte-swapped pairs.
            */
            uint32_t src = dma.dst; // ADDR1 is actually the source
            uint32_t dst = dma.src; // ADDR2 is actually the destination

            // Re-resolve destination from the actual destination address
            dest_type = cd_dma_resolve_dest(dst, &dst_ptr, &dst_mask);
            if (!dst_ptr) {
                geo_log(GEO_LOG_WRN,
                    "DMA e2dd to unknown address: %06x\n", dst);
                break;
            }

            for (uint32_t i = 0; i < dma.len; ++i) {
                uint16_t data = read16(pram, src & (SIZE_2M - 1));
                uint16_t swapped = (data >> 8) | (data << 8);
                dma_write_word(dst_ptr, dst_mask, &dst, swapped, dest_type);
                dma_write_word(dst_ptr, dst_mask, &dst, data, dest_type);
                src += 2;
            }
            break;
        }
        case 0xcffd: { // Fill odd bytes with incrementing addresses
            /* Writes 4 words per iteration (addr bytes high to low), address
               increments by 8
            */
            uint32_t address = dma.dst;
            uint32_t dst = dma.dst;
            for (uint32_t i = 0; i < dma.len; ++i) {
                dma_write_word(dst_ptr, dst_mask, &dst, (address >> 24),
                    dest_type);
                dma_write_word(dst_ptr, dst_mask, &dst, (address >> 16),
                    dest_type);
                dma_write_word(dst_ptr, dst_mask, &dst, (address >> 8),
                    dest_type);
                dma_write_word(dst_ptr, dst_mask, &dst, address, dest_type);
                address += 8;
            }
            break;
        }
        default:
            geo_log(GEO_LOG_WRN, "Unknown DMA config: %04x\n", dma.config);
            break;
    }

    dma.enabled = 0;
}

// CD Register Read/Write (0xFF0000 - 0xFF01FF)
static uint8_t cd_reg_read_8(uint32_t addr) {
    addr &= 0x01ff;

    switch (addr) {
        case 0x000f: // REG_IRQACK - CD IRQ acknowledge status
            return cd_irq_mask;

        case 0x0017: // Unknown (front loader BIOS reads this)
            return 0x00;

        case 0x0061: // REG_DMASTART - DMA status (bit 6 = busy)
            return dma.enabled ? 0x40 : 0x00;

        case 0x0103: // LC8951 register data read
            return geo_lc8951_reg_read(&lc);

        case 0x0161: { // REG_CDDINPUT - CD communication response read
            uint8_t nybble;
            if (cd.stat_nybble & 1)
                nybble = cd.status[cd.stat_nybble >> 1] & 0x0f;
            else
                nybble = (cd.status[cd.stat_nybble >> 1] >> 4) & 0x0f;
            return nybble | (cd.strobe << 4);
        }

        case 0x0167: // CD status lines
            return 0x00;
    }

    return 0x00;
}

static uint16_t cd_reg_read_16(uint32_t addr) {
    addr &= 0x01ff;

    switch (addr) {
        case 0x0004: // VBL Interrupt Mask (VITAL - saved/restored on stack)
            return irq_mask2;

        case 0x011c: { // REG_CDCONFIG
            /* 11CLNNNN 00000000 - C = CD Mech, L = Lid Status, N = Nationality

               Country Code (Details from Top Loader motherboard)
               =================================
               |       | JN4 | JN3 | JN2 | JN1 |
               =================================
               | JAPAN |  X  |  X  |  X  |  X  |
               | U S A |  X  |  X  |  X  |  0  |
               | E U R |  X  |  X  |  0  |  X  |
               ---------------------------------
            */
            unsigned nat = 0x0000;
            switch (ngsys.region) {
                case REGION_JP: nat = 0x0f00; break;
                case REGION_US: nat = 0x0e00; break;
                default: nat = 0x0d00; break; // Europe and Asia (?)
            }

            unsigned mech = 0x0000; // 0 = Top Loader/CDZ, 1 = Front Loader
            unsigned tray = ngsys.sys <= SYSTEM_CDT ? 0x1000 : 0x0000;
            return (uint16_t)(nat | mech | tray) | 0xc000;
        }

        case 0x0188: // REG_CDDALEFTL - CDDA left channel (bit-reversed)
        case 0x018a: { // REG_CDDARIGHTL - CDDA right channel (bit-reversed)
            if (!cdda_playing || cdda_sector_pos >= CDDA_SAMPS_PER_SECTOR)
                return 0;
            int ch = (addr == 0x018a) ? 1 : 0;
            return reverse_bits_16(
                (uint16_t)cdda_sector_cache[cdda_sector_pos * 2 + ch]);
        }
    }

    // Fall through to byte handler for registers not word-specific
    uint8_t hi = cd_reg_read_8(addr);
    uint8_t lo = cd_reg_read_8(addr + 1);
    return (hi << 8) | lo;
}

static void cd_reg_write_8(uint32_t addr, uint8_t val) {
    addr &= 0x01ff;

    switch (addr) {
        case 0x000d: // Unknown registers (silently accepted)
        case 0x000e:
        case 0x0011:
        case 0x0015:
        case 0x0017:
        case 0x0167:
        case 0x016d:
            return;

        case 0x000f: { // REG_IRQACK - CD IRQ acknowledge
            if (val & 0x20) { // Vector 21 (decoder)
                cd_irq_mask &= ~0x20;
                cd_pending_irq &= ~CD_INT_DECODER;
            }
            if (val & 0x10) { // Vector 22 (communication)
                cd_irq_mask &= ~0x10;
                cd_pending_irq &= ~CD_INT_COMMUNICATION;
            }
            if (val & 0x08) { // Vector 23
                geo_log(GEO_LOG_WRN, "IRQ Ack Vector 0x5c/23\n");
            }
            if (val & 0x04) { // Vector 24
                geo_log(GEO_LOG_WRN, "IRQ Ack Vector 0x60/24\n");
            }
            cd_update_interrupts();
            return;
        }

        case 0x0061: // REG_DMASTART - DMA control
            if (val == 0x40) {
                dma.enabled = 1;
                cd_dma_execute();
            } else if (val == 0x00) {
                dma.enabled = 0;
                memset(&dma, 0, sizeof(dma));
            }
            return;

        case 0x0101: // LC8951 register address select (4 LSBs)
            lc.regptr = val & 0x0f;
            return;

        case 0x0103: // LC8951 register data write
            geo_lc8951_reg_write(&lc, val);
            return;

        case 0x0105: // REG_TRANSAREA - active transfer area select
            reg_transarea = val & 0x07;
            return;

        case 0x0111: // REG_DISBLSPR - Sprite display (1 = disable, 0 = enable)
            reg_disblspr = val;
            geo_lspc_disblspr_wr(reg_disblspr);
            return;
        case 0x0115: // REG_DISBLFIX - FIX display (1 = disable, 0 = enable)
            reg_disblfix = val;
            geo_lspc_disblfix_wr(reg_disblfix);
            return;
        case 0x0119: // REG_ENVIDEO - Video output (1 = enable, 0 = disable)
            reg_envideo = val;
            geo_lspc_envideo_wr(reg_envideo);
            return;

        /* DRAM Upload Zone Bus Request Registers
           Writing to REG_UPMAP* maps the corresponding DRAM bank into the
           transfer area (0xE00000). Writing to REG_UPUNMAP* releases it.
           While mapped, the 68K has bus ownership and can read/write directly.
        */
        case 0x0121: busreq_spr = 1; return; // REG_UPMAPSPR
        case 0x0123: busreq_pcm = 1; return; // REG_UPMAPPCM
        case 0x0127: // REG_UPMAPZ80
            busreq_z80 = 1; geo_z80_busreq(1); return;
        case 0x0129: busreq_fix = 1; return; // REG_UPMAPFIX

        case 0x0131: busreq_spr = 0; return; // REG_UPUNMAPSPR (alt)
        case 0x0133: busreq_pcm = 0; return; // REG_UPUNMAPPCM (alt)

        case 0x0141: busreq_spr = 0; return; // REG_UPUNMAPSPR
        case 0x0143: busreq_pcm = 0; return; // REG_UPUNMAPPCM
        case 0x0147: // REG_UPUNMAPZ80
            busreq_z80 = 0; geo_z80_busreq(0); return;
        case 0x0149: busreq_fix = 0; return; // REG_UPUNMAPFIX

        case 0x0163: { // REG_CDDOUTPUT - CD command write (4-bit nybble)
            if (cd.cmd_nybble >= 10)
                cd.cmd_nybble = 0;
            uint8_t byte_idx = cd.cmd_nybble / 2;
            if (cd.cmd_nybble & 1)
                cd.cmd[byte_idx] = (cd.cmd[byte_idx] & 0xf0) | (val & 0x0f);
            else
                cd.cmd[byte_idx] = (cd.cmd[byte_idx] & 0x0f) |
                    ((val & 0x0f) << 4);
            return;
        }

        case 0x0165: { // REG_CDDCTRL - CD communication pointer control
            switch (val) {
                case 0x00: break; // No-op
                case 0x01: // Advance command pointer
                    cd.cmd_nybble = (cd.cmd_nybble + 1) % 10;
                    if (cd.cmd_nybble == 0) {
                        // 10 nybbles received, command complete
                        cd_comm_process_command();
                    }
                    break;
                case 0x02: // Advance response pointer
                    cd.strobe = 0;
                    cd.stat_nybble = (cd.stat_nybble + 1) % 10;
                    break;
                case 0x03: // Set strobe
                    cd.strobe = 1;
                    break;
            }
            return;
        }

        case 0x016f: // REG_UPLOAD_EN - Watchdog: 0x00 = enable, 0x01 = disable
            geo_watchdog_enable(val == 0);
            return;

        case 0x0181: { // REG_CDIRQ_EN - CD communication reset (active low)
            cd_irq_enabled = (val != 0);
            /* Reset packet pointers:
               commandPointer = 0, responsePointer = 9, strobe = 1
            */
            cd.cmd_nybble = 0;
            cd.stat_nybble = 9;
            cd.strobe = 1;
            return;
        }
        case 0x0183: // REG_Z80RST - Z80 reset/enable (active low)
            if (val == 0x00) {
                z80_enabled = 0;
                geo_z80_assert_reset();
            }
            else {
                z80_enabled = 1;
                geo_z80_clear_reset();
            }
            return;

        case 0x01a1: // REG_SPRBANK - SPR DRAM bank select (2 LSBs)
            spr_bank = val & 0x03;
            return;

        case 0x01a3: // REG_PCMBANK - PCM DRAM bank select (1 LSB)
            pcm_bank = val & 0x01;
            return;
    }

}

static void cd_reg_write_16(uint32_t addr, uint16_t val) {
    addr &= 0x01ff;

    switch (addr) {
        case 0x0000: // CD-ROM drive reset (word write to $FF0000)
            cd.playing_audio = 0;
            cd.playing_data = 0;
            cdda_playing = 0;
            cd.drive_status = CD_STATUS_IDLE;
            return;

        case 0x0002: // CDROM Interrupt Mask - enables Vector 22 interrupts
            irq_mask1 = val;
            return;

        case 0x0004: // VBL Interrupt Mask (bits 4+5 must be set to enable)
            irq_mask2 = val;
            // Fire latched VBL if mask just became ready
            if (vbl_pending && (irq_mask2 & 0x030) == 0x030) {
                vbl_pending = 0;
                geo_m68k_interrupt(irq_vbl_level);
            }
            return;

        // Unknown word registers (silently accepted)
        case 0x0006:
        case 0x0008:
        case 0x000a:
            return;

        /* DMA Registers (LC8953, word-addressed)
           The DMA engine uses a microcode architecture. REG_DMA_MODE sets the
           16-bit microcode word, and the extension words at $FF0080-$FF008E
           form the 9-bit opcode table. REG_DMASTART ($FF0061) bit 6 triggers
           execution of the programmed transfer.
        */
        case 0x0064: // REG_DMA_ADDR1 high - DMA address 1 (high word)
            dma.dst = (dma.dst & 0xffff) | ((uint32_t)val << 16);
            return;
        case 0x0066: // REG_DMA_ADDR1 low - DMA address 1 (low word)
            dma.dst = (dma.dst & 0xffff0000) | (uint32_t)val;
            return;
        case 0x0068: // REG_DMA_ADDR2 high - DMA address 2 (high word)
            dma.src = (dma.src & 0xffff) | ((uint32_t)val << 16);
            return;
        case 0x006a: // REG_DMA_ADDR2 low - DMA address 2 (low word)
            dma.src = (dma.src & 0xffff0000) | (uint32_t)val;
            return;
        case 0x006c: // REG_DMA_VALUE - DMA fill value
            dma.val = val;
            return;
        case 0x0070: // REG_DMA_COUNT high - DMA transfer length (high word)
            dma.len = (dma.len & 0xffff) | ((uint32_t)val << 16);
            return;
        case 0x0072: // REG_DMA_COUNT low - DMA transfer length (low word)
            dma.len = (dma.len & 0xffff0000) | (uint32_t)val;
            return;
        case 0x007e: // REG_DMA_MODE - DMA microcode configuration word
            dma.config = val;
            return;
        case 0x0080: case 0x0082: case 0x0084: case 0x0086:
        case 0x0088: case 0x008a: case 0x008c: case 0x008e:
            return; // DMA microcode extension words (16x 9-bit opcodes)
    }

    // Fall through to byte handler for byte-oriented registers
    cd_reg_write_8(addr, (val >> 8) & 0xff);
    cd_reg_write_8(addr + 1, val & 0xff);
}

/* Transfer Area (0xE00000 - 0xEFFFFF)
   The transfer area provides 68K access to the DRAM banks while the
   corresponding bus request is held. REG_TRANSAREA selects which DRAM
   bank is mapped into this window:
     0 = SPR DRAM (1M bank, selected by REG_SPRBANK)
     1 = PCM DRAM (512K bank, selected by REG_PCMBANK)
     4 = Z80 DRAM (64K)
     5 = FIX DRAM (128K)
   For PCM, Z80, and FIX, the DRAM is 8-bit and mapped to odd bytes
   only. The address is shifted right by 1 to form the DRAM address.
   SPR DRAM is 16-bit and uses direct word-addressed access.
*/
static uint8_t transfer_read_8(uint32_t addr) {
    addr &= 0xfffff;

    switch (reg_transarea) {
        case TRANSAREA_SPR: { // SPR
            if (busreq_spr)
                return spr_dram[(spr_bank * SIZE_1M) + (addr & (SIZE_1M - 1))];
            break;
        }
        case TRANSAREA_PCM: { // PCM - Odd bytes only
            if (busreq_pcm && (addr & 1))
                return pcm_dram[(pcm_bank * SIZE_512K) +
                    ((addr >> 1) & (SIZE_512K - 1))];
            break;
        }
        case TRANSAREA_Z80: { // Z80 - Odd bytes only
            if (busreq_z80 && (addr & 1))
                return z80_ram_cd[(addr >> 1) & (SIZE_64K - 1)];
            break;
        }
        case TRANSAREA_FIX: { // FIX - Odd bytes only
            if (busreq_fix && (addr & 1))
                return fix_ram[(addr >> 1) & (SIZE_128K - 1)];
            break;
        }
    }
    return 0xff;
}

static uint16_t transfer_read_16(uint32_t addr) {
    addr &= 0xfffff;

    switch (reg_transarea) {
        case TRANSAREA_SPR: { // SPR - Word read
            uint32_t spr_addr = (spr_bank * SIZE_1M) + (addr & (SIZE_1M - 2));
            if (busreq_spr)
                return (spr_dram[spr_addr] << 8) | spr_dram[spr_addr + 1];
            break;
        }
        case TRANSAREA_PCM: { // PCM
            uint32_t pcm_addr = (pcm_bank * SIZE_512K) +
                ((addr >> 1) & (SIZE_512K - 1));
            if (busreq_pcm)
                return pcm_dram[pcm_addr] | 0xff00;
            break;
        }
        case TRANSAREA_Z80: { // Z80
            uint32_t z_addr = (addr >> 1) & (SIZE_64K - 1);
            if (busreq_z80)
                return z80_ram_cd[z_addr] | 0xff00;
            break;
        }
        case TRANSAREA_FIX: { // FIX
            uint32_t fix_addr = (addr >> 1) & (SIZE_128K - 1);
            if (busreq_fix)
                return fix_ram[fix_addr] | 0xff00;
            break;
        }
    }
    return 0xffff;
}

static void transfer_write_8(uint32_t addr, uint8_t val) {
    addr &= 0xfffff;

    switch (reg_transarea) {
        case TRANSAREA_SPR: { // SPR
            if (!busreq_spr)
                return;
            spr_dram[(spr_bank * SIZE_1M) + (addr & (SIZE_1M - 1))] = val;
            return;
        }
        case TRANSAREA_PCM: { // PCM - Odd bytes only
            if (busreq_pcm && (addr & 1))
                pcm_dram[(pcm_bank * SIZE_512K) +
                    ((addr >> 1) & (SIZE_512K - 1))] = val;
            return;
        }
        case TRANSAREA_Z80: { // Z80 - Odd bytes only
            if (busreq_z80 && (addr & 1))
                z80_ram_cd[(addr >> 1) & (SIZE_64K - 1)] = val;
            return;
        }
        case TRANSAREA_FIX: { // FIX - Odd bytes only
            if (busreq_fix && (addr & 1))
                fix_ram[(addr >> 1) & (SIZE_128K - 1)] = val;
            return;
        }
    }
}

static void transfer_write_16(uint32_t addr, uint16_t val) {
    addr &= 0xfffff;

    switch (reg_transarea) {
        case TRANSAREA_SPR: { // SPR - Word write
            if (!busreq_spr)
                return;
            uint32_t spr_addr = (spr_bank * SIZE_1M) + (addr & (SIZE_1M - 2));
            spr_dram[spr_addr] = val >> 8;
            spr_dram[spr_addr + 1] = val & 0xff;
            return;
        }
        case TRANSAREA_PCM: { // PCM
            if (!busreq_pcm)
                return;
            uint32_t pcm_addr = (pcm_bank * SIZE_512K) +
                ((addr >> 1) & (SIZE_512K - 1));
            pcm_dram[pcm_addr] = val & 0xff;
            return;
        }
        case TRANSAREA_Z80: { // Z80
            if (!busreq_z80)
                return;
            uint32_t z_addr = (addr >> 1) & (SIZE_64K - 1);
            z80_ram_cd[z_addr] = val & 0xff;
            return;
        }
        case TRANSAREA_FIX: { // FIX
            if (!busreq_fix)
                return;
            uint32_t fix_addr = (addr >> 1) & (SIZE_128K - 1);
            fix_ram[fix_addr] = val & 0xff;
            return;
        }
    }
}

/* Neo Geo CD 68K Memory Map
 * =====================================================================
 * |    Address Range    | Size |             Description              |
 * =====================================================================
 * | 0x000000 - 0x00007f | 128B | Vector Table (BIOS or Program RAM)   |
 * ---------------------------------------------------------------------
 * | 0x000080 - 0x1fffff |   2M | Program RAM (DRAM)                   |
 * ---------------------------------------------------------------------
 * | 0x200000 - 0x2fffff |   1M | Unused (open bus)                    |
 * ---------------------------------------------------------------------
 * | 0x300000 - 0x3fffff |      | Memory Mapped Registers              |
 * ---------------------------------------------------------------------
 * | 0x400000 - 0x401fff |   8K | Banked Palette RAM                   |
 * |---------------------|------|--------------------------------------
 * | 0x402000 - 0x7fffff |      | Palette RAM Mirror                   |
 * ---------------------------------------------------------------------
 * | 0x800000 - 0xbfffff |   8K | Backup RAM (8K, odd bytes, mirrored) |
 * ---------------------------------------------------------------------
 * | 0xc00000 - 0xc7ffff | 512K | BIOS ROM                             |
 * |---------------------|------|--------------------------------------
 * | 0xc80000 - 0xcfffff |      | BIOS ROM Mirror                      |
 * ---------------------------------------------------------------------
 * | 0xd00000 - 0xd0ffff |  64K | NVRAM (battery-backed SRAM)          |
 * |---------------------|------|--------------------------------------
 * | 0xd10000 - 0xdfffff |      | NVRAM Mirror                         |
 * ---------------------------------------------------------------------
 * | 0xe00000 - 0xefffff |   1M | Transfer Area (mapped DRAM upload)   |
 * ---------------------------------------------------------------------
 * | 0xff0000 - 0xff01ff |      | CD Registers (active at 0xff0000)    |
 * |---------------------|------|--------------------------------------
 * | 0xff0200 - 0xffffff |      | CD Register Mirror                   |
 * ---------------------------------------------------------------------
 */

unsigned geo_cd_m68k_read_8(unsigned address) {
    address &= 0xffffff;

    if (address < 0x000080) { // Vector Table
        m68k_modify_timeslice(1);
        return vectable ?
            read08(pram, address) : read08(romdata->b, address);
    }
    else if (address < 0x200000) { // Program RAM (2MB)
        return read08(pram, address);
    }
    else if (address < 0x300000) { // Unused
        return 0xff;
    }
    else if (address < 0x400000) { // I/O and System Registers
        switch (address) {
            case 0x300000: // REG_P1CNT
                return geo_input_cb[0](0);
            case 0x300001: // REG_DIPSW
                return geo_input_sys_cb[3]();
            case 0x300081: // REG_SYSTYPE
                return geo_input_sys_cb[2]() & ~0x40;
            case 0x320000: // REG_SOUND (read)
                return ngsys.sound_reply;
            case 0x320001: // REG_STATUS_A
                return geo_input_sys_cb[0]();
            case 0x340000: // REG_P2CNT
                return geo_input_cb[1](1);
            case 0x380000: // REG_STATUS_B
                return geo_input_sys_cb[1]();
            case 0x3c0000: case 0x3c0002: case 0x3c0008: case 0x3c000a: // LSPC
            case 0x3c0004: case 0x3c000c: case 0x3c0006: case 0x3c000e:
                break;
        }
    }
    else if (address < 0x800000) { // Palette RAM
        return geo_lspc_palram_rd08(address);
    }
    else if (address < 0xc00000) { // Backup RAM (8K mirrored)
        if (address & 0x01)
            return bram[(address >> 1) & 0x1fff];
        return 0xff;
    }
    else if (address < 0xd00000) { // BIOS ROM (512K mirrored)
        return read08(romdata->b, address & 0x7ffff);
    }
    else if (address < 0xe00000) { // NVRAM (64K mirrored)
        return read08(ngsys.nvram, address & 0xffff);
    }
    else if (address < 0xf00000) { // Transfer area
        return transfer_read_8(address);
    }
    else { // CD registers
        return cd_reg_read_8(address);
    }

    return 0xff;
}

unsigned geo_cd_m68k_read_16(unsigned address) {
    address &= 0xffffff;

    if (address < 0x000080) {
        m68k_modify_timeslice(1);
        return vectable ?
            read16(pram, address) : read16(romdata->b, address);
    }
    else if (address < 0x200000) {
        return read16(pram, address);
    }
    else if (address < 0x300000) {
        return 0xffff;
    }
    else if (address < 0x400000) {
        switch (address) {
            case 0x300000: { // REG_P1CNT
                uint8_t val = geo_input_cb[0](0);
                return (val << 8) | val;
            }
            case 0x340000: { // REG_P2CNT
                uint8_t val = geo_input_cb[1](1);
                return (val << 8) | val;
            }
            case 0x380000: { // REG_STATUS_B
                uint8_t val = geo_input_sys_cb[1]();
                return (val << 8) | val;
            }
            case 0x3c0000:
            case 0x3c0002:
            case 0x3c0008:
            case 0x3c000a: // REG_VRAMADDR/REG_VRAMRW
                return geo_lspc_vram_rd();
            case 0x3c0004: case 0x3c000c: // REG_VRAMMOD
                return geo_lspc_vrammod_rd();
            case 0x3c0006: case 0x3c000e: // REG_LSPCMODE
                return geo_lspc_mode_rd();
        }
    }
    else if (address < 0x800000) {
        return geo_lspc_palram_rd16(address);
    }
    else if (address < 0xc00000) {
        return bram[(address >> 1) & 0x1fff] | 0xff00;
    }
    else if (address < 0xd00000) {
        return read16(romdata->b, address & 0x7ffff);
    }
    else if (address < 0xe00000) {
        return read16(ngsys.nvram, address & 0xffff);
    }
    else if (address < 0xf00000) {
        return transfer_read_16(address);
    }
    else {
        return cd_reg_read_16(address);
    }

    return 0xffff;
}

void geo_cd_m68k_write_8(unsigned address, unsigned value) {
    address &= 0xffffff;

    if (address < 0x200000) { // Program RAM
        pram[address] = value;
    }
    else if (address < 0x300000) { // Unused
        return;
    }
    else if (address < 0x400000) { // I/O and System Registers
        switch (address) {
            case 0x300001: // REG_DIPSW (write = watchdog kick)
                geo_watchdog_reset();
                return;
            case 0x320000: // REG_SOUND (write = send command to Z80)
                ngsys.sound_code = value & 0xff;
                if (z80_enabled)
                    geo_z80_nmi();
                return;
            case 0x380051: // REG_RTCCTRL (no RTC on CD systems)
                return;

            /* System Registers (0x3A0001 - 0x3A001F)
               Data written doesn't matter. Bits are set by address, not value.
               Registers go in pairs: bit 4 of the address is the data bit.
            */
            case 0x3a0001: // REG_NOSHADOW
                geo_lspc_shadow_wr(0);
                return;
            case 0x3a0003: // REG_SWPBIOS
                vectable = 0;
                return;
            case 0x3a0005: return; // REG_CRDUNLOCK1 (no memory card on CD)
            case 0x3a0007: return; // REG_CRDLOCK2 (no memory card on CD)
            case 0x3a0009: return; // REG_CRDREGSEL (no memory card on CD)
            case 0x3a000b: return; // REG_BRDFIX (no cart S/M1 ROM on CD)
            case 0x3a000d: // REG_SRAMLOCK
                reg_sramlock = 1;
                return;
            case 0x3a000f: // REG_PALBANK1
                geo_lspc_palram_bank(1);
                return;
            case 0x3a0011: // REG_SHADOW
                geo_lspc_shadow_wr(1);
                return;
            case 0x3a0013: // REG_SWPROM
                vectable = 1;
                return;
            case 0x3a0015: return; // REG_CRDLOCK1 (no memory card on CD)
            case 0x3a0017: return; // REG_CRDUNLOCK2 (no memory card on CD)
            case 0x3a0019: return; // REG_CRDNORMAL (no memory card on CD)
            case 0x3a001b: return; // REG_CRTFIX (no cart S/M1 ROM on CD)
            case 0x3a001d: // REG_SRAMUNLOCK
                reg_sramlock = 0;
                return;
            case 0x3a001f: // REG_PALBANK0
                geo_lspc_palram_bank(0);
                return;

            /* LSPC Video Registers (0x3C0000 - 0x3C000E)
               Byte writes store the same data in both MSB and LSB.
            */
            case 0x3c0000: case 0x3c0002: case 0x3c0004: case 0x3c0006:
            case 0x3c0008: case 0x3c000a: case 0x3c000c: case 0x3c000e:
                geo_cd_m68k_write_16(address, (value << 8) | (value & 0xff));
                return;
        }
    }
    else if (address < 0x800000) {
        geo_lspc_palram_wr08(address, value);
    }
    else if (address < 0xc00000) { // Backup RAM
        bram[(address >> 1) & 0x1fff] = value;
    }
    else if (address < 0xd00000) { // BIOS ROM (read-only)
        return;
    }
    else if (address < 0xe00000) { // NVRAM
        if (!reg_sramlock)
            ngsys.nvram[address & 0xffff] = value;
    }
    else if (address < 0xf00000) { // Transfer area
        transfer_write_8(address, value);
    }
    else { // CD registers
        cd_reg_write_8(address, value);
    }
}

void geo_cd_m68k_write_16(unsigned address, unsigned value) {
    address &= 0xffffff;

    if (address < 0x200000) {
        write16(pram, address, value);
    }
    else if (address < 0x300000) {
        return;
    }
    else if (address < 0x400000) {
        switch (address) {
            case 0x300000: // REG_DIPSW (write = watchdog kick)
                geo_watchdog_reset();
                return;
            case 0x320000: // REG_SOUND (write = send command to Z80)
                ngsys.sound_code = (value >> 8) & 0xff;
                if (z80_enabled)
                    geo_z80_nmi();
                return;
            case 0x3c0000: // REG_VRAMADDR
                geo_lspc_vramaddr_wr(value);
                return;
            case 0x3c0002: // REG_VRAMRW
                geo_lspc_vram_wr(value);
                return;
            case 0x3c0004: // REG_VRAMMOD
                geo_lspc_vrammod_wr((int16_t)value);
                return;
            case 0x3c0006: // REG_LSPCMODE
                geo_lspc_mode_wr(value);
                return;
            case 0x3c0008: // REG_TIMERHIGH
                geo_irq2_displayCounterSetHigh(value);
                return;
            case 0x3c000a: // REG_TIMERLOW
                geo_irq2_displayCounterSetLow(value);
                if (ngsys.irq2_ctrl & IRQ_TIMER_RELOAD_WRITE) {
                    geo_irq2_deferCounterLoadDisplayCounter();
                }
                return;
            case 0x3c000c: { // REG_IRQACK
                if (value & 0x04)
                    m68k_set_virq(irq_vbl_level, 0);
                if (value & 0x02)
                    geo_irq2_acknowledgePending();
                if (value & 0x01)
                    m68k_set_virq(IRQ_RESET, 0);
                return;
            }
            case 0x3c000e: return; // REG_TIMERSTOP
        }
    }
    else if (address < 0x800000) {
        geo_lspc_palram_wr16(address, value);
    }
    else if (address < 0xc00000) {
        bram[(address >> 1) & 0x1fff] = value & 0xff;
    }
    else if (address < 0xd00000) {
        return;
    }
    else if (address < 0xe00000) {
        if (!reg_sramlock)
            write16(ngsys.nvram, address & 0xffff, value);
    }
    else if (address < 0xf00000) {
        transfer_write_16(address, value);
    }
    else {
        cd_reg_write_16(address, value);
    }
}

/* CD Timing
   The CD drive operates at 1x speed (75 sectors/second, ~320000 master
   cycles per sector) for audio tracks and standard CD systems, and 2x
   speed (150 sectors/second, ~160000 master cycles) for data tracks on
   CDZ and Universe BIOS systems. When idle, a slower rate (~64Hz) is
   used for communication IRQs only.

   A single timer handles both sector decoding and communication IRQs.
   Each tick of the sector timer:
     1. If playing data, runs the LC8951 sector decoder
     2. If the decoder interrupt conditions are met, fires Vector 21
     3. Fires the communication interrupt (Vector 22) unconditionally
*/

void geo_cd_tick(unsigned mcycles) {
    cd_sector_counter += mcycles;
    cd_frame_mcycs += mcycles;

    // Set timer rate based on play state
    int is_playing = (cd.drive_status == CD_STATUS_PLAY) &&
        (cd.playing_data || cd.playing_audio);

    if (is_playing) {
        unsigned speed2x = ngsys.sys == SYSTEM_CDZ || ngsys.sys == SYSTEM_CDU;
        if (cd.playing_data && speed2x)
            cd_sector_rate = CD_SECTOR_RATE_2X;
        else
            cd_sector_rate = CD_SECTOR_RATE_1X;
    }
    else {
        cd_sector_rate = CD_SECTOR_RATE_IDLE;
    }

    // Single timer handling sector decode, position advance, and communication
    // CD-ROM timer callback: handles sector decoding and IRQ processing
    while (cd_sector_counter >= cd_sector_rate) {
        cd_sector_counter -= cd_sector_rate;

        if (cd.drive_status == CD_STATUS_PLAY &&
            (cd.playing_data || cd.playing_audio)) {
            // Determine audio vs data from current LBA's track type
            unsigned cur_track = find_track_for_lba(cd.play_lba);
            int is_audio = geo_disc_track_is_audio(cur_track);

            cd.playing_audio = is_audio;
            cd.playing_data = !is_audio;
            cdda_playing = is_audio;

            // Only run the LC8951 sector decoder for data tracks
            if (cd.playing_data)
                geo_lc8951_sector_decoded(&lc, cd.play_lba);

            if (lc.decoder_enabled &&
                (irq_mask1 & 0x500) == 0x500 &&
                (lc.ifctrl & LC_IFCTRL_DECIEN) &&
                !(lc.ifstat & LC_IFSTAT_DECI)) {
                cd_irq_mask |= 0x20;
                geo_cd_irq_set(CD_INT_DECODER);
                sector_decoded_this_frame = 1;
            }

            cd.play_lba++;
        }

        // Communication IRQ (fires every tick)
        if ((irq_mask1 & 0x50) == 0x50 && cd_irq_enabled) {
            cd_irq_mask |= 0x10;
            geo_cd_irq_set(CD_INT_COMMUNICATION);
        }
    }
}

// VBL Masking
int geo_cd_vbl_enabled(void) {
    return (irq_mask2 & 0x030) == 0x030;
}

// Called by LSPC at VBL time - if mask isn't ready, latch as pending
void geo_cd_set_vbl_pending(void) {
    vbl_pending = 1;
}

/* CDDA Audio Access
   CDDA playback is demand-driven by the audio mixer, not clocked by the
   master cycle counter. The mixer calls geo_cd_read_cdda() to pull exactly
   the number of stereo samples it needs per frame. A cached 588-sample
   sector (one Red Book audio sector = 2352 bytes = 588 stereo samples)
   bridges the sector boundary.

   The BIOS can also read instantaneous CDDA levels through the bit-reversed
   registers REG_CDDALEFTL ($FF0188) and REG_CDDARIGHTL ($FF018A), which
   are handled by the NEO-MGA.
*/

// Read new audio data from CD as needed, silence when not playing
void geo_cd_read_cdda(int16_t *out, size_t numsamps) {
    for (size_t i = 0; i < numsamps; ++i) {
        if (!cdda_playing) { // Silence!
            out[i << 1] = out[(i << 1) + 1] = 0;
            continue;
        }

        // Check if a new sector should be read
        if (cdda_sector_pos >= CDDA_SAMPS_PER_SECTOR) {
            if (cdda_audio_lba < geo_disc_leadout() &&
                geo_disc_track_is_audio(find_track_for_lba(cdda_audio_lba))) {
                if (!geo_disc_read_audio(cdda_audio_lba, cdda_sector_cache))
                    memset(cdda_sector_cache, 0, sizeof(cdda_sector_cache));
            }
            else {
                memset(cdda_sector_cache, 0, sizeof(cdda_sector_cache));
            }
            cdda_audio_lba++;
            cdda_sector_pos = 0;
        }

        out[i << 1]     = cdda_sector_cache[cdda_sector_pos << 1];
        out[(i << 1) + 1] = cdda_sector_cache[(cdda_sector_pos << 1) + 1];
        ++cdda_sector_pos;
    }
}

void geo_cd_frame_end(void) {
    cd_frame_mcycs = 0;
}

// Check for a pattern at a BIOS offset (addresses relative to C00000)
static int bios_pattern(uint8_t *bios, size_t sz, uint32_t offset,
                        const uint8_t *pat, size_t patsz) {
    if (offset + patsz > sz) return 0;
    return memcmp(bios + offset, pat, patsz) == 0;
}

// Apply a patch at a BIOS offset
static void bios_patch(uint8_t *bios, size_t sz, uint32_t offset,
                       const uint8_t *pat, size_t patsz) {
    if (offset + patsz > sz) return;
    memcpy(bios + offset, pat, patsz);
}

// NOP instruction (4E71) and STOP #$2000 + NOP
static const uint8_t STOP_NOP[] = { 0x4E, 0x72, 0x20, 0x00, 0x4E, 0x71 };

int geo_cd_detect_bios(uint8_t *bios, size_t sz) {
    // Validity check: first 4 bytes must be 00 10 F3 00
    const uint8_t valid[] = { 0x00, 0x10, 0xF3, 0x00 };
    if (!bios_pattern(bios, sz, 0x0000, valid, 4))
        return CD_BIOS_UNKNOWN;

    // Family detection
    const uint8_t pat_front[] = { 0x00, 0xC0, 0xC8, 0x5E };
    const uint8_t pat_top[]   = { 0x00, 0xC0, 0xC2, 0x22 };
    const uint8_t pat_cdz[]   = { 0x00, 0xC0, 0xA3, 0xE8 };
    const uint8_t pat_uni[]   = { 0xA4, 0x4B, 0x15, 0x2F };

    if (bios_pattern(bios, sz, 0x0150, pat_uni, 4))
        return CD_BIOS_UNI;
    if (bios_pattern(bios, sz, 0x006C, pat_cdz, 4))
        return CD_BIOS_CDZ;
    if (bios_pattern(bios, sz, 0x006C, pat_front, 4))
        return CD_BIOS_FRONT;
    if (bios_pattern(bios, sz, 0x006C, pat_top, 4))
        return CD_BIOS_TOP;

    return CD_BIOS_UNKNOWN;
}

// Apply speed hack patches (replace busy-wait loops with STOP)
static void bios_patch_speed_hack(uint8_t *bios, size_t sz, int family) {
    // Each patch replaces SUBQ.L #1,D1; BEQ.W xxxx (53 81 67 00 xx xx)
    // with STOP #0; NOP; NOP (73 00 4E 71 4E 71)
    const uint8_t subq_beq[] = { 0x53, 0x81, 0x67, 0x00 };

    switch (family) {
        case CD_BIOS_CDZ: {
            const uint32_t addrs[] = {0xE6E0, 0xE724, 0xE764, 0xE836, 0xE860};
            for (int i = 0; i < 5; ++i)
                if (bios_pattern(bios, sz, addrs[i], subq_beq, 4))
                    bios_patch(bios, sz, addrs[i], STOP_NOP, 6);
            break;
        }
        case CD_BIOS_FRONT: {
            const uint32_t addrs[] = {0x10716, 0x10758, 0x10798, 0x10864};
            for (int i = 0; i < 4; ++i)
                if (bios_pattern(bios, sz, addrs[i], subq_beq, 4))
                    bios_patch(bios, sz, addrs[i], STOP_NOP, 6);
            break;
        }
        case CD_BIOS_TOP: {
            const uint32_t addrs[] = {0x0FFCA, 0x1000E, 0x1004E, 0x10120};
            for (int i = 0; i < 4; ++i)
                if (bios_pattern(bios, sz, addrs[i], subq_beq, 4))
                    bios_patch(bios, sz, addrs[i], STOP_NOP, 6);
            break;
        }
    }
}

void geo_cd_set_speed_hack(int enabled) {
    speed_hack = enabled;
}

int geo_cd_sector_decoded_this_frame(void) {
    return sector_decoded_this_frame;
}

void geo_cd_clear_sector_decoded(void) {
    sector_decoded_this_frame = 0;
}

void geo_cd_postload(void) {
    // First, byteswap the BIOS
    geo_m68k_bios_bswap();

    // Detect BIOS family and apply patches
    if (romdata->b && romdata->bsz >= SIZE_512K) {
        bios_family = geo_cd_detect_bios(romdata->b, romdata->bsz);
        geo_log(GEO_LOG_DBG,
            "CD BIOS family: %s (SP=%02x%02x%02x%02x vec6C=%02x%02x%02x%02x)\n",
            bios_family == CD_BIOS_CDZ ? "CDZ" :
            bios_family == CD_BIOS_UNI ? "CD Universe BIOS" :
            bios_family == CD_BIOS_TOP ? "Top Loader" :
            bios_family == CD_BIOS_FRONT ? "Front Loader" : "Unknown",
            romdata->b[0], romdata->b[1], romdata->b[2], romdata->b[3],
            romdata->b[0x6C], romdata->b[0x6D], romdata->b[0x6E],
            romdata->b[0x6F]);

        // Apply speed hack if enabled
        if (speed_hack)
            bios_patch_speed_hack(romdata->b, romdata->bsz, bios_family);
    }
}

void geo_cd_init(void) {
    cd_frame_mcycs = 0;
    romdata = geo_romdata_ptr();

    memset(pram, 0, SIZE_2M);
    memset(spr_dram, 0, SIZE_4M);
    memset(pcm_dram, 0, SIZE_1M);
    memset(z80_ram_cd, 0, SIZE_64K);
    memset(fix_ram, 0, SIZE_128K);
    memset(bram, 0, SIZE_8K);

    // Default the Universe BIOS to CDZ mode
    if (ngsys.sys == SYSTEM_CDU) {
        bram[0] = 0x56;
        bram[1] = 0x32;
        bram[2] = 0x02;
        switch (ngsys.region) { // Try to use the correct region
            default: case REGION_US: bram[3] = 0x10; break;
            case REGION_JP: bram[3] = 0x00; break;
            case REGION_EU: bram[3] = 0x20; break;
            case REGION_AS: bram[3] = 0x30; break; // Brazil?
        }
    }

    geo_lc8951_reset(&lc);
    cd_comm_reset();
    memset(&dma, 0, sizeof(dma));

    // Redirect ROM data pointers to CD RAM for LSPC and YM2610
    romdata->c = spr_dram;
    romdata->csz = SIZE_4M;
    romdata->s = fix_ram;
    romdata->ssz = SIZE_128K;
    romdata->v1 = pcm_dram;
    romdata->v1sz = SIZE_1M;
    romdata->v2 = pcm_dram;
    romdata->v2sz = SIZE_1M;

    // Point M1/SM to Z80 CD RAM
    romdata->m = z80_ram_cd;
    romdata->msz = SIZE_64K;

    // Recalculate LSPC masks for the new ROM sizes, assign FIX data pointer
    geo_lspc_postload();
    geo_lspc_set_fix(LSPC_FIX_CD);
}

void geo_cd_deinit(void) {
    // Nothing dynamically allocated
}

void geo_cd_reset(void) {
    vectable = 0;
    reg_transarea = 0;
    spr_bank = 0;
    pcm_bank = 0;
    reg_disblspr = 0;
    reg_disblfix = 0;
    reg_envideo = 0;
    busreq_spr = 0;
    busreq_pcm = 0;
    busreq_fix = 0;
    z80_enabled = 0;
    reg_sramlock = 0;
    cd_irq_mask = 0;
    cd_pending_irq = 0;
    cd_irq_enabled = 0;
    irq_mask1 = 0;
    irq_mask2 = 0;
    vbl_pending = 0;
    cd_sector_counter = 0;
    cdda_playing = 0;
    cdda_audio_lba = 0;
    cdda_sector_pos = CDDA_SAMPS_PER_SECTOR;
    memset(cdda_sector_cache, 0, sizeof(cdda_sector_cache));
    cd_frame_mcycs = 0;
    cached_track = 1;
    cached_track_start = 0;
    cached_track_end = 0;

    geo_lc8951_reset(&lc);
    cd_comm_reset();
    memset(&dma, 0, sizeof(dma));

    /* CDZ BIOS polls Status (0x00) expecting STOPPED before proceeding.
       Real hardware: drive spins up and transitions to STOPPED autonomously.
       Since we have a disc loaded at boot, start directly in STOPPED state.
    */
    cd.drive_status = CD_STATUS_STOP;
    cd.status[0] = cd.drive_status;
    cd_comm_build_response();
}

const void* geo_cd_bram_ptr(void) {
    return bram;
}

const void* geo_cd_pram_ptr(void) {
    return pram;
}

static void cdcomm_state_save(uint8_t *st) {
    for (size_t i = 0; i < 5; ++i) geo_serial_push8(st, cd.cmd[i]);
    for (size_t i = 0; i < 5; ++i) geo_serial_push8(st, cd.status[i]);
    geo_serial_push8(st, cd.cmd_nybble);
    geo_serial_push8(st, cd.stat_nybble);
    geo_serial_push8(st, cd.strobe);
    geo_serial_push8(st, cd.drive_status);
    geo_serial_push32(st, cd.play_lba);
    geo_serial_push32(st, cd.target_lba);
    geo_serial_push8(st, cd.playing_audio);
    geo_serial_push8(st, cd.playing_data);
}

static void cdcomm_state_load(uint8_t *st) {
    for (size_t i = 0; i < 5; ++i) cd.cmd[i] = geo_serial_pop8(st);
    for (size_t i = 0; i < 5; ++i) cd.status[i] = geo_serial_pop8(st);
    cd.cmd_nybble = geo_serial_pop8(st);
    cd.stat_nybble = geo_serial_pop8(st);
    cd.strobe = geo_serial_pop8(st);
    cd.drive_status = geo_serial_pop8(st);
    cd.play_lba = geo_serial_pop32(st);
    cd.target_lba = geo_serial_pop32(st);
    cd.playing_audio = geo_serial_pop8(st);
    cd.playing_data = geo_serial_pop8(st);
}

static void cddma_state_save(uint8_t *st) {
    geo_serial_push32(st, dma.src);
    geo_serial_push32(st, dma.dst);
    geo_serial_push32(st, dma.len);
    geo_serial_push16(st, dma.val);
    geo_serial_push16(st, dma.config);
    geo_serial_push8(st, dma.enabled);
}

static void cddma_state_load(uint8_t *st) {
    dma.src = geo_serial_pop32(st);
    dma.dst = geo_serial_pop32(st);
    dma.len = geo_serial_pop32(st);
    dma.val = geo_serial_pop16(st);
    dma.config = geo_serial_pop16(st);
    dma.enabled = geo_serial_pop8(st);
}

void geo_cd_state_save(uint8_t *st) {
    geo_serial_pushblk(st, pram, SIZE_2M);
    geo_serial_pushblk(st, spr_dram, SIZE_4M);
    geo_serial_pushblk(st, pcm_dram, SIZE_1M);
    geo_serial_pushblk(st, z80_ram_cd, SIZE_64K);
    geo_serial_pushblk(st, fix_ram, SIZE_128K);
    geo_serial_pushblk(st, bram, SIZE_8K);

    // CD controller state (field-by-field)
    geo_lc8951_state_save(&lc, st);
    cdcomm_state_save(st);
    cddma_state_save(st);

    // Registers
    geo_serial_push8(st, vectable);
    geo_serial_push8(st, reg_transarea);
    geo_serial_push8(st, spr_bank);
    geo_serial_push8(st, pcm_bank);
    geo_serial_push8(st, reg_disblspr);
    geo_serial_push8(st, reg_disblfix);
    geo_serial_push8(st, reg_envideo);
    geo_serial_push8(st, z80_enabled);
    geo_serial_push8(st, reg_sramlock);
    geo_serial_push8(st, cd_irq_mask);
    geo_serial_push8(st, cd_irq_enabled);
    geo_serial_push32(st, cd_sector_counter);

    // Bus request and IRQ mask state
    geo_serial_push8(st, busreq_spr);
    geo_serial_push8(st, busreq_pcm);
    geo_serial_push8(st, busreq_z80);
    geo_serial_push8(st, busreq_fix);
    geo_serial_push16(st, irq_mask1);
    geo_serial_push16(st, irq_mask2);
    geo_serial_push32(st, cd_sector_rate);

    // IRQ state
    geo_serial_push8(st, cd_pending_irq);
    geo_serial_push8(st, vbl_pending);
    geo_serial_push8(st, sector_decoded_this_frame);
    geo_serial_push32(st, cd_frame_mcycs);

    // CDDA state
    geo_serial_pushblk(st, (uint8_t*)cdda_sector_cache,
        sizeof(cdda_sector_cache));
    geo_serial_push32(st, (uint32_t)cdda_sector_pos);
    geo_serial_push32(st, cdda_audio_lba);
    geo_serial_push8(st, cdda_playing);
}

void geo_cd_state_load(uint8_t *st, unsigned ver) {
    geo_serial_popblk(pram, st, SIZE_2M);
    geo_serial_popblk(spr_dram, st, SIZE_4M);
    geo_serial_popblk(pcm_dram, st, SIZE_1M);
    geo_serial_popblk(z80_ram_cd, st, SIZE_64K);
    geo_serial_popblk(fix_ram, st, SIZE_128K);
    geo_serial_popblk(bram, st, SIZE_8K);

    if (ver <= 4u) {
        // Legacy: raw struct blobs (version 4 and earlier)
        geo_serial_popblk((uint8_t*)&lc, st, sizeof(lc));
        geo_serial_popblk((uint8_t*)&cd, st, sizeof(cd));
        geo_serial_popblk((uint8_t*)&dma, st, sizeof(dma));
    }
    else {
        // Version 5+: field-by-field serialization
        geo_lc8951_state_load(&lc, st);
        cdcomm_state_load(st);
        cddma_state_load(st);
    }

    vectable = geo_serial_pop8(st);
    reg_transarea = geo_serial_pop8(st);
    spr_bank = geo_serial_pop8(st);
    pcm_bank = geo_serial_pop8(st);
    reg_disblspr = geo_serial_pop8(st);
    reg_disblfix = geo_serial_pop8(st);
    reg_envideo = geo_serial_pop8(st);
    z80_enabled = geo_serial_pop8(st);
    reg_sramlock = geo_serial_pop8(st);
    cd_irq_mask = geo_serial_pop8(st);
    cd_irq_enabled = geo_serial_pop8(st);
    cd_sector_counter = geo_serial_pop32(st);

    busreq_spr = geo_serial_pop8(st);
    busreq_pcm = geo_serial_pop8(st);
    busreq_z80 = geo_serial_pop8(st);
    busreq_fix = geo_serial_pop8(st);
    irq_mask1 = geo_serial_pop16(st);
    irq_mask2 = geo_serial_pop16(st);
    cd_sector_rate = geo_serial_pop32(st);

    cd_pending_irq = geo_serial_pop8(st);
    vbl_pending = geo_serial_pop8(st);
    sector_decoded_this_frame = geo_serial_pop8(st);
    cd_frame_mcycs = geo_serial_pop32(st);

    geo_serial_popblk((uint8_t*)cdda_sector_cache, st,
        sizeof(cdda_sector_cache));
    cdda_sector_pos = (size_t)geo_serial_pop32(st);
    cdda_audio_lba = geo_serial_pop32(st);
    cdda_playing = geo_serial_pop8(st);

    // Rebuild track cache from current position
    cached_track = 1;
    cached_track_start = 0;
    cached_track_end = 0;
    if (geo_disc_num_tracks() > 0)
        find_track_for_lba(cd.play_lba);

    // Restore LSPC rendering state
    geo_lspc_disblspr_wr(reg_disblspr);
    geo_lspc_disblfix_wr(reg_disblfix);
    geo_lspc_envideo_wr(reg_envideo);
}
