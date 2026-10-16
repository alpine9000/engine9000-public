/*
Copyright (c) 2022-2026 Rupert Carmichael
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

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "e9k-lib.h"

int geo_z80_run(unsigned);
int geo_z80_stepInstruction(void);
void geo_z80_debugSuppressBreakpointAtPc(void);
void geo_z80_busreq(unsigned);
void geo_z80_nmi(void);
void geo_z80_assert_irq(unsigned);
void geo_z80_clear_irq(void);
void geo_z80_assert_reset(void);
void geo_z80_clear_reset(void);
void geo_z80_init(void);
void geo_z80_reset(void);
void geo_z80_set_mrom(unsigned);
void geo_z80_set_cd_mode(void);
void geo_z80_state_load(uint8_t*);
void geo_z80_state_save(uint8_t*);
const void* geo_z80_ram_ptr(void);
size_t geo_z80_debugReadRegs(e9k_debug_processor_reg_t *out, size_t cap);
size_t geo_z80_debugReadMemory(uint32_t addr, uint8_t *out, size_t cap);
int geo_z80_debugWriteMemory(uint32_t addr, uint32_t value, size_t size);
size_t geo_z80_debugDisassemble(uint32_t pc, char *out, size_t cap);
void geo_z80_debugAddBreakpoint(uint32_t addr);
void geo_z80_debugRemoveBreakpoint(uint32_t addr);
#ifdef E9K_HACK_REGISTER_LOG
uint16_t geo_z80_getPc(void);
#endif
