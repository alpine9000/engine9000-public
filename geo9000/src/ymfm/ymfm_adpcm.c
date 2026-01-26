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

#include "ymfm.h"
#include "ymfm_adpcm.h"

// ADPCM Channel A
#define CHANNELS_A 6
#define REGISTERS_A 0x30

// ADPCM Channel B
#define REGISTERS_B 0x11
#define STEP_MIN 127
#define STEP_MAX 24576

static uint8_t m_regdata_a[REGISTERS_A];         // register data
static uint8_t m_regdata_b[REGISTERS_B];         // register data

// internal state
static adpcm_a_channel m_channel_a[CHANNELS_A];  // array of channels
static adpcm_b_channel m_channel_b;              // channel

// hacks
static bool accum_wrap = true; // default to real hardware behaviour

//*********************************************************
// ADPCM "A" REGISTERS
//*********************************************************
//
// ADPCM-A register map:
//
//      System-wide registers:
//           00 x------- Dump (disable=1) or keyon (0) control
//              --xxxxxx Mask of channels to dump or keyon
//           01 --xxxxxx Total level
//           02 xxxxxxxx Test register
//        08-0D x------- Pan left
//              -x------ Pan right
//              ---xxxxx Instrument level
//        10-15 xxxxxxxx Start address (low)
//        18-1D xxxxxxxx Start address (high)
//        20-25 xxxxxxxx End address (low)
//        28-2D xxxxxxxx End address (high)
//

//-------------------------------------------------
//  reset - reset the register state
//-------------------------------------------------

static inline void adpcm_a_registers_reset(void)
{
	memset(&m_regdata_a[0], 0, REGISTERS_A);

	// initialize the pans to on by default, and max instrument volume;
	// some neogeo homebrews (for example ffeast) rely on this
	m_regdata_a[0x08] = m_regdata_a[0x09] = m_regdata_a[0x0a] =
	m_regdata_a[0x0b] = m_regdata_a[0x0c] = m_regdata_a[0x0d] = 0xdf;
}

// direct read/write access
static inline void adpcm_a_registers_write(uint32_t index, uint8_t data) {
	m_regdata_a[index] = data;
}

// system-wide registers
/*static inline uint32_t adpcm_a_registers_dump(void) {
	return bitfield(m_regdata_a[0x00], 7, 1);
}

static inline uint32_t adpcm_a_registers_dump_mask(void) {
	return bitfield(m_regdata_a[0x00], 0, 6);
}*/

static inline uint32_t adpcm_a_registers_total_level(void) {
	return bitfield(m_regdata_a[0x01], 0, 6);
}

/*static inline uint32_t adpcm_a_registers_test(void) {
	return m_regdata_a[0x02];
}*/

// per-channel registers
static inline uint32_t adpcm_a_registers_ch_pan_left(uint32_t choffs) {
	return bitfield(m_regdata_a[choffs + 0x08], 7, 1);
}

static inline uint32_t adpcm_a_registers_ch_pan_right(uint32_t choffs) {
	return bitfield(m_regdata_a[choffs + 0x08], 6, 1);
}

static inline uint32_t adpcm_a_registers_ch_instrument_level(uint32_t choffs) {
	return bitfield(m_regdata_a[choffs + 0x08], 0, 5);
}

static inline uint32_t adpcm_a_registers_ch_start(uint32_t choffs) {
	return m_regdata_a[choffs + 0x10] | (m_regdata_a[choffs + 0x18] << 8);
}

static inline uint32_t adpcm_a_registers_ch_end(uint32_t choffs) {
	return m_regdata_a[choffs + 0x20] | (m_regdata_a[choffs + 0x28] << 8);
}

//*********************************************************
// ADPCM "A" CHANNEL
//*********************************************************

//-------------------------------------------------
//  adpcm_a_channel - constructor
//-------------------------------------------------

static inline void adpcm_a_channel_init(uint32_t choffs)
{
	m_channel_a[choffs].m_choffs = choffs;
	m_channel_a[choffs].m_address_shift =8;
	m_channel_a[choffs].m_playing = 0;
	m_channel_a[choffs].m_curnibble = 0;
	m_channel_a[choffs].m_curbyte = 0;
	m_channel_a[choffs].m_curaddress = 0;
	m_channel_a[choffs].m_accumulator = 0;
	m_channel_a[choffs].m_step_index = 0;
}

//-------------------------------------------------
//  reset - reset the channel state
//-------------------------------------------------

static inline void adpcm_a_channel_reset(uint32_t choffs)
{
	m_channel_a[choffs].m_playing = 0;
	m_channel_a[choffs].m_curnibble = 0;
	m_channel_a[choffs].m_curbyte = 0;
	m_channel_a[choffs].m_curaddress = 0;
	m_channel_a[choffs].m_accumulator = 0;
	m_channel_a[choffs].m_step_index = 0;
}


//-------------------------------------------------
//  keyonoff - signal key on/off
//-------------------------------------------------

static inline void adpcm_a_channel_keyonoff(uint32_t choffs, bool on)
{
	// QUESTION: repeated key ons restart the sample?
	m_channel_a[choffs].m_playing = on;
	if (m_channel_a[choffs].m_playing)
	{
		m_channel_a[choffs].m_curaddress =
			adpcm_a_registers_ch_start(m_channel_a[choffs].m_choffs) <<
			m_channel_a[choffs].m_address_shift;
		m_channel_a[choffs].m_curnibble = 0;
		m_channel_a[choffs].m_curbyte = 0;
		m_channel_a[choffs].m_accumulator = 0;
		m_channel_a[choffs].m_step_index = 0;
	}
}


//-------------------------------------------------
//  clock - master clocking function
//-------------------------------------------------

static inline bool adpcm_a_channel_clock(uint32_t choffs)
{
	// if not playing, just output 0
	if (m_channel_a[choffs].m_playing == 0)
	{
		m_channel_a[choffs].m_accumulator = 0;
		return false;
	}

	// if we're about to read nibble 0, fetch the data
	uint8_t data;
	if (m_channel_a[choffs].m_curnibble == 0)
	{
		// stop when we hit the end address; apparently only low 20 bits are used for
		// comparison on the YM2610: this affects sample playback in some games, for
		// example twinspri character select screen music will skip some samples if
		// this is not correct
		//
		// note also: end address is inclusive, so wait until we are about to fetch
		// the sample just after the end before stopping; this is needed for nitd's
		// jump sound, for example
		uint32_t end =
			(adpcm_a_registers_ch_end(m_channel_a[choffs].m_choffs) + 1) <<
			m_channel_a[choffs].m_address_shift;

		if (((m_channel_a[choffs].m_curaddress ^ end) & 0xfffff) == 0)
		{
			m_channel_a[choffs].m_playing = m_channel_a[choffs].m_accumulator = 0;
			return true;
		}

		m_channel_a[choffs].m_curbyte =
			ymfm_external_read(ACCESS_ADPCM_A, m_channel_a[choffs].m_curaddress++);
		data = m_channel_a[choffs].m_curbyte >> 4;
		m_channel_a[choffs].m_curnibble = 1;
	}

	// otherwise just extract from the previosuly-fetched byte
	else
	{
		data = m_channel_a[choffs].m_curbyte & 0xf;
		m_channel_a[choffs].m_curnibble = 0;
	}

	// compute the ADPCM delta
	static uint16_t const s_steps[49] =
	{
		 16,  17,   19,   21,   23,   25,   28,
		 31,  34,   37,   41,   45,   50,   55,
		 60,  66,   73,   80,   88,   97,  107,
		118, 130,  143,  157,  173,  190,  209,
		230, 253,  279,  307,  337,  371,  408,
		449, 494,  544,  598,  658,  724,  796,
		876, 963, 1060, 1166, 1282, 1411, 1552
	};
	int32_t delta = (2 * bitfield(data, 0, 3) + 1) * s_steps[m_channel_a[choffs].m_step_index] / 8;
	if (bitfield(data, 3, 1))
		delta = -delta;

	// the 12-bit accumulator wraps on the ym2610 and ym2608 (like the msm5205)
	if (accum_wrap)
	{
		m_channel_a[choffs].m_accumulator = (m_channel_a[choffs].m_accumulator + delta) & 0xfff;
	}
	else {
		// hack to fix games with glitchy audio
		m_channel_a[choffs].m_accumulator = clamp(m_channel_a[choffs].m_accumulator + delta, -2048, 2047);
	}

	// adjust ADPCM step
	static int8_t const s_step_inc[8] = { -1, -1, -1, -1, 2, 5, 7, 9 };
	m_channel_a[choffs].m_step_index =
		clamp(m_channel_a[choffs].m_step_index + s_step_inc[bitfield(data, 0, 3)], 0, 48);

	return false;
}


//-------------------------------------------------
//  output - return the computed output value, with
//  panning applied
//-------------------------------------------------

static inline void adpcm_a_channel_output(uint32_t choffs, int32_t *output)
{
	// volume combines instrument and total levels
	int vol = (adpcm_a_registers_ch_instrument_level(m_channel_a[choffs].m_choffs) ^ 0x1f) +
		(adpcm_a_registers_total_level() ^ 0x3f);

	// if combined is maximum, don't add to outputs
	if (vol >= 63)
		return;

	// convert into a shift and a multiplier
	// QUESTION: verify this from other sources
	int8_t mul = 15 - (vol & 7);
	uint8_t shift = 4 + 1 + (vol >> 3);

	// m_accumulator is a 12-bit value; shift up to sign-extend;
	// the downshift is incorporated into 'shift'
	int16_t value = (((int16_t)(m_channel_a[choffs].m_accumulator << 4) * mul) >> shift) & ~3;

	// apply to left/right as appropriate
	if (adpcm_a_registers_ch_pan_left(m_channel_a[choffs].m_choffs))
		output[0] += value;
	if (adpcm_a_registers_ch_pan_right(m_channel_a[choffs].m_choffs))
		output[1] += value;
}


//*********************************************************
// ADPCM "A" ENGINE
//*********************************************************

//-------------------------------------------------
//  adpcm_a_engine - constructor
//-------------------------------------------------

void adpcm_a_engine_init(void)
{
	// create the channels
	for (int chnum = 0; chnum < CHANNELS_A; chnum++)
		adpcm_a_channel_init(chnum);
}


//-------------------------------------------------
//  reset - reset the engine state
//-------------------------------------------------

void adpcm_a_engine_reset(void)
{
	// reset register state
	adpcm_a_registers_reset();

	// reset each channel
	for (int i = 0; i < CHANNELS_A; ++i)
		adpcm_a_channel_reset(i);
}


//-------------------------------------------------
//  clock - master clocking function
//-------------------------------------------------

uint32_t adpcm_a_engine_clock(uint32_t chanmask)
{
	// clock each channel, setting a bit in result if it finished
	uint32_t result = 0;
	for (int chnum = 0; chnum < CHANNELS_A; chnum++)
		if (bitfield(chanmask, chnum, 1))
			if (adpcm_a_channel_clock(chnum))
				result |= 1 << chnum;

	// return the bitmask of completed samples
	return result;
}


//-------------------------------------------------
//  update - master update function
//-------------------------------------------------

void adpcm_a_engine_output(int32_t *output, uint32_t chanmask)
{
	// mask out some channels
	chanmask &= 0xffffffff;

	// compute the output of each channel
	for (int chnum = 0; chnum < CHANNELS_A; chnum++)
		if (bitfield(chanmask, chnum, 1))
			adpcm_a_channel_output(chnum, output);
}


//-------------------------------------------------
//  write - handle writes to the ADPCM-A registers
//-------------------------------------------------

void adpcm_a_engine_write(uint32_t regnum, uint8_t data)
{
	// store the raw value to the register array;
	// most writes are passive, consumed only when needed
	adpcm_a_registers_write(regnum, data);

	// actively handle writes to the control register
	if (regnum == 0x00)
		for (int chnum = 0; chnum < CHANNELS_A; chnum++)
			if (bitfield(data, chnum, 1))
				adpcm_a_channel_keyonoff(chnum, bitfield(~data, 7, 1));
}

//*********************************************************
// ADPCM "B" REGISTERS
//*********************************************************
//
// ADPCM-B register map:
//
//      System-wide registers:
//           00 x------- Start of synthesis/analysis
//              -x------ Record
//              --x----- External/manual driving
//              ---x---- Repeat playback
//              ----x--- Speaker off
//              -------x Reset
//           01 x------- Pan left
//              -x------ Pan right
//              ----x--- Start conversion
//              -----x-- DAC enable
//              ------x- DRAM access (1=8-bit granularity; 0=1-bit)
//              -------x RAM/ROM (1=ROM, 0=RAM)
//           02 xxxxxxxx Start address (low)
//           03 xxxxxxxx Start address (high)
//           04 xxxxxxxx End address (low)
//           05 xxxxxxxx End address (high)
//           06 xxxxxxxx Prescale value (low)
//           07 -----xxx Prescale value (high)
//           08 xxxxxxxx CPU data/buffer
//           09 xxxxxxxx Delta-N frequency scale (low)
//           0a xxxxxxxx Delta-N frequency scale (high)
//           0b xxxxxxxx Level control
//           0c xxxxxxxx Limit address (low)
//           0d xxxxxxxx Limit address (high)
//           0e xxxxxxxx DAC data [YM2608/10]
//           0f xxxxxxxx PCM data [YM2608/10]
//           0e xxxxxxxx DAC data high [Y8950]
//           0f xx------ DAC data low [Y8950]
//           10 -----xxx DAC data exponent [Y8950]
//

//-------------------------------------------------
//  reset - reset the register state
//-------------------------------------------------

static inline void adpcm_b_registers_reset(void)
{
	memset(&m_regdata_b[0], 0, REGISTERS_B);

	// default limit to wide open
	m_regdata_b[0x0c] = m_regdata_b[0x0d] = 0xff;
}

// direct read/write access
static inline void adpcm_b_registers_write(uint32_t index, uint8_t data) {
	m_regdata_b[index] = data;
}

// system-wide registers
static inline uint32_t adpcm_b_registers_execute(void) {
	return bitfield(m_regdata_b[0x00], 7, 1);
}

static inline uint32_t adpcm_b_registers_record(void) {
	return bitfield(m_regdata_b[0x00], 6, 1);
}

static inline uint32_t adpcm_b_registers_external(void) {
	return bitfield(m_regdata_b[0x00], 5, 1);
}

static inline uint32_t adpcm_b_registers_repeat(void) {
	return bitfield(m_regdata_b[0x00], 4, 1);
}

/*static inline uint32_t adpcm_b_registers_speaker(void) {
	return bitfield(m_regdata_b[0x00], 3, 1);
}*/

static inline uint32_t adpcm_b_registers_resetflag(void) {
	return bitfield(m_regdata_b[0x00], 0, 1);
}

static inline uint32_t adpcm_b_registers_pan_left(void) {
	return bitfield(m_regdata_b[0x01], 7, 1);
}

static inline uint32_t adpcm_b_registers_pan_right(void) {
	return bitfield(m_regdata_b[0x01], 6, 1);
}

/*static inline uint32_t adpcm_b_registers_start_conversion(void) {
	return bitfield(m_regdata_b[0x01], 3, 1);
}

static inline uint32_t adpcm_b_registers_dac_enable(void) {
	return bitfield(m_regdata_b[0x01], 2, 1);
}*/

static inline uint32_t adpcm_b_registers_dram_8bit(void) {
	return bitfield(m_regdata_b[0x01], 1, 1);
}

static inline uint32_t adpcm_b_registers_rom_ram(void) {
	return bitfield(m_regdata_b[0x01], 0, 1);
}

static inline uint32_t adpcm_b_registers_start(void) {
	return m_regdata_b[0x02] | (m_regdata_b[0x03] << 8);
}

static inline uint32_t adpcm_b_registers_end(void) {
	return m_regdata_b[0x04] | (m_regdata_b[0x05] << 8);
}

/*static inline uint32_t adpcm_b_registers_prescale(void) {
	return m_regdata_b[0x06] | (bitfield(m_regdata_b[0x07], 0, 3) << 8);
}*/

static inline uint32_t adpcm_b_registers_cpudata(void) {
	return m_regdata_b[0x08];
}

static inline uint32_t adpcm_b_registers_delta_n(void) {
	return m_regdata_b[0x09] | (m_regdata_b[0x0a] << 8);
}

static inline uint32_t adpcm_b_registers_level(void) {
	return m_regdata_b[0x0b];
}

static inline uint32_t adpcm_b_registers_limit(void) {
	return m_regdata_b[0x0c] | (m_regdata_b[0x0d] << 8);
}

/*static inline uint32_t adpcm_b_registers_dac(void) {
	return m_regdata_b[0x0e];
}

static inline uint32_t adpcm_b_registers_pcm(void) {
	return m_regdata_b[0x0f];
}*/

//*********************************************************
// ADPCM "B" CHANNEL
//*********************************************************

//-------------------------------------------------
//  adpcm_b_channel - init
//-------------------------------------------------

static inline void adpcm_b_channel_init(void)
{
	m_channel_b.m_address_shift = 8;
	m_channel_b.m_status = STATUS_BRDY;
	m_channel_b.m_curnibble = 0;
	m_channel_b.m_curbyte = 0;
	m_channel_b.m_dummy_read = 0;
	m_channel_b.m_position = 0;
	m_channel_b.m_curaddress = 0;
	m_channel_b.m_accumulator = 0;
	m_channel_b.m_prev_accum = 0;
	m_channel_b.m_adpcm_step = STEP_MIN;
}


//-------------------------------------------------
//  reset - reset the channel state
//-------------------------------------------------

static inline void adpcm_b_channel_reset(void)
{
	m_channel_b.m_status = STATUS_BRDY;
	m_channel_b.m_curnibble = 0;
	m_channel_b.m_curbyte = 0;
	m_channel_b.m_dummy_read = 0;
	m_channel_b.m_position = 0;
	m_channel_b.m_curaddress = 0;
	m_channel_b.m_accumulator = 0;
	m_channel_b.m_prev_accum = 0;
	m_channel_b.m_adpcm_step = STEP_MIN;
}


//-------------------------------------------------
//  address_shift - compute the current address
//  shift amount based on register settings
//-------------------------------------------------

static inline uint32_t adpcm_b_channel_address_shift(void)
{
	// if a constant address shift, just provide that
	if (m_channel_b.m_address_shift != 0)
		return m_channel_b.m_address_shift;

	// if ROM or 8-bit DRAM, shift is 5 bits
	if (adpcm_b_registers_rom_ram())
		return 5;
	if (adpcm_b_registers_dram_8bit())
		return 5;

	// otherwise, shift is 2 bits
	return 2;
}


// limit checker; stops at the last byte of the chunk described by address_shift()
static inline bool adpcm_b_channel_at_limit(void) {
	return (m_channel_b.m_curaddress ==
		(((adpcm_b_registers_limit() + 1) << adpcm_b_channel_address_shift()) - 1));
}

// end checker; stops at the last byte of the chunk described by address_shift()
static inline bool adpcm_b_channel_at_end(void) {
	return (m_channel_b.m_curaddress ==
		(((adpcm_b_registers_end() + 1) << adpcm_b_channel_address_shift()) - 1));
}


//-------------------------------------------------
//  load_start - load the start address and
//  initialize the state
//-------------------------------------------------

static inline void adpcm_b_channel_load_start(void)
{
	m_channel_b.m_status = (m_channel_b.m_status & (uint8_t)~STATUS_EOS) | STATUS_PLAYING;
	m_channel_b.m_curaddress = adpcm_b_registers_external() ?
		(adpcm_b_registers_start() << adpcm_b_channel_address_shift()) : 0;
	m_channel_b.m_curnibble = 0;
	m_channel_b.m_curbyte = 0;
	m_channel_b.m_position = 0;
	m_channel_b.m_accumulator = 0;
	m_channel_b.m_prev_accum = 0;
	m_channel_b.m_adpcm_step = STEP_MIN;
}


//-------------------------------------------------
//  clock - master clocking function
//-------------------------------------------------

static inline void adpcm_b_channel_clock(void)
{
	// only process if active and not recording (which we don't support)
	if (!adpcm_b_registers_execute() || adpcm_b_registers_record() || (m_channel_b.m_status & STATUS_PLAYING) == 0)
	{
		m_channel_b.m_status &= ~STATUS_PLAYING;
		return;
	}

	// otherwise, advance the step
	uint32_t position = m_channel_b.m_position + adpcm_b_registers_delta_n();
	m_channel_b.m_position = (uint16_t)position;
	if (position < 0x10000)
		return;

	// if we're about to process nibble 0, fetch sample
	if (m_channel_b.m_curnibble == 0)
	{
		// playing from RAM/ROM
		if (adpcm_b_registers_external())
			m_channel_b.m_curbyte = ymfm_external_read(ACCESS_ADPCM_B, m_channel_b.m_curaddress);
	}

	// extract the nibble from our current byte
	uint8_t data = (uint8_t)(m_channel_b.m_curbyte << (4 * m_channel_b.m_curnibble)) >> 4;
	m_channel_b.m_curnibble ^= 1;

	// we just processed the last nibble
	if (m_channel_b.m_curnibble == 0)
	{
		// if playing from RAM/ROM, check the end/limit address or advance
		if (adpcm_b_registers_external())
		{
			// handle the sample end, either repeating or stopping
			if (adpcm_b_channel_at_end())
			{
				// if repeating, go back to the start
				if (adpcm_b_registers_repeat())
					adpcm_b_channel_load_start();

				// otherwise, done; set the EOS bit
				else
				{
					m_channel_b.m_accumulator = 0;
					m_channel_b.m_prev_accum = 0;
					m_channel_b.m_status = (m_channel_b.m_status & (uint8_t)~STATUS_PLAYING) | STATUS_EOS;
					return;
				}
			}

			// wrap at the limit address
			else if (adpcm_b_channel_at_limit())
				m_channel_b.m_curaddress = 0;

			// otherwise, advance the current address
			else
			{
				m_channel_b.m_curaddress++;
				m_channel_b.m_curaddress &= 0xffffff;
			}
		}

		// if CPU-driven, copy the next byte and request more
		else
		{
			m_channel_b.m_curbyte = adpcm_b_registers_cpudata();
			m_channel_b.m_status |= STATUS_BRDY;
		}
	}

	// remember previous value for interpolation
	m_channel_b.m_prev_accum = m_channel_b.m_accumulator;

	// forecast to next forecast: 1/8, 3/8, 5/8, 7/8, 9/8, 11/8, 13/8, 15/8
	int32_t delta = (2 * bitfield(data, 0, 3) + 1) * m_channel_b.m_adpcm_step / 8;
	if (bitfield(data, 3, 1))
		delta = -delta;

	// add and clamp to 16 bits
	m_channel_b.m_accumulator = clamp(m_channel_b.m_accumulator + delta, -32768, 32767);

	// scale the ADPCM step: 0.9, 0.9, 0.9, 0.9, 1.2, 1.6, 2.0, 2.4
	static uint8_t const s_step_scale[8] = { 57, 57, 57, 57, 77, 102, 128, 153 };
	m_channel_b.m_adpcm_step = clamp((m_channel_b.m_adpcm_step * s_step_scale[bitfield(data, 0, 3)]) / 64, STEP_MIN, STEP_MAX);
}


//-------------------------------------------------
//  output - return the computed output value, with
//  panning applied
//-------------------------------------------------

static inline void adpcm_b_channel_output(int32_t *output, uint32_t rshift)
{
	// do a linear interpolation between samples
	int32_t result = (m_channel_b.m_prev_accum *
		(int32_t)((m_channel_b.m_position ^ 0xffff) + 1) +
		m_channel_b.m_accumulator * (int32_t)(m_channel_b.m_position)) >> 16;

	// apply volume (level) in a linear fashion and reduce
	result = (result * (int32_t)(adpcm_b_registers_level())) >> (8 + rshift);

	// apply to left/right
	if (adpcm_b_registers_pan_left())
		output[0] += result;
	if (adpcm_b_registers_pan_right())
		output[1] += result;
}


static inline uint8_t adpcm_b_channel_status(void) {
	return m_channel_b.m_status;
}


//-------------------------------------------------
//  read - handle special register reads
//-------------------------------------------------

static inline uint8_t adpcm_b_channel_read(uint32_t regnum)
{
	uint8_t result = 0;

	// register 8 reads over the bus under some conditions
	if (regnum == 0x08 && !adpcm_b_registers_execute() && !adpcm_b_registers_record() && adpcm_b_registers_external())
	{
		// two dummy reads are consumed first
		if (m_channel_b.m_dummy_read != 0)
		{
			adpcm_b_channel_load_start();
			m_channel_b.m_dummy_read--;
		}

		// read the data
		else
		{
			// read from outside of the chip
			result = ymfm_external_read(ACCESS_ADPCM_B, m_channel_b.m_curaddress++);

			// did we hit the end? if so, signal EOS
			if (adpcm_b_channel_at_end())
			{
				m_channel_b.m_status = STATUS_EOS | STATUS_BRDY;
			}
			else
			{
				// signal ready
				m_channel_b.m_status = STATUS_BRDY;
			}

			// wrap at the limit address
			if (adpcm_b_channel_at_limit())
				m_channel_b.m_curaddress = 0;
		}
	}
	return result;
}


//-------------------------------------------------
//  write - handle special register writes
//-------------------------------------------------

static inline void adpcm_b_channel_write(uint32_t regnum, uint8_t value)
{
	// register 0 can do a reset; also use writes here to reset the
	// dummy read counter
	if (regnum == 0x00)
	{
		if (adpcm_b_registers_execute())
			adpcm_b_channel_load_start();
		else
			m_channel_b.m_status &= (uint8_t)~STATUS_EOS;
		if (adpcm_b_registers_resetflag())
			adpcm_b_channel_reset();
		if (adpcm_b_registers_external())
			m_channel_b.m_dummy_read = 2;
	}

	// register 8 writes over the bus under some conditions
	else if (regnum == 0x08)
	{
		// if writing from the CPU during execute, clear the ready flag
		if (adpcm_b_registers_execute() && !adpcm_b_registers_record() && !adpcm_b_registers_external())
			m_channel_b.m_status &= (uint8_t)~STATUS_BRDY;

		// if writing during "record", pass through as data
		else if (!adpcm_b_registers_execute() && adpcm_b_registers_record() && adpcm_b_registers_external())
		{
			// clear out dummy reads and set start address
			if (m_channel_b.m_dummy_read != 0)
			{
				adpcm_b_channel_load_start();
				m_channel_b.m_dummy_read = 0;
			}

			// did we hit the end? if so, signal EOS
			if (adpcm_b_channel_at_end())
			{
				m_channel_b.m_status = STATUS_EOS | STATUS_BRDY;
			}

			// otherwise, write the data and signal ready
			else
			{
				ymfm_external_write(ACCESS_ADPCM_B, m_channel_b.m_curaddress++, value);
				m_channel_b.m_status = STATUS_BRDY;
			}
		}
	}
}


//*********************************************************
// ADPCM "B" ENGINE
//*********************************************************

//-------------------------------------------------
//  adpcm_b_engine - init
//-------------------------------------------------

void adpcm_b_engine_init(void)
{
	// create the channel
	adpcm_b_channel_init();
}


//-------------------------------------------------
//  reset - reset the engine state
//-------------------------------------------------

void adpcm_b_engine_reset(void)
{
	// reset registers
	adpcm_b_registers_reset();

	// reset each channel
	adpcm_b_channel_reset();
}


//-------------------------------------------------
//  clock - master clocking function
//-------------------------------------------------

void adpcm_b_engine_clock(void)
{
	// clock each channel, setting a bit in result if it finished
	adpcm_b_channel_clock();
}


//-------------------------------------------------
//  output - master output function
//-------------------------------------------------

void adpcm_b_engine_output(int32_t *output, uint32_t rshift)
{
	// compute the output of each channel
	adpcm_b_channel_output(output, rshift);
}


//-------------------------------------------------
//  write - handle writes to the ADPCM-B registers
//-------------------------------------------------

void adpcm_b_engine_write(uint32_t regnum, uint8_t data)
{
	// store the raw value to the register array;
	// most writes are passive, consumed only when needed
	adpcm_b_registers_write(regnum, data);

	// let the channel handle any special writes
	adpcm_b_channel_write(regnum, data);
}

// read from the ADPCM-B registers
uint32_t adpcm_b_engine_read(uint32_t regnum) {
	return adpcm_b_channel_read(regnum);
}

// status
uint8_t adpcm_b_engine_status(void) {
	return adpcm_b_channel_status();
}


//-------------------------------------------------
// hacks
//-------------------------------------------------

void adpcm_a_set_accum_wrap(bool wrap) {
	accum_wrap = wrap;
}


//-------------------------------------------------
//  states - Read/write ADPCMA/B state data
//-------------------------------------------------

void adpcm_state_load(uint8_t *st) {
	for (int i = 0; i < REGISTERS_A; ++i)
		m_regdata_a[i] = geo_serial_pop8(st);

	for (int i = 0; i < REGISTERS_B; ++i)
		m_regdata_b[i] = geo_serial_pop8(st);

	for (int i = 0; i < CHANNELS_A; ++i)
	{
		m_channel_a[i].m_choffs = geo_serial_pop32(st);
		m_channel_a[i].m_address_shift = geo_serial_pop32(st);
		m_channel_a[i].m_playing = geo_serial_pop32(st);
		m_channel_a[i].m_curnibble = geo_serial_pop32(st);
		m_channel_a[i].m_curbyte = geo_serial_pop32(st);
		m_channel_a[i].m_curaddress = geo_serial_pop32(st);
		m_channel_a[i].m_accumulator = geo_serial_pop32(st);
		m_channel_a[i].m_step_index = geo_serial_pop32(st);
	}

	m_channel_b.m_address_shift = geo_serial_pop32(st);
	m_channel_b.m_status = geo_serial_pop32(st);
	m_channel_b.m_address_shift = geo_serial_pop32(st);
	m_channel_b.m_status = geo_serial_pop32(st);
	m_channel_b.m_curnibble = geo_serial_pop32(st);
	m_channel_b.m_curbyte = geo_serial_pop32(st);
	m_channel_b.m_dummy_read = geo_serial_pop32(st);
	m_channel_b.m_position = geo_serial_pop32(st);
	m_channel_b.m_curaddress = geo_serial_pop32(st);
	m_channel_b.m_accumulator = geo_serial_pop32(st);
	m_channel_b.m_prev_accum = geo_serial_pop32(st);
	m_channel_b.m_adpcm_step = geo_serial_pop32(st);
}

void adpcm_state_save(uint8_t *st) {
	for (int i = 0; i < REGISTERS_A; ++i)
		geo_serial_push8(st, m_regdata_a[i]);

	for (int i = 0; i < REGISTERS_B; ++i)
		geo_serial_push8(st, m_regdata_b[i]);

	for (int i = 0; i < CHANNELS_A; ++i)
	{
		geo_serial_push32(st, m_channel_a[i].m_choffs);
		geo_serial_push32(st, m_channel_a[i].m_address_shift);
		geo_serial_push32(st, m_channel_a[i].m_playing);
		geo_serial_push32(st, m_channel_a[i].m_curnibble);
		geo_serial_push32(st, m_channel_a[i].m_curbyte);
		geo_serial_push32(st, m_channel_a[i].m_curaddress);
		geo_serial_push32(st, m_channel_a[i].m_accumulator);
		geo_serial_push32(st, m_channel_a[i].m_step_index);
	}

	geo_serial_push32(st, m_channel_b.m_address_shift);
	geo_serial_push32(st, m_channel_b.m_status);
	geo_serial_push32(st, m_channel_b.m_address_shift);
	geo_serial_push32(st, m_channel_b.m_status);
	geo_serial_push32(st, m_channel_b.m_curnibble);
	geo_serial_push32(st, m_channel_b.m_curbyte);
	geo_serial_push32(st, m_channel_b.m_dummy_read);
	geo_serial_push32(st, m_channel_b.m_position);
	geo_serial_push32(st, m_channel_b.m_curaddress);
	geo_serial_push32(st, m_channel_b.m_accumulator);
	geo_serial_push32(st, m_channel_b.m_prev_accum);
	geo_serial_push32(st, m_channel_b.m_adpcm_step);
}
