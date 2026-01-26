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

/* Every genius makes it more complicated. It takes a super-genius to make it
   simpler.
            - Terry A. Davis
*/

#ifndef GEOLITH_H
#define GEOLITH_H

#define SIZE_1K     0x000400
#define SIZE_2K     0x000800
#define SIZE_4K     0x001000
#define SIZE_8K     0x002000
#define SIZE_16K    0x004000
#define SIZE_32K    0x008000
#define SIZE_64K    0x010000
#define SIZE_128K   0x020000

#define SYSTEM_AES  0x00 // Console
#define SYSTEM_MVS  0x01 // Arcade
#define SYSTEM_UNI  0x02 // Universe BIOS

#define REGION_US   0x00 // USA
#define REGION_JP   0x01 // Japan
#define REGION_AS   0x02 // Asia
#define REGION_EU   0x03 // Europe

#define NUMINPUTS_NG    2
#define NUMINPUTS_SYS   5

#define FRAMERATE_AES   59.599484
#define FRAMERATE_MVS   59.185606

enum geo_memtype {
    GEO_MEMTYPE_MAINRAM,
    GEO_MEMTYPE_Z80RAM,
    GEO_MEMTYPE_VRAM,
    GEO_MEMTYPE_PALRAM,
    GEO_MEMTYPE_NVRAM,
    GEO_MEMTYPE_CARTRAM,
    GEO_MEMTYPE_EXTRAM,
    GEO_MEMTYPE_MEMCARD,
    GEO_MEMTYPE_DYNFIX,
    GEO_MEMTYPE_MAX
};

enum geo_savedata {
    GEO_SAVEDATA_NVRAM,
    GEO_SAVEDATA_CARTRAM,
    GEO_SAVEDATA_MEMCARD,
    GEO_SAVEDATA_MAX
};

enum geo_loglevel {
    GEO_LOG_DBG,
    GEO_LOG_INF,
    GEO_LOG_WRN,
    GEO_LOG_ERR,
    GEO_LOG_SCR
};

typedef struct _ngsys_t {
    // Timer Interrupt (IRQ2)
    uint8_t irq2_ctrl;
    uint32_t irq2_reload;
    uint32_t irq2_counter;
    uint32_t irq2_frags;
    uint32_t irq2_dec;

    // Memory used for saving cabinet stats and/or game data
    uint8_t nvram[SIZE_64K];
    uint8_t memcard[SIZE_2K];

    // Cartridge RAM - Sometimes used for saving, other times for game logic
    uint8_t cartram[SIZE_8K];

    // Extended RAM - Some games use extra on-cartridge RAM (not for saving)
    uint8_t extram[SIZE_128K];

    // Watchdog anti-freezing system to reset games due to bugs/hardware faults
    uint32_t watchdog;

    // 68K/Z80 Communication
    uint8_t sound_code;
    uint8_t sound_reply;

    // Cartridge RAM (Battery Backed SRAM) Presence
    uint8_t sram_present;
} ngsys_t;

typedef struct _romdata_t {
    // BIOS ROM (System ROM)
    uint8_t *b;
    size_t bsz;

    // SFIX ROM (System Graphics ROM for FIX layer)
    uint8_t *sfix;
    size_t sfixsz;

    // SM1 ROM (System Music ROM)
    uint8_t *sm;
    size_t smsz;

    // L0 ROM (Vertical Shrink ROM)
    uint8_t *l0;
    size_t l0sz;

    // P ROM (Program ROM)
    uint8_t *p;
    size_t psz;

    // S ROM (Graphics ROM for FIX layer)
    uint8_t *s;
    size_t ssz;

    // M1 ROM (Music ROM)
    uint8_t *m;
    size_t msz;

    // V1 ROM (Voice ROM - ADPCM sample data A - ~18KHz)
    uint8_t *v1;
    size_t v1sz;

    // V2 ROM (Voice ROM - ADPCM sample data B - ~1.85KHz to ~55KHz)
    uint8_t *v2;
    size_t v2sz;

    // C ROM (Graphics ROM for sprites)
    uint8_t *c;
    size_t csz;
} romdata_t;

romdata_t* geo_romdata_ptr(void);

int geo_bios_load_mem(void*, size_t);
int geo_bios_load_file(const char*);
void geo_bios_unload(void);

void geo_watchdog_reset(void);

int geo_savedata_load(unsigned, const char*);
int geo_savedata_save(unsigned, const char*);
unsigned geo_cartram_present(void);

void geo_exec(void);
void geo_init(void);
void geo_reset(int);

void geo_log_set_callback(void (*)(int, const char *, ...));
void geo_input_set_callback(unsigned, unsigned (*)(unsigned));
void geo_input_sys_set_callback(unsigned, unsigned (*)(void));
void geo_set_vblank_callback(void (*cb)(void *), void *user);
void geo_vblank_notify(void);

int geo_get_region(void);
void geo_set_region(int);

int geo_get_system(void);
void geo_set_system(int);
void geo_set_div68k(int);
void geo_set_adpcm_wrap(int);
void geo_set_watchdog_frames(unsigned);

uint32_t geo_calc_mask(unsigned, unsigned);

int geo_state_load(const char*);
int geo_state_load_raw(const void*);

int geo_state_save(const char*);
const void* geo_state_save_raw(void);

size_t geo_state_size(void);

const void* geo_mem_ptr(unsigned, size_t*);

extern void (*geo_log)(int, const char *, ...);

extern unsigned (*geo_input_cb[NUMINPUTS_NG])(unsigned);
extern unsigned (*geo_input_sys_cb[NUMINPUTS_SYS])(void);

extern ngsys_t ngsys;

#endif
