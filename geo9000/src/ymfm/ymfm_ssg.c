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
#include "ymfm_ssg.h"

#define REGISTERS_SSG 0x10

// internal state
static uint32_t m_tone_count[3];             // current tone counter
static uint32_t m_tone_state[3];             // current tone state
static uint32_t m_envelope_count;            // envelope counter
static uint32_t m_envelope_state;            // envelope state
static uint32_t m_noise_count;               // current noise counter
static uint32_t m_noise_state;               // current noise state
static uint8_t m_regdata[REGISTERS_SSG];     // register data

//*********************************************************
// SSG REGISTERS
//*********************************************************
//
// SSG register map:
//
//      System-wide registers:
//           06 ---xxxxx Noise period
//           07 x------- I/O B in(0) or out(1)
//              -x------ I/O A in(0) or out(1)
//              --x----- Noise enable(0) or disable(1) for channel C
//              ---x---- Noise enable(0) or disable(1) for channel B
//              ----x--- Noise enable(0) or disable(1) for channel A
//              -----x-- Tone enable(0) or disable(1) for channel C
//              ------x- Tone enable(0) or disable(1) for channel B
//              -------x Tone enable(0) or disable(1) for channel A
//           0B xxxxxxxx Envelope period fine
//           0C xxxxxxxx Envelope period coarse
//           0D ----x--- Envelope shape: continue
//              -----x-- Envelope shape: attack/decay
//              ------x- Envelope shape: alternate
//              -------x Envelope shape: hold
//           0E xxxxxxxx 8-bit parallel I/O port A
//           0F xxxxxxxx 8-bit parallel I/O port B
//
//      Per-channel registers:
//     00,02,04 xxxxxxxx Tone period (fine) for channel A,B,C
//     01,03,05 ----xxxx Tone period (coarse) for channel A,B,C
//     08,09,0A ---x---- Mode: fixed(0) or variable(1) for channel A,B,C
//              ----xxxx Amplitude for channel A,B,C
//

// direct read/write access
static inline uint8_t ssg_registers_read(uint32_t index) {
	return m_regdata[index];
}

static inline void ssg_registers_write(uint32_t index, uint8_t data) {
	m_regdata[index] = data;
}

// system-wide registers
static inline uint32_t ssg_registers_noise_period(void) {
	return bitfield(m_regdata[0x06], 0, 5);
}

static inline uint32_t ssg_registers_io_b_out(void) {
	return bitfield(m_regdata[0x07], 7, 1);
}

static inline uint32_t ssg_registers_io_a_out(void) {
	return bitfield(m_regdata[0x07], 6, 1);
}

static inline uint32_t ssg_registers_envelope_period(void) {
	return m_regdata[0x0b] | (m_regdata[0x0c] << 8);
}

static inline uint32_t ssg_registers_envelope_continue(void) {
	return bitfield(m_regdata[0x0d], 3, 1);
}

static inline uint32_t ssg_registers_envelope_attack(void) {
	return bitfield(m_regdata[0x0d], 2, 1);
}

static inline uint32_t ssg_registers_envelope_alternate(void) {
	return bitfield(m_regdata[0x0d], 1, 1);
}

static inline uint32_t ssg_registers_envelope_hold(void) {
	return bitfield(m_regdata[0x0d], 0, 1);
}

/*static inline uint32_t ssg_registers_io_a_data(void) {
	return m_regdata[0x0e];
}

static inline uint32_t ssg_registers_io_b_data(void) {
	return m_regdata[0x0f];
}*/

// per-channel registers
static inline uint32_t ssg_registers_ch_noise_enable_n(uint32_t choffs) {
	return bitfield(m_regdata[0x07], 3 + choffs, 1);
}

static inline uint32_t ssg_registers_ch_tone_enable_n(uint32_t choffs) {
	return bitfield(m_regdata[0x07], 0 + choffs, 1);
}

static inline uint32_t ssg_registers_ch_tone_period(uint32_t choffs) {
	return m_regdata[0x00 + 2 * choffs] | (bitfield(m_regdata[0x01 + 2 * choffs], 0, 4) << 8);
}

static inline uint32_t ssg_registers_ch_envelope_enable(uint32_t choffs) {
	return bitfield(m_regdata[0x08 + choffs], 4, 1);
}

static inline uint32_t ssg_registers_ch_amplitude(uint32_t choffs) {
	return bitfield(m_regdata[0x08 + choffs], 0, 4);
}


//-------------------------------------------------
//  reset - reset the register state
//-------------------------------------------------

static inline void ssg_registers_reset(void)
{
	memset(&m_regdata[0], 0, REGISTERS_SSG);
}

//-------------------------------------------------
//  ssg_engine - init
//-------------------------------------------------
void ssg_engine_init(void)
{
	m_tone_count[0] = m_tone_count[1] = m_tone_count[2] = 0;
	m_tone_state[0] = m_tone_state[1] = m_tone_state[2] = 0;
	m_envelope_count = 0;
	m_envelope_state = 0;
	m_noise_count = 0;
	m_noise_state = 1;
}

//-------------------------------------------------
//  reset - reset the engine state
//-------------------------------------------------

void ssg_engine_reset(void)
{
	// reset register state
	ssg_registers_reset();

	// reset engine state
	for (int chan = 0; chan < 3; chan++)
	{
		m_tone_count[chan] = 0;
		m_tone_state[chan] = 0;
	}
	m_envelope_count = 0;
	m_envelope_state = 0;
	m_noise_count = 0;
	m_noise_state = 1;
}

//-------------------------------------------------
//  clock - master clocking function
//-------------------------------------------------

void ssg_engine_clock(void)
{
	// clock tones; tone period units are clock/16 but since we run at clock/8
	// that works out for us to toggle the state (50% duty cycle) at twice the
	// programmed period
	for (int chan = 0; chan < 3; chan++)
	{
		m_tone_count[chan]++;
		if (m_tone_count[chan] >= ssg_registers_ch_tone_period(chan))
		{
			m_tone_state[chan] ^= 1;
			m_tone_count[chan] = 0;
		}
	}

	// clock noise; noise period units are clock/16 but since we run at clock/8,
	// our counter needs a right shift prior to compare; note that a period of 0
	// should produce an indentical result to a period of 1, so add a special
	// check against that case
	m_noise_count++;
	if ((m_noise_count >> 1) >= ssg_registers_noise_period() && m_noise_count != 1)
	{
		m_noise_state ^= (bitfield(m_noise_state, 0, 1) ^ bitfield(m_noise_state, 3, 1)) << 17;
		m_noise_state >>= 1;
		m_noise_count = 0;
	}

	// clock envelope; envelope period units are clock/8 (manual says clock/256
	// but that's for all 32 steps)
	m_envelope_count++;
	if (m_envelope_count >= ssg_registers_envelope_period())
	{
		m_envelope_state++;
		m_envelope_count = 0;
	}
}

//-------------------------------------------------
//  output - output the current state
//-------------------------------------------------

void ssg_engine_output(int32_t *output)
{
	// volume to amplitude table, taken from MAME's implementation but biased
	// so that 0 == 0
	static int16_t const s_amplitudes[32] =
	{
		     0,   32,   78,  141,  178,  222,  262,  306,
		   369,  441,  509,  585,  701,  836,  965, 1112,
		  1334, 1595, 1853, 2146, 2576, 3081, 3576, 4135,
		  5000, 6006, 7023, 8155, 9963,11976,14132,16382
	};

	// compute the envelope volume
	uint32_t envelope_volume;
	if ((ssg_registers_envelope_hold() | (ssg_registers_envelope_continue() ^ 1)) && m_envelope_state >= 32)
	{
		m_envelope_state = 32;
		envelope_volume = ((ssg_registers_envelope_attack() ^ ssg_registers_envelope_alternate()) & ssg_registers_envelope_continue()) ? 31 : 0;
	}
	else
	{
		uint32_t attack = ssg_registers_envelope_attack();
		if (ssg_registers_envelope_alternate())
			attack ^= bitfield(m_envelope_state, 5, 1);
		envelope_volume = (m_envelope_state & 31) ^ (attack ? 0 : 31);
	}

	// iterate over channels
	for (int chan = 0; chan < 3; chan++)
	{
		// noise depends on the noise state, which is the LSB of m_noise_state
		uint32_t noise_on = ssg_registers_ch_noise_enable_n(chan) | m_noise_state;

		// tone depends on the current tone state
		uint32_t tone_on = ssg_registers_ch_tone_enable_n(chan) | m_tone_state[chan];

		// if neither tone nor noise enabled, return 0
		uint32_t volume;
		if ((noise_on & tone_on) == 0)
			volume = 0;

		// if the envelope is enabled, use its amplitude
		else if (ssg_registers_ch_envelope_enable(chan))
			volume = envelope_volume;

		// otherwise, scale the tone amplitude up to match envelope values
		// according to the datasheet, amplitude 15 maps to envelope 31
		else
		{
			volume = ssg_registers_ch_amplitude(chan) * 2;
			if (volume != 0)
				volume |= 1;
		}

		// convert to amplitude
		output[chan] = s_amplitudes[volume];
	}
}

//-------------------------------------------------
//  read - handle reads from the SSG registers
//-------------------------------------------------

uint8_t ssg_engine_read(uint32_t regnum)
{
	// read from the I/O ports call the handlers if they are configured for input
	if (regnum == 0x0e && !ssg_registers_io_a_out())
		return ymfm_external_read(ACCESS_IO, 0);
	else if (regnum == 0x0f && !ssg_registers_io_b_out())
		return ymfm_external_read(ACCESS_IO, 1);

	// otherwise just return the register value
	return ssg_registers_read(regnum);
}

//-------------------------------------------------
//  write - handle writes to the SSG registers
//-------------------------------------------------

void ssg_engine_write(uint32_t regnum, uint8_t data)
{
	// store the raw value to the register array;
	// most writes are passive, consumed only when needed
	ssg_registers_write(regnum, data);

	// writes to the envelope shape register reset the state
	if (regnum == 0x0d)
		m_envelope_state = 0;

	// writes to the I/O ports call the handlers if they are configured for output
	else if (regnum == 0x0e && ssg_registers_io_a_out())
		ymfm_external_write(ACCESS_IO, 0, data);
	else if (regnum == 0x0f && ssg_registers_io_b_out())
		ymfm_external_write(ACCESS_IO, 1, data);
}

//-------------------------------------------------
//  states - Read/write SSG state data
//-------------------------------------------------

void ssg_state_load(uint8_t *st) {
	for (int i = 0; i < 3; ++i)
	{
		m_tone_count[i] = geo_serial_pop32(st);
		m_tone_state[i] = geo_serial_pop32(st);
	}

	m_envelope_count = geo_serial_pop32(st);
	m_envelope_state = geo_serial_pop32(st);
	m_noise_count = geo_serial_pop32(st);
	m_noise_state = geo_serial_pop32(st);

	for (int i = 0; i < REGISTERS_SSG; ++i)
		m_regdata[i] = geo_serial_pop8(st);
}

void ssg_state_save(uint8_t *st) {
	for (int i = 0; i < 3; ++i)
	{
		geo_serial_push32(st, m_tone_count[i]);
		geo_serial_push32(st, m_tone_state[i]);
	}

	geo_serial_push32(st, m_envelope_count);
	geo_serial_push32(st, m_envelope_state);
	geo_serial_push32(st, m_noise_count);
	geo_serial_push32(st, m_noise_state);

	for (int i = 0; i < REGISTERS_SSG; ++i)
		geo_serial_push8(st, m_regdata[i]);
}
