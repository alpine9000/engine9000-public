#include "geo_protect.h"

#include <string.h>

typedef struct geo_protect_entry
{
    int used;
    uint32_t addr;
    uint32_t addrMask;
    uint32_t sizeBits;
    uint32_t mode;
    uint32_t value;

    uint8_t sizeBytes;
    uint8_t valueBytes[4];
    uint32_t addrBytes[4];
} geo_protect_entry_t;

static geo_protect_entry_t geo_protect_entries[GEO_PROTECT_COUNT];
static uint64_t geo_protect_enabledMask = 0;

// 24-bit address space, 4K pages => 4096 pages => 4096-bit mask => 64 u64s.
static uint64_t geo_protect_pageMask[64];

static uint32_t
geo_protect_maskForAddr(uint32_t addr24)
{
    addr24 &= 0x00ffffffu;
    if (addr24 >= 0x00100000u && addr24 < 0x00200000u) {
        // Main RAM is mirrored every 64K in this 1MB window.
        return 0x00f0ffffu;
    }
    if (addr24 >= 0x00d00000u && addr24 < 0x00e00000u) {
        // Backup RAM is mirrored every 64K in this 1MB window.
        return 0x00f0ffffu;
    }
    return 0x00ffffffu;
}

static uint32_t
geo_protect_canonicalAddr(uint32_t addr24)
{
    addr24 &= 0x00ffffffu;
    if (addr24 >= 0x00100000u && addr24 < 0x00200000u) {
        return 0x00100000u | (addr24 & 0xffffu);
    }
    if (addr24 >= 0x00d00000u && addr24 < 0x00e00000u) {
        return 0x00d00000u | (addr24 & 0xffffu);
    }
    return addr24;
}

static uint8_t
geo_protect_sizeBytes(uint32_t sizeBits)
{
    if (sizeBits == 8u) {
        return 1;
    }
    if (sizeBits == 16u) {
        return 2;
    }
    if (sizeBits == 32u) {
        return 4;
    }
    return 0;
}

static uint32_t
geo_protect_maskValue(uint32_t v, uint32_t sizeBits)
{
    if (sizeBits == 8u) {
        return v & 0xffu;
    }
    if (sizeBits == 16u) {
        return v & 0xffffu;
    }
    return v;
}

static void
geo_protect_fillEntry(geo_protect_entry_t *e)
{
    if (!e) {
        return;
    }

    e->sizeBytes = geo_protect_sizeBytes(e->sizeBits);
    memset(e->valueBytes, 0, sizeof(e->valueBytes));
    memset(e->addrBytes, 0, sizeof(e->addrBytes));

    for (uint8_t i = 0; i < e->sizeBytes; ++i) {
        uint8_t shift = (uint8_t)((e->sizeBytes - 1u - i) * 8u);
        e->valueBytes[i] = (uint8_t)((e->value >> shift) & 0xffu);
        e->addrBytes[i] = (e->addr + (uint32_t)i) & 0x00ffffffu;
    }
}

static void
geo_protect_rebuildPageMask(void)
{
    memset(geo_protect_pageMask, 0, sizeof(geo_protect_pageMask));

    if (geo_protect_enabledMask == 0) {
        return;
    }

    for (uint32_t index = 0; index < GEO_PROTECT_COUNT; ++index) {
        if (!geo_protect_entries[index].used) {
            continue;
        }
        if (((geo_protect_enabledMask >> index) & 1u) == 0u) {
            continue;
        }
        const geo_protect_entry_t *e = &geo_protect_entries[index];
        for (uint8_t i = 0; i < e->sizeBytes; ++i) {
            uint32_t addr24 = e->addrBytes[i] & 0x00ffffffu;
            uint32_t page = addr24 >> 12;
            geo_protect_pageMask[page >> 6] |= (1ull << (page & 63u));
        }
    }
}

void
geo_protect_reset(void)
{
    memset(geo_protect_entries, 0, sizeof(geo_protect_entries));
    geo_protect_enabledMask = 0;
    geo_protect_rebuildPageMask();
}

int
geo_protect_add(uint32_t addr24, uint32_t sizeBits, uint32_t mode, uint32_t value)
{
    uint8_t sizeBytes = geo_protect_sizeBytes(sizeBits);
    if (sizeBytes == 0) {
        return -1;
    }
    if (mode != GEO_PROTECT_MODE_BLOCK && mode != GEO_PROTECT_MODE_SET) {
        return -1;
    }

    uint32_t canonAddr = geo_protect_canonicalAddr(addr24);
    uint32_t addrMask = geo_protect_maskForAddr(addr24);
    uint32_t maskedValue = geo_protect_maskValue(value, sizeBits);

    for (uint32_t i = 0; i < GEO_PROTECT_COUNT; ++i) {
        const geo_protect_entry_t *e = &geo_protect_entries[i];
        if (!e->used) {
            continue;
        }
        if (e->addr == canonAddr &&
            e->addrMask == addrMask &&
            e->sizeBits == sizeBits &&
            e->mode == mode &&
            e->value == maskedValue) {
            return (int)i;
        }
    }

    for (uint32_t i = 0; i < GEO_PROTECT_COUNT; ++i) {
        if (geo_protect_entries[i].used) {
            continue;
        }
        geo_protect_entry_t *e = &geo_protect_entries[i];
        memset(e, 0, sizeof(*e));
        e->used = 1;
        e->addr = canonAddr;
        e->addrMask = addrMask;
        e->sizeBits = sizeBits;
        e->mode = mode;
        e->value = maskedValue;
        geo_protect_fillEntry(e);
        geo_protect_enabledMask |= (1ull << i);
        geo_protect_rebuildPageMask();
        return (int)i;
    }

    return -1;
}

void
geo_protect_remove(uint32_t index)
{
    if (index >= GEO_PROTECT_COUNT) {
        return;
    }
    memset(&geo_protect_entries[index], 0, sizeof(geo_protect_entries[index]));
    geo_protect_enabledMask &= ~(1ull << index);
    geo_protect_rebuildPageMask();
}

size_t
geo_protect_read(geo_debug_protect_t *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    size_t count = GEO_PROTECT_COUNT;
    if (cap < count) {
        count = cap;
    }
    for (size_t i = 0; i < count; ++i) {
        const geo_protect_entry_t *e = &geo_protect_entries[i];
        out[i].addr = e->addr;
        out[i].addrMask = e->addrMask;
        out[i].sizeBits = e->sizeBits;
        out[i].mode = e->mode;
        out[i].value = e->value;
    }
    return count;
}

uint64_t
geo_protect_getEnabledMask(void)
{
    return geo_protect_enabledMask;
}

void
geo_protect_setEnabledMask(uint64_t mask)
{
    geo_protect_enabledMask = mask;
    geo_protect_rebuildPageMask();
}

void
geo_protect_filterWrite(uint32_t addr24, uint32_t sizeBits, uint32_t oldValue, int oldValueValid, uint32_t *inoutValue)
{
    if (!inoutValue) {
        return;
    }
    if (geo_protect_enabledMask == 0) {
        return;
    }

    uint8_t sizeBytes = geo_protect_sizeBytes(sizeBits);
    if (sizeBytes == 0) {
        return;
    }

    addr24 &= 0x00ffffffu;

    int anyPage = 0;
    for (uint8_t i = 0; i < sizeBytes; ++i) {
        uint32_t a = (addr24 + (uint32_t)i) & 0x00ffffffu;
        uint32_t page = a >> 12;
        if ((geo_protect_pageMask[page >> 6] >> (page & 63u)) & 1ull) {
            anyPage = 1;
            break;
        }
    }
    if (!anyPage) {
        return;
    }

    uint8_t bytes[4] = {0};
    uint8_t oldBytes[4] = {0};
    uint32_t v = geo_protect_maskValue(*inoutValue, sizeBits);

    for (uint8_t i = 0; i < sizeBytes; ++i) {
        uint8_t shift = (uint8_t)((sizeBytes - 1u - i) * 8u);
        bytes[i] = (uint8_t)((v >> shift) & 0xffu);
    }
    if (oldValueValid) {
        uint32_t ov = geo_protect_maskValue(oldValue, sizeBits);
        for (uint8_t i = 0; i < sizeBytes; ++i) {
            uint8_t shift = (uint8_t)((sizeBytes - 1u - i) * 8u);
            oldBytes[i] = (uint8_t)((ov >> shift) & 0xffu);
        }
    }

    for (uint8_t writeIndex = 0; writeIndex < sizeBytes; ++writeIndex) {
        uint32_t writeAddr = (addr24 + (uint32_t)writeIndex) & 0x00ffffffu;
        uint32_t page = writeAddr >> 12;
        if (((geo_protect_pageMask[page >> 6] >> (page & 63u)) & 1ull) == 0ull) {
            continue;
        }

        for (uint32_t entryIndex = 0; entryIndex < GEO_PROTECT_COUNT; ++entryIndex) {
            if (!geo_protect_entries[entryIndex].used) {
                continue;
            }
            if (((geo_protect_enabledMask >> entryIndex) & 1ull) == 0ull) {
                continue;
            }

            const geo_protect_entry_t *e = &geo_protect_entries[entryIndex];
            for (uint8_t byteIndex = 0; byteIndex < e->sizeBytes; ++byteIndex) {
                if ((writeAddr & e->addrMask) != (e->addrBytes[byteIndex] & e->addrMask)) {
                    continue;
                }

                if (e->mode == GEO_PROTECT_MODE_SET) {
                    bytes[writeIndex] = e->valueBytes[byteIndex];
                } else if (oldValueValid) {
                    bytes[writeIndex] = oldBytes[writeIndex];
                }
                goto next_write_byte;
            }
        }
next_write_byte:
        ;
    }

    uint32_t outValue = 0;
    for (uint8_t i = 0; i < sizeBytes; ++i) {
        outValue = (outValue << 8) | (uint32_t)bytes[i];
    }
    *inoutValue = outValue;
}

