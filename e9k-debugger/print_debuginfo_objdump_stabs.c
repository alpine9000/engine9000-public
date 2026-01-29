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

#include "print_debuginfo_objdump_stabs.h"

#include "alloc.h"
#include "debug.h"
#include "debugger.h"
#include "machine.h"

static char *
print_debuginfo_objdump_stabs_strdup(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)alloc_alloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len + 1);
    return out;
}

static int
print_debuginfo_objdump_stabs_debugEnabled(void)
{
    static int cached = -1;
    if (cached >= 0) {
        return cached;
    }
    const char *v = getenv("E9K_PRINT_DEBUG");
    if (!v || !*v || strcmp(v, "0") == 0) {
        cached = 0;
        return cached;
    }
    cached = 1;
    return cached;
}

static int
print_debuginfo_objdump_stabs_debugWantsSymbol(const char *name)
{
    const char *want = getenv("E9K_PRINT_DEBUG_SYM");
    if (!want || !*want) {
        return 0;
    }
    if (!name) {
        return 0;
    }
    return strstr(name, want) != NULL;
}

static int
print_debuginfo_objdump_stabs_preferData(void)
{
    const char *v = getenv("E9K_STABS_PREFER_DATA");
    if (!v || !*v || strcmp(v, "0") == 0) {
        return 0;
    }
    return 1;
}

typedef struct print_debuginfo_objdump_stabs_type_def {
    uint32_t alias;
    uint32_t bits;
} print_debuginfo_objdump_stabs_type_def_t;

static void
print_debuginfo_objdump_stabs_typeEnsure(print_debuginfo_objdump_stabs_type_def_t **defs, size_t *cap, uint32_t id)
{
    if (!defs || !cap) {
        return;
    }
    if (id < *cap) {
        return;
    }
    size_t next = *cap ? *cap : 256;
    while (id >= next) {
        next *= 2;
    }
    print_debuginfo_objdump_stabs_type_def_t *nextDefs =
        (print_debuginfo_objdump_stabs_type_def_t *)alloc_realloc(*defs, sizeof(**defs) * next);
    if (!nextDefs) {
        return;
    }
    size_t old = *cap;
    memset(&nextDefs[old], 0, sizeof(**defs) * (next - old));
    *defs = nextDefs;
    *cap = next;
}

static uint32_t
print_debuginfo_objdump_stabs_typeResolveBits(const print_debuginfo_objdump_stabs_type_def_t *defs, size_t cap, uint32_t id)
{
    uint32_t cur = id;
    for (int i = 0; i < 64; ++i) {
        if (cur == 0 || cur >= cap) {
            return 0;
        }
        if (defs[cur].bits != 0) {
            return defs[cur].bits;
        }
        uint32_t next = defs[cur].alias;
        if (next == 0 || next == cur) {
            return 0;
        }
        cur = next;
    }
    return 0;
}

static int
print_debuginfo_objdump_stabs_parseTypeDef(const char *stabStr, uint32_t *outTypeId, uint32_t *outAlias, uint32_t *outBits)
{
    if (outTypeId) {
        *outTypeId = 0;
    }
    if (outAlias) {
        *outAlias = 0;
    }
    if (outBits) {
        *outBits = 0;
    }
    if (!stabStr || !*stabStr || !outTypeId || !outAlias || !outBits) {
        return 0;
    }

    const char *p = strstr(stabStr, ":t");
    if (!p) {
        return 0;
    }
    p += 2;
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    uint32_t typeId = (uint32_t)strtoul(p, (char **)&p, 10);
    if (*p != '=') {
        return 0;
    }
    ++p;

    if (isdigit((unsigned char)*p)) {
        uint32_t alias = (uint32_t)strtoul(p, NULL, 10);
        *outTypeId = typeId;
        *outAlias = alias;
        return 1;
    }

    const char *sz = strstr(p, "@s");
    if (sz) {
        sz += 2;
        if (isdigit((unsigned char)*sz)) {
            uint32_t bits = (uint32_t)strtoul(sz, NULL, 10);
            *outTypeId = typeId;
            *outBits = bits;
            return 1;
        }
    }
    return 0;
}

static int
print_debuginfo_objdump_stabs_parseVarTypeId(const char *stabStr, uint32_t *outTypeId)
{
    if (outTypeId) {
        *outTypeId = 0;
    }
    if (!stabStr || !outTypeId) {
        return 0;
    }
    const char *colon = strchr(stabStr, ':');
    if (!colon || !colon[1] || !isalpha((unsigned char)colon[1])) {
        return 0;
    }
    const char *p = colon + 2;
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    *outTypeId = (uint32_t)strtoul(p, NULL, 10);
    return 1;
}

static int
print_debuginfo_objdump_stabs_hasVariable(print_index_t *index, const char *name)
{
    if (!index || !name) {
        return 0;
    }
    for (int i = 0; i < index->varCount; ++i) {
        if (index->vars[i].name && strcmp(index->vars[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
print_debuginfo_objdump_stabs_addVariable(print_index_t *index, const char *name, uint32_t addr, size_t byteSize, int hasByteSize)
{
    if (!index || !name || !*name) {
        return 0;
    }
    if (print_debuginfo_objdump_stabs_hasVariable(index, name)) {
        return 1;
    }
    if (index->varCount >= index->varCap) {
        int next = index->varCap ? index->varCap * 2 : 128;
        print_variable_t *nextVars = (print_variable_t *)alloc_realloc(index->vars, sizeof(*nextVars) * (size_t)next);
        if (!nextVars) {
            return 0;
        }
        index->vars = nextVars;
        index->varCap = next;
    }
    print_variable_t *var = &index->vars[index->varCount++];
    memset(var, 0, sizeof(*var));
    var->name = print_debuginfo_objdump_stabs_strdup(name);
    var->addr = addr;
    var->typeRef = 0;
    var->byteSize = byteSize;
    var->hasByteSize = hasByteSize ? 1 : 0;
    return var->name != NULL;
}

static int
print_debuginfo_objdump_stabs_hasSymbol(print_index_t *index, const char *name)
{
    if (!index || !name) {
        return 0;
    }
    for (int i = 0; i < index->symbolCount; ++i) {
        if (index->symbols[i].name && strcmp(index->symbols[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
print_debuginfo_objdump_stabs_addSymbol(print_index_t *index, const char *name, uint32_t addr)
{
    if (!index || !name || !*name) {
        return 0;
    }
    if (print_debuginfo_objdump_stabs_hasSymbol(index, name)) {
        return 1;
    }
    if (index->symbolCount >= index->symbolCap) {
        int next = index->symbolCap ? index->symbolCap * 2 : 128;
        print_symbol_t *nextSyms = (print_symbol_t *)alloc_realloc(index->symbols, sizeof(*nextSyms) * (size_t)next);
        if (!nextSyms) {
            return 0;
        }
        index->symbols = nextSyms;
        index->symbolCap = next;
    }
    print_symbol_t *sym = &index->symbols[index->symbolCount++];
    memset(sym, 0, sizeof(*sym));
    sym->name = print_debuginfo_objdump_stabs_strdup(name);
    sym->addr = addr;
    return sym->name != NULL;
}

static int
print_debuginfo_objdump_stabs_getSectionSizes(const char *elfPath, uint32_t *outDataSize, uint32_t *outBssSize)
{
    if (outDataSize) {
        *outDataSize = 0;
    }
    if (outBssSize) {
        *outBssSize = 0;
    }
    if (!elfPath || !*elfPath) {
        return 0;
    }
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "%s -h '%s'", objdump, elfPath);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        unsigned idx = 0;
        char name[64];
        char sizeHex[32];
        if (sscanf(line, " %u %63s %31s", &idx, name, sizeHex) != 3) {
            continue;
        }
        if (strcmp(name, ".data") == 0) {
            uint32_t v = (uint32_t)strtoul(sizeHex, NULL, 16);
            if (outDataSize) {
                *outDataSize = v;
            }
        } else if (strcmp(name, ".bss") == 0) {
            uint32_t v = (uint32_t)strtoul(sizeHex, NULL, 16);
            if (outBssSize) {
                *outBssSize = v;
            }
        }
    }
    pclose(fp);
    return 1;
}

static int
print_debuginfo_objdump_stabs_parseStabStringName(const char *stabStr, char *outName, size_t cap)
{
    if (!stabStr || !outName || cap == 0) {
        return 0;
    }
    outName[0] = '\0';
    const char *colon = strchr(stabStr, ':');
    if (!colon || colon == stabStr) {
        return 0;
    }
    size_t len = (size_t)(colon - stabStr);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(outName, stabStr, len);
    outName[len] = '\0';
    return 1;
}

int
print_debuginfo_objdump_stabs_loadSymbols(const char *elfPath, print_index_t *index)
{
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    uint32_t dataSize = 0;
    uint32_t bssSize = 0;
    (void)print_debuginfo_objdump_stabs_getSectionSizes(elfPath, &dataSize, &bssSize);

    uint32_t dataBase = debugger.machine.dataBaseAddr;
    uint32_t bssBase = debugger.machine.bssBaseAddr;
    int preferData = print_debuginfo_objdump_stabs_preferData();
    if (print_debuginfo_objdump_stabs_debugEnabled()) {
        debug_printf("print: stabs sizes data=0x%X bss=0x%X bases data=0x%08X bss=0x%08X prefer=%s\n",
                     (unsigned)dataSize, (unsigned)bssSize, (unsigned)dataBase, (unsigned)bssBase,
                     preferData ? "data" : "bss");
    }

    print_debuginfo_objdump_stabs_type_def_t *typeDefs = NULL;
    size_t typeCap = 0;

    typedef struct pending_var {
        char *name;
        char stabType[8];
        uint32_t nValue;
        uint32_t base;
        char chosenSection[8];
        uint32_t addr;
        uint32_t typeId;
    } pending_var_t;

    pending_var_t *pending = NULL;
    size_t pendingCount = 0;
    size_t pendingCap = 0;

    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        alloc_free(typeDefs);
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "%s -G '%s'", objdump, elfPath);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        alloc_free(typeDefs);
        return 0;
    }
    int added = 0;
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[12];
        int count = 0;
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor) {
            continue;
        }
        while (count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (!*cursor) {
                break;
            }
            tokens[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (count < 7) {
            continue;
        }
        const char *stabType = tokens[1];
        if (!stabType) {
            continue;
        }
        const char *stabStr = tokens[count - 1];
        if (!stabStr || !*stabStr) {
            continue;
        }

        // Collect simple STABS type aliases/sizes from LSYM entries.
        if (strcmp(stabType, "LSYM") == 0) {
            uint32_t typeId = 0;
            uint32_t alias = 0;
            uint32_t bits = 0;
            if (print_debuginfo_objdump_stabs_parseTypeDef(stabStr, &typeId, &alias, &bits)) {
                print_debuginfo_objdump_stabs_typeEnsure(&typeDefs, &typeCap, typeId);
                if (typeId < typeCap) {
                    if (alias != 0) {
                        typeDefs[typeId].alias = alias;
                    }
                    if (bits != 0) {
                        typeDefs[typeId].bits = bits;
                    }
                }
            }
            continue;
        }

        if (strcmp(stabType, "STSYM") != 0 && strcmp(stabType, "LCSYM") != 0) {
            continue;
        }
        const char *nValueStr = tokens[4];
        if (!nValueStr || !*nValueStr) {
            continue;
        }
        errno = 0;
        uint32_t nValue = (uint32_t)strtoul(nValueStr, NULL, 16);
        if (errno != 0) {
            continue;
        }
        char name[256];
        if (!print_debuginfo_objdump_stabs_parseStabStringName(stabStr, name, sizeof(name))) {
            continue;
        }
        uint32_t typeId = 0;
        (void)print_debuginfo_objdump_stabs_parseVarTypeId(stabStr, &typeId);

        uint32_t base = 0;
        const char *chosenSection = "unknown";
        uint32_t dataAddr = (dataBase != 0) ? (dataBase + nValue) : 0;
        uint32_t bssAddr = (bssBase != 0) ? (bssBase + nValue) : 0;

        if (strcmp(stabType, "LCSYM") == 0) {
            base = bssBase;
            chosenSection = "bss";
        } else {
            // STSYM appears to be ambiguous on some m68k-amiga toolchains; default to bss unless overridden.
            if (preferData) {
                base = dataBase ? dataBase : bssBase;
                chosenSection = base == dataBase ? "data" : "bss";
            } else {
                base = bssBase ? bssBase : dataBase;
                chosenSection = base == bssBase ? "bss" : "data";
            }
        }
        if (base == dataBase && dataSize != 0 && nValue >= dataSize && bssBase != 0 && (bssSize == 0 || nValue < bssSize)) {
            base = bssBase;
            chosenSection = "bss";
        } else if (base == bssBase && bssSize != 0 && nValue >= bssSize && dataBase != 0 && (dataSize == 0 || nValue < dataSize)) {
            base = dataBase;
            chosenSection = "data";
        }
        if (base == 0) {
            if (print_debuginfo_objdump_stabs_debugEnabled() && print_debuginfo_objdump_stabs_debugWantsSymbol(name)) {
                debug_printf("print: stabs sym '%s' type=%s n_value=0x%X base=<unset> data=0x%08X bss=0x%08X\n",
                             name, stabType, (unsigned)nValue, (unsigned)dataAddr, (unsigned)bssAddr);
            }
            continue;
        }
        uint32_t addr = (base + nValue) & 0x00ffffffu;

        if (pendingCount >= pendingCap) {
            size_t next = pendingCap ? pendingCap * 2 : 64;
            pending_var_t *nextPending = (pending_var_t *)alloc_realloc(pending, sizeof(*nextPending) * next);
            if (!nextPending) {
                break;
            }
            pending = nextPending;
            pendingCap = next;
        }
        pending_var_t *pv = &pending[pendingCount++];
        memset(pv, 0, sizeof(*pv));
        pv->name = print_debuginfo_objdump_stabs_strdup(name);
        strncpy(pv->stabType, stabType, sizeof(pv->stabType) - 1);
        pv->nValue = nValue;
        pv->base = base;
        strncpy(pv->chosenSection, chosenSection, sizeof(pv->chosenSection) - 1);
        pv->addr = addr;
        pv->typeId = typeId;

        // Keep symbol table populated for now; variables are added after size resolution.
        (void)print_debuginfo_objdump_stabs_addSymbol(index, name, addr);
        added = 1;
    }
    pclose(fp);

    for (size_t i = 0; i < pendingCount; ++i) {
        pending_var_t *pv = &pending[i];
        if (!pv->name) {
            continue;
        }
        uint32_t bits = 0;
        if (pv->typeId != 0) {
            bits = print_debuginfo_objdump_stabs_typeResolveBits(typeDefs, typeCap, pv->typeId);
        }
        size_t byteSize = 0;
        if (bits != 0 && (bits % 8u) == 0u) {
            byteSize = (size_t)(bits / 8u);
        }
        if (byteSize != 0) {
            if (print_debuginfo_objdump_stabs_addVariable(index, pv->name, pv->addr, byteSize, 1)) {
                added = 1;
            }
        } else {
            (void)print_debuginfo_objdump_stabs_addVariable(index, pv->name, pv->addr, 0, 0);
        }
        if (print_debuginfo_objdump_stabs_debugEnabled() && print_debuginfo_objdump_stabs_debugWantsSymbol(pv->name)) {
            debug_printf("print: stabs sym '%s' type=%s typeId=%u n_value=0x%X %s=0x%08X addr=0x%08X size=%u\n",
                         pv->name, pv->stabType, (unsigned)pv->typeId, (unsigned)pv->nValue,
                         pv->chosenSection, (unsigned)pv->base, (unsigned)pv->addr, (unsigned)byteSize);
        }
        alloc_free(pv->name);
        pv->name = NULL;
    }
    alloc_free(pending);
    alloc_free(typeDefs);
    return added;
}
