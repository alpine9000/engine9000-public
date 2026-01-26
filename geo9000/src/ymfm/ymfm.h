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

#ifndef YMFM_H
#define YMFM_H

#include <assert.h>

// external I/O access classes
enum {
	ACCESS_IO = 0,
	ACCESS_ADPCM_A,
	ACCESS_ADPCM_B,
	ACCESS_PCM,
	ACCESS_CLASSES
};

// various envelope states
enum {
	EG_DEPRESS = 0,		// OPLL only; set EG_HAS_DEPRESS to enable
	EG_ATTACK = 1,
	EG_DECAY = 2,
	EG_SUSTAIN = 3,
	EG_RELEASE = 4,
	EG_REVERB = 5,		// OPQ/OPZ only; set EG_HAS_REVERB to enable
	EG_STATES = 6
};


// the following functions must be implemented by any derived classes; the
// default implementations are sufficient for some minimal operation, but will
// likely need to be overridden to integrate with the outside world; they are
// all prefixed with ymfm_ to reduce the likelihood of namespace collisions

// the chip implementation calls this whenever data is read from outside
// of the chip; our responsibility is to provide the data requested
extern uint8_t ymfm_external_read(uint32_t type, uint32_t address);

// the chip implementation calls this whenever data is written outside
// of the chip; our responsibility is to pass the written data on to any consumers
extern void ymfm_external_write(uint32_t type, uint32_t address, uint8_t data);

//
// timing and synchronizaton
//

// the chip implementation calls this when a write happens to the mode
// register, which could affect timers and interrupts; our responsibility
// is to ensure the system is up to date before calling the engine's
// engine_mode_write() method
extern void ymfm_sync_mode_write(uint8_t data);

// the chip implementation calls this when the chip's status has changed,
// which may affect the interrupt state; our responsibility is to ensure
// the system is up to date before calling the engine's
// engine_check_interrupts() method
extern void ymfm_sync_check_interrupts(void);

// the chip implementation calls this when one of the two internal timers
// has changed state; our responsibility is to arrange to call the engine's
// engine_timer_expired() method after the provided number of clocks; if
// duration_in_clocks is negative, we should cancel any outstanding timers
extern void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks);

// the chip implementation calls this to indicate that the chip should be
// considered in a busy state until the given number of clocks has passed;
// our responsibility is to compute and remember the ending time based on
// the chip's clock for later checking
extern void ymfm_set_busy_end(uint32_t clocks);

// the chip implementation calls this to see if the chip is still currently
// is a busy state, as specified by a previous call to ymfm_set_busy_end();
// our responsibility is to compare the current time against the previously
// noted busy end time and return true if we haven't yet passed it
extern bool ymfm_is_busy(void);

//
// I/O functions
//

// the chip implementation calls this when the state of the IRQ signal has
// changed due to a status change; our responsibility is to respond as
// needed to the change in IRQ state, signaling any consumers
extern void ymfm_update_irq(bool asserted);

//-------------------------------------------------
//  bitfield - extract a bitfield from the given
//  value, starting at bit 'start' for a length of
//  'length' bits
//-------------------------------------------------

static inline uint32_t bitfield(uint32_t value, int start, int length)
{
	return (value >> start) & ((1 << length) - 1);
}


//-------------------------------------------------
//  clamp - clamp between the minimum and maximum
//  values provided
//-------------------------------------------------

static inline int32_t clamp(int32_t value, int32_t minval, int32_t maxval)
{
	if (value < minval)
		return minval;
	if (value > maxval)
		return maxval;
	return value;
}

#include "geo_serial.h"

#endif // YMFM_H
