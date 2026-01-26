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
#include <stdio.h>

#include "m68k/m68k.h"
#include "m68k/m68kcpu.h"

#include "geo.h"
#include "geo_lspc.h"
#include "geo_m68k.h"
#include "geo_rtc.h"
#include "geo_serial.h"
#include "geo_z80.h"
#include "geo_debug_text.h"
#include "geo_checkpoint.h"
#include "geo_debugger.h"
#include "geo_protect.h"

#define SMATAP 0x98ec // NEO-SMA Tapped bits - 2, 3, 5, 6, 7, 11, 12, and 15
#define GEO_DBG_TEXT_ADDR 0xFFFF0 // Fake debug output register
#define GEO_DBG_CHECKPOINT_ADDR 0xFFFEC // Fake checkpoint register

// Game ROM data
static romdata_t *romdata = NULL;

// Main RAM
static uint8_t ram[SIZE_64K];

// Dynamic FIX data
static uint8_t dynfix[SIZE_128K];

// Board type
static unsigned boardtype = BOARD_DEFAULT;

// Cartridge Registers (general purpose usage)
static uint16_t cartreg[2] = { 0, 0 };
static uint32_t protreg = 0x00000000;

// Switchable bank offset address
static uint32_t banksw_addr = 0;
static uint32_t banksw_mask = 0;

// Vector Table currently in use
static uint8_t vectable = VECTOR_TABLE_BIOS;

// Registers
static uint8_t reg_sramlock = 0; // SRAM Lock
static uint8_t reg_crdlock[2] = { 0, 0 }; // Memory Card Lock
static uint8_t reg_crdregsel = 0; // Register Select for Memory Card
static uint8_t reg_crtfix = 0; // Sets S ROM/M1 ROM or SFIX/SM1 ROM
static uint8_t reg_poutput = 0; // Joypad Port Outputs

// NEO-SMA Pseudo-Random Number addresses
static uint32_t sma_prn_addr[2] = { 0, 0 };
static uint16_t sma_prn = 0x2345; // Pseudo-random number initial value

// NEO-SMA bankswitching
static uint32_t sma_addr = 0x00000000;
static uint32_t *sma_offset;
static uint8_t *sma_scramble;

// Function pointers for handling fixed and switchable bank areas
static uint8_t (*geo_m68k_read_fixed_8)(uint32_t);
static uint16_t (*geo_m68k_read_fixed_16)(uint32_t);

static uint8_t (*geo_m68k_read_banksw_8)(uint32_t);
static uint16_t (*geo_m68k_read_banksw_16)(uint32_t);

static void (*geo_m68k_write_banksw_8)(uint32_t, uint8_t);
static void (*geo_m68k_write_banksw_16)(uint32_t, uint16_t);

static inline uint16_t parity(uint16_t v) {
    /* This technique is used, adapted for 16-bit values:
       https://graphics.stanford.edu/~seander/bithacks.html#ParityParallel
    */
    v ^= v >> 8;
    v ^= v >> 4;
    v &= 0xf;
    return (0x6996 >> v) & 1;
}

static inline uint16_t geo_m68k_prn_read(void) {
    sma_prn = (sma_prn << 1) | parity(sma_prn & SMATAP);
    return sma_prn;
}

// Helpers for reading 8, 16, and 32-bit values in the 68K address space
static inline uint8_t read08(uint8_t *ptr, uint32_t addr) {
    return ptr[addr];
}

static inline uint16_t read16(uint8_t *ptr, uint32_t addr) {
    return (ptr[addr] << 8) | ptr[addr + 1];
}

static inline uint16_t read16be(uint8_t *ptr, uint32_t addr) {
    return (ptr[addr + 1] << 8) | ptr[addr];
}

static inline void geo_debug_print_byte(uint8_t byte) {
    geo_debug_text_write(byte);
}

static inline void geo_debug_checkpoint_write(uint8_t index) {
    geo_checkpoint_write(index);
}

static inline void write08(uint8_t *ptr, uint32_t addr, uint8_t data) {
    ptr[addr] = data;
}

static inline void write16(uint8_t *ptr, uint32_t addr, uint16_t data) {
    ptr[addr + 1] = data & 0xff;
    ptr[addr] = data >> 8;
}

static inline void write16be(uint8_t *ptr, uint32_t addr, uint16_t data) {
    ptr[addr] = data & 0xff;
    ptr[addr + 1] = data >> 8;
}

static inline void swapb16_range(void *ptr, size_t len) {
    uint16_t *x = (uint16_t*)ptr;
    for (size_t i = 0; i < len >> 1; ++i)
        x[i] = (x[i] << 8) | (x[i] >> 8);
}

// Default Fixed Bank Routines
static uint8_t geo_m68k_read_fixed_8_default(uint32_t addr) {
    return read08(romdata->p, addr);
}

static uint16_t geo_m68k_read_fixed_16_default(uint32_t addr) {
    return read16(romdata->p, addr);
}

// The King of Fighters 2003 (bootleg 1), The King of Fighters 2004 Ultra Plus
/* This is a typical NEO-PVC board with 8-bit reads into the Fixed Program ROM
   area at 0x58197 mapped to the PVC RAM at 0x1ff2.
*/
static uint8_t geo_m68k_read_fixed_8_kf2k3bl(uint32_t addr) {
    if (addr == 0x058197)
        return ngsys.cartram[0x1ff2];
    return read08(romdata->p, addr);
}

// The King of Fighters 10th Anniversary Bootleg
/* Handling of the fixed P ROM bank is typical other than Extended RAM being
   addressed in the top 128K of the bank, and the ability to flip between
   reading from the 0th and 7th bank, which is controlled by special writes to
   the switchable bank region. There are only 121 bytes different between these
   banks, so it is likely there is some sort of protection mechanism relying on
   these differences, but the rest of the P ROM is the same.
*/
static uint8_t geo_m68k_read_fixed_8_kof10th(uint32_t addr) {
    if (addr >= 0x0e0000)
        return read08(ngsys.extram, addr & 0x1ffff);
    return read08(romdata->p, addr + protreg);
}

static uint16_t geo_m68k_read_fixed_16_kof10th(uint32_t addr) {
    if (addr >= 0x0e0000)
        return read16be(ngsys.extram, addr & 0x1fffe);
    return read16(romdata->p, addr + protreg);
}

// Default Switchable Bank Routines
static uint8_t geo_m68k_read_banksw_8_default(uint32_t addr) {
    return read08(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static uint16_t geo_m68k_read_banksw_16_default(uint32_t addr) {
    return read16(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static void geo_m68k_write_banksw_8_default(uint32_t addr, uint8_t data) {
    if (addr >= 0x2ffff0)
        banksw_addr = (((data & banksw_mask) * 0x100000) + 0x100000);
    else
        geo_log(GEO_LOG_DBG, "8-bit write at %06x: %02x\n", addr, data);
}

static void geo_m68k_write_banksw_16_default(uint32_t addr, uint16_t data) {
    if (addr >= 0x2ffff0)
        banksw_addr = (((data & banksw_mask) * 0x100000) + 0x100000);
    else
        geo_log(GEO_LOG_DBG, "16-bit write at %06x: %04x\n", addr, data);
}

// Linkable Multiplayer Boards
/* Some boards could be daisy-chained using a 3.5mm stereo jack cable to allow
   multiple systems to be linked together for special multiplayer modes.
   Information about this seems lacking but some initial research was done by
   FB Neo, which allows enough faking of the functionality to allow Riding Hero
   to run correctly in AES single cartridge mode.
   League Bowling, Riding Hero, and Thrash Rally are known to have this option.
*/
static uint8_t geo_m68k_read_banksw_8_linkable(uint32_t addr) {
    if (addr == 0x200000) {
        cartreg[0] ^= 0x08;
        return cartreg[0];
    }
    else if (addr == 0x200001) {
        return 0;
    }

    return read08(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static void geo_m68k_write_banksw_8_linkable(uint32_t addr, uint8_t data) {
    if (addr >= 0x2ffff0)
        banksw_addr = ((data * 0x100000) + 0x100000) & 0xffffff;
    else if (addr == 0x200001)
        return; // More research is required
    else
        geo_log(GEO_LOG_DBG, "8-bit write at %06x\n", addr);
}

// PRO-CT0
/* PRO-CT0 is also known as SNK-9201 and ALPHA-8921, and was used for sprite
   graphics serialization in the same manner as the NEO-ZMC2, but more
   importantly was used for a simple challenge-response anti-piracy system in
   two known games: Fatal Fury 2 and Super Sidekicks. The current
   implementation in this emulator uses hardcoded responses like older versions
   of MAME and FB Neo, but lower level emulation is possible.
*/
static uint8_t geo_m68k_read_banksw_8_ct0(uint32_t addr) {
    uint16_t ret = (protreg >> 24) & 0xff;

    switch (addr) {
        case 0x200001:
        case 0x236001:
        case 0x236009:
        case 0x255551:
        case 0x2ff001:
        case 0x2ffff1:
            return ret;
        case 0x236005:
        case 0x23600D:
            return ((ret & 0x0f) << 4) | ((ret & 0xf0) >> 4);
    }

    return read08(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static uint16_t geo_m68k_read_banksw_16_ct0(uint32_t addr) {
    uint16_t ret = (protreg >> 24) & 0xff;

    switch (addr) {
        case 0x200000:
        case 0x236000:
        case 0x236008:
        case 0x255550:
        case 0x2ff000:
        case 0x2ffff0:
            return ret;
        case 0x236004:
        case 0x23600c:
            return ((ret & 0x0f) << 4) | ((ret & 0xf0) >> 4);
    }

    return read16(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static void geo_m68k_write_banksw_8_ct0(uint32_t addr, uint8_t data) {
    switch (addr) {
        case 0x236001:
        case 0x236005:
        case 0x236009:
        case 0x23600d:
        case 0x255551:
        case 0x2ff001:
        case 0x2ffff1:
            protreg <<= 8;
            return;
    }

    if (addr >= 0x2ffff0)
        banksw_addr = ((data * 0x100000) + 0x100000) & 0xffffff;
}

static void geo_m68k_write_banksw_16_ct0(uint32_t addr, uint16_t data) {
    switch (addr) {
        case 0x211112:
            protreg = 0xff000000;
            return;
        case 0x233332:
            protreg = 0x0000ffff;
            return;
        case 0x242812:
            protreg = 0x81422418;
            return;
        case 0x244442:
            protreg = 0x00ff0000;
            return;
        case 0x255552:
            protreg = 0xff00ff00;
            return;
        case 0x256782:
            protreg = 0xf05a3601;
            return;
    }

    if (addr >= 0x2ffff0)
        banksw_addr = ((data * 0x100000) + 0x100000) & 0xffffff;
}

// NEO-SMA Switchable Bank Routines
static uint8_t geo_m68k_read_banksw_8_sma(uint32_t addr) {
    // Read SMA Chip Presence
    if (addr == 0x2fe446)
        return 0x37; // Always return this value for SMA presence
    else if (addr == 0x2fe447)
        return 0x9a; // Always return this value for SMA presence
    else if (addr == sma_prn_addr[0] || addr == sma_prn_addr[1])
        return geo_m68k_prn_read() & 0xff;

    return read08(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static uint16_t geo_m68k_read_banksw_16_sma(uint32_t addr) {
    // Read SMA Chip Presence at 0x2fe447 - mask bit 0 for 16-bit reads
    if (addr == 0x2fe446)
        return 0x9a37; // Always return this value for SMA presence
    else if (addr == sma_prn_addr[0] || addr == sma_prn_addr[1])
        return geo_m68k_prn_read();

    return read16(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static void geo_m68k_write_banksw_16_sma(uint32_t addr, uint16_t data) {
    /* If the NEO-SMA bankswitch address is written to, the data must be
       unscrambled. A 6-bit bank value is derived by shifting the original
       value by 6 Lookup Table (LUT) entries, and then ORing the least
       significant bits together in their corresponding position. For example,
       the 4th value may be derived by shifting the data being written by the
       4th LUT entry, and the least significant bit will be in the 4th position
       of the final unscrambled value.
    */
    if (addr == sma_addr) {
        uint8_t unscrambled = 0;

        for (size_t i = 0; i < 6; ++i)
            unscrambled |= (((data >> sma_scramble[i]) & 0x01) << i);

        /* A 64 entry LUT is used to determine the offset into P ROM. The
           6-bit unscrambled value derived above serves as the index into the
           Bankswitch LUT, which contains the memory offset value.
        */
        banksw_addr = (0x100000 + sma_offset[unscrambled]) & 0xffffff;
    }
}

// Set the SMA LUTs and pseudo-random number generator read addresses
void geo_m68k_sma_init(uint32_t *a, uint32_t *b, uint8_t *s) {
    sma_prn_addr[0] = a[0];
    sma_prn_addr[1] = a[1];
    sma_addr = a[2];
    sma_offset = b;
    sma_scramble = s;
}

// NEO-PVC Switchable Bank Routines
/* PVC Palette Protection Packing/Unpacking
   Colour data may be packed or unpacked. Packed values are 16-bit while
   unpacked values are 32-bit. No data is lost packing or unpacking, as a byte
   is used to store R, G, B, (5 bits) and D (1 bit) each. Packing simply merges
   the data into a 16-bit value with no space wasted.

   Packed Format:
   DrgbRRRR GGGGBBBB

   Unpacked Format:
   0000000D 000RRRRr 000GGGGg 000BBBBb
*/

// Unpack a value from 0x2fffe0 to 0x2fffe2 and 0x2fffe4
static inline void geo_m68k_pvc_unpack(void) {
    uint8_t d = ngsys.cartram[0x1fe1] >> 7; // 0000 000D
    uint8_t r = // 000R RRRr
        ((ngsys.cartram[0x1fe1] & 0x40) >> 6) |
        ((ngsys.cartram[0x1fe1] & 0x0f) << 1);
    uint8_t g = // 000G GGGg
        ((ngsys.cartram[0x1fe1] & 0x20) >> 5) |
        ((ngsys.cartram[0x1fe0] & 0xf0) >> 3);
    uint8_t b = // 000B BBBb
        ((ngsys.cartram[0x1fe1] & 0x10) >> 4) |
        ((ngsys.cartram[0x1fe0] & 0x0f) << 1);

    ngsys.cartram[0x1fe5] = d;
    ngsys.cartram[0x1fe4] = r;
    ngsys.cartram[0x1fe3] = g;
    ngsys.cartram[0x1fe2] = b;
}

// Pack a value from 0x2fffe8 and 0x2fffea to 0x2fffec
static inline void geo_m68k_pvc_pack(void) {
    uint8_t d = ngsys.cartram[0x1feb] & 0x01;
    uint8_t r = ngsys.cartram[0x1fea] & 0x1f;
    uint8_t g = ngsys.cartram[0x1fe9] & 0x1f;
    uint8_t b = ngsys.cartram[0x1fe8] & 0x1f;

    ngsys.cartram[0x1fec] = (b >> 1) | ((g & 0x1e) << 3); // GGGG BBBB
    ngsys.cartram[0x1fed] = (r >> 1) | ((b & 0x01) << 4) | ((g & 0x01) << 5) |
        ((r & 0x01) << 6) | (d << 7); // Drgb RRRR
}

// Swap Banks (PVC)
static inline void geo_m68k_pvc_bankswap(void) {
    /* The offset into ROM for the Switchable Bank Area is a 24-bit value:
       0x2ffff2 is used for the upper 16 bits, and the most significant byte of
       0x2ffff0 is used for the lower 8 bits. This is again offset by 0x100000.
       The magic numbers applied after calculating the bank offset come from
       both FB Neo and MAME - further information is seemingly unavailable.
    */
    uint32_t bankaddress = (ngsys.cartram[0x1ff3] << 16) |
        (ngsys.cartram[0x1ff2] << 8) | ngsys.cartram[0x1ff1];

    ngsys.cartram[0x1ff0] = 0xa0;
    ngsys.cartram[0x1ff1] &= 0xfe;
    ngsys.cartram[0x1ff3] &= 0x7f;

    banksw_addr = (bankaddress + 0x100000) & 0xffffff;
}

static uint8_t geo_m68k_read_banksw_8_pvc(uint32_t addr) {
    if (addr >= 0x2fe000)
        return ngsys.cartram[(addr & 0x1fff) ^ 1];

    return read08(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static uint16_t geo_m68k_read_banksw_16_pvc(uint32_t addr) {
    if (addr >= 0x2fe000)
        return read16be(ngsys.cartram, addr & 0x1fff);

    return read16(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static void geo_m68k_write_banksw_8_pvc(uint32_t addr, uint8_t data) {
    if (addr >= 0x2fe000)
        write08(ngsys.cartram, (addr & 0x1fff) ^ 1, data & 0xff);

    if (addr >= 0x2fffe0 && addr <= 0x2fffe3) // Unpack
        geo_m68k_pvc_unpack();
    else if (addr >= 0x2fffe8 && addr <= 0x2fffeb) // Pack
        geo_m68k_pvc_pack();
    else if (addr >= 0x2ffff0 && addr <= 0x2ffff3) // Bankswap
        geo_m68k_pvc_bankswap();
}

static void geo_m68k_write_banksw_16_pvc(uint32_t addr, uint16_t data) {
    if (addr >= 0x2fe000)
        write16be(ngsys.cartram, addr & 0x1fff, data & 0xffff);

    if (addr >= 0x2fffe0 && addr <= 0x2fffe3) // Unpack
        geo_m68k_pvc_unpack();
    else if (addr >= 0x2fffe8 && addr <= 0x2fffeb) // Pack
        geo_m68k_pvc_pack();
    else if (addr >= 0x2ffff0 && addr <= 0x2ffff3) // Bankswap
        geo_m68k_pvc_bankswap();
}

// The King of Fighters '98
/* Encrypted sets use an overlay system for copy protection. Writes to
   0x20aaaa containing the 16-bit value 0x0090 will activate the overlay, which
   affects fixed PROM addresses 0x100-0x103. When 0x00f0 is written to the same
   address, the original ROM data is used -- the first four characters of the
   "NEO-GEO" header, stored in 16-bit big endian format originally. Since ROM
   data in this emulator is byteswapped to 16-bit little endian at boot, the
   overlay values are similarly used in their 16-bit little endian form here.
*/
static void geo_m68k_write_banksw_16_kof98(uint32_t addr, uint16_t data) {
    if (addr == 0x20aaaa) {
        cartreg[0] = data;

        if (cartreg[0] == 0x0090) { // Apply the protection overlay
            romdata->p[0x100] = 0x00;
            romdata->p[0x101] = 0xc2;
            romdata->p[0x102] = 0x00;
            romdata->p[0x103] = 0xfd;
        }
        else if (cartreg[0] == 0x00f0) { // Use the original ROM data (NEO-)
            romdata->p[0x100] = 0x4e;
            romdata->p[0x101] = 0x45;
            romdata->p[0x102] = 0x4f;
            romdata->p[0x103] = 0x2d;
        }
    }
    else if (addr == 0x205554) { // Unknown protection or debug related write?
        return; // Always writes 0x0055
    }
    else if (addr >= 0x2ffff0) {
        banksw_addr = ((data * 0x100000) + 0x100000) & 0xffffff;
    }
    else {
        geo_log(GEO_LOG_DBG, "16-bit write at %06x: %04x\n", addr, data);
    }
}

// The King of Fighters 2003 (bootleg 2) and The King of Fighters 2004 Plus
/* This is merely a variation on the regular NEO-PVC bank swapping. All logic
   beyond 16-bit writes which result in a bankswap are equivalent to NEO-PVC.
*/
static inline void geo_m68k_kf2k3bla_bankswap(void) {
    uint32_t bankaddress = (ngsys.cartram[0x1ff3] << 16) |
        (ngsys.cartram[0x1ff2] << 8) | ngsys.cartram[0x1ff0];

    ngsys.cartram[0x1ff0] &= 0xfe;
    ngsys.cartram[0x1ff3] &= 0x7f;

    banksw_addr = (bankaddress + 0x100000) & 0xffffff;
}

static void geo_m68k_write_banksw_16_kf2k3bla(uint32_t addr, uint16_t data) {
    if (addr >= 0x2fe000)
        write16be(ngsys.cartram, addr & 0x1fff, data & 0xffff);

    if (addr >= 0x2fffe0 && addr <= 0x2fffe3) // Unpack
        geo_m68k_pvc_unpack();
    else if (addr >= 0x2fffe8 && addr <= 0x2fffeb) // Pack
        geo_m68k_pvc_pack();
    else if (addr >= 0x2ffff0 && addr <= 0x2ffff3) // Bankswap
        geo_m68k_kf2k3bla_bankswap();
}

// Metal Slug X - Challenge/Response Protection
/* There is no real documentation available on this beyond the code, but the
   same code is used in any emulators supporting this board type without dirty
   soft patching. It's magic!
*/
// cartreg[0] is the "command" register, cartreg[1] is the "counter" register
static uint16_t geo_m68k_read_banksw_16_mslugx(uint32_t addr) {
    if (addr >= 0x2fffe0 && addr <= 0x2fffef) {
        switch (cartreg[0]) {
            case 0x0001: {
                uint16_t ret =
                    (read08(romdata->p, 0xdedd2 + ((cartreg[1] >> 3) & 0xfff))
                    >> (~cartreg[1] & 0x07)) & 0x0001;
                ++cartreg[1];
                return ret;
            }
            case 0x0fff: {
                int32_t select = read16(ram, 0xf00a) - 1;
                return (read08(romdata->p, 0xdedd2 + ((select >> 3) & 0x0fff))
                    >> (~select & 0x07)) & 0x0001;
            }
            default: {
                geo_log(GEO_LOG_DBG, "mslugx read: %06x\n", addr);
                break;
            }
        }
    }

    return read16(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static void geo_m68k_write_banksw_16_mslugx(uint32_t addr, uint16_t data) {
    if (addr >= 0x2fffe0 && addr <= 0x2fffef) {
        switch (addr) {
            case 0x2fffe0: {
                cartreg[0] = 0;
                break;
            }
            case 0x2fffe2: case 0x2fffe4: {
                cartreg[0] |= data;
                break;
            }
            case 0x2fffe6: {
                break;
            }
            case 0x2fffea: {
                cartreg[0] = 0;
                cartreg[1] = 0;
                break;
            }
            default: {
                geo_log(GEO_LOG_DBG, "mslugx write: %06x, %04x\n", addr, data);
                break;
            }
        }
    }
    else if (addr >= 0x2ffff0) {
        banksw_addr = ((data * 0x100000) + 0x100000) & 0xffffff;
    }
    else {
        geo_log(GEO_LOG_DBG, "16-bit write at %06x\n", addr);
    }
}

// Metal Slug 5 Plus (bootleg)
/* Shift the data left 16 when 16-bit writes to 0x2ffff4 occur to get the new
   switchable bank address offset. Simple. Everything else about the board is
   the typical behaviour.
*/
static void geo_m68k_write_banksw_16_ms5plus(uint32_t addr, uint16_t data) {
    if (addr == 0x2ffff4)
        banksw_addr = data << 16;
}

// Crouching Tiger Hidden Dragon 2003 (Original and Super Plus)
/* This board uses a simple LUT to determine the bank to switch to. There are
   8 entries, selectable by the bottom 3 bits of the data written.
*/
static void geo_m68k_write_banksw_16_cthd2003(uint32_t addr, uint16_t data) {
    if (addr == 0x2ffff0) {
        unsigned boffsets[8] = {
            0x200000, 0x100000, 0x200000, 0x100000,
            0x200000, 0x100000, 0x400000, 0x300000
        };
        banksw_addr = boffsets[data & 0x07];
    }
}

// BrezzaSoft Gambling Boards
/* BrezzaSoft Gambling Boards contains 8K of battery backed RAM mapped to
   0x200000-0x201fff. Reads outside of this space but still within the
   Switchable Bank area should return all 1s, while writes to this space should
   be ignored. If this is not done correctly, the game will boot directly to
   the betting screen and not show the title screen or demo. The game will also
   boot directly to the betting screen if there are any unused credits.
*/
static uint8_t geo_m68k_read_banksw_8_brezza(uint32_t addr) {
    if (addr <= 0x201fff)
        return read08(ngsys.cartram, addr & 0x1fff);
    return 0xff;
}

static uint16_t geo_m68k_read_banksw_16_brezza(uint32_t addr) {
    if (addr <= 0x201fff)
        return read16(ngsys.cartram, addr & 0x1fff);

    // These are for V-Liner but do not seem to break Jockey Grand Prix
    if (addr == 0x280000)
        return geo_input_sys_cb[4]();
    else if (addr == 0x2c0000)
        return 0xffc0;

    return 0xffff;
}

static void geo_m68k_write_banksw_8_brezza(uint32_t addr, uint8_t data) {
    if (addr <= 0x201fff)
        write08(ngsys.cartram, addr & 0x1fff, data);
}

static void geo_m68k_write_banksw_16_brezza(uint32_t addr, uint16_t data) {
    if (addr <= 0x201fff)
        write16(ngsys.cartram, addr & 0x1fff, data);
}

// The King of Fighters 10th Anniversary Bootleg
/* This board is tricky in a number of ways. There are two blocks of Cartridge
   RAM, one 8K and the other 128K, in addition to using a dynamic FIX layer.

   The 8K RAM is addressed at the top of the Switchable bank region.

   The 128K RAM is addressed at the top of the Fixed bank region for reads. For
   writes, it is addressed at the bottom 256K of the Switchable bank region,
   with the writes being mirrored above 128K.

   The dynamic FIX layer data is also addressed in the bottom 256K, but only
   when the 8K RAM at 0x1ffc has a non-zero value. In this case, the write
   address is halved and masked, then the top byte is ignored and the bottom
   byte is written with the 0th and 5th bits swapped.

   There are two special write addresses in the Switchable bank region:
     0x2ffff8: Fixed P ROM bank switch - 0th or 7th bank
     0x2ffff0: Switchable P ROM bank switch - 1st to 6th bank (wrap the 7th and
               8th banks to the 1st bank)
*/
static uint8_t geo_m68k_read_banksw_8_kof10th(uint32_t addr) {
    if (addr >= 0x2fe000)
        return read08(ngsys.cartram, addr & 0x1fff);
    return read08(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static uint16_t geo_m68k_read_banksw_16_kof10th(uint32_t addr) {
    if (addr >= 0x2fe000)
        return read16be(ngsys.cartram, addr & 0x1fff);
    return read16(romdata->p, (addr & 0xfffff) + banksw_addr);
}

static void geo_m68k_write_banksw_8_kof10th(uint32_t addr, uint8_t data) {
    if (addr >= 0x2fe000) {
        if (addr == 0x2ffff0) {
            banksw_addr = (((data & banksw_mask) * 0x100000) + 0x100000);
            if (banksw_addr >= 0x700000)
                banksw_addr = 0x100000;
        }

        write08(ngsys.cartram, addr & 0x1fff, data);
    }
}

static void geo_m68k_write_banksw_16_kof10th(uint32_t addr, uint16_t data) {
    if (addr < 0x240000) {
        if (ngsys.cartram[0x1ffc]) {
            // Ignore the top byte and swap the 0th and 5th bits
            dynfix[(addr >> 1) & 0x1ffff] =
                (data & 0xde) | ((data & 0x01) << 5) | ((data & 0x20) >> 5);
        }
        else {
            write16be(ngsys.extram, addr & 0x1ffff, data);
        }
    }
    else if (addr >= 0x2fe000) {
        switch (addr) {
            case 0x2ffff0: {
                banksw_addr = (((data & banksw_mask) * 0x100000) + 0x100000);
                /* If the 7th or 8th bank is selected, wrap back to the 1st
                   bank. The 7th is used only for the fixed region, and there
                   is no 8th bank.
                */
                if (banksw_addr >= 0x700000)
                    banksw_addr = 0x100000;
                break;
            }
            case 0x2ffff8: {
                if (read16be(ngsys.cartram, 0x1ff8) != data)
                    protreg = (data & 0x01) ? 0x000000 : 0x700000;
                break;
            }
        }

        write16be(ngsys.cartram, addr & 0x1ffe, data);
    }
}

/* 68K Memory Map
 * =====================================================================
 * |    Address Range    | Size |             Description              |
 * =====================================================================
 * | 0x000000 - 0x0fffff |   1M | Fixed Bank of 68k program ROM        |
 * ---------------------------------------------------------------------
 * | 0x100000 - 0x10f2ff |      | User RAM                             |
 * |---------------------|  64K |---------------------------------------
 * | 0x10f300 - 0x10ffff |      | System ROM-reserved RAM              |
 * ---------------------------------------------------------------------
 * | 0x110000 - 0x1fffff |  64K | User/System RAM mirror               |
 * ---------------------------------------------------------------------
 * | 0x200000 - 0x2fffff |   1M | Switchable Bank of 68K program ROM   |
 * ---------------------------------------------------------------------
 * | 0x300000 - 0x3fffff |      | Memory Mapped Registers              |
 * ---------------------------------------------------------------------
 * | 0x400000 - 0x401fff |   8K | Banked Palette RAM                   |
 * ---------------------------------------------------------------------
 * | 0x402000 - 0x7fffff |      | Palette RAM Mirror                   |
 * ---------------------------------------------------------------------
 * | 0x800000 - 0xbfffff |   4M | Memory Card                          |
 * ---------------------------------------------------------------------
 * | 0xc00000 - 0xc1ffff | 128K | BIOS ROM                             |
 * ---------------------------------------------------------------------
 * | 0xc20000 - 0xcfffff |      | BIOS ROM Mirror                      |
 * ---------------------------------------------------------------------
 * | 0xd00000 - 0xd0ffff |  64K | Backup RAM (MVS Only)                |
 * ---------------------------------------------------------------------
 * | 0xd10000 - 0xdfffff |      | Backup RAM Mirror                    |
 * ---------------------------------------------------------------------
 */

unsigned m68k_read_memory_8(unsigned address) {
    unsigned result = 0xff;
    uint32_t addr24 = (uint32_t)(address & 0x00ffffffu);
    if (address < 0x000080) { // Vector Table
        m68k_modify_timeslice(1);
        result = vectable ?
            geo_m68k_read_fixed_8(address) : read08(romdata->b, address);
        goto out;
    }
    else if (address < 0x100000) { // Fixed 1M Program ROM Bank
        m68k_modify_timeslice(1);
        result = geo_m68k_read_fixed_8(address);
        goto out;
    }
    else if (address < 0x200000) { // RAM - Mirrored every 64K
        result = read08(ram, address & 0xffff);
        goto out;
    }
    else if (address < 0x300000) { // Switchable 1M Program ROM Bank
        m68k_modify_timeslice(1);
        result = geo_m68k_read_banksw_8(address);
        goto out;
    }
    else if (address < 0x400000) { // Memory Mapped Registers
        switch (address) {
            case 0x300000: { // REG_P1CNT
                result = geo_input_cb[0](0);
                goto out;
            }
            case 0x300001: { // REG_DIPSW
                /* Hardware DIP Switches (Active Low)
                   Bit 7:    Stop Mode
                   Bit 6:    Free Play
                   Bit 5:    Enable Multiplayer
                   Bits 3-4: Comm. ID Code
                   Bit 2:    0 = Normal Controller, 1 = Mahjong Keyboard
                   Bit 1:    0 = 1 Chute, 1 = 2 Chutes
                   Bit 0:    Settings Mode
                */
                result = geo_input_sys_cb[3]();
                goto out;
            }
            case 0x300081: { // REG_SYSTYPE
                /* Bit 7:    Test button - Activates system menu
                   Bit 6:    0 = 2 slots, 1 = 4 or 6 slots
                   Bits 0-5: Unknown
                */
                // Only 1 and 2 slot System ROMs are supported, therefore ~0x40
                result = geo_input_sys_cb[2]() & ~0x40; // Active Low
                goto out;
            }
            case 0x320000: { // REG_SOUND
                result = ngsys.sound_reply; // Z80 Reply Code
                goto out;
            }
            case 0x320001: { // REG_STATUS_A
                /* Bit 7:    RTC Data Bit
                   Bit 6:    RTC Time Pulse
                   Bit 5:    0 = 4 Slot, 1 = 6 Slot
                   Bit 4:    Coin-in 4
                   Bit 3:    Coin-in 3
                   Bit 2:    Service Button
                   Bit 1:    Coin-in 2
                   Bit 0:    Coin-in 1

                   Note: Universe BIOS on AES hardware requires bits 3 and 4
                         to be unset.
                */
                result = geo_input_sys_cb[0]() | (geo_rtc_rd() << 6);
                goto out;
            }
            case 0x340000: { // REG_P2CNT
                result = geo_input_cb[1](1);
                goto out;
            }
            case 0x380000: { // REG_STATUS_B
                /* Aux Inputs (Lower bits Active Low)
                   Bit 7:    0 = AES, 1 = MVS
                   Bit 6:    Memory card write protect
                   Bit 4-5:  Memory card inserted if 00
                   Bit 3:    Player 2 Select
                   Bit 2:    Player 2 Start
                   Bit 1:    Player 1 Select
                   Bit 0:    Player 1 Start
                */
                result = geo_input_sys_cb[1]() |
                    (geo_get_system() == SYSTEM_AES ? 0x00 : 0x80);
                goto out;
            }
            case 0x3c0000: case 0x3c0002: case 0x3c0008: case 0x3c000a: {
                // REG_VRAMADDR, REG_VRAMRW, REG_TIMERHIGH, REG_TIMERLOW
                break;
            }
            case 0x3c0004: case 0x3c000c: {
                // REG_VRAMMOD, REG_IRQACK
                break;
            }
            case 0x3c0006: case 0x3c000e: {
                /* REG_LSPCMODE, REG_TIMERSTOP
                   Bits 7-15: Raster line counter (with offset of 0xf8)
                   Bits 4-6:  000
                   Bit 3:     0 = 60Hz, 1 = 50Hz
                   Bits 0-2:  Auto animation counter
                */
                break;
            }
        }
    }
    else if (address < 0x800000) { // Palette RAM - Mirrored every 8K
        result = geo_lspc_palram_rd08(address);
        goto out;
    }
    else if (address < 0xc00000) { // Memory Card
        /* 8-bit Memory Card reads return 0xff for even addresses, and memcard
           data for odd addresses. This is effectively half of a 16-bit read.
        */
        m68k_modify_timeslice(2);
        geo_log(GEO_LOG_DBG, "8-bit Memory Card Read: %06x\n", address);
        if (address & 0x01) {
            result = ngsys.memcard[(address >> 1) & 0x7ff];
        } else {
            result = 0xff;
        }
        goto out;
    }
    else if (address < 0xd00000) { // BIOS ROM
        result = read08(romdata->b, address & 0x1ffff);
        goto out;
    }
    else if (address < 0xe00000) { // Backup RAM - Mirrored every 64K
        result = read08(ngsys.nvram, address & 0xffff);
        goto out;
    }

    geo_log(GEO_LOG_DBG, "Unknown 8-bit 68K Read at %06x\n", address);
    result = 0xff;
out:
    geo_debugger_watchpoint_read(addr24, (uint32_t)result, 8);
    return result;
}

unsigned m68k_read_memory_16(unsigned address) {
    unsigned result = 0xffff;
    uint32_t addr24 = (uint32_t)(address & 0x00ffffffu);
    if (address & 0x01)
        geo_log(GEO_LOG_WRN, "Unaligned 16-bit Read: %06x\n", address);

    if (address < 0x000080) { // Vector Table
        m68k_modify_timeslice(1);
        result = vectable ?
            geo_m68k_read_fixed_16(address) : read16(romdata->b, address);
        goto out;
    }
    else if (address < 0x100000) { // Fixed 1M Program ROM Bank
        m68k_modify_timeslice(1);
        result = geo_m68k_read_fixed_16(address);
        goto out;
    }
    else if (address < 0x200000) { // RAM - Mirrored every 64K
        result = read16(ram, address & 0xffff);
        goto out;
    }
    else if (address < 0x300000) { // Switchable 1M Program ROM Bank
        m68k_modify_timeslice(1);
        result = geo_m68k_read_banksw_16(address);
        goto out;
    }
    else if (address < 0x400000) { // Memory Mapped Registers
        switch (address) {
            case 0x300000: { // REG_P1CNT
                uint8_t val = geo_input_cb[0](0);
                result = (val << 8) | val;
                goto out;
            }
            case 0x340000: { // REG_P2CNT
                uint8_t val = geo_input_cb[1](1);
                result = (val << 8) | val;
                goto out;
            }
            case 0x380000: { // REG_STATUS_B
                uint8_t val = geo_input_sys_cb[1]() |
                    (geo_get_system() == SYSTEM_AES ? 0x00 : 0x80);
                result = (val << 8) | val;
                goto out;
            }
            case 0x3c0000: case 0x3c0002: case 0x3c0008: case 0x3c000a: {
                // REG_VRAMADDR, REG_VRAMRW, REG_TIMERHIGH, REG_TIMERLOW
                result = geo_lspc_vram_rd();
                goto out;
            }
            case 0x3c0004: case 0x3c000c: {
                // REG_VRAMMOD, REG_IRQACK
                result = geo_lspc_vrammod_rd();
                goto out;
            }
            case 0x3c0006: case 0x3c000e: {
                // REG_LSPCMODE, REG_TIMERSTOP
                result = geo_lspc_mode_rd();
                goto out;
            }
        }
    }
    else if (address < 0x800000) { // Palette RAM - Mirrored every 8K
        result = geo_lspc_palram_rd16(address);
        goto out;
    }
    else if (address < 0xc00000) { // Memory Card
        /* Emulate an 8-bit, 2K memory card - since only the low byte is used,
           the address must be divided by two to get the correct byte. The
           upper byte is always 0xff.
        */
        m68k_modify_timeslice(2);
        result = ngsys.memcard[(address >> 1) & 0x7ff] | 0xff00;
        goto out;
    }
    else if (address < 0xd00000) { // BIOS ROM
        result = read16(romdata->b, address & 0x1ffff);
        goto out;
    }
    else if (address < 0xe00000) { // Backup RAM - Mirrored every 64K
        result = read16(ngsys.nvram, address & 0xffff);
        goto out;
    }

    geo_log(GEO_LOG_DBG, "Unknown 16-bit 68K Read at %06x\n", address);
    result = 0xffff;
out:
    geo_debugger_watchpoint_read(addr24, (uint32_t)result, 16);
    return result;
}

unsigned m68k_read_memory_32(unsigned address) {
    uint32_t addr24 = (uint32_t)(address & 0x00ffffffu);
    geo_debugger_watchpoint_suspend();
    unsigned hi = m68k_read_memory_16(address);
    unsigned lo = m68k_read_memory_16(address + 2);
    geo_debugger_watchpoint_resume();
    unsigned result = (hi << 16) | (lo & 0xffffu);
    geo_debugger_watchpoint_read(addr24, (uint32_t)result, 32);
    return result;
}

void m68k_write_memory_8(unsigned address, unsigned value) {
    uint32_t addr24 = (uint32_t)(address & 0x00ffffffu);
    uint32_t old_value = 0;
    int old_value_valid = 0;
    unsigned v8 = value & 0xffu;
    address = addr24;

    if (address >= 0x100000 && address < 0x200000) { // RAM (mirrored)
        old_value = read08(ram, address & 0xffff);
        old_value_valid = 1;
    } else if (address >= 0x400000 && address < 0x800000) { // Palette RAM (mirrored)
        old_value = geo_lspc_palram_rd08(address);
        old_value_valid = 1;
    } else if (address >= 0x800000 && address < 0xc00000) { // Memory Card
        old_value = ngsys.memcard[(address >> 1) & 0x7ff];
        old_value_valid = 1;
    } else if (address >= 0xd00000 && address < 0xe00000) { // Backup RAM (mirrored)
        old_value = read08(ngsys.nvram, address & 0xffff);
        old_value_valid = 1;
    }

    uint32_t protectValue = (uint32_t)v8;
    geo_protect_filterWrite(addr24, 8, old_value, old_value_valid, &protectValue);
    v8 = protectValue & 0xffu;

    if (address < 0x100000) { // Fixed 1M Program ROM Bank
        geo_log(GEO_LOG_DBG, "68K write to Program ROM: %06x %02x\n",
            address, v8);
        if (address == GEO_DBG_TEXT_ADDR) {
            geo_debug_print_byte((uint8_t)v8);
            goto out;
        }
        if (address == GEO_DBG_CHECKPOINT_ADDR) {
            geo_debug_checkpoint_write((uint8_t)v8);
            goto out;
        }
    }
    else if (address < 0x200000) { // RAM - Mirrored every 64K
        m68k_modify_timeslice(1);
        write08(ram, address & 0xffff, v8);
    }
    else if (address < 0x300000) { // Switchable 1M Program ROM Bank
        m68k_modify_timeslice(1);
        geo_m68k_write_banksw_8(address, v8);
    }
    else if (address < 0x400000) { // Memory Mapped Registers
        switch (address) {
            // Writes to 0x300001 reset watchdog timer via DOGE pin
            case 0x300001: {
                geo_watchdog_reset();
                goto out;
            }
            case 0x320000: { // REG_SOUND
                ngsys.sound_code = v8;
                geo_z80_nmi();
                goto out;
            }
            case 0x380001: { // REG_POUTPUT
                reg_poutput = v8;
                goto out;
            }
            case 0x380011: { // REG_CRDBANK
                // TODO - memory card bank selection, lowest 3 bits
                goto out;
            }
            case 0x380021: { // REG_SLOT
                // Switch slot for MVS, mirror of REG_POUTPUT for AES
                if (geo_get_system() == SYSTEM_AES)
                    reg_poutput = v8;
                goto out;
            }
            case 0x380031: { // REG_LEDLATCHES
                goto out;
            }
            case 0x380041: { // REG_LEDDATA
                goto out;
            }
            case 0x380051: { // REG_RTCCTRL
                if (geo_get_system())
                    geo_rtc_wr(v8 & 0x07);
                goto out;
            }
            case 0x380061: // REG_RESETCC1, REG_RESETCC1 - Reset Coin Counters
            case 0x380063: {
                goto out;
            }
            case 0x380065: // REG_RESETCL1, REG_RESETCL2 - Reset Coin Lockouts
            case 0x380067: {
                goto out;
            }
            case 0x3800e1: // REG_SETCC1, REG_SETCC1 - Set Coin Counters
            case 0x3800e3: {
                goto out;
            }
            case 0x3a0001: { // REG_NOSHADOW
                geo_log(GEO_LOG_DBG, "REG_NOSHADOW write: %02x\n", v8);
                geo_lspc_shadow_wr(0);
                goto out;
            }
            case 0x3a0003: { // REG_SWPBIOS
                vectable = VECTOR_TABLE_BIOS;
                geo_log(GEO_LOG_DBG, "Selected BIOS vector table\n");
                goto out;
            }
            case 0x3a0005: { // REG_CRDUNLOCK1
                geo_log(GEO_LOG_DBG, "REG_CRDUNLOCK1 write: %02x\n", v8);
                reg_crdlock[0] = 0;
                goto out;
            }
            case 0x3a0007: { // REG_CRDLOCK2
                geo_log(GEO_LOG_DBG, "REG_CRDLOCK2 write: %02x\n", v8);
                reg_crdlock[1] = 1;
                goto out;
            }
            case 0x3a0009: { // REG_CRDREGSEL
                geo_log(GEO_LOG_DBG, "REG_CRDREGSEL write: %02x\n", v8);
                reg_crdregsel = 1;
                goto out;
            }
            case 0x3a000b: { // REG_BRDFIX
                reg_crtfix = 0;
                geo_z80_set_mrom(0);
                geo_lspc_set_fix(LSPC_FIX_BOARD);
                goto out;
            }
            case 0x3a000d: { // REG_SRAMLOCK
                reg_sramlock = 1;
                goto out;
            }
            case 0x3a000f: { // REG_PALBANK1
                geo_lspc_palram_bank(1);
                goto out;
            }
            case 0x3a0011: { // REG_SHADOW
                geo_log(GEO_LOG_DBG, "REG_SHADOW write: %02x\n", v8);
                geo_lspc_shadow_wr(1);
                goto out;
            }
            case 0x3a0013: { // REG_SWPROM
                vectable = VECTOR_TABLE_CART;
                geo_log(GEO_LOG_DBG, "Selected Cartridge vector table\n");
                goto out;
            }
            case 0x3a0015: { // REG_CRDLOCK1
                geo_log(GEO_LOG_DBG, "REG_CRDLOCK1 write: %02x\n", v8);
                reg_crdlock[0] = 1;
                goto out;
            }
            case 0x3a0017: { // REG_CRDUNLOCK2
                geo_log(GEO_LOG_DBG, "REG_CRDUNLOCK2 write: %02x\n", v8);
                reg_crdlock[1] = 0;
                goto out;
            }
            case 0x3a0019: { // REG_CRDNORMAL
                geo_log(GEO_LOG_DBG, "REG_CRDNORMAL write: %02x\n", v8);
                reg_crdregsel = 0;
                goto out;
            }
            case 0x3a001b: { // REG_CRTFIX
                reg_crtfix = 1;
                geo_z80_set_mrom(1);
                geo_lspc_set_fix(LSPC_FIX_CART);
                goto out;
            }
            case 0x3a001d: { // REG_SRAMUNLOCK
                reg_sramlock = 0;
                goto out;
            }
           case 0x3a001f: { // REG_PALBANK0
                geo_lspc_palram_bank(0);
                goto out;
            }
            case 0x3c0000: case 0x3c0002: case 0x3c0004: case 0x3c0006:
            case 0x3c0008: case 0x3c000a: case 0x3c000c: case 0x3c000e: {
                /* Byte writes are only effective on even addresses, and they
                   store the same data in both bytes.
                */
                geo_debugger_watchpoint_suspend();
                m68k_write_memory_16(address, (v8 << 8) | v8);
                geo_debugger_watchpoint_resume();
                goto out;
            }

        }
        geo_log(GEO_LOG_DBG, "Unknown 8-bit Write: %06x, %02x\n",
            address, v8);
    }
    else if (address < 0x800000) { // Palette RAM - Mirrored every 8K
        geo_lspc_palram_wr08(address, v8);
    }
    else if (address < 0xc00000) { // Memory Card - Mirrored every 2K
        m68k_modify_timeslice(2);
        if (!reg_crdlock[0] && !reg_crdlock[1]) {
            ngsys.memcard[(address >> 1) & 0x7ff] = v8;
        }
    }
    else if (address < 0xd00000) { // BIOS ROM
        geo_log(GEO_LOG_DBG, "68K Write to BIOS ROM: %06x %02x\n",
            address, v8);
    }
    else if (address < 0xe00000) { // Backup RAM - Mirrored every 64K
        if (!reg_sramlock)
            ngsys.nvram[address & 0xffff] = v8;
    }
out:
    geo_debugger_watchpoint_write(addr24, (uint32_t)v8, old_value, 8, old_value_valid);
}

void m68k_write_memory_16(unsigned address, unsigned value) {
    if (address & 0x01)
        geo_log(GEO_LOG_WRN, "Unaligned 16-bit Write: %06x %04x\n",
            address, value);

    uint32_t addr24 = (uint32_t)(address & 0x00ffffffu);
    uint32_t old_value = 0;
    int old_value_valid = 0;
    unsigned v16 = value & 0xffffu;
    address = addr24;

    if (address >= 0x100000 && address < 0x200000) { // RAM (mirrored)
        old_value = read16(ram, address & 0xffff);
        old_value_valid = 1;
    } else if (address >= 0x400000 && address < 0x800000) { // Palette RAM (mirrored)
        old_value = geo_lspc_palram_rd16(address);
        old_value_valid = 1;
    } else if (address >= 0x800000 && address < 0xc00000) { // Memory Card
        old_value = (uint32_t)ngsys.memcard[(address >> 1) & 0x7ff] | 0xff00u;
        old_value_valid = 1;
    } else if (address >= 0xd00000 && address < 0xe00000) { // Backup RAM (mirrored)
        old_value = read16(ngsys.nvram, address & 0xffff);
        old_value_valid = 1;
    }

    uint32_t protectValue = (uint32_t)v16;
    geo_protect_filterWrite(addr24, 16, old_value, old_value_valid, &protectValue);
    v16 = protectValue & 0xffffu;

    if (address < 0x100000) { // Fixed 1M Program ROM Bank
        geo_log(GEO_LOG_DBG, "68K Write to Program ROM: %06x %04x\n",
            address, v16);
        if (address == GEO_DBG_CHECKPOINT_ADDR) {
            geo_debug_checkpoint_write((uint8_t)(v16 >> 8));
            goto out;
        }
    }
    else if (address < 0x200000) { // RAM - Mirrored every 64K
        m68k_modify_timeslice(1);
        write16(ram, address & 0xffff, v16);
    }
    else if (address < 0x300000) { // Switchable 1M Program ROM Bank
        m68k_modify_timeslice(1);
        geo_m68k_write_banksw_16(address, v16);
    }
    else if (address < 0x400000) { // Memory Mapped Registers
        switch (address) {
            case 0x320000: { // REG_SOUND
                ngsys.sound_code = (v16 >> 8) & 0xff; // Use the upper byte
                geo_z80_nmi();
                goto out;
            }
            case 0x3c0000: { // REG_VRAMADDR
                geo_lspc_vramaddr_wr(v16);
                goto out;
            }
            case 0x3c0002: { // REG_VRAMRW
                geo_lspc_vram_wr(v16);
                goto out;
            }
            case 0x3c0004: { // REG_VRAMMOD
                geo_lspc_vrammod_wr((int16_t)v16);
                goto out;
            }
            case 0x3c0006: { // REG_LSPCMODE
                geo_lspc_mode_wr(v16);
                goto out;
            }
            case 0x3c0008: { // REG_TIMERHIGH
                ngsys.irq2_reload =
                    (ngsys.irq2_reload & 0xffff) | ((uint32_t)v16 << 16);
                goto out;
            }
            case 0x3c000a: { // REG_TIMERLOW
                ngsys.irq2_reload =
                    (ngsys.irq2_reload & 0xffff0000) | ((uint32_t)v16 & 0xffff);

                // Reload counter when REG_TIMERLOW is written
                if (ngsys.irq2_ctrl & IRQ_TIMER_RELOAD_WRITE)
                    ngsys.irq2_counter = ngsys.irq2_reload;

                goto out;
            }
            case 0x3c000c: { // REG_IRQACK
                /* Bit 2: Ack VBlank
                   Bit 1: Ack HBlank
                   Bit 0: Ack IRQ3 (Reset)
                */
                if (v16 & 0x04) // VBlank
                    m68k_set_virq(IRQ_VBLANK, 0);
                if (v16 & 0x02) // HBlank/Timer
                    m68k_set_virq(IRQ_TIMER, 0);
                if (v16 & 0x01) // IRQ3 - Pending after reset
                    m68k_set_virq(IRQ_RESET, 0);

                goto out;
            }
            case 0x3c000e: { // REG_TIMERSTOP
                /* Bit 0=1: Stop timer counter during first and last 16 lines
                   (32 total) when in PAL mode -- FIXME
                */
                goto out;
            }
        }
        geo_log(GEO_LOG_DBG, "Unknown 16-bit 68K Write: %06x %04x\n",
            address, v16);
    }
    else if (address < 0x800000) { // Palette RAM - Mirrored every 8K
        geo_lspc_palram_wr16(address, v16);
    }
    else if (address < 0xc00000) { // Memory Card - Mirrored every 2K
        m68k_modify_timeslice(2);
        if (!reg_crdlock[0] && !reg_crdlock[1]) {
            ngsys.memcard[(address >> 1) & 0x7ff] = v16 & 0xff;
        }
    }
    else if (address < 0xd00000) { // BIOS ROM
        geo_log(GEO_LOG_DBG, "68K Write to BIOS ROM: %06x %04x\n",
            address, v16);
    }
    else if (address < 0xe00000) { // Backup RAM - Mirrored every 64K
        if (!reg_sramlock)
            write16(ngsys.nvram, address & 0xffff, v16);
    }
out:
    geo_debugger_watchpoint_write(addr24, (uint32_t)v16, old_value, 16, old_value_valid);
}

void m68k_write_memory_32(unsigned address, unsigned value) {
    uint32_t addr24 = (uint32_t)(address & 0x00ffffffu);
    uint32_t v32 = (uint32_t)value;
    geo_debugger_watchpoint_suspend();
    unsigned old_hi = m68k_read_memory_16(address);
    unsigned old_lo = m68k_read_memory_16(address + 2);
    m68k_write_memory_16(address, (v32 >> 16) & 0xffffu);
    m68k_write_memory_16(address + 2, v32 & 0xffffu);
    geo_debugger_watchpoint_resume();
    uint32_t old_value = ((uint32_t)old_hi << 16) | ((uint32_t)old_lo & 0xffffu);
    geo_debugger_watchpoint_write(addr24, v32, old_value, 32, 1);
}

void geo_m68k_reset(void) {
    /* Vector table MUST be set to BIOS before pulsing reset to ensure the
       initial stack pointer is correct.
    */
    vectable = VECTOR_TABLE_BIOS;

    m68k_pulse_reset();
    sma_prn = 0x2345;
    reg_sramlock = 0;
    reg_crdlock[0] = reg_crdlock[1] = 0;
    reg_crdregsel = 0;

    if (romdata->psz > 0x100000)
        banksw_addr = 0x100000;
    else
        banksw_addr = 0;
}

void geo_m68k_init(void) {
    // Initialize the 68K CPU
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);

    // Zero the cartridge registers (most games do not use these)
    cartreg[0] = cartreg[1] = 0;

    if (geo_get_system() == SYSTEM_AES)
        reg_crtfix = 1;

    romdata = geo_romdata_ptr();
}

int geo_m68k_run(unsigned cycs) {
    return m68k_execute(cycs);
}

void geo_m68k_interrupt(unsigned level) {
    if ((m68k_get_virq(level)) == 0)
        m68k_set_virq(level, 1);
}

// Acknowledge interrupts
int geo_m68k_int_ack(int level) {
    if (level) { }
    //geo_log(GEO_LOG_INF, "IRQ ACK Level: %02x\n", level);
    return M68K_INT_ACK_AUTOVECTOR;
}

void geo_m68k_board_set(unsigned btype) {
    // Set board type
    boardtype = btype;

    // Set default handlers
    geo_m68k_read_fixed_8 = &geo_m68k_read_fixed_8_default;
    geo_m68k_read_fixed_16 = &geo_m68k_read_fixed_16_default;
    geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_default;
    geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_default;
    geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_default;
    geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_default;

    // Set special handlers
    switch (btype) {
        case BOARD_LINKABLE: // Linkable Multiplayer Boards
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_linkable;
            geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_linkable;
            break;
        case BOARD_CT0: // PRO-CT0
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_ct0;
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_ct0;
            geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_ct0;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_ct0;
            break;
        case BOARD_SMA: // NEO-SMA
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_sma;
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_sma;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_sma;
            break;
        case BOARD_PVC: // NEO-PVC
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_pvc;
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_pvc;
            geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_pvc;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_pvc;
            break;
        case BOARD_KOF98: // The King of Fighters '98
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_kof98;
            break;
        case BOARD_KF2K3BL: // The King of Fighters 2003 (bootleg set 1)
            geo_m68k_read_fixed_8 = &geo_m68k_read_fixed_8_kf2k3bl;
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_pvc;
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_pvc;
            geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_pvc;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_pvc;
            break;
        case BOARD_KF2K3BLA: // The King of Fighters 2003 (bootleg set 2)
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_pvc;
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_pvc;
            geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_pvc;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_kf2k3bla;
            break;
        case BOARD_MSLUGX: // Metal Slug X
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_mslugx;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_mslugx;
            break;
        case BOARD_MS5PLUS: // Metal Slug 5 Plus (bootleg)
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_ms5plus;
            break;
        case BOARD_CTHD2003: // Crouching Tiger Hidden Dragon 2003
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_cthd2003;
            break;
        case BOARD_BREZZASOFT: // Jockey Grand Prix, V-Liner
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_brezza;
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_brezza;
            geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_brezza;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_brezza;
            ngsys.sram_present = 1;
            break;
        case BOARD_KOF10TH: // The King of Fighters 10th Anniversary Bootleg
            geo_m68k_read_fixed_8 = &geo_m68k_read_fixed_8_kof10th;
            geo_m68k_read_fixed_16 = &geo_m68k_read_fixed_16_kof10th;
            geo_m68k_read_banksw_8 = &geo_m68k_read_banksw_8_kof10th;
            geo_m68k_read_banksw_16 = &geo_m68k_read_banksw_16_kof10th;
            geo_m68k_write_banksw_8 = &geo_m68k_write_banksw_8_kof10th;
            geo_m68k_write_banksw_16 = &geo_m68k_write_banksw_16_kof10th;
            romdata->s = dynfix;
            romdata->ssz = SIZE_128K;

            /* According to MAME, there is an Altera protection chip which
               patches over the P ROM to allow the game to work correctly.
            */
            // Enables XOR for RAM moves, forces SoftDIPs, and USA region
            romdata->p[0x0124] = 0x00;
            romdata->p[0x0125] = 0x0d;
            romdata->p[0x0126] = 0xf7;
            romdata->p[0x0127] = 0xa8;

            // Run code to change "S" data
            romdata->p[0x8bf4] = 0x4e;
            romdata->p[0x8bf5] = 0xf9;
            romdata->p[0x8bf6] = 0x00;
            romdata->p[0x8bf7] = 0x0d;
            romdata->p[0x8bf8] = 0xf9;
            romdata->p[0x8bf9] = 0x80;
            break;
    }
}

// Musashi disassembler memory accessors
unsigned m68k_read_disassembler_8(unsigned address) {
    return m68k_read_memory_8(address);
}
unsigned m68k_read_disassembler_16(unsigned address) {
    return m68k_read_memory_16(address);
}
unsigned m68k_read_disassembler_32(unsigned address) {
    return m68k_read_memory_32(address);
}

void geo_m68k_postload(void) {
    // Byteswap the BIOS and P ROM
    swapb16_range(romdata->b, romdata->bsz);
    swapb16_range(romdata->p, romdata->psz);

    // Calculate the switchable PROM bank mask
    banksw_mask = (romdata->psz > 0x100000) ?
        geo_calc_mask(8, (romdata->psz - 0x100000) >> 20) : 0;
}

uint8_t geo_m68k_reg_poutput(void) {
    return reg_poutput;
}

void geo_m68k_state_load(uint8_t *st) {
    m68k_set_reg(M68K_REG_D0, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_D1, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_D2, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_D3, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_D4, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_D5, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_D6, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_D7, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A0, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A1, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A2, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A3, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A4, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A5, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A6, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_A7, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_PC, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_SR, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_SP, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_USP, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_ISP, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_MSP, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_PPC, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_IR, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_PREF_ADDR, geo_serial_pop32(st));
    m68k_set_reg(M68K_REG_PREF_DATA, geo_serial_pop32(st));

    m68ki_cpu.int_level = geo_serial_pop32(st);
    m68ki_cpu.virq_state = geo_serial_pop32(st);
    m68ki_cpu.nmi_pending = geo_serial_pop32(st);

    geo_serial_popblk(ram, st, SIZE_64K);
    cartreg[0] = geo_serial_pop16(st);
    cartreg[1] = geo_serial_pop16(st);
    protreg = geo_serial_pop32(st);
    banksw_addr = geo_serial_pop32(st);
    vectable = geo_serial_pop8(st);
    reg_sramlock = geo_serial_pop8(st);
    reg_crdlock[0] = geo_serial_pop8(st);
    reg_crdlock[1] = geo_serial_pop8(st);
    reg_crdregsel = geo_serial_pop8(st);
    reg_crtfix = geo_serial_pop8(st);
    reg_poutput = geo_serial_pop8(st);

    m68k_write_memory_8(reg_crtfix ? 0x3a001b : 0x3a000b, reg_crtfix);

    if (boardtype == BOARD_KOF98) {
        geo_m68k_write_banksw_16_kof98(0x20aaaa, cartreg[0]);
    }
    else if (boardtype == BOARD_KOF10TH) {
        geo_serial_popblk(dynfix, st, SIZE_128K);
        geo_serial_popblk(ngsys.extram, st, SIZE_128K);
    }
}

void geo_m68k_state_save(uint8_t *st) {
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D0));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D1));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D2));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D3));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D4));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D5));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D6));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_D7));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A0));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A1));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A2));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A3));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A4));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A5));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A6));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_A7));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_PC));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_SR));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_SP));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_USP));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_ISP));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_MSP));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_PPC));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_IR));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_PREF_ADDR));
    geo_serial_push32(st, m68k_get_reg(NULL, M68K_REG_PREF_DATA));

    geo_serial_push32(st, m68ki_cpu.int_level);
    geo_serial_push32(st, m68ki_cpu.virq_state);
    geo_serial_push32(st, m68ki_cpu.nmi_pending);

    geo_serial_pushblk(st, ram, SIZE_64K);
    geo_serial_push16(st, cartreg[0]);
    geo_serial_push16(st, cartreg[1]);
    geo_serial_push32(st, protreg);
    geo_serial_push32(st, banksw_addr);
    geo_serial_push8(st, vectable);
    geo_serial_push8(st, reg_sramlock);
    geo_serial_push8(st, reg_crdlock[0]);
    geo_serial_push8(st, reg_crdlock[1]);
    geo_serial_push8(st, reg_crdregsel);
    geo_serial_push8(st, reg_crtfix);
    geo_serial_push8(st, reg_poutput);

    if (boardtype == BOARD_KOF10TH) {
        geo_serial_pushblk(st, dynfix, SIZE_128K);
        geo_serial_pushblk(st, ngsys.extram, SIZE_128K);
    }
}

// Return a pointer to Main RAM
const void* geo_m68k_ram_ptr(void) {
    return ram;
}

// Return a pointer to Dynamic FIX data
const void* geo_m68k_dynfix_ptr(void) {
    return dynfix;
}
