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

// These values ultimately come from MAME, but are available at neogeodev

#ifndef GEO_SMA_H
#define GEO_SMA_H

uint32_t sma_addr_kof99[3] = { 0x2ffff8, 0x2ffffa, 0x2ffff0 };
uint32_t sma_bank_kof99[64] = {
    0x000000, 0x100000, 0x200000, 0x300000, // 00
    0x3cc000, 0x4cc000, 0x3f2000, 0x4f2000, // 04
    0x407800, 0x507800, 0x40d000, 0x50d000, // 08
    0x417800, 0x517800, 0x420800, 0x520800, // 12
    0x424800, 0x524800, 0x429000, 0x529000, // 16
    0x42e800, 0x52e800, 0x431800, 0x531800, // 20
    0x54d000, 0x551000, 0x567000, 0x592800, // 24
    0x588800, 0x581800, 0x599800, 0x594800, // 28
    0x598000,                               // 32
};
uint8_t sma_scramble_kof99[6] = { 14, 6, 8, 10, 12, 5 };

uint32_t sma_addr_garou[3] = { 0x2fffcc, 0x2ffff0, 0x2fffc0 };
uint32_t sma_bank_garou[64] = {
    0x000000, 0x100000, 0x200000, 0x300000, // 00
    0x280000, 0x380000, 0x2d0000, 0x3d0000, // 04
    0x2f0000, 0x3f0000, 0x400000, 0x500000, // 08
    0x420000, 0x520000, 0x440000, 0x540000, // 12
    0x498000, 0x598000, 0x4a0000, 0x5a0000, // 16
    0x4a8000, 0x5a8000, 0x4b0000, 0x5b0000, // 20
    0x4b8000, 0x5b8000, 0x4c0000, 0x5c0000, // 24
    0x4c8000, 0x5c8000, 0x4d0000, 0x5d0000, // 28
    0x458000, 0x558000, 0x460000, 0x560000, // 32
    0x468000, 0x568000, 0x470000, 0x570000, // 36
    0x478000, 0x578000, 0x480000, 0x580000, // 40
    0x488000, 0x588000, 0x490000, 0x590000, // 44
    0x5d0000, 0x5d8000, 0x5e0000, 0x5e8000, // 48
    0x5f0000, 0x5f8000, 0x600000,           // 52
};
uint8_t sma_scramble_garou[6] = { 5, 9, 7, 6, 14, 12 };

uint32_t sma_addr_garouh[3] = { 0x2fffcc, 0x2ffff0, 0x2fffc0 };
uint32_t sma_bank_garouh[64] = {
    0x000000, 0x100000, 0x200000, 0x300000, // 00
    0x280000, 0x380000, 0x2d0000, 0x3d0000, // 04
    0x2c8000, 0x3c8000, 0x400000, 0x500000, // 08
    0x420000, 0x520000, 0x440000, 0x540000, // 12
    0x598000, 0x698000, 0x5a0000, 0x6a0000, // 16
    0x5a8000, 0x6a8000, 0x5b0000, 0x6b0000, // 20
    0x5b8000, 0x6b8000, 0x5c0000, 0x6c0000, // 24
    0x5c8000, 0x6c8000, 0x5d0000, 0x6d0000, // 28
    0x458000, 0x558000, 0x460000, 0x560000, // 32
    0x468000, 0x568000, 0x470000, 0x570000, // 36
    0x478000, 0x578000, 0x480000, 0x580000, // 40
    0x488000, 0x588000, 0x490000, 0x590000, // 44
    0x5d8000, 0x6d8000, 0x5e0000, 0x6e0000, // 48
    0x5e8000, 0x6e8000, 0x6e8000, 0x000000, // 52
    0x000000, 0x000000, 0x000000, 0x000000, // 56
    0x000000, 0x000000, 0x000000, 0x000000, // 60
};
uint8_t sma_scramble_garouh[6] = { 4, 8, 14, 2, 11, 13 };

uint32_t sma_addr_mslug3[3] = { 0x2ffff8, 0x2ffffa, 0x2fffe4 };
uint32_t sma_bank_mslug3[64] = {
    0x000000, 0x020000, 0x040000, 0x060000, // 00
    0x070000, 0x090000, 0x0b0000, 0x0d0000, // 04
    0x0e0000, 0x0f0000, 0x120000, 0x130000, // 08
    0x140000, 0x150000, 0x180000, 0x190000, // 12
    0x1a0000, 0x1b0000, 0x1e0000, 0x1f0000, // 16
    0x200000, 0x210000, 0x240000, 0x250000, // 20
    0x260000, 0x270000, 0x2a0000, 0x2b0000, // 24
    0x2c0000, 0x2d0000, 0x300000, 0x310000, // 28
    0x320000, 0x330000, 0x360000, 0x370000, // 32
    0x380000, 0x390000, 0x3c0000, 0x3d0000, // 36
    0x400000, 0x410000, 0x440000, 0x450000, // 40
    0x460000, 0x470000, 0x4a0000, 0x4b0000, // 44
    0x4c0000,
};
uint8_t sma_scramble_mslug3[6] = { 14, 12, 15, 6, 3, 9 };

uint32_t sma_addr_mslug3a[3] = { 0x2ffff8, 0x2ffffa, 0x2fffe4 };
uint32_t sma_bank_mslug3a[64] = {
    0x000000, 0x030000, 0x040000, 0x070000, // 00
    0x080000, 0x0a0000, 0x0c0000, 0x0e0000, // 04
    0x0f0000, 0x100000, 0x130000, 0x140000, // 08
    0x150000, 0x160000, 0x190000, 0x1a0000, // 12
    0x1b0000, 0x1c0000, 0x1f0000, 0x200000, // 16
    0x210000, 0x220000, 0x250000, 0x260000, // 20
    0x270000, 0x280000, 0x2b0000, 0x2c0000, // 24
    0x2d0000, 0x2e0000, 0x310000, 0x320000, // 28
    0x330000, 0x340000, 0x370000, 0x380000, // 32
    0x390000, 0x3a0000, 0x3D0000, 0x3e0000, // 36
    0x400000, 0x410000, 0x440000, 0x450000, // 40
    0x460000, 0x470000, 0x4a0000, 0x4b0000, // 44
    0x4c0000,
};
uint8_t sma_scramble_mslug3a[6] = { 15, 3, 1, 6, 12, 11 };

uint32_t sma_addr_kof2000[3] = { 0x2fffd8, 0x2fffda, 0x2fffec };
uint32_t sma_bank_kof2000[64] = {
    0x000000, 0x100000, 0x200000, 0x300000, // 00
    0x3f7800, 0x4f7800, 0x3ff800, 0x4ff800, // 04
    0x407800, 0x507800, 0x40f800, 0x50f800, // 08
    0x416800, 0x516800, 0x41d800, 0x51d800, // 12
    0x424000, 0x524000, 0x523800, 0x623800, // 16
    0x526000, 0x626000, 0x528000, 0x628000, // 20
    0x52a000, 0x62a000, 0x52b800, 0x62b800, // 24
    0x52d000, 0x62d000, 0x52e800, 0x62e800, // 28
    0x618000, 0x619000, 0x61a000, 0x61a800, // 32
};
uint8_t sma_scramble_kof2000[6] = { 15, 14, 7, 3, 10, 5 };

#endif
