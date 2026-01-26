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

#include <stdint.h>
#include <stdlib.h>

#include <miniz.h>

#include "geo.h"
#include "geo_export.h"
#include "geo_lspc.h"
#include "geo_m68k.h"
#include "geo_memcard.h"
#include "geo_mixer.h"
#include "geo_rtc.h"
#include "geo_serial.h"
#include "geo_ymfm.h"
#include "geo_checkpoint.h"
#include "geo_cycles.h"
#include "geo_z80.h"

#define DIV_M68K 2
#define DIV_Z80 6
#define DIV_YM2610 72 // 72 for medium fidelity, 8 for high

#define MCYC_PER_LINE 1536
#define MCYC_PER_FRAME (MCYC_PER_LINE * 264) // 405504

// Log callback
void (*geo_log)(int, const char *, ...);

// Input poll callbacks
unsigned (*geo_input_cb[NUMINPUTS_NG])(unsigned);
unsigned (*geo_input_sys_cb[NUMINPUTS_SYS])(void);
static void (*geo_vblank_cb)(void *) = NULL;
static void *geo_vblank_user = NULL;

// Neo Geo System
ngsys_t ngsys;

// ROM data
static romdata_t romdata;

static uint8_t state[485301]; // Maximum size
static size_t state_sz = 0;

// System being emulated
static uint8_t sys = 0;
static uint8_t region = 0;

static uint32_t mcycs = 0;
static uint32_t zcycs = 0;
static uint32_t ymcycs = 0;
static uint32_t ymsamps = 0;

static unsigned icycs = 0;

// Remove the 68K Clock Divider
unsigned oc = 0;
unsigned irq2_fragmask = 0x01;

// Watchdog Frames
static unsigned watchdog_frames = 8;

// Set the log callback
void geo_log_set_callback(void (*cb)(int, const char *, ...)) {
    geo_log = cb;
}

// Set the Input Callbacks to allow the emulator to strobe the input state
void geo_input_set_callback(unsigned port, unsigned (*cb)(unsigned)) {
    geo_input_cb[port] = cb;
}

// Set the Status A/B and System Type Register Callbacks
void geo_input_sys_set_callback(unsigned port, unsigned (*cb)(void)) {
    geo_input_sys_cb[port] = cb;
}

GEO_EXPORT void geo_set_vblank_callback(void (*cb)(void *), void *user) {
    geo_vblank_cb = cb;
    geo_vblank_user = user;
}

void geo_vblank_notify(void) {
    if (geo_vblank_cb) {
        geo_vblank_cb(geo_vblank_user);
    }
}

// Set the region of the system to be emulated
void geo_set_region(int r) {
    region = r;
}

// Get the region of the system currently being emulated
int geo_get_region(void) {
    return region;
}

// Set the system to be emulated
void geo_set_system(int s) {
    sys = s;
}

// Get the system currently being emulated
int geo_get_system(void) {
    return sys;
}

// Set the 68K Clock Divider on or off
void geo_set_div68k(int d) {
    oc = !d;
    irq2_fragmask = oc ? 0x03 : 0x01;
}

void geo_set_adpcm_wrap(int w) {
    geo_ymfm_adpcm_wrap(w);
}

void geo_set_watchdog_frames(unsigned w) {
    watchdog_frames = w;
}

romdata_t* geo_romdata_ptr(void) {
    return &romdata;
}

// Calculate a mask for an arbitrarily sized memory block
uint32_t geo_calc_mask(unsigned nbits, unsigned val) {
    /* Masks over blocks of memory with a size which is not a power of two need
       every bit below the most significant set bit to also be set. Loop
       through the value from left to right to find the first bit set, then
       subtract 1 from the next highest power of two to get a mask which covers
       all possible values in the space being addressed.

       Example: If there are 5 addressable banks, banks 0-4 must be
                addressable. In this case, the mask must be 0b0111 or 0x7 for
                all banks to be covered when a value is masked with AND.
    */
    if (val)
        val -= 1;

    for (unsigned i = 1 << (nbits - 1); i != 0; i >>= 1) {
        if (i & val)
            return (i << 1) - 1;
    }
    return 0;
}

static int geo_bios_load(mz_zip_archive *zip_archive) {
    const char *biosrom;

    switch (geo_get_system()) {
        default: case SYSTEM_AES: {
            biosrom = geo_get_region() == REGION_JP ?
                "neo-po.bin" : "neo-epo.bin";
            break;
        }
        case SYSTEM_MVS: {
            switch (geo_get_region()) {
                default: case REGION_US: // Winners don't use drugs
                    biosrom = "sp-u2.sp1";
                    break;
                case REGION_JP:
                    biosrom = "japan-j3.bin";
                    break;
                case REGION_AS:
                    biosrom = "sp-45.sp1";
                    break;
                case REGION_EU:
                    biosrom = "sp-s2.sp1";
                    break;
            }
            break;
        }
        case SYSTEM_UNI: {
            biosrom = "uni-bios_4_0.rom";
            break;
        }
    }

    geo_log(GEO_LOG_DBG, "Loading %s\n", biosrom);
    romdata.b = mz_zip_reader_extract_file_to_heap(zip_archive, biosrom,
        &(romdata.bsz), 0);

    if (romdata.b == NULL) {
        mz_zip_reader_end(zip_archive);
        geo_bios_unload();
        geo_log(GEO_LOG_ERR,
            "Failed to load %s from BIOS archive!\n", biosrom);
        return 0;
    }

    // Load L0 ROM
    romdata.l0 = mz_zip_reader_extract_file_to_heap(zip_archive,
        "000-lo.lo", &(romdata.l0sz), 0);
    if (romdata.l0 == NULL) {
        mz_zip_reader_end(zip_archive);
        geo_bios_unload();
        geo_log(GEO_LOG_ERR, "Failed to load 000-lo.lo from BIOS archive!\n");
        return 0;
    }

    if (geo_get_system() != SYSTEM_AES) {
        // SFIX
        romdata.sfix = mz_zip_reader_extract_file_to_heap(zip_archive,
            "sfix.sfix", &(romdata.sfixsz), 0);
        if (romdata.sfix == NULL) {
            mz_zip_reader_end(zip_archive);
            geo_bios_unload();
            geo_log(GEO_LOG_ERR,
                "Failed to load sfix.sfix from BIOS archive!\n");
            return 0;
        }

        // SM1
        romdata.sm = mz_zip_reader_extract_file_to_heap(zip_archive,
            "sm1.sm1", &(romdata.smsz), 0);
        if (romdata.sm == NULL) {
            mz_zip_reader_end(zip_archive);
            geo_bios_unload();
            geo_log(GEO_LOG_ERR,
                "Failed to load sm1.sm1 from BIOS archive!\n");
            return 0;
        }
    }

    mz_zip_reader_end(zip_archive);
    return 1;
}

// Load a zipped collection of System ROM data from a memory buffer
int geo_bios_load_mem(void *data, size_t size) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    // Make sure it's actually a zip file
    if (!mz_zip_reader_init_mem(&zip_archive, data, size, 0))
        return 0;
    return geo_bios_load(&zip_archive);
}

// Load a zipped collection of System ROM data from a file
int geo_bios_load_file(const char *biospath) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    // Make sure it's actually a zip file
    if (!mz_zip_reader_init_file(&zip_archive, biospath, 0))
        return 0;
    return geo_bios_load(&zip_archive);
}

void geo_bios_unload(void) {
    if (romdata.b)
        free(romdata.b);
    if (romdata.l0)
        free(romdata.l0);
    if (romdata.sfix)
        free(romdata.sfix);
    if (romdata.sm)
        free(romdata.sm);
}

// Load NVRAM, Cartridge RAM, or Memory Card data
int geo_savedata_load(unsigned datatype, const char *filename) {
    const uint8_t *dataptr = NULL;
    size_t datasize = 0;

    switch (datatype) {
        case GEO_SAVEDATA_NVRAM: {
            if (geo_get_system() == SYSTEM_AES)
                return 2;
            dataptr = geo_mem_ptr(GEO_MEMTYPE_NVRAM, &datasize);
            break;
        }
        case GEO_SAVEDATA_CARTRAM: {
            dataptr = geo_mem_ptr(GEO_MEMTYPE_CARTRAM, &datasize);
            break;
        }
        case GEO_SAVEDATA_MEMCARD: {
            dataptr = geo_mem_ptr(GEO_MEMTYPE_MEMCARD, &datasize);
            break;
        }
        default: return 2;
    }

    FILE *file;
    size_t filesize, result;

    // Open the file for reading
    file = fopen(filename, "rb");
    if (!file)
        return 0;

    // Find out the file's size
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (filesize != datasize)
        return 0;

    // Read the file into the memory card and then close it
    result = fread((void*)dataptr, sizeof(uint8_t), filesize, file);
    if (result != filesize)
        return 0;
    fclose(file);

    return 1; // Success!
}

// Save NVRAM, Cartridge RAM, or Memory Card data
int geo_savedata_save(unsigned datatype, const char *filename) {
    const uint8_t *dataptr = NULL;
    size_t datasize = 0;

    switch (datatype) {
        case GEO_SAVEDATA_NVRAM: {
            if (geo_get_system() == SYSTEM_AES)
                return 2;
            dataptr = geo_mem_ptr(GEO_MEMTYPE_NVRAM, &datasize);
            break;
        }
        case GEO_SAVEDATA_CARTRAM: {
            if (!ngsys.sram_present)
                return 2;
            dataptr = geo_mem_ptr(GEO_MEMTYPE_CARTRAM, &datasize);
            break;
        }
        case GEO_SAVEDATA_MEMCARD: {
            dataptr = geo_mem_ptr(GEO_MEMTYPE_MEMCARD, &datasize);
            break;
        }
        default: return 2;
    }

    FILE *file;
    file = fopen(filename, "wb");
    if (!file)
        return 0;

    // Write and close the file
    fwrite(dataptr, datasize, sizeof(uint8_t), file);
    fclose(file);

    return 1; // Success!
}

// Return presence of on-cartridge battery-backed save RAM
unsigned geo_cartram_present(void) {
    return ngsys.sram_present;
}

void geo_reset(int hard) {
    ngsys.sound_code = 0;
    ngsys.sound_reply = 0;
    ngsys.watchdog = 0;

    geo_m68k_reset();
    geo_z80_reset();
    geo_ymfm_reset(); // Reset the YM2610 to make sure everything is defaulted
    geo_lspc_init();
    geo_checkpoint_reset();
    geo_cycles_reset();

    if (hard)
        geo_m68k_interrupt(IRQ_RESET);
}

void geo_init(void) {
    geo_m68k_init();
    geo_z80_init();
    geo_ymfm_init();
    geo_rtc_init();
    geo_lspc_init();
    geo_checkpoint_reset();
    geo_cycles_reset();

    ngsys.irq2_ctrl = 0;
    ngsys.irq2_reload = 0;
    ngsys.irq2_counter = 0;
    ngsys.irq2_frags = 0;
    ngsys.irq2_dec = 0;
    watchdog_frames = 8;

    memset(ngsys.nvram, 0x00, SIZE_64K);
    memset(ngsys.cartram, 0x00, SIZE_8K);
    memset(ngsys.extram, 0x00, SIZE_128K);

    // Pre-format the memory card in case there is no existing memory card data
    geo_memcard_format(ngsys.memcard);
}

int geo_state_load_raw(const void *sstate) {
    uint8_t *st = (uint8_t*)sstate;
    geo_serial_begin();
    uint8_t stregion = geo_serial_pop8(st);
    uint8_t stsys = geo_serial_pop8(st);

    if ((region != stregion) || (sys != stsys)) {
        geo_log(GEO_LOG_WRN, "State load operation ignored: state is "
        "for a different system type or region\n");
        return 0;
    }

    mcycs = geo_serial_pop32(st);
    zcycs = geo_serial_pop32(st);
    ymcycs = geo_serial_pop32(st);
    ngsys.irq2_ctrl = geo_serial_pop8(st);
    ngsys.irq2_reload = geo_serial_pop32(st);
    ngsys.irq2_counter = geo_serial_pop32(st);
    ngsys.irq2_frags = geo_serial_pop32(st);
    ngsys.irq2_dec = geo_serial_pop32(st);
    geo_serial_popblk(ngsys.nvram, st, SIZE_64K);
    geo_serial_popblk(ngsys.memcard, st, SIZE_2K);

    if (ngsys.sram_present)
        geo_serial_popblk(ngsys.cartram, st, SIZE_8K);

    ngsys.watchdog = geo_serial_pop32(st);
    ngsys.sound_code = geo_serial_pop8(st);
    ngsys.sound_reply = geo_serial_pop8(st);

    geo_checkpoint_state_load(st);
    geo_cycles_state_load(st);
    geo_lspc_state_load(st);
    geo_m68k_state_load(st);
    geo_rtc_state_load(st);
    geo_ymfm_state_load(st);
    geo_z80_state_load(st);
    return 1;
}

// Load a state from a file
int geo_state_load(const char *filename) {
    FILE *file;
    size_t filesize, result;
    void *sstatefile;

    // Open the file for reading
    file = fopen(filename, "rb");
    if (!file)
        return 0;

    // Find out the file's size
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate memory to read the file into
    sstatefile = (void*)calloc(filesize, sizeof(uint8_t));
    if (sstatefile == NULL) {
        fclose(file);
        return 0;
    }

    // Read the file into memory and then close it
    result = fread(sstatefile, sizeof(uint8_t), filesize, file);
    if (result != filesize) {
        fclose(file);
        free(sstatefile);
        return 0;
    }

    // File has been read, now copy it into the emulator
    int ret = geo_state_load_raw((const void*)sstatefile);

    // Free the allocated memory and close file handle
    fclose(file);
    free(sstatefile);

    return ret; // Success!
}

const void* geo_state_save_raw(void) {
    geo_serial_begin();
    geo_serial_push8(state, region);
    geo_serial_push8(state, sys);
    geo_serial_push32(state, mcycs);
    geo_serial_push32(state, zcycs);
    geo_serial_push32(state, ymcycs);
    geo_serial_push8(state, ngsys.irq2_ctrl);
    geo_serial_push32(state, ngsys.irq2_reload);
    geo_serial_push32(state, ngsys.irq2_counter);
    geo_serial_push32(state, ngsys.irq2_frags);
    geo_serial_push32(state, ngsys.irq2_dec);
    geo_serial_pushblk(state, ngsys.nvram, SIZE_64K);
    geo_serial_pushblk(state, ngsys.memcard, SIZE_2K);

    if (ngsys.sram_present)
        geo_serial_pushblk(state, ngsys.cartram, SIZE_8K);

    geo_serial_push32(state, ngsys.watchdog);
    geo_serial_push8(state, ngsys.sound_code);
    geo_serial_push8(state, ngsys.sound_reply);

    geo_checkpoint_state_save(state);
    geo_cycles_state_save(state);
    geo_lspc_state_save(state);
    geo_m68k_state_save(state);
    geo_rtc_state_save(state);
    geo_ymfm_state_save(state);
    geo_z80_state_save(state);

    return (const void*)state;
}

// Save a state to a file
int geo_state_save(const char *filename) {
    // Open the file for writing
    FILE *file;
    file = fopen(filename, "wb");
    if (!file)
        return 0;

    // Snapshot the running state and get the memory address
    uint8_t *sstate = (uint8_t*)geo_state_save_raw();

    // Write and close the file
    fwrite(sstate, geo_serial_size(), sizeof(uint8_t), file);
    fclose(file);

    return 1; // Success!
}

// Return the size of the state
size_t geo_state_size(void) {
    // Perform a false state save to determine the size if it is unknown
    if (!state_sz) {
        const void *st = geo_state_save_raw();
        (void)st;
        state_sz = geo_serial_size();
    }
    return state_sz;
}

// Return a pointer to a raw memory block
const void* geo_mem_ptr(unsigned type, size_t *sz) {
    switch (type) {
        case GEO_MEMTYPE_MAINRAM: {
            if (sz) *sz = SIZE_64K;
            return geo_m68k_ram_ptr();
        }
        case GEO_MEMTYPE_Z80RAM: {
            if (sz) *sz = SIZE_2K;
            return geo_z80_ram_ptr();
        }
        case GEO_MEMTYPE_VRAM: {
            if (sz) *sz = SIZE_64K + SIZE_4K;
            return geo_lspc_vram_ptr();
        }
        case GEO_MEMTYPE_PALRAM: {
            if (sz) *sz = SIZE_16K;
            return geo_lspc_palram_ptr();
        }
        case GEO_MEMTYPE_NVRAM: {
            if (sz) *sz = SIZE_64K;
            return ngsys.nvram;
        }
        case GEO_MEMTYPE_CARTRAM: {
            if (sz) *sz = SIZE_8K;
            return ngsys.cartram;
        }
        case GEO_MEMTYPE_EXTRAM: {
            if (sz) *sz = SIZE_128K;
            return ngsys.extram;
        }
        case GEO_MEMTYPE_MEMCARD: {
            if (sz) *sz = SIZE_2K;
            return ngsys.memcard;
        }
        case GEO_MEMTYPE_DYNFIX: {
            if (sz) *sz = SIZE_128K;
            return geo_m68k_dynfix_ptr();
        }
    }

    return NULL;
}

// Increment the watchdog counter
static inline void geo_watchdog_increment(void) {
    /* If 8 frames have passed since the Watchdog was kicked, assume a bug or
       or hardware fault and recover by resetting the system. The true number
       of cycles needed for this to happen in hardware is 3244030, or slightly
       under 8 frames of video. Currently there is a single case where the
       watchdog can be kicked a few cycles too early. Hardware verification
       or an audit of 68K CPU timing will be required to understand the root
       cause.
    */
    if (++ngsys.watchdog >= watchdog_frames) {
        geo_log(GEO_LOG_WRN, "Watchdog reset\n");
        geo_reset(0);
    }
}

// Reset the Watchdog counter
void geo_watchdog_reset(void) {
    ngsys.watchdog = 0;
}

#include "geo_debugger.h"
void geo_exec(void) {
    int dbg_broke_midframe = 0;
    while (mcycs < MCYC_PER_FRAME) {
        // Allow debugger to halt mid-frame after a single-instruction step or breakpoint
        if (geo_debugger_break_now()) { dbg_broke_midframe = 1; break; }
        icycs = geo_m68k_run(1);
        mcycs += (icycs * DIV_M68K) >> oc;

        // If this is an arcade system, update the RTC
        if (sys)
            geo_rtc_sync(icycs >> oc);

        // Handle IRQ2 counter
        ngsys.irq2_dec = (icycs >> 1) >> oc; // Measured in pixel clocks
        ngsys.irq2_frags += icycs & irq2_fragmask;
        if (ngsys.irq2_frags >= (2U << oc)) {
            ngsys.irq2_frags -= (2U << oc);
            ++ngsys.irq2_dec;
        }

        for (uint32_t i = 0; i < ngsys.irq2_dec; ++i) {
            if (--ngsys.irq2_counter == 0) {
                /* Reload counter when it reaches 0 - if this bit is not set,
                   rely on unsigned integer underflow to prevent repeated
                   assertion of the IRQ line.
                */
                if (ngsys.irq2_ctrl & IRQ_TIMER_RELOAD_COUNT0)
                    ngsys.irq2_counter += ngsys.irq2_reload;

                // Timer Interrupt Enabled
                if (ngsys.irq2_ctrl & IRQ_TIMER_ENABLED)
                    geo_m68k_interrupt(IRQ_TIMER);
            }
        }

        geo_lspc_run(icycs >> oc);

        // Catch the Z80 and YM2610 up to the 68K
        while (zcycs < mcycs) {
            size_t scycs = geo_z80_run(1);
            zcycs += scycs * DIV_Z80;
            ymcycs += scycs;
            if (ymcycs >= DIV_YM2610) {
                ymcycs -= DIV_YM2610;
                ymsamps += geo_ymfm_exec();
            }
        }
    }

    mcycs %= MCYC_PER_FRAME;
    zcycs %= MCYC_PER_FRAME;

    if (dbg_broke_midframe) {
        // Skip audio and watchdog when breaking mid-frame to avoid unintended resets
        // Also drop any accumulated YM samples so we don't overflow the output buffer
        // on the next full-frame run after repeated mid-frame breaks (watch/breakpoint/step).
        ymsamps = 0;
        // Reset YM output buffer write position; geo_mixer_output() won't be called.
        (void)geo_ymfm_get_buffer();
        return;
    }

    // Pass audio generated this frame to the frontend for output
    geo_mixer_output(ymsamps);
    ymsamps = 0;

    // Increment the Watchdog counter at the end of each frame
    geo_watchdog_increment();
}
