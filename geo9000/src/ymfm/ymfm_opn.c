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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ymfm_opn.h"
#include "ymfm_fm.inc"

#define EOS_FLAGS_MASK 0xbf

// opn_registers internal state
static uint32_t m_lfo_counter;               // LFO counter
static uint8_t m_lfo_am;                     // current LFO AM value
static uint8_t m_regdata[REGISTERS];         // register data
static uint16_t m_waveform[WAVEFORMS][WAVEFORM_LENGTH]; // waveforms

// opn internal state
static uint32_t m_fidelity;                // configured fidelity
static uint16_t m_address;                 // address register
static uint8_t m_fm_mask;                  // FM channel mask
static uint8_t m_fm_samples_per_output;    // how many samples to repeat
static uint8_t m_eos_status;               // end-of-sample signals
static uint8_t m_flag_mask;                // flag mask control
static int32_t m_last_fm[3];               // last FM output
static uint16_t m_dac_data;             // 9-bit DAC data
static uint8_t m_dac_enable;            // DAC enabled?


// ======================> ssg_resampler
static void (*ssg_resampler_resample)(int32_t*);
static uint32_t m_ssg_resampler_sampindex;
static int32_t m_ssg_resampler_last[3];

// return a bitfield extracted from a byte
static inline uint32_t byte(uint32_t offset, uint32_t start, uint32_t count, uint32_t extra_offset)
{
	return bitfield(m_regdata[offset + extra_offset], start, count);
}

// return a bitfield extracted from a pair of bytes, MSBs listed first
static inline uint32_t word(uint32_t offset1, uint32_t start1, uint32_t count1, uint32_t offset2, uint32_t start2, uint32_t count2, uint32_t extra_offset)
{
	return (byte(offset1, start1, count1, extra_offset) << count2) | byte(offset2, start2, count2, extra_offset);
}

// simulate the DAC discontinuity (YM2612)
static inline int32_t dac_discontinuity(int32_t value)
{
	return (value < 0) ? (value - 3) : (value + 4);
}

// helper to apply KSR to the raw ADSR rate, ignoring ksr if the
// raw value is 0, and clamping to 63
static inline uint32_t effective_rate(uint32_t rawrate, uint32_t ksr)
{
	return (rawrate == 0) ? 0 : (rawrate + ksr < 63) ? rawrate + ksr : 63;
}


//*********************************************************
//  OPN/OPNA REGISTERS
//*********************************************************
// ======================> opn_registers
//
// OPN register map:
//
//      System-wide registers:
//           21 xxxxxxxx Test register
//           22 ----x--- LFO enable [OPNA+ only]
//              -----xxx LFO rate [OPNA+ only]
//           24 xxxxxxxx Timer A value (upper 8 bits)
//           25 ------xx Timer A value (lower 2 bits)
//           26 xxxxxxxx Timer B value
//           27 xx------ CSM/Multi-frequency mode for channel #2
//              --x----- Reset timer B
//              ---x---- Reset timer A
//              ----x--- Enable timer B
//              -----x-- Enable timer A
//              ------x- Load timer B
//              -------x Load timer A
//           28 x------- Key on/off operator 4
//              -x------ Key on/off operator 3
//              --x----- Key on/off operator 2
//              ---x---- Key on/off operator 1
//              ------xx Channel select
//
//     Per-channel registers (channel in address bits 0-1)
//     Note that all these apply to address+100 as well on OPNA+
//        A0-A3 xxxxxxxx Frequency number lower 8 bits
//        A4-A7 --xxx--- Block (0-7)
//              -----xxx Frequency number upper 3 bits
//        B0-B3 --xxx--- Feedback level for operator 1 (0-7)
//              -----xxx Operator connection algorithm (0-7)
//        B4-B7 x------- Pan left [OPNA]
//              -x------ Pan right [OPNA]
//              --xx---- LFO AM shift (0-3) [OPNA+ only]
//              -----xxx LFO PM depth (0-7) [OPNA+ only]
//
//     Per-operator registers (channel in address bits 0-1, operator in bits 2-3)
//     Note that all these apply to address+100 as well on OPNA+
//        30-3F -xxx---- Detune value (0-7)
//              ----xxxx Multiple value (0-15)
//        40-4F -xxxxxxx Total level (0-127)
//        50-5F xx------ Key scale rate (0-3)
//              ---xxxxx Attack rate (0-31)
//        60-6F x------- LFO AM enable [OPNA]
//              ---xxxxx Decay rate (0-31)
//        70-7F ---xxxxx Sustain rate (0-31)
//        80-8F xxxx---- Sustain level (0-15)
//              ----xxxx Release rate (0-15)
//        90-9F ----x--- SSG-EG enable
//              -----xxx SSG-EG envelope (0-7)
//
//     Special multi-frequency registers (channel implicitly #2; operator in address bits 0-1)
//        A8-AB xxxxxxxx Frequency number lower 8 bits
//        AC-AF --xxx--- Block (0-7)
//              -----xxx Frequency number upper 3 bits
//
//     Internal (fake) registers:
//        B8-BB --xxxxxx Latched frequency number upper bits (from A4-A7)
//        BC-BF --xxxxxx Latched frequency number upper bits (from AC-AF)
//

// system-wide registers
//static inline uint32_t opn_registers_test(void)       { return byte(0x21, 0, 8, 0); }
static inline uint32_t opn_registers_lfo_enable(void) { return byte(0x22, 3, 1, 0); }
static inline uint32_t opn_registers_lfo_rate(void)   { return byte(0x22, 0, 3, 0); }
uint32_t opn_registers_timer_a_value(void)            { return word(0x24, 0, 8, 0x25, 0, 2, 0); }
uint32_t opn_registers_timer_b_value(void)            { return byte(0x26, 0, 8, 0); }
uint32_t opn_registers_csm(void)                      { return (byte(0x27, 6, 2, 0) == 2); }
static inline uint32_t opn_registers_multi_freq(void) { return (byte(0x27, 6, 2, 0) != 0); }
uint32_t opn_registers_reset_timer_b(void)            { return byte(0x27, 5, 1, 0); }
uint32_t opn_registers_reset_timer_a(void)            { return byte(0x27, 4, 1, 0); }
uint32_t opn_registers_enable_timer_b(void)           { return byte(0x27, 3, 1, 0); }
uint32_t opn_registers_enable_timer_a(void)           { return byte(0x27, 2, 1, 0); }
uint32_t opn_registers_load_timer_b(void)             { return byte(0x27, 1, 1, 0); }
uint32_t opn_registers_load_timer_a(void)             { return byte(0x27, 0, 1, 0); }
static inline uint32_t opn_registers_multi_block_freq(uint32_t num)    { return word(0xac, 0, 6, 0xa8, 0, 8, num); }

// per-channel registers
static inline uint32_t opn_registers_ch_block_freq(uint32_t choffs)  { return word(0xa4, 0, 6, 0xa0, 0, 8, choffs); }
uint32_t opn_registers_ch_feedback(uint32_t choffs)                  { return byte(0xb0, 3, 3, choffs); }
uint32_t opn_registers_ch_algorithm(uint32_t choffs)                 { return byte(0xb0, 0, 3, choffs); }
uint32_t opn_registers_ch_output_any(uint32_t choffs)                { return byte(0xb4, 6, 2, choffs); }
uint32_t opn_registers_ch_output_0(uint32_t choffs)                  { return byte(0xb4, 7, 1, choffs); }
uint32_t opn_registers_ch_output_1(uint32_t choffs)                  { return byte(0xb4, 6, 1, choffs); }
uint32_t opn_registers_ch_output_2(uint32_t choffs)                  { (void)choffs; return 0; }
uint32_t opn_registers_ch_output_3(uint32_t choffs)                  { (void)choffs; return 0; }
static inline uint32_t opn_registers_ch_lfo_am_sens(uint32_t choffs) { return byte(0xb4, 4, 2, choffs); }
static inline uint32_t opn_registers_ch_lfo_pm_sens(uint32_t choffs) { return byte(0xb4, 0, 3, choffs); }

// per-operator registers
static inline uint32_t opn_registers_op_detune(uint32_t opoffs)        { return byte(0x30, 4, 3, opoffs); }
static inline uint32_t opn_registers_op_multiple(uint32_t opoffs)      { return byte(0x30, 0, 4, opoffs); }
static inline uint32_t opn_registers_op_total_level(uint32_t opoffs)   { return byte(0x40, 0, 7, opoffs); }
static inline uint32_t opn_registers_op_ksr(uint32_t opoffs)           { return byte(0x50, 6, 2, opoffs); }
static inline uint32_t opn_registers_op_attack_rate(uint32_t opoffs)   { return byte(0x50, 0, 5, opoffs); }
static inline uint32_t opn_registers_op_decay_rate(uint32_t opoffs)    { return byte(0x60, 0, 5, opoffs); }
uint32_t opn_registers_op_lfo_am_enable(uint32_t opoffs)               { return byte(0x60, 7, 1, opoffs); }
static inline uint32_t opn_registers_op_sustain_rate(uint32_t opoffs)  { return byte(0x70, 0, 5, opoffs); }
static inline uint32_t opn_registers_op_sustain_level(uint32_t opoffs) { return byte(0x80, 4, 4, opoffs); }
static inline uint32_t opn_registers_op_release_rate(uint32_t opoffs)  { return byte(0x80, 0, 4, opoffs); }
uint32_t opn_registers_op_ssg_eg_enable(uint32_t opoffs)               { return byte(0x90, 3, 1, opoffs); }
uint32_t opn_registers_op_ssg_eg_mode(uint32_t opoffs)                 { return byte(0x90, 0, 3, opoffs); }

//-------------------------------------------------
//  opn_registers - init
//-------------------------------------------------

void opn_registers_init(void) {
	m_lfo_counter = 0;
	m_lfo_am = 0;

	// create the waveforms
	for (uint32_t index = 0; index < WAVEFORM_LENGTH; index++)
		m_waveform[0][index] = abs_sin_attenuation(index) | (bitfield(index, 9, 1) << 15);
}


//-------------------------------------------------
//  reset - reset to initial state
//-------------------------------------------------

void opn_registers_reset(void)
{
	memset(&(m_regdata[0]), 0, REGISTERS);
	// enable output on both channels by default
	m_regdata[0xb4] = m_regdata[0xb5] =
	m_regdata[0xb6] = m_regdata[0x1b4] =
	m_regdata[0x1b5] = m_regdata[0x1b6] = 0xc0;
}


//-------------------------------------------------
//  read - handle reads to the register array
//-------------------------------------------------

/*static inline uint8_t opn_registers_read(uint16_t index) {
	return m_regdata[index];
}*/


//-------------------------------------------------
//  write - handle writes to the register array
//-------------------------------------------------

bool opn_registers_write(uint16_t index, uint8_t data, uint32_t *channel, uint32_t *opmask)
{
	assert(index < REGISTERS);

	// writes in the 0xa0-af/0x1a0-af region are handled as latched pairs
	// borrow unused registers 0xb8-bf/0x1b8-bf as temporary holding locations
	if ((index & 0xf0) == 0xa0)
	{
		if (bitfield(index, 0, 2) == 3)
			return false;

		uint32_t latchindex = 0xb8 | bitfield(index, 3, 1);
		latchindex |= index & 0x100;

		// writes to the upper half just latch (only low 6 bits matter)
		if (bitfield(index, 2, 1))
			m_regdata[latchindex] = data | 0x80;

		// writes to the lower half only commit if the latch is there
		else if (bitfield(m_regdata[latchindex], 7, 1))
		{
			m_regdata[index] = data;
			m_regdata[index | 4] = m_regdata[latchindex] & 0x3f;
			m_regdata[latchindex] = 0;
		}
		return false;
	}
	else if ((index & 0xf8) == 0xb8)
	{
		// registers 0xb8-0xbf are used internally
		return false;
	}

	// everything else is normal
	m_regdata[index] = data;

	// handle writes to the key on index
	if (index == 0x28)
	{
		*channel = bitfield(data, 0, 2);
		if (*channel == 3)
			return false;

		*channel += bitfield(data, 2, 1) * 3;
		*opmask = bitfield(data, 4, 4);
		return true;
	}
	return false;
}


//-------------------------------------------------
//  clock_noise_and_lfo - clock the noise and LFO,
//  handling clock division, depth, and waveform
//  computations
//-------------------------------------------------

int32_t opn_registers_clock_noise_and_lfo(void)
{
	// OPN has no noise generation

	// if LFO not enabled (not present on OPN), quick exit with 0s
	if (!opn_registers_lfo_enable())
	{
		m_lfo_counter = 0;

		// special case: if LFO is disabled on OPNA, it basically just keeps the counter
		// at 0; since position 0 gives an AM value of 0x3f, it is important to reflect
		// that here; for example, MegaDrive Venom plays some notes with LFO globally
		// disabled but enabling LFO on the operators, and it expects this added attenutation
		m_lfo_am = 0x3f;
		return 0;
	}

	// this table is based on converting the frequencies in the applications
	// manual to clock dividers, based on the assumption of a 7-bit LFO value
	static uint8_t const lfo_max_count[8] = { 109, 78, 72, 68, 63, 45, 9, 6 };
	uint32_t subcount = (uint8_t)(m_lfo_counter++);

	// when we cross the divider count, add enough to zero it and cause an
	// increment at bit 8; the 7-bit value lives from bits 8-14
	if (subcount >= lfo_max_count[opn_registers_lfo_rate()])
	{
		// note: to match the published values this should be 0x100 - subcount;
		// however, tests on the hardware and nuked bear out an off-by-one
		// error exists that causes the max LFO rate to be faster than published
		m_lfo_counter += 0x101 - subcount;
	}

	// AM value is 7 bits, staring at bit 8; grab the low 6 directly
	m_lfo_am = bitfield(m_lfo_counter, 8, 6);

	// first half of the AM period (bit 6 == 0) is inverted
	if (bitfield(m_lfo_counter, 8+6, 1) == 0)
		m_lfo_am ^= 0x3f;

	// PM value is 5 bits, starting at bit 10; grab the low 3 directly
	int32_t pm = bitfield(m_lfo_counter, 10, 3);

	// PM is reflected based on bit 3
	if (bitfield(m_lfo_counter, 10+3, 1))
		pm ^= 7;

	// PM is negated based on bit 4
	return bitfield(m_lfo_counter, 10+4, 1) ? -pm : pm;
}


void opn_registers_reset_lfo(void) {
	m_lfo_counter = 0;
}

// return LFO/noise states
uint32_t opn_registers_noise_state(void) {
	return 0;
}


//-------------------------------------------------
//  lfo_am_offset - return the AM offset from LFO
//  for the given channel
//-------------------------------------------------

uint32_t opn_registers_lfo_am_offset(uint32_t choffs)
{
	// shift value for AM sensitivity is [7, 3, 1, 0],
	// mapping to values of [0, 1.4, 5.9, and 11.8dB]
	uint32_t am_shift = (1 << (opn_registers_ch_lfo_am_sens(choffs) ^ 3)) - 1;

	// QUESTION: max sensitivity should give 11.8dB range, but this value
	// is directly added to an x.8 attenuation value, which will only give
	// 126/256 or ~4.9dB range -- what am I missing? The calculation below
	// matches several other emulators, including the Nuked implemenation.

	// raw LFO AM value on OPN is 0-3F, scale that up by a factor of 2
	// (giving 7 bits) before applying the final shift
	return (m_lfo_am << 1) >> am_shift;
}


//-------------------------------------------------
//  cache_operator_data - fill the operator cache
//  with prefetched data
//-------------------------------------------------

void opn_registers_cache_operator_data(uint32_t choffs, uint32_t opoffs, opdata_cache *cache)
{
	// set up the easy stuff
	cache->waveform = &(m_waveform[0][0]);

	// get frequency from the channel
	uint32_t block_freq = cache->block_freq = opn_registers_ch_block_freq(choffs);

	// if multi-frequency mode is enabled and this is channel 2,
	// fetch one of the special frequencies
	if (opn_registers_multi_freq() && choffs == 2)
	{
		if (opoffs == 2)
			block_freq = cache->block_freq = opn_registers_multi_block_freq(1);
		else if (opoffs == 10)
			block_freq = cache->block_freq = opn_registers_multi_block_freq(2);
		else if (opoffs == 6)
			block_freq = cache->block_freq = opn_registers_multi_block_freq(0);
	}

	// compute the keycode: block_freq is:
	//
	//     BBBFFFFFFFFFFF
	//     ^^^^???
	//
	// the 5-bit keycode uses the top 4 bits plus a magic formula
	// for the final bit
	uint32_t keycode = bitfield(block_freq, 10, 4) << 1;

	// lowest bit is determined by a mix of next lower FNUM bits
	// according to this equation from the YM2608 manual:
	//
	//   (F11 & (F10 | F9 | F8)) | (!F11 & F10 & F9 & F8)
	//
	// for speed, we just look it up in a 16-bit constant
	keycode |= bitfield(0xfe80, bitfield(block_freq, 7, 4), 1);

	// detune adjustment
	cache->detune = detune_adjustment(opn_registers_op_detune(opoffs), keycode);

	// multiple value, as an x.1 value (0 means 0.5)
	cache->multiple = opn_registers_op_multiple(opoffs) * 2;
	if (cache->multiple == 0)
		cache->multiple = 1;

	// phase step, or PHASE_STEP_DYNAMIC if PM is active; this depends on
	// block_freq, detune, and multiple, so compute it after we've done those
	if (opn_registers_lfo_enable() == 0 || opn_registers_ch_lfo_pm_sens(choffs) == 0)
		cache->phase_step = opn_registers_compute_phase_step(choffs, cache, 0);
	else
		cache->phase_step = PHASE_STEP_DYNAMIC;

	// total level, scaled by 8
	cache->total_level = opn_registers_op_total_level(opoffs) << 3;

	// 4-bit sustain level, but 15 means 31 so effectively 5 bits
	cache->eg_sustain = opn_registers_op_sustain_level(opoffs);
	cache->eg_sustain |= (cache->eg_sustain + 1) & 0x10;
	cache->eg_sustain <<= 5;

	// determine KSR adjustment for enevlope rates
	uint32_t ksrval = keycode >> (opn_registers_op_ksr(opoffs) ^ 3);
	cache->eg_rate[EG_ATTACK] = effective_rate(opn_registers_op_attack_rate(opoffs) * 2, ksrval);
	cache->eg_rate[EG_DECAY] = effective_rate(opn_registers_op_decay_rate(opoffs) * 2, ksrval);
	cache->eg_rate[EG_SUSTAIN] = effective_rate(opn_registers_op_sustain_rate(opoffs) * 2, ksrval);
	cache->eg_rate[EG_RELEASE] = effective_rate(opn_registers_op_release_rate(opoffs) * 4 + 2, ksrval);
}


//-------------------------------------------------
//  compute_phase_step - compute the phase step
//-------------------------------------------------

uint32_t opn_registers_compute_phase_step(uint32_t choffs, const opdata_cache *cache, int32_t lfo_raw_pm)
{
	// OPN phase calculation has only a single detune parameter
	// and uses FNUMs instead of keycodes

	// extract frequency number (low 11 bits of block_freq)
	uint32_t fnum = bitfield(cache->block_freq, 0, 11) << 1;

	// if there's a non-zero PM sensitivity, compute the adjustment
	uint32_t pm_sensitivity = opn_registers_ch_lfo_pm_sens(choffs);
	if (pm_sensitivity != 0)
	{
		// apply the phase adjustment based on the upper 7 bits
		// of FNUM and the PM depth parameters
		fnum += opn_lfo_pm_phase_adjustment(bitfield(cache->block_freq, 4, 7), pm_sensitivity, lfo_raw_pm);

		// keep fnum to 12 bits
		fnum &= 0xfff;
	}

	// apply block shift to compute phase step
	uint32_t block = bitfield(cache->block_freq, 11, 3);
	uint32_t phase_step = (fnum << block) >> 2;

	// apply detune based on the keycode
	phase_step += cache->detune;

	// clamp to 17 bits in case detune overflows
	// QUESTION: is this specific to the YM2612/3438?
	phase_step &= 0x1ffff;

	// apply frequency multiplier (which is cached as an x.1 value)
	return (phase_step * cache->multiple) >> 1;
}


//-------------------------------------------------
//  states - Read/write OPN state data
//-------------------------------------------------

void opn_state_load(uint8_t *st) {
	m_lfo_counter = geo_serial_pop32(st);
	m_lfo_am = geo_serial_pop8(st);

	for (int i = 0; i < REGISTERS; ++i)
		m_regdata[i] = geo_serial_pop8(st);

	m_address = geo_serial_pop16(st);
	m_eos_status = geo_serial_pop8(st);
	m_flag_mask = geo_serial_pop8(st);

	for (int i = 0; i < 3; ++i)
		m_last_fm[i] = geo_serial_pop32(st);

	m_dac_data = geo_serial_pop16(st);
	m_dac_enable = geo_serial_pop8(st);

	m_env_counter = geo_serial_pop32(st);
	m_status = geo_serial_pop8(st);
	m_clock_prescale = geo_serial_pop8(st);
	m_irq_mask = geo_serial_pop8(st);
	m_irq_state = geo_serial_pop8(st);
	m_timer_running[0] = geo_serial_pop8(st);
	m_timer_running[1] = geo_serial_pop8(st);
	m_total_clocks = geo_serial_pop8(st);

	for (int i = 0; i < CHANNELS; ++i)
	{
		m_channel[i].m_feedback[0] = geo_serial_pop16(st);
		m_channel[i].m_feedback[1] = geo_serial_pop16(st);
		m_channel[i].m_feedback_in = geo_serial_pop16(st);
	}

	for (int i = 0; i < OPERATORS; ++i)
	{
		m_operator[i].m_phase = geo_serial_pop32(st);
		m_operator[i].m_env_attenuation = geo_serial_pop16(st);
		m_operator[i].m_env_state = geo_serial_pop32(st);
		m_operator[i].m_ssg_inverted = geo_serial_pop8(st);
		m_operator[i].m_key_state = geo_serial_pop8(st);
		m_operator[i].m_keyon_live = geo_serial_pop8(st);
	}

	fm_engine_invalidate_caches();
}

void opn_state_save(uint8_t *st) {
	geo_serial_push32(st, m_lfo_counter);
	geo_serial_push8(st, m_lfo_am);

	for (int i = 0; i < REGISTERS; ++i)
		geo_serial_push8(st, m_regdata[i]);

	geo_serial_push16(st, m_address);
	geo_serial_push8(st, m_eos_status);
	geo_serial_push8(st, m_flag_mask);

	for (int i = 0; i < 3; ++i)
		geo_serial_push32(st, m_last_fm[i]);

	geo_serial_push16(st, m_dac_data);
	geo_serial_push8(st, m_dac_enable);

	geo_serial_push32(st, m_env_counter);
	geo_serial_push8(st, m_status);
	geo_serial_push8(st, m_clock_prescale);
	geo_serial_push8(st, m_irq_mask);
	geo_serial_push8(st, m_irq_state);
	geo_serial_push8(st, m_timer_running[0]);
	geo_serial_push8(st, m_timer_running[1]);
	geo_serial_push8(st, m_total_clocks);

	for (int i = 0; i < CHANNELS; ++i)
	{
		geo_serial_push16(st, m_channel[i].m_feedback[0]);
		geo_serial_push16(st, m_channel[i].m_feedback[1]);
		geo_serial_push16(st, m_channel[i].m_feedback_in);
	}

	for (int i = 0; i < OPERATORS; ++i)
	{
		geo_serial_push32(st, m_operator[i].m_phase);
		geo_serial_push16(st, m_operator[i].m_env_attenuation);
		geo_serial_push32(st, m_operator[i].m_env_state);
		geo_serial_push8(st, m_operator[i].m_ssg_inverted);
		geo_serial_push8(st, m_operator[i].m_key_state);
		geo_serial_push8(st, m_operator[i].m_keyon_live);
	}
}

//*********************************************************
//  SSG RESAMPLER
//*********************************************************

//-------------------------------------------------
//  add_last - helper to add the last computed
//  value to the sums, applying the given scale
//-------------------------------------------------

static inline void ssg_resampler_add_last(int32_t *sum0, int32_t *sum1, int32_t *sum2, int32_t scale)
{
	*sum0 += m_ssg_resampler_last[0] * scale;
	*sum1 += m_ssg_resampler_last[1] * scale;
	*sum2 += m_ssg_resampler_last[2] * scale;
}


//-------------------------------------------------
//  clock_and_add - helper to clock a new value
//  and then add it to the sums, applying the
//  given scale
//-------------------------------------------------

static inline void ssg_resampler_clock_and_add(int32_t *sum0, int32_t *sum1, int32_t *sum2, int32_t scale)
{
	ssg_engine_clock();
	ssg_engine_output(m_ssg_resampler_last);
	ssg_resampler_add_last(sum0, sum1, sum2, scale);
}


//-------------------------------------------------
//  write_to_output - helper to write the sums to
//  the appropriate outputs, applying the given
//  divisor to the final result
//-------------------------------------------------

static inline void ssg_resampler_write_to_output(int32_t *output, int32_t sum0, int32_t sum1, int32_t sum2, int32_t divisor)
{
	// mixing to one, apply a 2/3 factor to prevent overflow
	output[2] = (sum0 + sum1 + sum2) * 2 / (3 * divisor);

	// track the sample index here
	m_ssg_resampler_sampindex++;
}


//-------------------------------------------------
//  resample_2_1 - resample SSG output to the
//  target at a rate of 1 SSG sample to every
//  2 output samples
//-------------------------------------------------

static void ssg_resampler_resample_2_1(int32_t *output)
{
	if (m_ssg_resampler_sampindex % 2 == 0)
	{
		ssg_engine_clock();
		ssg_engine_output(m_ssg_resampler_last);
	}
	ssg_resampler_write_to_output(output, m_ssg_resampler_last[0],
		m_ssg_resampler_last[1], m_ssg_resampler_last[2], 1);
}


//-------------------------------------------------
//  resample_2_9 - resample SSG output to the
//  target at a rate of 9 SSG samples to every
//  2 output samples
//-------------------------------------------------

static void ssg_resampler_resample_2_9(int32_t *output)
{
	int32_t sum0 = 0, sum1 = 0, sum2 = 0;
	if (bitfield(m_ssg_resampler_sampindex, 0, 1) != 0)
		ssg_resampler_add_last(&sum0, &sum1, &sum2, 1);
	ssg_resampler_clock_and_add(&sum0, &sum1, &sum2, 2);
	ssg_resampler_clock_and_add(&sum0, &sum1, &sum2, 2);
	ssg_resampler_clock_and_add(&sum0, &sum1, &sum2, 2);
	ssg_resampler_clock_and_add(&sum0, &sum1, &sum2, 2);
	if (bitfield(m_ssg_resampler_sampindex, 0, 1) == 0)
		ssg_resampler_clock_and_add(&sum0, &sum1, &sum2, 1);
	ssg_resampler_write_to_output(output, sum0, sum1, sum2, 9);
}


//-------------------------------------------------
//  resample_nop - no-op resampler
//-------------------------------------------------

static void ssg_resampler_resample_nop(int32_t *output)
{
	if (output) { }

	// nothing to do except increment the sample index
	++m_ssg_resampler_sampindex;
}


//-------------------------------------------------
//  ssg_resampler - init
//-------------------------------------------------

static inline void ssg_resampler_init(void)
{
	m_ssg_resampler_sampindex = 0;
	ssg_resampler_resample = &ssg_resampler_resample_nop;
	m_ssg_resampler_last[0] = m_ssg_resampler_last[1] = m_ssg_resampler_last[2] = 0;
}


//-------------------------------------------------
//  configure - configure a new ratio
//-------------------------------------------------

static inline void ssg_resampler_configure(uint8_t outsamples, uint8_t srcsamples)
{
	switch (outsamples * 10 + srcsamples)
	{
		case 2*10 + 1:	/* 2:1 */	ssg_resampler_resample = &ssg_resampler_resample_2_1; break;
		case 2*10 + 9:	/* 2:9 */	ssg_resampler_resample = &ssg_resampler_resample_2_9; break;
		default: assert(false); break;
	}
}


//*********************************************************
//  YM2610
//*********************************************************

//-------------------------------------------------
//  update_prescale - update the prescale value,
//  recomputing derived values
//-------------------------------------------------

static inline void ym2610_update_prescale(void)
{
	// Fidelity:   ---- minimum ----    ---- medium -----    ---- maximum-----
	//              rate = clock/144     rate = clock/144     rate = clock/16
	// Prescale    FM rate  SSG rate    FM rate  SSG rate    FM rate  SSG rate
	//     6          1:1     2:9          1:1     2:9         9:1     2:1

	// compute the number of FM samples per output sample, and select the
	// resampler function
	if (m_fidelity == OPN_FIDELITY_MIN || m_fidelity == OPN_FIDELITY_MED)
	{
		m_fm_samples_per_output = 1;
		ssg_resampler_configure(2, 9);
	}
	else
	{
		m_fm_samples_per_output = 9;
		ssg_resampler_configure(2, 1);
	}
}


//-------------------------------------------------
//  ym2610 - init
//-------------------------------------------------

void ym2610_init(void)
{
	m_fidelity = OPN_FIDELITY_MAX;
	m_address = 0;
	m_fm_mask = 0x36;
	m_eos_status = 0x00;
	m_flag_mask = EOS_FLAGS_MASK;
	ssg_engine_init();
	ssg_resampler_init();
	adpcm_a_engine_init();
	adpcm_b_engine_init();
	ym2610_update_prescale();
}


// pass-through helpers
uint32_t ym2610_sample_rate(uint32_t input_clock) {
	switch (m_fidelity)
	{
		case OPN_FIDELITY_MIN:	return input_clock / 144;
		case OPN_FIDELITY_MED:	return input_clock / 144;
		default:
		case OPN_FIDELITY_MAX:	return input_clock / 16;
	}
}


uint32_t ym2610_ssg_effective_clock(uint32_t input_clock) {
	return input_clock / 4;
}


void ym2610_invalidate_caches(void) {
	fm_engine_invalidate_caches();
}


void ym2610_set_fidelity(uint32_t fidelity) {
	m_fidelity = fidelity;
	ym2610_update_prescale();
}


//-------------------------------------------------
//  reset - reset the system
//-------------------------------------------------

void ym2610_reset(void)
{
	// reset the engines
	fm_engine_reset();
	ssg_engine_reset();
	adpcm_a_engine_reset();
	adpcm_b_engine_reset();

	// initialize our special interrupt states
	m_eos_status = 0x00;
	m_flag_mask = EOS_FLAGS_MASK;
}


//-------------------------------------------------
//  read_status - read the status register
//-------------------------------------------------

static inline uint8_t ym2610_read_status(void)
{
	uint8_t result = fm_engine_status() & (STATUS_TIMERA | STATUS_TIMERB);
	if (ymfm_is_busy())
		result |= STATUS_BUSY;
	return result;
}


//-------------------------------------------------
//  read_data - read the data register
//-------------------------------------------------

static inline uint8_t ym2610_read_data(void)
{
	uint8_t result = 0;
	if (m_address < 0x0e)
	{
		// 00-0D: Read from SSG
		result = ssg_engine_read(m_address & 0x0f);
	}
	else if (m_address < 0x10)
	{
		// 0E-0F: I/O ports not supported
		result = 0xff;
	}
	else if (m_address == 0xff)
	{
		// FF: ID code
		result = 1;
	}
	return result;
}


//-------------------------------------------------
//  read_status_hi - read the extended status
//  register
//-------------------------------------------------

static inline uint8_t ym2610_read_status_hi(void)
{
	return m_eos_status & m_flag_mask;
}


//-------------------------------------------------
//  read_data_hi - read the upper data register
//-------------------------------------------------

static inline uint8_t ym2610_read_data_hi(void)
{
	uint8_t result = 0;
	return result;
}


//-------------------------------------------------
//  read - handle a read from the device
//-------------------------------------------------

uint8_t ym2610_read(uint32_t offset)
{
	uint8_t result = 0;
	switch (offset & 3)
	{
		case 0: // status port, YM2203 compatible
			result = ym2610_read_status();
			break;

		case 1: // data port (only SSG)
			result = ym2610_read_data();
			break;

		case 2: // status port, extended
			result = ym2610_read_status_hi();
			break;

		case 3: // ADPCM-B data
			result = ym2610_read_data_hi();
			break;
	}
	return result;
}


//-------------------------------------------------
//  write_address - handle a write to the address
//  register
//-------------------------------------------------

static inline void ym2610_write_address(uint8_t data)
{
	// just set the address
	m_address = data;
}


//-------------------------------------------------
//  write - handle a write to the data register
//-------------------------------------------------

static inline void ym2610_write_data(uint8_t data)
{
	// ignore if paired with upper address
	if (bitfield(m_address, 8, 1))
		return;

	if (m_address < 0x0e)
	{
		// 00-0D: write to SSG
		ssg_engine_write(m_address & 0x0f, data);
	}
	else if (m_address < 0x10)
	{
		// 0E-0F: I/O ports not supported
	}
	else if (m_address < 0x1c)
	{
		// 10-1B: write to ADPCM-B
		// YM2610 effectively forces external mode on, and disables recording
		if (m_address == 0x10)
			data = (data | 0x20) & ~0x40;
		adpcm_b_engine_write(m_address & 0x0f, data);
	}
	else if (m_address == 0x1c)
	{
		// 1C: EOS flag reset
		m_flag_mask = ~data & EOS_FLAGS_MASK;
		m_eos_status &= ~(data & EOS_FLAGS_MASK);
	}
	else
	{
		// 1D-FF: write to FM
		fm_engine_write(m_address, data);
	}

	// mark busy for a bit
	ymfm_set_busy_end(32 * fm_engine_clock_prescale());
}


//-------------------------------------------------
//  write_address_hi - handle a write to the upper
//  address register
//-------------------------------------------------

static inline void ym2610_write_address_hi(uint8_t data)
{
	// just set the address
	m_address = 0x100 | data;
}


//-------------------------------------------------
//  write_data_hi - handle a write to the upper
//  data register
//-------------------------------------------------

static inline void ym2610_write_data_hi(uint8_t data)
{
	// ignore if paired with upper address
	if (!bitfield(m_address, 8, 1))
		return;

	if (m_address < 0x130)
	{
		// 100-12F: write to ADPCM-A
		adpcm_a_engine_write(m_address & 0x3f, data);
	}
	else
	{
		// 130-1FF: write to FM
		fm_engine_write(m_address, data);
	}

	// mark busy for a bit
	ymfm_set_busy_end(32 * fm_engine_clock_prescale());
}


//-------------------------------------------------
//  write - handle a write to the register
//  interface
//-------------------------------------------------

void ym2610_write(uint32_t offset, uint8_t data)
{
	switch (offset & 3)
	{
		case 0: // address port
			ym2610_write_address(data);
			break;

		case 1: // data port
			ym2610_write_data(data);
			break;

		case 2: // upper address port
			ym2610_write_address_hi(data);
			break;

		case 3: // upper data port
			ym2610_write_data_hi(data);
			break;
	}
}


//-------------------------------------------------
//  clock_fm_and_adpcm - clock FM and ADPCM state
//-------------------------------------------------

static inline void ym2610_clock_fm_and_adpcm(void)
{
	// clock the system
	uint32_t env_counter = fm_engine_clock(m_fm_mask);

	// clock the ADPCM-A engine on every envelope cycle
	if (bitfield(env_counter, 0, 2) == 0)
		m_eos_status |= adpcm_a_engine_clock(0x3f);

	// clock the ADPCM-B engine every cycle
	adpcm_b_engine_clock();

	// we track the last ADPCM-B EOS value in bit 6 (which is hidden from callers);
	// if it changed since the last sample, update the visible EOS state in bit 7
	uint8_t live_eos = ((adpcm_b_engine_status() & STATUS_EOS) != 0) ? 0x40 : 0x00;
	if (((live_eos ^ m_eos_status) & 0x40) != 0)
		m_eos_status = (m_eos_status & ~0xc0) | live_eos | (live_eos << 1);

	// update the FM content; OPNB is 13-bit with no intermediate clipping
	m_last_fm[0] = m_last_fm[1] = m_last_fm[2] = 0;
	fm_engine_output(m_last_fm, 1, 32767, m_fm_mask);

	// mix in the ADPCM and clamp
	adpcm_a_engine_output(m_last_fm, 0x3f);
	adpcm_b_engine_output(m_last_fm, 1);

	m_last_fm[0] = clamp(m_last_fm[0], -32768, 32767);
	m_last_fm[1] = clamp(m_last_fm[1], -32768, 32767);
	m_last_fm[2] = clamp(m_last_fm[2], -32768, 32767);
}


//-------------------------------------------------
//  generate - generate one sample of sound
//-------------------------------------------------

void ym2610_generate(int32_t *output)
{
	// FM output is just repeated the prescale number of times
	if (m_ssg_resampler_sampindex % m_fm_samples_per_output == 0)
		ym2610_clock_fm_and_adpcm();
	output[0] = m_last_fm[0];
	output[1] = m_last_fm[1];

	// resample the SSG as configured
	ssg_resampler_resample(output);
}


//*********************************************************
//  YM2612
//*********************************************************

//-------------------------------------------------
//  ym2612 - constructor
//-------------------------------------------------

void ym2612_init(void)
{
	m_address = 0;
	m_dac_data = 0;
	m_dac_enable = 0;
}


uint32_t ym2612_sample_rate(uint32_t input_clock) {
	return fm_engine_sample_rate(input_clock);
}


void ym2612_invalidate_caches(void) {
	fm_engine_invalidate_caches();
}


//-------------------------------------------------
//  reset - reset the system
//-------------------------------------------------

void ym2612_reset(void)
{
	// reset the engines
	fm_engine_reset();
}


//-------------------------------------------------
//  read_status - read the status register
//-------------------------------------------------

static inline uint8_t ym2612_read_status(void)
{
	uint8_t result = fm_engine_status();
	if (ymfm_is_busy())
		result |= STATUS_BUSY;
	return result;
}


//-------------------------------------------------
//  read - handle a read from the device
//-------------------------------------------------

uint8_t ym2612_read(uint32_t offset)
{
	uint8_t result = 0;
	switch (offset & 3)
	{
		case 0: // status port, YM2203 compatible
			result = ym2612_read_status();
			break;

		case 1: // data port (unused)
		case 2: // status port, extended
		case 3: // data port (unused)
			//debug::log_unexpected_read_write("Unexpected read from YM2612 offset %d\n", offset & 3);
			break;
	}
	return result;
}


//-------------------------------------------------
//  write_address - handle a write to the address
//  register
//-------------------------------------------------

static inline void ym2612_write_address(uint8_t data)
{
	// just set the address
	m_address = data;
}


//-------------------------------------------------
//  write_data - handle a write to the data
//  register
//-------------------------------------------------

static inline void ym2612_write_data(uint8_t data)
{
	// ignore if paired with upper address
	if (bitfield(m_address, 8, 1))
		return;

	if (m_address == 0x2a)
	{
		// 2A: DAC data (most significant 8 bits)
		m_dac_data = (m_dac_data & ~0x1fe) | ((data ^ 0x80) << 1);
	}
	else if (m_address == 0x2b)
	{
		// 2B: DAC enable (bit 7)
		m_dac_enable = bitfield(data, 7, 1);
	}
	else if (m_address == 0x2c)
	{
		// 2C: test/low DAC bit
		m_dac_data = (m_dac_data & ~1) | bitfield(data, 3, 1);
	}
	else
	{
		// 00-29, 2D-FF: write to FM
		fm_engine_write(m_address, data);
	}

	// mark busy for a bit
	ymfm_set_busy_end(32 * fm_engine_clock_prescale());
}


//-------------------------------------------------
//  write_address_hi - handle a write to the upper
//  address register
//-------------------------------------------------

static inline void ym2612_write_address_hi(uint8_t data)
{
	// just set the address
	m_address = 0x100 | data;
}


//-------------------------------------------------
//  write_data_hi - handle a write to the upper
//  data register
//-------------------------------------------------

static inline void ym2612_write_data_hi(uint8_t data)
{
	// ignore if paired with upper address
	if (!bitfield(m_address, 8, 1))
		return;

	// 100-1FF: write to FM
	fm_engine_write(m_address, data);

	// mark busy for a bit
	ymfm_set_busy_end(32 * fm_engine_clock_prescale());
}


//-------------------------------------------------
//  write - handle a write to the register
//  interface
//-------------------------------------------------

void ym2612_write(uint32_t offset, uint8_t data)
{
	switch (offset & 3)
	{
		case 0: // address port
			ym2612_write_address(data);
			break;

		case 1: // data port
			ym2612_write_data(data);
			break;

		case 2: // upper address port
			ym2612_write_address_hi(data);
			break;

		case 3: // upper data port
			ym2612_write_data_hi(data);
			break;
	}
}


//-------------------------------------------------
//  generate - generate one sample of sound
//-------------------------------------------------

void ym2612_generate(int32_t *output)
{
	// clock the system
	fm_engine_clock(ALL_CHANNELS);

	// sum individual channels to apply DAC discontinuity on each
	output[0] = output[1] = 0;
	int32_t temp[2];

	// first do FM-only channels; OPN2 is 9-bit with intermediate clipping
	int const last_fm_channel = m_dac_enable ? 5 : 6;
	for (int chan = 0; chan < last_fm_channel; chan++)
	{
		temp[0] = temp[1] = 0;
		fm_engine_output(temp, 5, 256, 1 << chan);
		output[0] += dac_discontinuity(temp[0]);
		output[1] += dac_discontinuity(temp[1]);
	}

	// add in DAC
	if (m_dac_enable)
	{
		// DAC enabled: start with DAC value then add the first 5 channels only
		int32_t dacval = dac_discontinuity((int16_t)(m_dac_data << 7) >> 7);
		output[0] += opn_registers_ch_output_0(0x102) ? dacval : dac_discontinuity(0);
		output[1] += opn_registers_ch_output_1(0x102) ? dacval : dac_discontinuity(0);
	}

	// output is technically multiplexed rather than mixed, but that requires
	// a better sound mixer than we usually have, so just average over the six
	// channels; also apply a 64/65 factor to account for the discontinuity
	// adjustment above
	output[0] = (output[0] * 128) * 64 / (6 * 65);
	output[1] = (output[1] * 128) * 64 / (6 * 65);
}


//-------------------------------------------------
//  generate - generate one sample of YM3438 sound
//-------------------------------------------------

void ym3438_generate(int32_t *output)
{
	// clock the system
	fm_engine_clock(ALL_CHANNELS);

	// first do FM-only channels; OPN2C is 9-bit with intermediate clipping
	if (!m_dac_enable)
	{
		// DAC disabled: all 6 channels sum together
		output[0] = output[1] = 0;
		fm_engine_output(output, 5, 256, ALL_CHANNELS);
	}
	else
	{
		// DAC enabled: start with DAC value then add the first 5 channels only
		int32_t dacval = (int16_t)(m_dac_data << 7) >> 7;
		output[0] = opn_registers_ch_output_0(0x102) ? dacval : 0;
		output[1] = opn_registers_ch_output_1(0x102) ? dacval : 0;
		fm_engine_output(output, 5, 256, ALL_CHANNELS ^ (1 << 5));
	}

	// YM3438 doesn't have the same DAC discontinuity, though its output is
	// multiplexed like the YM2612
	output[0] = (output[0] * 128) / 6;
	output[1] = (output[1] * 128) / 6;
}
