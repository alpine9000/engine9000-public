/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "protect.h"

#include "debug.h"
#include "libretro_host.h"

static int
protect_sizeOk(uint32_t sizeBits)
{
    return sizeBits == 8 || sizeBits == 16 || sizeBits == 32;
}

void
protect_clear(void)
{
    (void)libretro_host_debugResetProtects();
}

int
protect_addBlock(uint32_t addr24, uint32_t sizeBits)
{
    addr24 &= 0x00ffffffu;
    if (!protect_sizeOk(sizeBits)) {
        return 0;
    }
    uint32_t index = 0;
    return libretro_host_debugAddProtect(addr24, sizeBits, GEO_PROTECT_MODE_BLOCK, 0, &index) ? 1 : 0;
}

int
protect_addSet(uint32_t addr24, uint32_t value, uint32_t sizeBits)
{
    addr24 &= 0x00ffffffu;
    if (!protect_sizeOk(sizeBits)) {
        return 0;
    }
    uint32_t index = 0;
    return libretro_host_debugAddProtect(addr24, sizeBits, GEO_PROTECT_MODE_SET, value, &index) ? 1 : 0;
}

int
protect_remove(uint32_t addr24, uint32_t sizeBits)
{
    addr24 &= 0x00ffffffu;

    geo_debug_protect_t protects[GEO_PROTECT_COUNT];
    size_t count = 0;
    if (!libretro_host_debugReadProtects(protects, GEO_PROTECT_COUNT, &count)) {
        return 0;
    }

    uint64_t enabledMask = 0;
    if (!libretro_host_debugGetProtectEnabledMask(&enabledMask)) {
        return 0;
    }

    for (uint32_t i = 0; i < (uint32_t)count; ++i) {
        if (((enabledMask >> i) & 1ull) == 0ull) {
            continue;
        }
        const geo_debug_protect_t *p = &protects[i];
        if (sizeBits != 0 && p->sizeBits != sizeBits) {
            continue;
        }
        if ((addr24 & p->addrMask) != (p->addr & p->addrMask)) {
            continue;
        }
        return libretro_host_debugRemoveProtect(i) ? 1 : 0;
    }

    return 0;
}

int
protect_handleWatchbreak(const geo_debug_watchbreak_t *wb)
{
    (void)wb;
    // Core-side protect does not use watchbreaks.
    return 0;
}

void
protect_debugList(void)
{
    geo_debug_protect_t protects[GEO_PROTECT_COUNT];
    size_t count = 0;
    if (!libretro_host_debugReadProtects(protects, GEO_PROTECT_COUNT, &count)) {
        debug_printf("protect: unavailable\n");
        return;
    }

    uint64_t enabledMask = 0;
    if (!libretro_host_debugGetProtectEnabledMask(&enabledMask)) {
        debug_printf("protect: unavailable\n");
        return;
    }

    int enabledCount = 0;
    for (uint32_t i = 0; i < (uint32_t)count; ++i) {
        if (((enabledMask >> i) & 1ull) != 0ull) {
            enabledCount++;
        }
    }

    debug_printf("protect: %d entr%s\n", enabledCount, enabledCount == 1 ? "y" : "ies");
    for (uint32_t i = 0; i < (uint32_t)count; ++i) {
        if (((enabledMask >> i) & 1ull) == 0ull) {
            continue;
        }
        const geo_debug_protect_t *p = &protects[i];
        const char *mode = p->mode == GEO_PROTECT_MODE_SET ? "set" : "block";
        if (p->mode == GEO_PROTECT_MODE_SET) {
            debug_printf("  [%u] %s addr=0x%06X size=%u val=0x%08X mask=0x%06X\n",
                         (unsigned)i, mode,
                         (unsigned)(p->addr & 0x00ffffffu),
                         (unsigned)p->sizeBits,
                         (unsigned)p->value,
                         (unsigned)(p->addrMask & 0x00ffffffu));
        } else {
            debug_printf("  [%u] %s addr=0x%06X size=%u mask=0x%06X\n",
                         (unsigned)i, mode,
                         (unsigned)(p->addr & 0x00ffffffu),
                         (unsigned)p->sizeBits,
                         (unsigned)(p->addrMask & 0x00ffffffu));
        }
    }
}

