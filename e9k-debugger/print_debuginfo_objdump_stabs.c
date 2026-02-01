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
    char *def;
    char *name;
} print_debuginfo_objdump_stabs_type_def_t;

#define PRINT_DEBINFO_STABS_TYPE_BASE 0x80000000u

static int
print_debuginfo_objdump_stabs_parseStabStringName(const char *stabStr, char *outName, size_t cap);

static void
print_debuginfo_objdump_stabs_typeEnsure(print_debuginfo_objdump_stabs_type_def_t **defs, size_t *cap, uint32_t id);

static int
print_debuginfo_objdump_stabs_parseTypeId(const char *p, const char **outEnd, uint32_t *outTypeId)
{
    if (outEnd) {
        *outEnd = NULL;
    }
    if (outTypeId) {
        *outTypeId = 0;
    }
    if (!p || !outEnd || !outTypeId) {
        return 0;
    }

    if (isdigit((unsigned char)*p)) {
        errno = 0;
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (errno != 0 || !end || end == p || v > 0xfffffffful) {
            return 0;
        }
        *outTypeId = (uint32_t)v;
        *outEnd = end;
        return 1;
    }

    if (*p == '(') {
        ++p;
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        errno = 0;
        char *endA = NULL;
        unsigned long a = strtoul(p, &endA, 10);
        if (errno != 0 || !endA || endA == p || *endA != ',') {
            return 0;
        }
        const char *pB = endA + 1;
        if (!isdigit((unsigned char)*pB)) {
            return 0;
        }
        errno = 0;
        char *endB = NULL;
        unsigned long b = strtoul(pB, &endB, 10);
        if (errno != 0 || !endB || endB == pB || *endB != ')') {
            return 0;
        }
        if (a > 0xfffful || b > 0xfffful) {
            return 0;
        }
        *outTypeId = (uint32_t)((a << 16) | b);
        *outEnd = endB + 1;
        return 1;
    }

    return 0;
}

static int
print_debuginfo_objdump_stabs_stripslash(char *s)
{
    if (!s) {
        return 0;
    }
    size_t len = strlen(s);
    if (len == 0) {
        return 0;
    }
    if (s[len - 1] != '\\') {
        return 0;
    }
    s[len - 1] = '\0';
    return 1;
}

static int
print_debuginfo_objdump_stabs_appendString(char **dst, const char *src)
{
    if (!dst || !src) {
        return 0;
    }
    if (!*dst) {
        *dst = print_debuginfo_objdump_stabs_strdup(src);
        return *dst != NULL;
    }
    size_t a = strlen(*dst);
    size_t b = strlen(src);
    char *next = (char *)alloc_realloc(*dst, a + b + 1);
    if (!next) {
        return 0;
    }
    memcpy(next + a, src, b + 1);
    *dst = next;
    return 1;
}

static void
print_debuginfo_objdump_stabs_splitNestedAliasDef(print_debuginfo_objdump_stabs_type_def_t **defs,
                                                  size_t *cap,
                                                  uint32_t typeId)
{
    if (!defs || !*defs || !cap || *cap == 0 || typeId == 0 || typeId >= *cap) {
        return;
    }
    if (!(*defs)[typeId].def || !*(*defs)[typeId].def) {
        return;
    }

    const char *def = (*defs)[typeId].def;
    uint32_t innerId = 0;
    const char *end = NULL;
    if (!print_debuginfo_objdump_stabs_parseTypeId(def, &end, &innerId)) {
        return;
    }
    if (!end || *end != '=' || !end[1]) {
        return;
    }
    if (innerId == 0) {
        return;
    }

    print_debuginfo_objdump_stabs_typeEnsure(defs, cap, innerId);
    if (innerId >= *cap) {
        return;
    }

    if ((*defs)[typeId].alias == 0) {
        (*defs)[typeId].alias = innerId;
    }
    if (!(*defs)[innerId].def) {
        (*defs)[innerId].def = print_debuginfo_objdump_stabs_strdup(end + 1);
    }
}

static uint32_t
print_debuginfo_objdump_stabs_typeDieOffset(uint32_t typeId)
{
    return PRINT_DEBINFO_STABS_TYPE_BASE | (typeId & 0x7fffffffu);
}

static print_type_t *
print_debuginfo_objdump_stabs_findType(print_index_t *index, uint32_t dieOffset)
{
    if (!index) {
        return NULL;
    }
    for (int i = 0; i < index->typeCount; ++i) {
        if (index->types[i] && index->types[i]->dieOffset == dieOffset) {
            return index->types[i];
        }
    }
    return NULL;
}

static print_type_t *
print_debuginfo_objdump_stabs_addType(print_index_t *index, uint32_t dieOffset)
{
    if (!index) {
        return NULL;
    }
    if (index->typeCount >= index->typeCap) {
        int next = index->typeCap ? index->typeCap * 2 : 128;
        print_type_t **nextTypes = (print_type_t **)alloc_realloc(index->types, sizeof(*nextTypes) * (size_t)next);
        if (!nextTypes) {
            return NULL;
        }
        index->types = nextTypes;
        index->typeCap = next;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->dieOffset = dieOffset;
    index->types[index->typeCount++] = type;
    return type;
}

static print_type_t *
print_debuginfo_objdump_stabs_addAnonBaseType(print_index_t *index, size_t byteSize)
{
    if (!index || byteSize == 0) {
        return NULL;
    }
    print_type_t *type = print_debuginfo_objdump_stabs_addType(index, 0);
    if (!type) {
        return NULL;
    }
    type->kind = print_type_base;
    type->byteSize = byteSize;
    type->encoding = print_base_encoding_unsigned;
    type->name = print_debuginfo_objdump_stabs_strdup("");
    return type;
}

static print_type_t *
print_debuginfo_objdump_stabs_addAnonPointerType(print_index_t *index, print_type_t *target)
{
    if (!index) {
        return NULL;
    }
    print_type_t *type = print_debuginfo_objdump_stabs_addType(index, 0);
    if (!type) {
        return NULL;
    }
    type->kind = print_type_pointer;
    type->byteSize = 4;
    type->targetType = target;
    type->name = print_debuginfo_objdump_stabs_strdup("");
    return type;
}

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
        p = strstr(stabStr, ":T");
        if (!p) {
            return 0;
        }
    }
    p += 2;
    uint32_t typeId = 0;
    if (!print_debuginfo_objdump_stabs_parseTypeId(p, &p, &typeId)) {
        return 0;
    }
    if (*p != '=') {
        return 0;
    }
    ++p;

    if (isdigit((unsigned char)*p) || *p == '(') {
        uint32_t alias = 0;
        const char *end = NULL;
        if (print_debuginfo_objdump_stabs_parseTypeId(p, &end, &alias)) {
            *outTypeId = typeId;
            *outAlias = alias;
            return 1;
        }
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
    uint32_t typeId = 0;
    const char *end = NULL;
    if (!print_debuginfo_objdump_stabs_parseTypeId(p, &end, &typeId)) {
        return 0;
    }
    *outTypeId = typeId;
    return 1;
}

static int
print_debuginfo_objdump_stabs_parseFullTypeDef(const char *stabStr, uint32_t *outTypeId, const char **outDef)
{
    if (outTypeId) {
        *outTypeId = 0;
    }
    if (outDef) {
        *outDef = NULL;
    }
    if (!stabStr || !*stabStr || !outTypeId || !outDef) {
        return 0;
    }
    const char *p = strstr(stabStr, ":t");
    if (!p) {
        p = strstr(stabStr, ":T");
        if (!p) {
            return 0;
        }
    }
    p += 2;
    uint32_t typeId = 0;
    if (!print_debuginfo_objdump_stabs_parseTypeId(p, &p, &typeId)) {
        return 0;
    }
    if (*p != '=') {
        return 0;
    }
    ++p;
    *outTypeId = typeId;
    *outDef = p;
    return 1;
}

static int
print_debuginfo_objdump_stabs_parseStructByteSize(const char *def, size_t *outByteSize)
{
    if (outByteSize) {
        *outByteSize = 0;
    }
    if (!def || !outByteSize) {
        return 0;
    }
    if (def[0] != 's' && def[0] != 'u') {
        return 0;
    }
    const char *p = def + 1;
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    unsigned long bytes = strtoul(p, NULL, 10);
    if (bytes == 0) {
        return 0;
    }
    *outByteSize = (size_t)bytes;
    return 1;
}

static print_type_t *
print_debuginfo_objdump_stabs_buildType(print_index_t *index,
                                       const print_debuginfo_objdump_stabs_type_def_t *defs,
                                       size_t cap,
                                       uint32_t typeId,
                                       int depth);

static print_type_t *
print_debuginfo_objdump_stabs_parseTypeSpec(print_index_t *index,
                                            const print_debuginfo_objdump_stabs_type_def_t *defs,
                                            size_t cap,
                                            const char *spec,
                                            size_t specLen,
                                            size_t bitSize,
                                            int depth)
{
    if (!index || !spec || specLen == 0) {
        return NULL;
    }

    char tmp[128];
    size_t tl = specLen;
    if (tl >= sizeof(tmp)) {
        tl = sizeof(tmp) - 1;
    }
    memcpy(tmp, spec, tl);
    tmp[tl] = '\0';

    if (tmp[0] == '*' && tmp[1] != '\0') {
        const char *p = tmp + 1;
        uint32_t tid = 0;
        const char *end = NULL;
        if (print_debuginfo_objdump_stabs_parseTypeId(p, &end, &tid)) {
            print_type_t *target = print_debuginfo_objdump_stabs_buildType(index, defs, cap, tid, depth + 1);
            return print_debuginfo_objdump_stabs_addAnonPointerType(index, target);
        }
    }

    if (isdigit((unsigned char)tmp[0]) || tmp[0] == '(') {
        uint32_t tid = 0;
        const char *end = NULL;
        if (print_debuginfo_objdump_stabs_parseTypeId(tmp, &end, &tid)) {
            return print_debuginfo_objdump_stabs_buildType(index, defs, cap, tid, depth + 1);
        }
    }

    if (bitSize != 0) {
        size_t bytes = (bitSize + 7u) / 8u;
        if (bytes == 0) {
            bytes = 1;
        }
        return print_debuginfo_objdump_stabs_addAnonBaseType(index, bytes);
    }

    return NULL;
}

static int
print_debuginfo_objdump_stabs_buildStructMembers(print_index_t *index,
                                                 print_type_t *type,
                                                 const print_debuginfo_objdump_stabs_type_def_t *defs,
                                                 size_t cap,
                                                 const char *def,
                                                 int depth)
{
    if (!index || !type || !def) {
        return 0;
    }
    if (def[0] != 's' && def[0] != 'u') {
        return 0;
    }

    const char *p = def + 1;
    while (*p && isdigit((unsigned char)*p)) {
        ++p;
    }

    int count = 0;
    const char *scan = p;
    while (*scan) {
        const char *colon = strchr(scan, ':');
        if (!colon) {
            break;
        }
        const char *semi = strchr(colon + 1, ';');
        if (!semi) {
            break;
        }
        ++count;
        scan = semi + 1;
        if (*scan == ';') {
            break;
        }
    }
    if (count <= 0) {
        return 1;
    }

    type->members = (print_member_t *)alloc_calloc((size_t)count, sizeof(*type->members));
    if (!type->members) {
        return 0;
    }
    type->memberCount = 0;

    for (int i = 0; i < count; ++i) {
        const char *colon = strchr(p, ':');
        if (!colon) {
            break;
        }
        const char *semi = strchr(colon + 1, ';');
        if (!semi) {
            break;
        }

        size_t nameLen = (size_t)(colon - p);
        char nameBuf[256];
        if (nameLen == 0) {
            strncpy(nameBuf, "<anon>", sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
        } else {
            if (nameLen >= sizeof(nameBuf)) {
                nameLen = sizeof(nameBuf) - 1;
            }
            memcpy(nameBuf, p, nameLen);
            nameBuf[nameLen] = '\0';
        }

        const char *specStart = colon + 1;
        const char *comma1 = strchr(specStart, ',');
        const char *comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;
        if (!comma1 || !comma2 || comma2 > semi) {
            p = semi + 1;
            continue;
        }
        size_t specLen = (size_t)(comma1 - specStart);
        if (specLen == 0) {
            p = semi + 1;
            continue;
        }
        unsigned long bitOffset = strtoul(comma1 + 1, NULL, 10);
        unsigned long bitSize = strtoul(comma2 + 1, NULL, 10);

        print_member_t *member = &type->members[type->memberCount++];
        member->name = print_debuginfo_objdump_stabs_strdup(nameBuf[0] ? nameBuf : "<anon>");
        member->offset = (uint32_t)(bitOffset / 8ul);
        member->type = print_debuginfo_objdump_stabs_parseTypeSpec(index, defs, cap, specStart, specLen, (size_t)bitSize, depth);
        if (!member->type && bitSize != 0) {
            size_t bytes = ((size_t)bitSize + 7u) / 8u;
            if (bytes == 0) {
                bytes = 1;
            }
            member->type = print_debuginfo_objdump_stabs_addAnonBaseType(index, bytes);
        }

        p = semi + 1;
        if (*p == ';') {
            break;
        }
    }
    return 1;
}

static print_type_t *
print_debuginfo_objdump_stabs_buildType(print_index_t *index,
                                       const print_debuginfo_objdump_stabs_type_def_t *defs,
                                       size_t cap,
                                       uint32_t typeId,
                                       int depth)
{
    if (!index || !defs || cap == 0 || typeId == 0 || typeId >= cap) {
        return NULL;
    }
    if (depth > 64) {
        return NULL;
    }

    uint32_t dieOffset = print_debuginfo_objdump_stabs_typeDieOffset(typeId);
    print_type_t *existing = print_debuginfo_objdump_stabs_findType(index, dieOffset);
    if (existing) {
        return existing;
    }

    print_type_t *type = print_debuginfo_objdump_stabs_addType(index, dieOffset);
    if (!type) {
        return NULL;
    }
    type->kind = print_type_invalid;
    type->name = print_debuginfo_objdump_stabs_strdup(defs[typeId].name ? defs[typeId].name : "");

    if (defs[typeId].alias != 0 && defs[typeId].alias < cap) {
        type->kind = print_type_typedef;
        type->targetType = print_debuginfo_objdump_stabs_buildType(index, defs, cap, defs[typeId].alias, depth + 1);
        return type;
    }

    if (defs[typeId].def && *defs[typeId].def) {
        const char *def = defs[typeId].def;
        if (def[0] == '*') {
            type->kind = print_type_pointer;
            type->byteSize = 4;
            const char *p = def + 1;
            uint32_t tid = 0;
            const char *end = NULL;
            if (print_debuginfo_objdump_stabs_parseTypeId(p, &end, &tid)) {
                type->targetType = print_debuginfo_objdump_stabs_buildType(index, defs, cap, tid, depth + 1);
            }
            return type;
        }
        if (def[0] == 'B' || def[0] == 'k' || def[0] == 'K') {
            const char *p = def + 1;
            uint32_t tid = 0;
            const char *end = NULL;
            if (print_debuginfo_objdump_stabs_parseTypeId(p, &end, &tid)) {
                type->kind = (def[0] == 'B') ? print_type_volatile : print_type_const;
                type->targetType = print_debuginfo_objdump_stabs_buildType(index, defs, cap, tid, depth + 1);
                if (type->targetType && type->byteSize == 0) {
                    type->byteSize = type->targetType->byteSize;
                }
                return type;
            }
        }
        if (def[0] == 's' || def[0] == 'u') {
            type->kind = print_type_struct;
            size_t byteSize = 0;
            if (print_debuginfo_objdump_stabs_parseStructByteSize(def, &byteSize)) {
                type->byteSize = byteSize;
            }
            (void)print_debuginfo_objdump_stabs_buildStructMembers(index, type, defs, cap, def, depth + 1);
            return type;
        }
    }

    if (defs[typeId].bits != 0 && (defs[typeId].bits % 8u) == 0u) {
        type->kind = print_type_base;
        type->byteSize = (size_t)(defs[typeId].bits / 8u);
        type->encoding = print_base_encoding_unsigned;
        return type;
    }

    type->kind = print_type_base;
    type->byteSize = 4;
    type->encoding = print_base_encoding_unsigned;
    return type;
}

static int
print_debuginfo_objdump_stabs_setVariable(print_index_t *index, const char *name, uint32_t addr, uint32_t typeRef, size_t byteSize, int hasByteSize)
{
    if (!index || !name || !*name) {
        return 0;
    }
    for (int i = 0; i < index->varCount; ++i) {
        if (index->vars[i].name && strcmp(index->vars[i].name, name) == 0) {
            index->vars[i].addr = addr;
            if (typeRef != 0) {
                index->vars[i].typeRef = typeRef;
            }
            if (hasByteSize) {
                index->vars[i].byteSize = byteSize;
                index->vars[i].hasByteSize = 1;
            }
            return 1;
        }
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
    var->typeRef = typeRef;
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

static int
print_debuginfo_objdump_stabs_symbolMatch(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    if (strcmp(a, b) == 0) {
        return 1;
    }
    if (a[0] == '_' && strcmp(a + 1, b) == 0) {
        return 1;
    }
    if (b[0] == '_' && strcmp(a, b + 1) == 0) {
        return 1;
    }
    return 0;
}

static void
print_debuginfo_objdump_stabs_maybeParseInlineTypeDef(const char *stabStr,
                                                      print_debuginfo_objdump_stabs_type_def_t **typeDefs,
                                                      size_t *typeCap)
{
    if (!stabStr || !*stabStr || !typeDefs || !typeCap) {
        return;
    }
    const char *colon = strchr(stabStr, ':');
    if (!colon || !colon[1] || !colon[2]) {
        return;
    }
    if (!isalpha((unsigned char)colon[1])) {
        return;
    }
    const char *p = colon + 2;
    uint32_t typeId = 0;
    const char *end = NULL;
    if (!print_debuginfo_objdump_stabs_parseTypeId(p, &end, &typeId)) {
        return;
    }
    if (!end || *end != '=' || !end[1]) {
        return;
    }

    print_debuginfo_objdump_stabs_typeEnsure(typeDefs, typeCap, typeId);
    if (typeId >= *typeCap) {
        return;
    }
    if (!(*typeDefs)[typeId].def) {
        (*typeDefs)[typeId].def = print_debuginfo_objdump_stabs_strdup(end + 1);
        if ((*typeDefs)[typeId].def) {
            print_debuginfo_objdump_stabs_splitNestedAliasDef(typeDefs, typeCap, typeId);
        }
    }
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
        int needsSymLookup;
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
    uint32_t pendingTypeId = 0;
    char *pendingTypeDef = NULL;
    int pendingTypeContinue = 0;
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

        // Collect STABS type definitions from LSYM entries.
        if (strcmp(stabType, "LSYM") == 0) {
            int startedTypeDef = 0;
            uint32_t typeId = 0;
            uint32_t alias = 0;
            uint32_t bits = 0;
            const char *def = NULL;
            if (print_debuginfo_objdump_stabs_parseFullTypeDef(stabStr, &typeId, &def)) {
                startedTypeDef = 1;
                if (pendingTypeDef) {
                    print_debuginfo_objdump_stabs_typeEnsure(&typeDefs, &typeCap, pendingTypeId);
                    if (pendingTypeId < typeCap && !typeDefs[pendingTypeId].def) {
                        typeDefs[pendingTypeId].def = pendingTypeDef;
                        pendingTypeDef = NULL;
                        print_debuginfo_objdump_stabs_splitNestedAliasDef(&typeDefs, &typeCap, pendingTypeId);
                    } else {
                        alloc_free(pendingTypeDef);
                        pendingTypeDef = NULL;
                    }
                    pendingTypeId = 0;
                    pendingTypeContinue = 0;
                }
                print_debuginfo_objdump_stabs_typeEnsure(&typeDefs, &typeCap, typeId);
                if (typeId < typeCap) {
                    if (def && *def) {
                        pendingTypeId = typeId;
                        pendingTypeDef = print_debuginfo_objdump_stabs_strdup(def);
                        if (pendingTypeDef) {
                            pendingTypeContinue = print_debuginfo_objdump_stabs_stripslash(pendingTypeDef);
                            if (!pendingTypeContinue && !typeDefs[typeId].def) {
                                typeDefs[typeId].def = pendingTypeDef;
                                pendingTypeDef = NULL;
                                print_debuginfo_objdump_stabs_splitNestedAliasDef(&typeDefs, &typeCap, typeId);
                            }
                        }
                    }
                    if (!typeDefs[typeId].name) {
                        char typeName[256];
                        if (print_debuginfo_objdump_stabs_parseStabStringName(stabStr, typeName, sizeof(typeName))) {
                            typeDefs[typeId].name = print_debuginfo_objdump_stabs_strdup(typeName);
                        }
                    }
                }
            }
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
            if (!startedTypeDef && pendingTypeDef && pendingTypeContinue) {
                char pieceBuf[2048];
                strncpy(pieceBuf, stabStr, sizeof(pieceBuf) - 1);
                pieceBuf[sizeof(pieceBuf) - 1] = '\0';
                int cont = print_debuginfo_objdump_stabs_stripslash(pieceBuf);
                (void)print_debuginfo_objdump_stabs_appendString(&pendingTypeDef, pieceBuf);
                pendingTypeContinue = cont;
                if (!pendingTypeContinue) {
                    print_debuginfo_objdump_stabs_typeEnsure(&typeDefs, &typeCap, pendingTypeId);
                    if (pendingTypeId < typeCap && !typeDefs[pendingTypeId].def) {
                        typeDefs[pendingTypeId].def = pendingTypeDef;
                        pendingTypeDef = NULL;
                        print_debuginfo_objdump_stabs_splitNestedAliasDef(&typeDefs, &typeCap, pendingTypeId);
                    } else {
                        alloc_free(pendingTypeDef);
                        pendingTypeDef = NULL;
                    }
                    pendingTypeId = 0;
                    pendingTypeContinue = 0;
                }
            }
            continue;
        }

        if (strcmp(stabType, "STSYM") != 0 && strcmp(stabType, "LCSYM") != 0 && strcmp(stabType, "GSYM") != 0) {
            continue;
        }
        // Some toolchains emit inline type definitions on variable stabs, e.g. "name:G479=B477".
        print_debuginfo_objdump_stabs_maybeParseInlineTypeDef(stabStr, &typeDefs, &typeCap);

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

        int needsSymLookup = 0;
        if (strcmp(stabType, "GSYM") == 0) {
            needsSymLookup = 1;
            chosenSection = "sym";
        } else if (strcmp(stabType, "LCSYM") == 0) {
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
        if (!needsSymLookup && base == dataBase && dataSize != 0 && nValue >= dataSize && bssBase != 0 && (bssSize == 0 || nValue < bssSize)) {
            base = bssBase;
            chosenSection = "bss";
        } else if (!needsSymLookup && base == bssBase && bssSize != 0 && nValue >= bssSize && dataBase != 0 && (dataSize == 0 || nValue < dataSize)) {
            base = dataBase;
            chosenSection = "data";
        }
        if (!needsSymLookup && base == 0) {
            if (print_debuginfo_objdump_stabs_debugEnabled() && print_debuginfo_objdump_stabs_debugWantsSymbol(name)) {
                debug_printf("print: stabs sym '%s' type=%s n_value=0x%X base=<unset> data=0x%08X bss=0x%08X\n",
                             name, stabType, (unsigned)nValue, (unsigned)dataAddr, (unsigned)bssAddr);
            }
            continue;
        }
        uint32_t addr = needsSymLookup ? 0 : ((base + nValue) & 0x00ffffffu);

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
        pv->needsSymLookup = needsSymLookup;

        // Keep symbol table populated for now; variables are added after size resolution.
        if (!pv->needsSymLookup) {
            (void)print_debuginfo_objdump_stabs_addSymbol(index, name, addr);
        }
        added = 1;
    }
    pclose(fp);

    if (pendingTypeDef) {
        print_debuginfo_objdump_stabs_typeEnsure(&typeDefs, &typeCap, pendingTypeId);
        if (pendingTypeId < typeCap && !typeDefs[pendingTypeId].def) {
            typeDefs[pendingTypeId].def = pendingTypeDef;
            pendingTypeDef = NULL;
            print_debuginfo_objdump_stabs_splitNestedAliasDef(&typeDefs, &typeCap, pendingTypeId);
        } else {
            alloc_free(pendingTypeDef);
            pendingTypeDef = NULL;
        }
        pendingTypeId = 0;
        pendingTypeContinue = 0;
    }

    // Resolve GSYM addresses from the main symbol table (objdump --syms).
    {
        int wantSyms = 0;
        for (size_t i = 0; i < pendingCount; ++i) {
            if (pending[i].needsSymLookup && pending[i].name) {
                wantSyms = 1;
                break;
            }
        }
        if (wantSyms) {
            char symsCmd[PATH_MAX * 2];
            snprintf(symsCmd, sizeof(symsCmd), "%s --syms '%s'", objdump, elfPath);
            FILE *sf = popen(symsCmd, "r");
            if (sf) {
                char sline[1024];
                while (fgets(sline, sizeof(sline), sf)) {
                    char *tokens[8];
                    int count = 0;
                    char *cursor = sline;
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
                    if (count < 2) {
                        continue;
                    }
                    uint32_t symVal = 0;
                    if (!tokens[0] || !isxdigit((unsigned char)tokens[0][0])) {
                        continue;
                    }
                    errno = 0;
                    char *endptr = NULL;
                    symVal = (uint32_t)strtoul(tokens[0], &endptr, 16);
                    if (errno != 0 || !endptr || endptr == tokens[0]) {
                        continue;
                    }
                    const char *symName = tokens[count - 1];
                    if (!symName) {
                        continue;
                    }
                    const char *section = NULL;
                    for (int ti = 1; ti < count - 1; ++ti) {
                        if (tokens[ti] && tokens[ti][0] == '.') {
                            section = tokens[ti];
                            break;
                        }
                    }
                    if (!section) {
                        continue;
                    }
                    for (size_t i = 0; i < pendingCount; ++i) {
                        pending_var_t *pv = &pending[i];
                        if (!pv->needsSymLookup || !pv->name) {
                            continue;
                        }
                        if (!print_debuginfo_objdump_stabs_symbolMatch(symName, pv->name)) {
                            continue;
                        }
                        uint32_t base = 0;
                        const char *chosen = "unknown";
                        if (strcmp(section, ".text") == 0 || strncmp(section, ".text.", 6) == 0) {
                            base = debugger.machine.textBaseAddr;
                            chosen = "text";
                        } else if (strcmp(section, ".data") == 0 || strncmp(section, ".data.", 6) == 0) {
                            base = dataBase;
                            chosen = "data";
                        } else if (strcmp(section, ".bss") == 0 || strncmp(section, ".bss.", 5) == 0) {
                            base = bssBase;
                            chosen = "bss";
                        } else if (strcmp(section, ".rodata") == 0 || strncmp(section, ".rodata.", 8) == 0) {
                            base = dataBase ? dataBase : debugger.machine.textBaseAddr;
                            chosen = dataBase ? "data" : "text";
                        }
                        if (base == 0) {
                            continue;
                        }
                        pv->base = base;
                        strncpy(pv->chosenSection, chosen, sizeof(pv->chosenSection) - 1);
                        pv->addr = (base + symVal) & 0x00ffffffu;
                        pv->needsSymLookup = 0;
                        (void)print_debuginfo_objdump_stabs_addSymbol(index, pv->name, pv->addr);
                    }
                }
                pclose(sf);
            }
        }
    }

    for (size_t i = 0; i < pendingCount; ++i) {
        pending_var_t *pv = &pending[i];
        if (!pv->name) {
            continue;
        }
        if (pv->needsSymLookup || pv->addr == 0) {
            alloc_free(pv->name);
            pv->name = NULL;
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
        if (byteSize == 0 && pv->typeId != 0) {
            uint32_t cur = pv->typeId;
            for (int j = 0; j < 16; ++j) {
                if (cur == 0 || cur >= typeCap) {
                    break;
                }
                if (typeDefs[cur].def) {
                    (void)print_debuginfo_objdump_stabs_parseStructByteSize(typeDefs[cur].def, &byteSize);
                    if (byteSize != 0) {
                        break;
                    }
                }
                uint32_t next = typeDefs[cur].alias;
                if (next == 0 || next == cur) {
                    break;
                }
                cur = next;
            }
        }

        uint32_t typeRef = 0;
        if (pv->typeId != 0 && pv->typeId < typeCap) {
            print_type_t *t = print_debuginfo_objdump_stabs_buildType(index, typeDefs, typeCap, pv->typeId, 0);
            if (t) {
                typeRef = t->dieOffset;
            }
        }
        if (byteSize != 0) {
            if (print_debuginfo_objdump_stabs_setVariable(index, pv->name, pv->addr, typeRef, byteSize, 1)) {
                added = 1;
            }
        } else {
            (void)print_debuginfo_objdump_stabs_setVariable(index, pv->name, pv->addr, typeRef, 0, 0);
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
    if (typeDefs) {
        for (size_t i = 0; i < typeCap; ++i) {
            alloc_free(typeDefs[i].def);
            typeDefs[i].def = NULL;
            alloc_free(typeDefs[i].name);
            typeDefs[i].name = NULL;
        }
    }
    alloc_free(typeDefs);
    return added;
}
