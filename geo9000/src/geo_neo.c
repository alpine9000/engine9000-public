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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "geo.h"
#include "geo_lspc.h"
#include "geo_m68k.h"
#include "geo_neo.h"
#include "geo_sma.h"
#include "geo_z80.h"

static uint32_t flags = 0;

static inline uint32_t read32le(uint8_t *ptr, uint32_t addr) {
    return ptr[addr] | (ptr[addr + 1] << 8) |
        (ptr[addr + 2] << 16) | (ptr[addr + 3] << 24);
}

int geo_neo_load(void *data, size_t size) {
    uint8_t *neodata = (uint8_t*)data; // Assign internal pointer to NEO data
    if (size) { }

    // Bytes 0-3 should be 'N' 'E' 'O' 1
    if (neodata[0] != 'N' || neodata[1] != 'E' ||
        neodata[2] != 'O' || neodata[3] != 0x01) {
        geo_log(GEO_LOG_ERR, "Not a valid NEO ROM: %c %c %c %c\n",
            neodata[0], neodata[1], neodata[2], neodata[3]);
        return 0;
    }

    // Get pointer to ROM data struct
    romdata_t *romdata = geo_romdata_ptr();

    // Parse ROM info
    romdata->psz = read32le(neodata, 4);
    romdata->ssz = read32le(neodata, 8);
    romdata->msz = read32le(neodata, 12);
    romdata->v1sz = read32le(neodata, 16);
    romdata->v2sz = read32le(neodata, 20);
    romdata->csz = read32le(neodata, 24);

    geo_log(GEO_LOG_INF, "P  ROM Size: %d\n", romdata->psz);
    geo_log(GEO_LOG_INF, "S  ROM Size: %d\n", romdata->ssz);
    geo_log(GEO_LOG_INF, "M1 ROM Size: %d\n", romdata->msz);
    geo_log(GEO_LOG_INF, "V1 ROM Size: %d\n", romdata->v1sz);
    geo_log(GEO_LOG_INF, "V2 ROM Size: %d\n", romdata->v2sz);
    geo_log(GEO_LOG_INF, "C  ROM Size: %d\n", romdata->csz);

    // Parse Year, Genre, Screenshot, and NGH info
    uint32_t year = read32le(neodata, 28);
    uint32_t genre = read32le(neodata, 32);
    uint32_t screenshot = read32le(neodata, 36);
    uint32_t ngh = read32le(neodata, 40);

    // Parse Name
    char name[34];
    memcpy((char*)name, (uint8_t*)neodata + 44, 33);
    name[33] = '\0';

    // Parse Manufacturer
    char manu[18];
    memcpy((char*)manu, (uint8_t*)neodata + 77, 17);
    manu[17] = '\0';

    geo_log(GEO_LOG_INF, "Year: %d\n", year);
    geo_log(GEO_LOG_INF, "Genre: %d\n", genre);
    geo_log(GEO_LOG_INF, "Screenshot: %d\n", screenshot);
    geo_log(GEO_LOG_INF, "NGH: %03X\n", ngh);
    geo_log(GEO_LOG_INF, "Name: %s\n", name);
    geo_log(GEO_LOG_INF, "Manufacturer: %s\n", manu);

    // Assign pointers and sizes
    uint32_t rom_offset = 4096; // ROM data starts at byte 4096
    romdata->p = &neodata[rom_offset];
    rom_offset += romdata->psz;

    romdata->s = &neodata[rom_offset];
    rom_offset += romdata->ssz;

    romdata->m = &neodata[rom_offset];
    rom_offset += romdata->msz;

    if (!romdata->v2sz) { // Single V ROM
        romdata->v1 = romdata->v2 = &neodata[rom_offset];
        romdata->v2sz = romdata->v1sz;
        rom_offset += romdata->v1sz;
    }
    else { // V1 and V2 ROMs
        romdata->v1 = &neodata[rom_offset];
        romdata->v2 = &neodata[rom_offset + romdata->v1sz];
        rom_offset += romdata->v1sz + romdata->v2sz;
    }

    romdata->c = &neodata[rom_offset];

    // Perform C ROM mask calculation
    geo_lspc_postload();

    // Set FIX data to S ROM by default - handles AES and MVS use cases
    geo_lspc_set_fix(LSPC_FIX_CART);

    // Set default Fix Layer bank switching type
    geo_lspc_set_fix_banksw(FIX_BANKSW_NONE);

    // Perform byte swapping on the BIOS and P ROM, calculate P bankswitch mask
    geo_m68k_postload();

    // Set default bankswitching
    geo_m68k_board_set(BOARD_DEFAULT);

    // Handle special cases if necessary
    switch (ngh) {
        case 0x006: case 0x019: case 0x038: {
            // Riding Hero, League Bowling, Thrash Rally
            geo_m68k_board_set(BOARD_LINKABLE);
            break;
        }
        case 0x008: { // Jockey Grand Prix
            geo_m68k_board_set(BOARD_BREZZASOFT);
            break;
        }
        case 0x004: case 0x027: case 0x036: case 0x048: {
            /* Mahjong Controller Games:
                 Mahjong Kyo Retsuden
                 Minasan no Okagesamadesu!
                 Bakatonosama Mahjong Manyuuki
                 Janshin Densetsu
               Idol Mahjong Final Romance 2 is a CD to cartridge bootleg and
               does not support the Mahjong Controller.
            */
            flags |= GEO_DB_MAHJONG;
            break;
        }
        case 0x047: case 0x052: { // Fatal Fury 2, Super Sidekicks
            geo_m68k_board_set(BOARD_CT0);
            break;
        }
        case 0x066: { // Digger Man (prototype), Karnov's Revenge
            /* Digger Man (prototype) is not compatible with the AES. Digger
               Man (prototype) also shares the same NGH with Karnov's Revenge,
               so use the P ROM size to differentiate the games.
            */
            if (romdata->psz < 0x100000 && geo_get_system() == SYSTEM_AES) {
                geo_log(GEO_LOG_ERR, "This title is compatible with the Neo"
                    " Geo MVS and Universe BIOS only\n");
                return 0;
            }
            break;
        }
        case 0x080: { // Quiz King of Fighters
            if (geo_get_region() == REGION_US &&
                geo_get_system() == SYSTEM_MVS) {
                geo_log(GEO_LOG_ERR, "This title is incompatible with US MVS"
                    " systems\n");
                return 0;
            }
            break;
        }
        case 0x236: { // The Irritating Maze
            flags |= GEO_DB_IRRMAZE;
            break;
        }
        case 0x242: { // KOF 98
            geo_m68k_board_set(BOARD_KOF98);
            break;
        }
        case 0x250: { // Metal Slug X
            geo_m68k_board_set(BOARD_MSLUGX);
            break;
        }
        case 0x151: case 0x251: { // KOF 99
            if (romdata->psz > 0x500000) { // Encrypted
                geo_m68k_sma_init(sma_addr_kof99, sma_bank_kof99,
                    sma_scramble_kof99);
                geo_m68k_board_set(BOARD_SMA);
            }
            break;
        }
        case 0x253: { // Garou - Mark of the Wolves
            // Use heuristics to handle descrambling differences
            if (neodata[0xc1000 + 0x3e481] == 0x9f) { // NEO-SMA KE (AES)
                geo_m68k_sma_init(sma_addr_garouh, sma_bank_garouh,
                    sma_scramble_garouh);
                geo_m68k_board_set(BOARD_SMA);
                geo_lspc_set_fix_banksw(FIX_BANKSW_LINE);
            }
            else if (neodata[0xc1000 + 0x3e481] == 0x41) { // NEO-SMA KF (MVS)
                geo_m68k_sma_init(sma_addr_garou, sma_bank_garou,
                    sma_scramble_garou);
                geo_m68k_board_set(BOARD_SMA);
                geo_lspc_set_fix_banksw(FIX_BANKSW_LINE);
            }
            // No SMA protection or FIX bankswitching for Bootleg and Prototype
            break;
        }
        case 0x256: { // Metal Slug 3
            geo_lspc_set_fix_banksw(FIX_BANKSW_LINE);

            if (romdata->psz > 0x500000) { // Encrypted
                if (neodata[0x1000 + 0x141] == 0x33) { // mslug3a
                    geo_m68k_sma_init(sma_addr_mslug3a, sma_bank_mslug3a,
                        sma_scramble_mslug3a);
                }
                else { // mslug3, mslug3h
                    geo_m68k_sma_init(sma_addr_mslug3, sma_bank_mslug3,
                        sma_scramble_mslug3);
                }
                geo_m68k_board_set(BOARD_SMA);
            }
            break;
        }
        case 0x257: { // KOF 2000
            geo_lspc_set_fix_banksw(FIX_BANKSW_TILE);

            if (romdata->psz > 0x500000) { // Encrypted
                geo_m68k_sma_init(sma_addr_kof2000, sma_bank_kof2000,
                    sma_scramble_kof2000);
                geo_m68k_board_set(BOARD_SMA);
            }
            break;
        }
        case 0x263: { // Metal Slug 4
            // mslug4 and mslug4h use FIX bankswitching, ms4plus does not
            if (neodata[0x1000 + 0x809] != 0x0c) {
                geo_lspc_set_fix_banksw(FIX_BANKSW_LINE);
            }
            else if (geo_get_system() == SYSTEM_AES) {
                geo_log(GEO_LOG_ERR, "This title is compatible with the Neo"
                    " Geo MVS and Universe BIOS only\n");
                return 0;
            }
            break;
        }
        case 0x266: { // Matrimelee
            /* Fix the FIX layer! TerraOnion's NeoBuilder tool decrypts this
               incorrectly for matrimbl. Other free tools seem to do it the
               same way, so the pragmatic solution is to do it properly here.
               The last 512K block of C ROM contains the encrypted FIX layer
               data.
            */
            if (neodata[0x1000 + 0x500088] == 0x22) { // matrimbl
                uint8_t *ptr = romdata->c + (romdata->csz - romdata->ssz);
                for (size_t i = 0; i < romdata->ssz; ++i) {
                    romdata->s[i] = ptr[(i & ~0x1f) + ((i & 0x07) << 2) +
                            ((~i & 0x08) >> 2) + ((i & 0x10) >> 4)];
                }
            }
            geo_lspc_set_fix_banksw(FIX_BANKSW_TILE);
            break;
        }
        case 0x268: { // Metal Slug 5
            if (neodata[0x1000 + 0x26b] == 0xb9) { // Metal Slug 5 Plus
                if (geo_get_system() == SYSTEM_AES) {
                    geo_log(GEO_LOG_ERR, "This title is compatible with the"
                        " Neo Geo MVS and Universe BIOS only\n");
                    return 0;
                }
                geo_m68k_board_set(BOARD_MS5PLUS);
            }
            else if (neodata[0x1000 + 0x267] == 0x4f) // Official Releases
                geo_m68k_board_set(BOARD_PVC);
            break;
        }
        case 0x269: { // SNK vs. Capcom - SVC Chaos
            if (neodata[0x1000 + 0x9e91] == 0x0f) { // MVS-only bootlegs
                if (geo_get_system() == SYSTEM_AES) {
                    geo_log(GEO_LOG_ERR, "This title is compatible with the"
                        " Neo Geo MVS and Universe BIOS only\n");
                    return 0;
                }
            }

            /* The official release, SNK vs. Capcom (bootleg), and
               SNK vs. Capcom Super Plus use the NEO-PVC. The official release
               also uses Fix Bankswitching Type 2 (per-tile banking). The other
               known bootlegs use neither.
            */
            if (neodata[0x1000 + 0x3d25] == 0xc4) // Official Release Only
                geo_lspc_set_fix_banksw(FIX_BANKSW_TILE);
            if (neodata[0x1000 + 0x2f8f] == 0xc0)
                geo_m68k_board_set(BOARD_PVC);

            /* This game will trigger watchdog early in some cases before the
               bonus level.
            */
            geo_set_watchdog_frames(9);
            break;
        }
        case 0x271: { // KOF 2003
            if (neodata[0x1000 + 0x689] == 0x10) { // kf2k3bla/kf2k3pl
                geo_m68k_board_set(BOARD_KF2K3BLA);
            }
            else if (neodata[0x1000 + 0xc1] == 0x02) { // kf2k3bl and kf2k3upl
                geo_m68k_board_set(BOARD_KF2K3BL);
            }
            else { // Official Releases
                geo_lspc_set_fix_banksw(FIX_BANKSW_TILE);
                geo_m68k_board_set(BOARD_PVC);
            }
            break;
        }
        case 0x275: { // The King of Fighters 10th Anniversary
            if (geo_get_system() == SYSTEM_AES) {
                geo_log(GEO_LOG_ERR, "This title is compatible with the Neo"
                    " Geo MVS and Universe BIOS only\n");
                return 0;
            }

            // Special handling for original bootleg, not KoF 10th 2005 Unique
            if (neodata[0x1000 + 0x125] == 0x00)
                geo_m68k_board_set(BOARD_KOF10TH);
            break;
        }
        case 0x3e7: case 0x999: { // V-Liner
            /* Some tools set V-Liner's NGH to 999 and others to 3E7. 3E7 is
               correct, but 0x3e7 is 999 in decimal, so it is possible the
               developer intended for it to be 999 but did not realize the NGH
               field is supposed to be BCD, not hex.
            */
            flags |= GEO_DB_VLINER;
            geo_m68k_board_set(BOARD_BREZZASOFT);
            break;
        }
        case 0x5003: { // Crouching Tiger Hidden Dragon bootlegs
            if (geo_get_system() == SYSTEM_AES) {
                geo_log(GEO_LOG_ERR, "This title is compatible with the Neo"
                    " Geo MVS and Universe BIOS only\n");
                return 0;
            }

            /* Crouching Tiger Hidden Dragon 2003 Super Plus Alternative
               requires no special handling.
            */
            if (neodata[0x1000 + 0x30d9] != 0x03)
                geo_m68k_board_set(BOARD_CTHD2003);
            break;
        }
        default: {
            break;
        }
    }

    return 1;
}

uint32_t geo_neo_flags(void) {
    return flags;
}
