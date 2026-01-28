/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <string.h>
#include <stdint.h>

#include "dasm.h"
#include "alloc.h"
#include "libretro_host.h"

typedef struct dasm_ami_cache {
    char **lines;
    uint64_t *addrs;
    int n;
    int cap;
    uint32_t anchorAddr;
    int anchorValid;
} dasm_ami_cache_t;

static dasm_ami_cache_t g_dasmAmi;

static void
dasm_ami_clear(void)
{
    if (g_dasmAmi.lines) {
        for (int i = 0; i < g_dasmAmi.n; ++i) {
            alloc_free(g_dasmAmi.lines[i]);
        }
        alloc_free(g_dasmAmi.lines);
    }
    alloc_free(g_dasmAmi.addrs);
    memset(&g_dasmAmi, 0, sizeof(g_dasmAmi));
}

static void
dasm_ami_init(void)
{
    memset(&g_dasmAmi, 0, sizeof(g_dasmAmi));
}

static void
dasm_ami_shutdown(void)
{
    dasm_ami_clear();
}

static int
dasm_ami_preloadText(void)
{
    // Dynamic disassembly: nothing to preload.
    return 1;
}

static int
dasm_ami_getTotal(void)
{
    // Streaming disassembly: no finite total.
    return 0;
}

static int
dasm_ami_getAddrHexWidth(void)
{
    return 6;
}

static const char *
dasm_ami_stripBytes(const char *text)
{
    if (!text) {
        return "";
    }
    const char *p = text;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    // PUAE m68k_disasm_2() typically emits:
    //   "%08X " [pc] + optional "[%06X] " + optional words "%04X " ... + instr text
    // We already show the address separately in the UI, so strip these prefixes.
    int addrDigits = 0;
    while (addrDigits < 8) {
        char c = p[addrDigits];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            break;
        }
        addrDigits++;
    }
    if (addrDigits == 8 && p[8] == ' ') {
        p += 9;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }

    if (p[0] == '[') {
        const char *rb = strchr(p, ']');
        if (rb) {
            p = rb + 1;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
        }
    }

    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        // Skip illegal marker "[ " if present.
        if (p[0] == '[' && p[1] == ' ') {
            p += 2;
            continue;
        }
        // Skip instruction word dump tokens like "4E75 ".
        int tokLen = 0;
        while (p[tokLen] && p[tokLen] != ' ' && p[tokLen] != '\t') {
            tokLen++;
        }
        int isHex = 1;
        for (int i = 0; i < tokLen; ++i) {
            char c = p[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                isHex = 0;
                break;
            }
        }
        if (isHex && tokLen == 4) {
            p += tokLen;
            continue;
        }
        break;
    }

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static uint32_t
dasm_ami_normAddr(uint32_t addr)
{
    addr &= 0x00ffffffu;
    addr &= ~1u;
    return addr;
}

static uint32_t
dasm_ami_nextInstr(uint32_t addr, size_t lenHint)
{
    uint32_t cur = dasm_ami_normAddr(addr);
    size_t len = lenHint;
    if (len == 0) {
        char buf[64];
        if (!libretro_host_debugDisassembleQuick(cur, buf, sizeof(buf), &len) || len == 0) {
            len = 2;
        }
    }
    if (len > 0x1000u) {
        len = 2;
    }
    uint32_t next = (uint32_t)(cur + (uint32_t)len);
    next = dasm_ami_normAddr(next);
    if (next == cur) {
        next = dasm_ami_normAddr(cur + 2u);
    }
    return next;
}

static uint32_t
dasm_ami_prevInstr(uint32_t addr)
{
    uint32_t cur = dasm_ami_normAddr(addr);
    if (cur < 2u) {
        return 0u;
    }
    uint32_t prev = dasm_ami_normAddr(cur - 2u);
    const uint32_t maxBackBytes = 64u;
    for (uint32_t back = 2u; back <= maxBackBytes && back <= cur; back += 2u) {
        uint32_t cand = dasm_ami_normAddr(cur - back);
        char buf[64];
        size_t len = 0;
        if (!libretro_host_debugDisassembleQuick(cand, buf, sizeof(buf), &len) || len == 0) {
            continue;
        }
        if (len > 0x1000u) {
            continue;
        }
        uint32_t next = dasm_ami_normAddr(cand + (uint32_t)len);
        if (next == cur) {
            prev = cand;
            break;
        }
    }
    return prev;
}

static int
dasm_ami_findIndexForAddr(uint64_t addr, int *out_index)
{
    g_dasmAmi.anchorAddr = dasm_ami_normAddr((uint32_t)addr);
    g_dasmAmi.anchorValid = 1;
    if (out_index) {
        // "Index space" is relative to this anchor.
        *out_index = 0;
    }
    return 1;
}

static int
dasm_ami_ensureCapacity(int want)
{
    if (want < 1) {
        want = 1;
    }
    if (g_dasmAmi.cap >= want && g_dasmAmi.lines && g_dasmAmi.addrs) {
        return 1;
    }
    int newCap = g_dasmAmi.cap ? g_dasmAmi.cap : 64;
    while (newCap < want) {
        newCap *= 2;
    }
    char **newLines = (char**)alloc_realloc(g_dasmAmi.lines, (size_t)newCap * sizeof(*newLines));
    uint64_t *newAddrs = (uint64_t*)alloc_realloc(g_dasmAmi.addrs, (size_t)newCap * sizeof(*newAddrs));
    if (!newLines || !newAddrs) {
        alloc_free(newLines);
        alloc_free(newAddrs);
        return 0;
    }
    g_dasmAmi.lines = newLines;
    g_dasmAmi.addrs = newAddrs;
    g_dasmAmi.cap = newCap;
    return 1;
}

static int
dasm_ami_getRangeByIndex(int start_index, int end_index,
                         const char ***out_lines,
                         const uint64_t **out_addrs,
                         int *out_first_index,
                         int *out_count)
{
    if (!out_lines || !out_addrs || !out_first_index || !out_count) {
        return 0;
    }
    *out_lines = NULL;
    *out_addrs = NULL;
    *out_first_index = 0;
    *out_count = 0;

    if (!g_dasmAmi.anchorValid) {
        // Caller should call findIndexForAddr() first.
        return 0;
    }
    if (end_index < start_index) {
        end_index = start_index;
    }
    int want = end_index - start_index + 1;
    if (!dasm_ami_ensureCapacity(want)) {
        return 0;
    }
    for (int i = 0; i < g_dasmAmi.n; ++i) {
        alloc_free(g_dasmAmi.lines[i]);
        g_dasmAmi.lines[i] = NULL;
    }
    g_dasmAmi.n = 0;

    // WinUAE-style behavior: treat the current PC (anchor) as an instruction boundary,
    // even if bytes immediately before could be decoded as an instruction whose extension
    // word overlaps the anchor. To keep the anchor visible in the window, compute the
    // address for each relative index independently around the anchor.
    const uint32_t anchorAddr = g_dasmAmi.anchorAddr;
    const int negCount = (start_index < 0) ? -start_index : 0;
    const int posCount = (end_index > 0) ? end_index : 0;

    uint32_t *negAddrs = NULL;
    uint32_t *posAddrs = NULL;

    if (negCount > 0) {
        negAddrs = (uint32_t*)alloc_calloc((size_t)negCount, sizeof(*negAddrs));
        if (!negAddrs) {
            return 0;
        }
        uint32_t cur = anchorAddr;
        for (int i = 0; i < negCount; ++i) {
            cur = dasm_ami_prevInstr(cur);
            negAddrs[i] = cur;
        }
    }
    if (posCount > 0) {
        posAddrs = (uint32_t*)alloc_calloc((size_t)(posCount + 1), sizeof(*posAddrs));
        if (!posAddrs) {
            alloc_free(negAddrs);
            return 0;
        }
        posAddrs[0] = anchorAddr;
        for (int i = 1; i <= posCount; ++i) {
            posAddrs[i] = dasm_ami_nextInstr(posAddrs[i - 1], 0);
        }
    }

    for (int i = 0; i < want; ++i) {
        const int rel = start_index + i;
        uint32_t addr = anchorAddr;
        if (rel < 0) {
            const int idx = (-rel) - 1;
            if (idx >= 0 && idx < negCount) {
                addr = negAddrs[idx];
            }
        } else if (rel > 0) {
            if (rel <= posCount && posAddrs) {
                addr = posAddrs[rel];
            } else {
                // Shouldn't happen, but keep behavior defined.
                addr = anchorAddr;
                for (int step = 0; step < rel; ++step) {
                    addr = dasm_ami_nextInstr(addr, 0);
                }
            }
        }

        char insBuf[256];
        size_t len = 0;
        insBuf[0] = '\0';
        if (!libretro_host_debugDisassembleQuick(addr, insBuf, sizeof(insBuf), &len) || len == 0) {
            strncpy(insBuf, "??", sizeof(insBuf) - 1);
            insBuf[sizeof(insBuf) - 1] = '\0';
            len = 2;
        }
        if (len > 0x1000u) {
            len = 2;
        }
        g_dasmAmi.addrs[i] = (uint64_t)dasm_ami_normAddr(addr);
        g_dasmAmi.lines[i] = alloc_strdup(dasm_ami_stripBytes(insBuf));
        g_dasmAmi.n++;
    }

    alloc_free(negAddrs);
    alloc_free(posAddrs);

    *out_lines = (const char**)g_dasmAmi.lines;
    *out_addrs = (const uint64_t*)g_dasmAmi.addrs;
    *out_first_index = start_index;
    *out_count = want;
    return 1;
}

const dasm_iface_t
dasm_ami_iface = {
    .flags = DASM_IFACE_FLAG_STREAMING,
    .init = dasm_ami_init,
    .shutdown = dasm_ami_shutdown,
    .preloadText = dasm_ami_preloadText,
    .getTotal = dasm_ami_getTotal,
    .getAddrHexWidth = dasm_ami_getAddrHexWidth,
    .findIndexForAddr = dasm_ami_findIndexForAddr,
    .getRangeByIndex = dasm_ami_getRangeByIndex,
};
