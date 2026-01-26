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

#ifndef GEO_M68K_H
#define GEO_M68K_H

#define BOARD_DEFAULT       0x00 // Default
#define BOARD_LINKABLE      0x01 // Linkable Multiplayer Boards
#define BOARD_CT0           0x02 // PRO-CT0
#define BOARD_SMA           0x03 // NEO-SMA
#define BOARD_PVC           0x04 // NEO-PVC
#define BOARD_KOF98         0x05 // The King of Fighters '98
#define BOARD_KF2K3BL       0x06 // The King of Fighters 2003 (bootleg set 1)
#define BOARD_KF2K3BLA      0x07 // The King of Fighters 2003 (bootleg set 2)
#define BOARD_MSLUGX        0x08 // Metal Slug X
#define BOARD_MS5PLUS       0x09 // Metal Slug 5 Plus (bootleg)
#define BOARD_CTHD2003      0x0a // Crouching Tiger Hidden Dragon 2003
#define BOARD_BREZZASOFT    0x0b // BrezzaSoft Gambling Boards with Cart RAM
#define BOARD_KOF10TH       0x0c // King of Fighters 10th Anniversary Bootleg

#define VECTOR_TABLE_BIOS   0x00
#define VECTOR_TABLE_CART   0x01

#define IRQ_VBLANK  0x01
#define IRQ_TIMER   0x02
#define IRQ_RESET   0x03

#define IRQ_TIMER_ENABLED       0x10
#define IRQ_TIMER_RELOAD_WRITE  0x20
#define IRQ_TIMER_RELOAD_VBLANK 0x40
#define IRQ_TIMER_RELOAD_COUNT0 0x80

void geo_m68k_init(void);
void geo_m68k_reset(void);

int geo_m68k_run(unsigned);

void geo_m68k_interrupt(unsigned);

void geo_m68k_board_set(unsigned);
void geo_m68k_sma_init(uint32_t*, uint32_t*, uint8_t*);

void geo_m68k_postload(void);

uint8_t geo_m68k_reg_poutput(void);

void geo_m68k_state_load(uint8_t*);
void geo_m68k_state_save(uint8_t*);

const void* geo_m68k_ram_ptr(void);
const void* geo_m68k_dynfix_ptr(void);

unsigned
m68k_read_memory_8(unsigned address);

void
m68k_write_memory_8(unsigned address, unsigned value);

void
m68k_write_memory_16(unsigned address, unsigned value);

void
m68k_write_memory_32(unsigned address, unsigned value);

#endif
