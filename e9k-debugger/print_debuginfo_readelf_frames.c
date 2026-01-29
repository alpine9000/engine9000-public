/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "print_debuginfo_readelf_frames.h"

#include "alloc.h"
#include "debugger.h"

static int
print_debuginfo_readelf_frames_parseHexRange(const char *s, uint32_t *outA, uint32_t *outB)
{
    if (outA) {
        *outA = 0;
    }
    if (outB) {
        *outB = 0;
    }
    if (!s || !outA || !outB) {
        return 0;
    }
    const char *p = strstr(s, "pc=");
    if (!p) {
        return 0;
    }
    p += 3;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    errno = 0;
    char *end = NULL;
    unsigned long a = strtoul(p, &end, 16);
    if (errno != 0 || !end || end == p) {
        return 0;
    }
    const char *dots = strstr(end, "..");
    if (!dots) {
        return 0;
    }
    const char *q = dots + 2;
    errno = 0;
    end = NULL;
    unsigned long b = strtoul(q, &end, 16);
    if (errno != 0 || !end || end == q) {
        return 0;
    }
    *outA = (uint32_t)a;
    *outB = (uint32_t)b;
    return 1;
}

static int
print_debuginfo_readelf_frames_parseCfa(const char *s, uint8_t *outReg, int32_t *outOffset)
{
    if (outReg) {
        *outReg = 0;
    }
    if (outOffset) {
        *outOffset = 0;
    }
    if (!s || !outReg || !outOffset) {
        return 0;
    }
    // Expected: r15+4, r15-12, etc.
    if (s[0] != 'r') {
        return 0;
    }
    const char *p = s + 1;
    errno = 0;
    char *end = NULL;
    unsigned long reg = strtoul(p, &end, 10);
    if (errno != 0 || !end || end == p) {
        return 0;
    }
    int sign = 0;
    if (*end == '+') {
        sign = 1;
        ++end;
    } else if (*end == '-') {
        sign = -1;
        ++end;
    } else {
        return 0;
    }
    errno = 0;
    char *end2 = NULL;
    long off = strtol(end, &end2, 10);
    if (errno != 0 || !end2 || end2 == end) {
        return 0;
    }
    *outReg = (uint8_t)reg;
    *outOffset = (int32_t)(sign * (int)off);
    return 1;
}

static print_cfi_fde_t *
print_debuginfo_readelf_frames_addFde(print_index_t *index, uint32_t pcStart, uint32_t pcEnd, uint8_t defaultReg, int32_t defaultOff)
{
    if (!index) {
        return NULL;
    }
    if (index->fdeCount >= index->fdeCap) {
        int next = index->fdeCap ? index->fdeCap * 2 : 256;
        print_cfi_fde_t *nextFdes = (print_cfi_fde_t *)alloc_realloc(index->fdes, sizeof(*nextFdes) * (size_t)next);
        if (!nextFdes) {
            return NULL;
        }
        index->fdes = nextFdes;
        index->fdeCap = next;
    }
    print_cfi_fde_t *fde = &index->fdes[index->fdeCount++];
    memset(fde, 0, sizeof(*fde));
    fde->pcStart = pcStart;
    fde->pcEnd = pcEnd;
    fde->defaultCfaReg = defaultReg;
    fde->defaultCfaOffset = defaultOff;
    return fde;
}

static int
print_debuginfo_readelf_frames_addRow(print_cfi_fde_t *fde, uint32_t loc, uint8_t reg, int32_t off)
{
    if (!fde) {
        return 0;
    }
    if (fde->rowCount >= fde->rowCap) {
        int next = fde->rowCap ? fde->rowCap * 2 : 8;
        print_cfi_row_t *nextRows = (print_cfi_row_t *)alloc_realloc(fde->rows, sizeof(*nextRows) * (size_t)next);
        if (!nextRows) {
            return 0;
        }
        fde->rows = nextRows;
        fde->rowCap = next;
    }
    print_cfi_row_t *row = &fde->rows[fde->rowCount++];
    row->loc = loc;
    row->cfaReg = reg;
    row->cfaOffset = off;
    return 1;
}

int
print_debuginfo_readelf_loadFrames(const char *elfPath, print_index_t *index)
{
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "%s --debug-dump=frames-interp '%s'", readelf, elfPath);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }

    uint8_t cieDefaultReg = 0;
    int32_t cieDefaultOff = 0;
    int cieHasDefault = 0;

    print_cfi_fde_t *curFde = NULL;
    int modeCie = 0;
    int modeFde = 0;
    int expectRows = 0;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // New CIE / FDE headers reset row parsing.
        if (strstr(line, " CIE ")) {
            modeCie = 1;
            modeFde = 0;
            curFde = NULL;
            expectRows = 0;
            cieHasDefault = 0;
            continue;
        }
        if (strstr(line, " FDE ")) {
            uint32_t pcStart = 0;
            uint32_t pcEnd = 0;
            if (print_debuginfo_readelf_frames_parseHexRange(line, &pcStart, &pcEnd)) {
                uint8_t defReg = cieHasDefault ? cieDefaultReg : 15;
                int32_t defOff = cieHasDefault ? cieDefaultOff : 4;
                curFde = print_debuginfo_readelf_frames_addFde(index, pcStart, pcEnd, defReg, defOff);
                modeCie = 0;
                modeFde = 1;
                expectRows = 0;
            }
            continue;
        }

        if (strstr(line, "LOC") && strstr(line, "CFA")) {
            expectRows = 1;
            continue;
        }
        if (!expectRows) {
            continue;
        }

        // Row line: "<hex> <cfa> ..."
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }
        if (!isxdigit((unsigned char)*p)) {
            continue;
        }
        errno = 0;
        char *end = NULL;
        unsigned long loc = strtoul(p, &end, 16);
        if (errno != 0 || !end || end == p) {
            continue;
        }
        while (*end && isspace((unsigned char)*end)) {
            ++end;
        }
        char cfaBuf[64];
        int cfaLen = 0;
        while (end[cfaLen] && !isspace((unsigned char)end[cfaLen]) && cfaLen < (int)sizeof(cfaBuf) - 1) {
            cfaBuf[cfaLen] = end[cfaLen];
            cfaLen++;
        }
        cfaBuf[cfaLen] = '\0';
        uint8_t reg = 0;
        int32_t off = 0;
        if (!print_debuginfo_readelf_frames_parseCfa(cfaBuf, &reg, &off)) {
            continue;
        }

        if (modeCie) {
            cieDefaultReg = reg;
            cieDefaultOff = off;
            cieHasDefault = 1;
        } else if (modeFde && curFde) {
            print_debuginfo_readelf_frames_addRow(curFde, (uint32_t)loc, reg, off);
        }
    }

    pclose(fp);
    return 1;
}

