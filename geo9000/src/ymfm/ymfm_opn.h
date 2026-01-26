/*
Copyright (c) 2021 Aaron Giles
Copyright (c) 2022 Rupert Carmichael
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

#ifndef YMFM_OPN_H
#define YMFM_OPN_H

#include "ymfm.h"
#include "ymfm_adpcm.h"
#include "ymfm_fm.h"
#include "ymfm_ssg.h"

//*********************************************************
//  OPN IMPLEMENTATION CLASSES
//*********************************************************

// A note about prescaling and sample rates.
//
// YM2203, YM2608, and YM2610 contain an onboard SSG (basically, a YM2149).
// In order to properly generate sound at fully fidelity, the output sample
// rate of the YM2149 must be input_clock / 8. This is much higher than the
// FM needs, but in the interest of keeping things simple, the OPN generate
// functions will output at the higher rate and just replicate the last FM
// sample as many times as needed.
//
// To make things even more complicated, the YM2203 and YM2608 allow for
// software-controlled prescaling, which affects the FM and SSG clocks in
// different ways. There are three settings: divide by 6/4 (FM/SSG); divide
// by 3/2; and divide by 2/1.
//
// Thus, the minimum output sample rate needed by each part of the chip
// varies with the prescale as follows:
//
//             ---- YM2203 -----    ---- YM2608 -----    ---- YM2610 -----
// Prescale    FM rate  SSG rate    FM rate  SSG rate    FM rate  SSG rate
//     6         /72      /16         /144     /32          /144    /32
//     3         /36      /8          /72      /16
//     2         /24      /4          /48      /8
//
// If we standardized on the fastest SSG rate, we'd end up with the following
// (ratios are output_samples:source_samples):
//
//             ---- YM2203 -----    ---- YM2608 -----    ---- YM2610 -----
//              rate = clock/4       rate = clock/8       rate = clock/16
// Prescale    FM rate  SSG rate    FM rate  SSG rate    FM rate  SSG rate
//     6         18:1     4:1         18:1     4:1          9:1    2:1
//     3          9:1     2:1          9:1     2:1
//     2          6:1     1:1          6:1     1:1
//
// However, that's a pretty big performance hit for minimal gain. Going to
// the other extreme, we could standardize on the fastest FM rate, but then
// at least one prescale case (3) requires the FM to be smeared across two
// output samples:
//
//             ---- YM2203 -----    ---- YM2608 -----    ---- YM2610 -----
//              rate = clock/24      rate = clock/48      rate = clock/144
// Prescale    FM rate  SSG rate    FM rate  SSG rate    FM rate  SSG rate
//     6          3:1     2:3          3:1     2:3          1:1    2:9
//     3        1.5:1     1:3        1.5:1     1:3
//     2          1:1     1:6          1:1     1:6
//
// Stepping back one factor of 2 addresses that issue:
//
//             ---- YM2203 -----    ---- YM2608 -----    ---- YM2610 -----
//              rate = clock/12      rate = clock/24      rate = clock/144
// Prescale    FM rate  SSG rate    FM rate  SSG rate    FM rate  SSG rate
//     6          6:1     4:3          6:1     4:3          1:1    2:9
//     3          3:1     2:3          3:1     2:3
//     2          2:1     1:3          2:1     1:3
//
// This gives us three levels of output fidelity:
//    OPN_FIDELITY_MAX -- highest sample rate, using fastest SSG rate
//    OPN_FIDELITY_MIN -- lowest sample rate, using fastest FM rate
//    OPN_FIDELITY_MED -- medium sample rate such that FM is never smeared
//
// At the maximum clocks for YM2203/YM2608 (4Mhz/8MHz), these rates will
// end up as:
//    OPN_FIDELITY_MAX = 1000kHz
//    OPN_FIDELITY_MIN =  166kHz
//    OPN_FIEDLITY_MED =  333kHz

// ======================> opn_fidelity

enum {
	OPN_FIDELITY_MAX,
	OPN_FIDELITY_MIN,
	OPN_FIDELITY_MED,

	OPN_FIDELITY_DEFAULT = OPN_FIDELITY_MAX
};

// return an array of operator indices for each channel
typedef struct _operator_mapping {
	uint32_t chan[CHANNELS];
} operator_mapping;

uint32_t opn_registers_timer_a_value(void);
uint32_t opn_registers_timer_b_value(void);
uint32_t opn_registers_csm(void);
uint32_t opn_registers_reset_timer_b(void);
uint32_t opn_registers_reset_timer_a(void);
uint32_t opn_registers_enable_timer_b(void);
uint32_t opn_registers_enable_timer_a(void);
uint32_t opn_registers_load_timer_b(void);
uint32_t opn_registers_load_timer_a(void);

uint32_t opn_registers_ch_feedback(uint32_t choffs);
uint32_t opn_registers_ch_algorithm(uint32_t choffs);
uint32_t opn_registers_ch_output_any(uint32_t choffs);
uint32_t opn_registers_ch_output_0(uint32_t choffs);
uint32_t opn_registers_ch_output_1(uint32_t choffs);
uint32_t opn_registers_ch_output_2(uint32_t choffs);
uint32_t opn_registers_ch_output_3(uint32_t choffs);

uint32_t opn_registers_op_lfo_am_enable(uint32_t opoffs);
uint32_t opn_registers_op_ssg_eg_enable(uint32_t opoffs);
uint32_t opn_registers_op_ssg_eg_mode(uint32_t opoffs);

void opn_registers_init(void);
void opn_registers_reset(void);
bool opn_registers_write(uint16_t index, uint8_t data, uint32_t *channel, uint32_t *opmask);
int32_t opn_registers_clock_noise_and_lfo(void);
void opn_registers_reset_lfo(void);
uint32_t opn_registers_lfo_am_offset(uint32_t choffs);
uint32_t opn_registers_noise_state(void);
void opn_registers_cache_operator_data(uint32_t choffs, uint32_t opoffs, opdata_cache *cache);
uint32_t opn_registers_compute_phase_step(uint32_t choffs, const opdata_cache *cache, int32_t lfo_raw_pm);

void opn_state_load(uint8_t *st);
void opn_state_save(uint8_t *st);

// ======================> ym2610/ym2610b

void ym2610_init(void);
void ym2610_set_fidelity(uint32_t fidelity);
void ym2610_reset(void);
uint32_t ym2610_sample_rate(uint32_t input_clock);
uint32_t ym2610_ssg_effective_clock(uint32_t input_clock);
void ym2610_invalidate_caches(void);
uint8_t ym2610_read(uint32_t offset);
void ym2610_write(uint32_t offset, uint8_t data);
void ym2610_generate(int32_t *output);


// ======================> ym2612

void ym2612_reset(void);
uint32_t ym2612_sample_rate(uint32_t input_clock);
void ym2612_invalidate_caches(void);
uint8_t ym2612_read(uint32_t offset);
void ym2612_write(uint32_t offset, uint8_t data);

void ym2612_init(void);

void ym2612_generate(int32_t *output);
void ym3438_generate(int32_t *output);

#endif // YMFM_OPN_H
