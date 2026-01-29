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

#include "print_debuginfo_readelf.h"

#include "alloc.h"
#include "debugger.h"

static void
print_debuginfo_readelf_trimRight(char *s)
{
    if (!s) {
        return;
    }
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || isspace((unsigned char)s[len - 1]))) {
        s[--len] = '\0';
    }
}

static char *
print_debuginfo_readelf_strdup(const char *s)
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

static char *
print_debuginfo_readelf_parseNameValue(const char *line)
{
    if (!line) {
        return NULL;
    }
    const char *colon = strrchr(line, ':');
    if (!colon || !colon[1]) {
        return NULL;
    }
    const char *start = colon + 1;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    if (!*start) {
        return NULL;
    }
    char buf[512];
    strncpy(buf, start, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    print_debuginfo_readelf_trimRight(buf);
    if (buf[0] == '\0') {
        return NULL;
    }
    return print_debuginfo_readelf_strdup(buf);
}

static int
print_debuginfo_readelf_parseDieHeader(const char *line, int *outDepth, uint32_t *outOffset, char *outTag, size_t tagCap)
{
    if (!line || !outDepth || !outOffset || !outTag || tagCap == 0) {
        return 0;
    }
    const char *p = strchr(line, '<');
    if (!p) {
        return 0;
    }
    const char *q = strchr(p + 1, '>');
    if (!q) {
        return 0;
    }
    char depthBuf[16];
    size_t depthLen = (size_t)(q - (p + 1));
    if (depthLen >= sizeof(depthBuf)) {
        return 0;
    }
    memcpy(depthBuf, p + 1, depthLen);
    depthBuf[depthLen] = '\0';
    int depth = atoi(depthBuf);

    const char *p2 = strchr(q + 1, '<');
    if (!p2) {
        return 0;
    }
    const char *q2 = strchr(p2 + 1, '>');
    if (!q2) {
        return 0;
    }
    char offsetBuf[32];
    size_t offLen = (size_t)(q2 - (p2 + 1));
    if (offLen == 0 || offLen >= sizeof(offsetBuf)) {
        return 0;
    }
    memcpy(offsetBuf, p2 + 1, offLen);
    offsetBuf[offLen] = '\0';
    errno = 0;
    char *end = NULL;
    unsigned long off = strtoul(offsetBuf, &end, 16);
    if (errno != 0 || !end || end == offsetBuf) {
        return 0;
    }

    const char *tagStart = strstr(line, "(DW_TAG_");
    const char *tagEnd = NULL;
    if (tagStart) {
        tagStart += 1;
        tagEnd = strchr(tagStart, ')');
    } else {
        tagStart = strstr(line, "DW_TAG_");
        if (tagStart) {
            tagEnd = tagStart;
            while (*tagEnd && !isspace((unsigned char)*tagEnd) && *tagEnd != ')' && *tagEnd != ',') {
                ++tagEnd;
            }
        }
    }
    if (!tagStart || !tagEnd || tagEnd <= tagStart) {
        return 0;
    }
    size_t len = (size_t)(tagEnd - tagStart);
    if (len >= tagCap) {
        len = tagCap - 1;
    }
    memcpy(outTag, tagStart, len);
    outTag[len] = '\0';
    *outDepth = depth;
    *outOffset = (uint32_t)off;
    return 1;
}

static print_dwarf_tag_t
print_debuginfo_readelf_tagFromString(const char *tag)
{
    if (!tag) {
        return print_dwarf_tag_unknown;
    }
    if (strcmp(tag, "DW_TAG_compile_unit") == 0) {
        return print_dwarf_tag_compile_unit;
    }
    if (strcmp(tag, "DW_TAG_base_type") == 0) {
        return print_dwarf_tag_base_type;
    }
    if (strcmp(tag, "DW_TAG_pointer_type") == 0) {
        return print_dwarf_tag_pointer_type;
    }
    if (strcmp(tag, "DW_TAG_structure_type") == 0) {
        return print_dwarf_tag_structure_type;
    }
    if (strcmp(tag, "DW_TAG_member") == 0) {
        return print_dwarf_tag_member;
    }
    if (strcmp(tag, "DW_TAG_array_type") == 0) {
        return print_dwarf_tag_array_type;
    }
    if (strcmp(tag, "DW_TAG_subrange_type") == 0) {
        return print_dwarf_tag_subrange_type;
    }
    if (strcmp(tag, "DW_TAG_typedef") == 0) {
        return print_dwarf_tag_typedef;
    }
    if (strcmp(tag, "DW_TAG_const_type") == 0) {
        return print_dwarf_tag_const_type;
    }
    if (strcmp(tag, "DW_TAG_volatile_type") == 0) {
        return print_dwarf_tag_volatile_type;
    }
    if (strcmp(tag, "DW_TAG_enumeration_type") == 0) {
        return print_dwarf_tag_enumeration_type;
    }
    if (strcmp(tag, "DW_TAG_enumerator") == 0) {
        return print_dwarf_tag_enumerator;
    }
    if (strcmp(tag, "DW_TAG_subprogram") == 0) {
        return print_dwarf_tag_subprogram;
    }
    if (strcmp(tag, "DW_TAG_lexical_block") == 0) {
        return print_dwarf_tag_lexical_block;
    }
    if (strcmp(tag, "DW_TAG_inlined_subroutine") == 0) {
        return print_dwarf_tag_inlined_subroutine;
    }
    if (strcmp(tag, "DW_TAG_formal_parameter") == 0) {
        return print_dwarf_tag_formal_parameter;
    }
    if (strcmp(tag, "DW_TAG_variable") == 0) {
        return print_dwarf_tag_variable;
    }
    return print_dwarf_tag_unknown;
}

static print_base_encoding_t
print_debuginfo_readelf_parseEncoding(const char *line)
{
    if (!line) {
        return print_base_encoding_unknown;
    }
    if (strstr(line, "DW_ATE_signed")) {
        return print_base_encoding_signed;
    }
    if (strstr(line, "DW_ATE_unsigned")) {
        return print_base_encoding_unsigned;
    }
    if (strstr(line, "DW_ATE_float")) {
        return print_base_encoding_float;
    }
    if (strstr(line, "DW_ATE_boolean")) {
        return print_base_encoding_boolean;
    }
    return print_base_encoding_unknown;
}

static int
print_debuginfo_readelf_parseFirstNumber(const char *line, uint64_t *out)
{
    if (!line || !out) {
        return 0;
    }
    const char *start = line;

    // Prefer scanning after the attribute separator ':' so we don't accidentally
    // parse DIE offsets like "<11a56>" as the value.
    const char *attr = strstr(line, "DW_AT_");
    if (attr) {
        const char *colon = strchr(attr, ':');
        if (colon) {
            start = colon + 1;
        }
    } else {
        const char *colon = strchr(line, ':');
        if (colon) {
            start = colon + 1;
        }
    }

    const char *p = start;
    while (*p) {
        if (isdigit((unsigned char)*p) || (*p == '0' && (p[1] == 'x' || p[1] == 'X'))) {
            errno = 0;
            char *end = NULL;
            uint64_t val = strtoull(p, &end, 0);
            if (errno == 0 && end && end != p) {
                *out = val;
                return 1;
            }
        }
        ++p;
    }
    return 0;
}

static int
print_debuginfo_readelf_parseTypeRef(const char *line, uint32_t *outRef)
{
    if (!line || !outRef) {
        return 0;
    }
    const char *p = strrchr(line, '<');
    const char *q = NULL;
    if (p) {
        q = strchr(p + 1, '>');
    }
    if (!p || !q) {
        return 0;
    }
    char buf[32];
    size_t len = (size_t)(q - (p + 1));
    if (len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, p + 1, len);
    buf[len] = '\0';
    *outRef = (uint32_t)strtoul(buf, NULL, 16);
    return 1;
}

static int
print_debuginfo_readelf_parseLocationAddr(const char *line, uint64_t *outAddr)
{
    if (!line || !outAddr) {
        return 0;
    }
    const char *op = strstr(line, "DW_OP_addr");
    const char *p = NULL;
    if (op) {
        p = strstr(op, "0x");
        if (!p) {
            p = strstr(op, "DW_OP_addr:");
            if (p) {
                p += strlen("DW_OP_addr:");
                while (*p && isspace((unsigned char)*p)) {
                    ++p;
                }
            }
        }
    } else {
        p = strstr(line, "0x");
    }
    if (!p || !*p) {
        return 0;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        // ok
    } else if (!isxdigit((unsigned char)p[0])) {
        return 0;
    }
    errno = 0;
    // readelf prints DW_OP_addr values as hex, sometimes without 0x.
    uint64_t val = strtoull(p, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *outAddr = val;
    return 1;
}

static int
print_debuginfo_readelf_parseAbstractOrigin(const char *line, uint32_t *outRef)
{
    return print_debuginfo_readelf_parseTypeRef(line, outRef);
}

static int
print_debuginfo_readelf_parseLocationExpr(const char *line, print_dwarf_node_t *node)
{
    if (!line || !node) {
        return 0;
    }
    if (strstr(line, "location list")) {
        return 0;
    }

    // Constant value (stack_value) - handle DW_OP_addr as a constant number.
    if (strstr(line, "DW_OP_stack_value")) {
        const char *p = strstr(line, "DW_OP_addr:");
        if (p) {
            p += strlen("DW_OP_addr:");
            while (*p && isspace((unsigned char)*p)) {
                ++p;
            }
            errno = 0;
            char *end = NULL;
            unsigned long long v = strtoull(p, &end, 16);
            if (errno == 0 && end && end != p) {
                node->constValue = (uint64_t)v;
                node->hasConstValue = 1;
                node->locationKind = print_dwarf_location_const;
                return 1;
            }
        }
        return 0;
    }

    // DW_OP_fbreg: <signed>
    {
        const char *p = strstr(line, "DW_OP_fbreg:");
        if (p) {
            p += strlen("DW_OP_fbreg:");
            while (*p && isspace((unsigned char)*p)) {
                ++p;
            }
            errno = 0;
            char *end = NULL;
            long v = strtol(p, &end, 0);
            if (errno == 0 && end && end != p) {
                node->locationKind = print_dwarf_location_fbreg;
                node->locationOffset = (int32_t)v;
                return 1;
            }
        }
    }

    // DW_OP_bregN: <signed>
    {
        const char *p = strstr(line, "DW_OP_breg");
        if (p) {
            p += strlen("DW_OP_breg");
            errno = 0;
            char *end = NULL;
            unsigned long reg = strtoul(p, &end, 10);
            if (errno == 0 && end && end != p) {
                const char *colon = strchr(end, ':');
                if (colon) {
                    const char *q = colon + 1;
                    while (*q && isspace((unsigned char)*q)) {
                        ++q;
                    }
                    errno = 0;
                    char *end2 = NULL;
                    long off = strtol(q, &end2, 0);
                    if (errno == 0 && end2 && end2 != q) {
                        node->locationKind = print_dwarf_location_breg;
                        node->locationReg = (uint8_t)reg;
                        node->locationOffset = (int32_t)off;
                        return 1;
                    }
                }
            }
        }
    }

    // DW_OP_regN (value in register)
    {
        const char *p = strstr(line, "DW_OP_reg");
        if (p) {
            p += strlen("DW_OP_reg");
            errno = 0;
            char *end = NULL;
            unsigned long reg = strtoul(p, &end, 10);
            if (errno == 0 && end && end != p) {
                node->locationKind = print_dwarf_location_reg;
                node->locationReg = (uint8_t)reg;
                return 1;
            }
        }
    }

    if (strstr(line, "DW_OP_call_frame_cfa")) {
        node->locationKind = print_dwarf_location_cfa;
        return 1;
    }

    // DW_OP_addr: <hex>
    {
        uint64_t addr = 0;
        if (print_debuginfo_readelf_parseLocationAddr(line, &addr)) {
            node->addr = addr;
            node->hasAddr = 1;
            node->locationKind = print_dwarf_location_addr;
            return 1;
        }
    }

    return 0;
}

static print_dwarf_node_t *
print_debuginfo_readelf_addNode(print_index_t *index, uint32_t offset, uint32_t parentOffset, print_dwarf_tag_t tag, uint32_t cuOffset, int depth)
{
    if (!index) {
        return NULL;
    }
    if (index->nodeCount >= index->nodeCap) {
        int next = index->nodeCap ? index->nodeCap * 2 : 256;
        print_dwarf_node_t *nextNodes = (print_dwarf_node_t *)alloc_realloc(index->nodes, sizeof(*nextNodes) * (size_t)next);
        if (!nextNodes) {
            return NULL;
        }
        index->nodes = nextNodes;
        index->nodeCap = next;
    }
    print_dwarf_node_t *node = &index->nodes[index->nodeCount++];
    memset(node, 0, sizeof(*node));
    node->offset = offset;
    node->parentOffset = parentOffset;
    node->tag = tag;
    if (depth < 0) {
        depth = 0;
    }
    if (depth > 255) {
        depth = 255;
    }
    node->depth = (uint8_t)depth;
    if (cuOffset != 0) {
        uint32_t altA = offset + cuOffset;
        uint32_t altB = 0;
        if (offset >= cuOffset) {
            altB = offset - cuOffset;
        }
        if (altA != 0 && altA != offset) {
            node->altOffset = altA;
            node->hasAltOffset = 1;
        }
        if (altB != 0 && altB != offset) {
            if (node->hasAltOffset) {
                node->altOffset2 = altB;
                node->hasAltOffset2 = 1;
            } else {
                node->altOffset = altB;
                node->hasAltOffset = 1;
            }
        }
    }
    return node;
}

static int
print_debuginfo_readelf_addSymbol(print_index_t *index, const char *name, uint32_t addr)
{
    if (!index || !name) {
        return 0;
    }
    if (index->symbolCount >= index->symbolCap) {
        int next = index->symbolCap ? index->symbolCap * 2 : 1024;
        print_symbol_t *nextSyms = (print_symbol_t *)alloc_realloc(index->symbols, sizeof(*nextSyms) * (size_t)next);
        if (!nextSyms) {
            return 0;
        }
        index->symbols = nextSyms;
        index->symbolCap = next;
    }
    print_symbol_t *sym = &index->symbols[index->symbolCount++];
    memset(sym, 0, sizeof(*sym));
    sym->name = print_debuginfo_readelf_strdup(name);
    sym->addr = addr;
    return sym->name ? 1 : 0;
}

int
print_debuginfo_readelf_loadSymbols(const char *elfPath, print_index_t *index)
{
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    char objdump[PATH_MAX];
    if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "%s --syms '%s'", objdump, elfPath);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[8];
        int count = 0;
        char *cursor = line;
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
        char *end = NULL;
        uint32_t addr = (uint32_t)strtoul(tokens[0], &end, 16);
        if (!end || end == tokens[0]) {
            continue;
        }
        const char *name = tokens[count - 1];
        if (!name || !*name) {
            continue;
        }
        print_debuginfo_readelf_addSymbol(index, name, addr);
    }
    pclose(fp);
    return 1;
}

int
print_debuginfo_readelf_loadDwarfInfo(const char *elfPath, print_index_t *index)
{
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    char readelf[PATH_MAX];
    if (!debugger_toolchainBuildBinary(readelf, sizeof(readelf), "readelf")) {
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "%s --debug-dump=info '%s'", readelf, elfPath);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    uint32_t parentStack[64];
    int depthStack[64];
    memset(parentStack, 0, sizeof(parentStack));
    memset(depthStack, 0, sizeof(depthStack));
    int stackDepth = 0;
    print_dwarf_node_t *current = NULL;
    uint32_t cuOffset = 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        const char *cu = strstr(line, "Compilation Unit @ offset ");
        if (cu) {
            const char *p = strstr(cu, "0x");
            if (!p) {
                p = cu + strlen("Compilation Unit @ offset ");
            }
            errno = 0;
            char *end = NULL;
            unsigned long off = strtoul(p, &end, 0);
            if (errno == 0 && end && end != p) {
                cuOffset = (uint32_t)off;
            }
        }
        int depth = 0;
        uint32_t offset = 0;
        char tagBuf[128];
        if (print_debuginfo_readelf_parseDieHeader(line, &depth, &offset, tagBuf, sizeof(tagBuf))) {
            print_dwarf_tag_t tag = print_debuginfo_readelf_tagFromString(tagBuf);
            while (stackDepth > 0 && depthStack[stackDepth - 1] >= depth) {
                --stackDepth;
            }
            uint32_t parentOffset = (stackDepth > 0) ? parentStack[stackDepth - 1] : 0;
            current = print_debuginfo_readelf_addNode(index, offset, parentOffset, tag, cuOffset, depth);
            if (current) {
                parentStack[stackDepth] = offset;
                depthStack[stackDepth] = depth;
                if (stackDepth < (int)(sizeof(parentStack) / sizeof(parentStack[0])) - 1) {
                    ++stackDepth;
                }
            }
            continue;
        }
        if (!current) {
            continue;
        }
        if (strstr(line, "DW_AT_abstract_origin")) {
            uint32_t ref = 0;
            if (print_debuginfo_readelf_parseAbstractOrigin(line, &ref)) {
                current->abstractOrigin = ref;
                current->hasAbstractOrigin = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_name")) {
            char *name = print_debuginfo_readelf_parseNameValue(line);
            if (name) {
                alloc_free(current->name);
                current->name = name;
            }
            continue;
        }
        if (strstr(line, "DW_AT_type")) {
            uint32_t ref = 0;
            if (print_debuginfo_readelf_parseTypeRef(line, &ref)) {
                current->typeRef = ref;
                current->hasTypeRef = 1;
            } else {
                uint64_t val = 0;
                if (print_debuginfo_readelf_parseFirstNumber(line, &val)) {
                    current->typeRef = (uint32_t)val;
                    current->hasTypeRef = 1;
                }
            }
            continue;
        }
        if (strstr(line, "DW_AT_low_pc")) {
            uint64_t val = 0;
            if (print_debuginfo_readelf_parseFirstNumber(line, &val)) {
                current->lowPc = val;
                current->hasLowPc = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_high_pc")) {
            uint64_t val = 0;
            if (print_debuginfo_readelf_parseFirstNumber(line, &val)) {
                current->highPc = val;
                current->hasHighPc = 1;
                if (current->hasLowPc && val < current->lowPc) {
                    current->highPcIsOffset = 1;
                }
            }
            continue;
        }
        if (strstr(line, "DW_AT_byte_size")) {
            uint64_t val = 0;
            if (print_debuginfo_readelf_parseFirstNumber(line, &val)) {
                current->byteSize = val;
                current->hasByteSize = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_frame_base")) {
            if (strstr(line, "DW_OP_call_frame_cfa")) {
                // We treat frame base as CFA (no frame pointer builds).
                current->frameBaseKind = print_dwarf_location_cfa;
                current->hasFrameBase = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_encoding")) {
            current->encoding = print_debuginfo_readelf_parseEncoding(line);
            continue;
        }
        if (strstr(line, "DW_AT_data_member_location")) {
            uint64_t val = 0;
            if (print_debuginfo_readelf_parseFirstNumber(line, &val)) {
                current->memberOffset = (int64_t)val;
                current->hasMemberOffset = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_location")) {
            uint64_t addr = 0;
            if (print_debuginfo_readelf_parseLocationExpr(line, current)) {
                continue;
            }
            if (print_debuginfo_readelf_parseLocationAddr(line, &addr)) {
                current->addr = addr;
                current->hasAddr = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_upper_bound")) {
            uint64_t val = 0;
            if (print_debuginfo_readelf_parseFirstNumber(line, &val)) {
                current->upperBound = (int64_t)val;
                current->hasUpperBound = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_count")) {
            uint64_t val = 0;
            if (print_debuginfo_readelf_parseFirstNumber(line, &val)) {
                current->count = (int64_t)val;
                current->hasCount = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_location")) {
            uint64_t addr = 0;
            if (print_debuginfo_readelf_parseLocationAddr(line, &addr)) {
                current->addr = addr;
                current->hasAddr = 1;
            }
            continue;
        }
    }
    pclose(fp);
    return 1;
}
