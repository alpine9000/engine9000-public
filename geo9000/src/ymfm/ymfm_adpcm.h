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

#ifndef YMFM_ADPCM_H
#define YMFM_ADPCM_H

#define STATUS_EOS 0x01
#define STATUS_BRDY 0x02
#define STATUS_PLAYING 0x04

// ======================> adpcm_a_engine
// init
void adpcm_a_engine_init(void);

// reset our status
void adpcm_a_engine_reset(void);

// master clocking function
uint32_t adpcm_a_engine_clock(uint32_t chanmask);

// compute sum of channel outputs
void adpcm_a_engine_output(int32_t *output, uint32_t chanmask);

// write to the ADPCM-A registers
void adpcm_a_engine_write(uint32_t regnum, uint8_t data);

// set accumulator wrapping on or off
void adpcm_a_set_accum_wrap(bool wrap);

// ======================> adpcm_b_engine
// init
void adpcm_b_engine_init(void);

// reset our status
void adpcm_b_engine_reset(void);

// master clocking function
void adpcm_b_engine_clock(void);

// compute sum of channel outputs
void adpcm_b_engine_output(int32_t *output, uint32_t rshift);

// read from the ADPCM-B registers
uint32_t adpcm_b_engine_read(uint32_t regnum);

// write to the ADPCM-B registers
void adpcm_b_engine_write(uint32_t regnum, uint8_t data);

// status
uint8_t adpcm_b_engine_status(void);

void adpcm_state_load(uint8_t *st);
void adpcm_state_save(uint8_t *st);

// ======================> adpcm_a_channel
typedef struct _adpcm_a_channel
{
	uint32_t m_choffs;                    // channel offset
	uint32_t m_address_shift;             // address bits shift-left
	uint32_t m_playing;                   // currently playing?
	uint32_t m_curnibble;                 // index of the current nibble
	uint32_t m_curbyte;                   // current byte of data
	uint32_t m_curaddress;                // current address
	int32_t m_accumulator;                // accumulator
	int32_t m_step_index;                 // index in the stepping table
} adpcm_a_channel;

// ======================> adpcm_b_channel
typedef struct _adpcm_b_channel
{
	uint32_t m_address_shift;       // address bits shift-left
	uint32_t m_status;              // currently playing?
	uint32_t m_curnibble;           // index of the current nibble
	uint32_t m_curbyte;             // current byte of data
	uint32_t m_dummy_read;          // dummy read tracker
	uint32_t m_position;            // current fractional position
	uint32_t m_curaddress;          // current address
	int32_t m_accumulator;          // accumulator
	int32_t m_prev_accum;           // previous accumulator (for linear interp)
	int32_t m_adpcm_step;           // next forecast
} adpcm_b_channel;

#endif // YMFM_ADPCM_H
