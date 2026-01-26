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

#ifndef YMFM_FM_H
#define YMFM_FM_H

// set phase_step to this value to recalculate it each sample; needed
// in the case of PM LFO changes
#define PHASE_STEP_DYNAMIC 1

// OPN Register defines
// this value is returned from the write() function for rhythm channels
#define RHYTHM_CHANNEL 0xff

// this is the size of a full sin waveform
#define WAVEFORM_LENGTH 0x400

// the following constants need to be defined per family:
//          uint32_t OUTPUTS: The number of outputs exposed (1-4)
//         uint32_t CHANNELS: The number of channels on the chip
//     uint32_t ALL_CHANNELS: A bitmask of all channels
//        uint32_t OPERATORS: The number of operators on the chip
//        uint32_t WAVEFORMS: The number of waveforms offered
//        uint32_t REGISTERS: The number of 8-bit registers allocated
// uint32_t DEFAULT_PRESCALE: The starting clock prescale
// uint32_t EG_CLOCK_DIVIDER: The clock divider of the envelope generator
// uint32_t CSM_TRIGGER_MASK: Mask of channels to trigger in CSM mode
//         uint32_t REG_MODE: The address of the "mode" register controlling timers
//     uint8_t STATUS_TIMERA: Status bit to set when timer A fires
//     uint8_t STATUS_TIMERB: Status bit to set when tiemr B fires
//       uint8_t STATUS_BUSY: Status bit to set when the chip is busy
//        uint8_t STATUS_IRQ: Status bit to set when an IRQ is signalled
//
// the following constants are uncommon:
//          bool DYNAMIC_OPS: True if ops/channel can be changed at runtime (OPL3+)
//       bool EG_HAS_DEPRESS: True if the chip has a DP ("depress"?) envelope stage (OPLL)
//        bool EG_HAS_REVERB: True if the chip has a faux reverb envelope stage (OPQ/OPZ)
//           bool EG_HAS_SSG: True if the chip has SSG envelope support (OPN)
//      bool MODULATOR_DELAY: True if the modulator is delayed by 1 sample (OPL pre-OPL3)
//
#define OUTPUTS 2
#define CHANNELS 6
#define ALL_CHANNELS 0x3f
#define OPERATORS 24
#define STATUS_TIMERA 0x01
#define STATUS_TIMERB 0x02
#define STATUS_BUSY 0x80
#define STATUS_IRQ 0
#define WAVEFORMS 1
#define REGISTERS 0x200
#define REG_MODE 0x27
#define DEFAULT_PRESCALE 6
#define DYNAMIC_OPS false
#define EG_CLOCK_DIVIDER 3
#define EG_HAS_DEPRESS false
#define EG_HAS_REVERB false
#define EG_HAS_SSG true
#define MODULATOR_DELAY false
#define CSM_TRIGGER_MASK 4

// "quiet" value, used to optimize when we can skip doing work
#define EG_QUIET 0x380

//*********************************************************
//  GLOBAL ENUMERATORS
//*********************************************************

// three different keyon sources; actual keyon is an OR over all of these
enum {
	KEYON_NORMAL = 0,
	KEYON_RHYTHM = 1,
	KEYON_CSM = 2
};


//*********************************************************
//  CORE IMPLEMENTATION
//*********************************************************

// ======================> opdata_cache

// this class holds data that is computed once at the start of clocking
// and remains static during subsequent sound generation
typedef struct _opdata_cache
{
	uint16_t const *waveform;         // base of sine table
	uint32_t phase_step;              // phase step, or PHASE_STEP_DYNAMIC if PM is active
	uint32_t total_level;             // total level * 8 + KSL
	uint32_t block_freq;              // raw block frequency value (used to compute phase_step)
	int32_t detune;                   // detuning value (used to compute phase_step)
	uint32_t multiple;                // multiple value (x.1, used to compute phase_step)
	uint32_t eg_sustain;              // sustain level, shifted up to envelope values
	uint8_t eg_rate[EG_STATES];       // envelope rate, including KSR
	uint8_t eg_shift;                 // envelope shift amount
} opdata_cache;


//*********************************************************
//  CORE ENGINE CLASSES
//*********************************************************
// ======================> fm_operator

// fm_operator represents an FM operator (or "slot" in FM parlance), which
// produces an output sine wave modulated by an envelope
typedef struct _fm_operator
{
	// internal state
	uint32_t m_choffs;                     // channel offset in registers
	uint32_t m_opoffs;                     // operator offset in registers
	uint32_t m_phase;                      // current phase value (10.10 format)
	uint16_t m_env_attenuation;            // computed envelope attenuation (4.6 format)
	uint32_t m_env_state;                  // current envelope state
	uint8_t m_ssg_inverted;                // non-zero if the output should be inverted (bit 0)
	uint8_t m_key_state;                   // current key state: on or off (bit 0)
	uint8_t m_keyon_live;                  // live key on state (bit 0 = direct, bit 1 = rhythm, bit 2 = CSM)
	opdata_cache m_cache;                  // cached values for performance
} fm_operator;


// ======================> fm_channel

// fm_channel represents an FM channel which combines the output of 2 or 4
// operators into a final result
typedef struct _fm_channel
{
	// internal state
	uint32_t m_choffs;     // channel offset in registers
	int16_t m_feedback[2]; // feedback memory for operator 1
	int16_t m_feedback_in; // next input value for op 1 feedback (set in output)
	fm_operator *m_op[4];  // up to 4 operators
} fm_channel;


// init
void fm_engine_init(void);

// reset the overall state
void fm_engine_reset(void);

// master clocking function
uint32_t fm_engine_clock(uint32_t chanmask);

// compute sum of channel outputs
void fm_engine_output(int32_t *output, uint32_t rshift, int32_t clipmax, uint32_t chanmask);

// write to the OPN registers
void fm_engine_write(uint16_t regnum, uint8_t data);

// return the current status
uint8_t fm_engine_status(void);

// set/reset bits in the status register, updating the IRQ status
uint8_t fm_engine_set_reset_status(uint8_t set, uint8_t reset);

// set the IRQ mask
void fm_engine_set_irq_mask(uint8_t mask);

// return the current clock prescale
uint32_t fm_engine_clock_prescale(void);

// set prescale factor (2/3/6)
void fm_engine_set_clock_prescale(uint32_t prescale);

// compute sample rate
uint32_t fm_engine_sample_rate(uint32_t baseclock);

// invalidate any caches
void fm_engine_invalidate_caches(void);

// timer callback; called by the interface when a timer fires
void fm_engine_timer_expired(uint32_t tnum);

// check interrupts; called by the interface after synchronization
void fm_engine_check_interrupts(void);

// mode register write; called by the interface after synchronization
void fm_engine_mode_write(uint8_t data);

// assign the current set of operators to channels
void fm_engine_assign_operators(void);

// update the state of the given timer
void fm_engine_update_timer(uint32_t which, uint32_t enable, int32_t delta_clocks);

#endif // YMFM_FM_H
