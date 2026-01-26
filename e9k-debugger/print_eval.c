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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "print_eval.h"
#include "alloc.h"
#include "debug.h"
#include "debugger.h"
#include "libretro.h"
#include "libretro_host.h"
#include "machine.h"

typedef enum print_dwarf_tag {
    print_dwarf_tag_unknown = 0,
    print_dwarf_tag_base_type,
    print_dwarf_tag_pointer_type,
    print_dwarf_tag_structure_type,
    print_dwarf_tag_member,
    print_dwarf_tag_array_type,
    print_dwarf_tag_subrange_type,
    print_dwarf_tag_typedef,
    print_dwarf_tag_const_type,
    print_dwarf_tag_volatile_type,
    print_dwarf_tag_enumeration_type,
    print_dwarf_tag_enumerator,
    print_dwarf_tag_variable
} print_dwarf_tag_t;

typedef enum print_base_encoding {
    print_base_encoding_unknown = 0,
    print_base_encoding_signed,
    print_base_encoding_unsigned,
    print_base_encoding_float,
    print_base_encoding_boolean
} print_base_encoding_t;

typedef struct print_dwarf_node {
    uint32_t offset;
    uint32_t parentOffset;
    print_dwarf_tag_t tag;
    char *name;
    uint32_t typeRef;
    uint64_t byteSize;
    uint64_t addr;
    int64_t memberOffset;
    int64_t upperBound;
    int64_t count;
    print_base_encoding_t encoding;
    int hasTypeRef;
    int hasByteSize;
    int hasAddr;
    int hasMemberOffset;
    int hasUpperBound;
    int hasCount;
} print_dwarf_node_t;

typedef struct print_symbol {
    char *name;
    uint32_t addr;
} print_symbol_t;

typedef struct print_variable {
    char *name;
    uint32_t addr;
    uint32_t typeRef;
} print_variable_t;

typedef enum print_type_kind {
    print_type_invalid = 0,
    print_type_base,
    print_type_pointer,
    print_type_struct,
    print_type_array,
    print_type_typedef,
    print_type_const,
    print_type_volatile,
    print_type_enum
} print_type_kind_t;

typedef struct print_member {
    char *name;
    uint32_t offset;
    struct print_type *type;
} print_member_t;

typedef struct print_type {
    uint32_t dieOffset;
    print_type_kind_t kind;
    char *name;
    size_t byteSize;
    print_base_encoding_t encoding;
    struct print_type *targetType;
    print_member_t *members;
    int memberCount;
    size_t arrayCount;
} print_type_t;

typedef struct print_index {
    char elfPath[PATH_MAX];
    print_dwarf_node_t *nodes;
    int nodeCount;
    int nodeCap;
    print_symbol_t *symbols;
    int symbolCount;
    int symbolCap;
    print_variable_t *vars;
    int varCount;
    int varCap;
    print_type_t **types;
    int typeCount;
    int typeCap;
    print_type_t *defaultU32;
} print_index_t;

typedef struct print_value {
    print_type_t *type;
    uint32_t address;
    uint64_t immediate;
    int hasAddress;
    int hasImmediate;
} print_value_t;

typedef struct print_temp_type {
    print_type_t *type;
    struct print_temp_type *next;
} print_temp_type_t;

static print_index_t print_eval_index = {0};

static void
print_eval_freeString(char *s)
{
    if (s) {
        alloc_free(s);
    }
}

static char *
print_eval_strdup(const char *s)
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

static void
print_eval_trimRight(char *s)
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
print_eval_parseNameValue(const char *line)
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
    print_eval_trimRight(buf);
    if (buf[0] == '\0') {
        return NULL;
    }
    return print_eval_strdup(buf);
}

static int
print_eval_parseDieHeader(const char *line, int *outDepth, uint32_t *outOffset, char *outTag, size_t tagCap)
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
    if (offLen >= sizeof(offsetBuf)) {
        return 0;
    }
    memcpy(offsetBuf, p2 + 1, offLen);
    offsetBuf[offLen] = '\0';
    uint32_t offset = (uint32_t)strtoul(offsetBuf, NULL, 16);
    const char *tagStart = strstr(line, "(DW_TAG_");
    if (!tagStart) {
        return 0;
    }
    tagStart += 1;
    const char *tagEnd = strchr(tagStart, ')');
    if (!tagEnd) {
        return 0;
    }
    size_t tagLen = (size_t)(tagEnd - tagStart);
    if (tagLen >= tagCap) {
        tagLen = tagCap - 1;
    }
    memcpy(outTag, tagStart, tagLen);
    outTag[tagLen] = '\0';
    *outDepth = depth;
    *outOffset = offset;
    return 1;
}

static print_dwarf_tag_t
print_eval_tagFromString(const char *tag)
{
    if (!tag) {
        return print_dwarf_tag_unknown;
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
    if (strcmp(tag, "DW_TAG_variable") == 0) {
        return print_dwarf_tag_variable;
    }
    return print_dwarf_tag_unknown;
}

static print_base_encoding_t
print_eval_parseEncoding(const char *line)
{
    if (!line) {
        return print_base_encoding_unknown;
    }
    if (strstr(line, "DW_ATE_float")) {
        return print_base_encoding_float;
    }
    if (strstr(line, "DW_ATE_boolean")) {
        return print_base_encoding_boolean;
    }
    if (strstr(line, "DW_ATE_unsigned") || strstr(line, "unsigned")) {
        return print_base_encoding_unsigned;
    }
    if (strstr(line, "DW_ATE_signed")) {
        return print_base_encoding_signed;
    }
    return print_base_encoding_unknown;
}

static int
print_eval_parseFirstNumber(const char *line, uint64_t *out)
{
    if (!line || !out) {
        return 0;
    }
    const char *p = strchr(line, ':');
    if (p) {
        ++p;
    } else {
        p = line;
    }
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
print_eval_parseTypeRef(const char *line, uint32_t *outRef)
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
print_eval_parseLocationAddr(const char *line, uint64_t *outAddr)
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
    errno = 0;
    uint64_t val = strtoull(p, NULL, 0);
    if (errno != 0) {
        return 0;
    }
    *outAddr = val;
    return 1;
}

static print_dwarf_node_t *
print_eval_addNode(print_index_t *index, uint32_t offset, uint32_t parentOffset, print_dwarf_tag_t tag)
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
    return node;
}

static print_dwarf_node_t *
print_eval_findNode(print_index_t *index, uint32_t offset)
{
    if (!index) {
        return NULL;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        if (index->nodes[i].offset == offset) {
            return &index->nodes[i];
        }
    }
    return NULL;
}

static int
print_eval_addSymbol(print_index_t *index, const char *name, uint32_t addr)
{
    if (!index || !name || !*name) {
        return 0;
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
    sym->name = print_eval_strdup(name);
    sym->addr = addr;
    return sym->name != NULL;
}

static int
print_eval_addVariable(print_index_t *index, const char *name, uint32_t addr, uint32_t typeRef)
{
    if (!index || !name || !*name) {
        return 0;
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
    var->name = print_eval_strdup(name);
    var->addr = addr;
    var->typeRef = typeRef;
    return var->name != NULL;
}

static void
print_eval_clearIndex(print_index_t *index)
{
    if (!index) {
        return;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        print_eval_freeString(index->nodes[i].name);
    }
    alloc_free(index->nodes);
    index->nodes = NULL;
    index->nodeCount = 0;
    index->nodeCap = 0;
    for (int i = 0; i < index->symbolCount; ++i) {
        print_eval_freeString(index->symbols[i].name);
    }
    alloc_free(index->symbols);
    index->symbols = NULL;
    index->symbolCount = 0;
    index->symbolCap = 0;
    for (int i = 0; i < index->varCount; ++i) {
        print_eval_freeString(index->vars[i].name);
    }
    alloc_free(index->vars);
    index->vars = NULL;
    index->varCount = 0;
    index->varCap = 0;
    for (int i = 0; i < index->typeCount; ++i) {
        print_type_t *type = index->types[i];
        if (type) {
            print_eval_freeString(type->name);
            for (int m = 0; m < type->memberCount; ++m) {
                print_eval_freeString(type->members[m].name);
            }
            alloc_free(type->members);
            alloc_free(type);
        }
    }
    alloc_free(index->types);
    index->types = NULL;
    index->typeCount = 0;
    index->typeCap = 0;
    index->defaultU32 = NULL;
    index->elfPath[0] = '\0';
}

static int
print_eval_parseSymbols(const char *elfPath, print_index_t *index)
{
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "m68k-neogeo-elf-objdump --syms '%s'", elfPath);
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
        print_eval_addSymbol(index, name, addr);
    }
    pclose(fp);
    return 1;
}

static int
print_eval_parseDwarfInfo(const char *elfPath, print_index_t *index)
{
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "m68k-neogeo-elf-readelf --debug-dump=info '%s'", elfPath);
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
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        int depth = 0;
        uint32_t offset = 0;
        char tagBuf[128];
        if (print_eval_parseDieHeader(line, &depth, &offset, tagBuf, sizeof(tagBuf))) {
            print_dwarf_tag_t tag = print_eval_tagFromString(tagBuf);
            while (stackDepth > 0 && depthStack[stackDepth - 1] >= depth) {
                --stackDepth;
            }
            uint32_t parentOffset = (stackDepth > 0) ? parentStack[stackDepth - 1] : 0;
            current = print_eval_addNode(index, offset, parentOffset, tag);
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
        if (strstr(line, "DW_AT_name")) {
            char *name = print_eval_parseNameValue(line);
            if (name) {
                print_eval_freeString(current->name);
                current->name = name;
            }
            continue;
        }
        if (strstr(line, "DW_AT_type")) {
            uint32_t ref = 0;
            if (print_eval_parseTypeRef(line, &ref)) {
                current->typeRef = ref;
                current->hasTypeRef = 1;
            } else {
                uint64_t val = 0;
                if (print_eval_parseFirstNumber(line, &val)) {
                    current->typeRef = (uint32_t)val;
                    current->hasTypeRef = 1;
                }
            }
            continue;
        }
        if (strstr(line, "DW_AT_byte_size")) {
            uint64_t val = 0;
            if (print_eval_parseFirstNumber(line, &val)) {
                current->byteSize = val;
                current->hasByteSize = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_encoding")) {
            current->encoding = print_eval_parseEncoding(line);
            continue;
        }
        if (strstr(line, "DW_AT_data_member_location")) {
            uint64_t val = 0;
            if (print_eval_parseFirstNumber(line, &val)) {
                current->memberOffset = (int64_t)val;
                current->hasMemberOffset = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_upper_bound")) {
            uint64_t val = 0;
            if (print_eval_parseFirstNumber(line, &val)) {
                current->upperBound = (int64_t)val;
                current->hasUpperBound = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_count")) {
            uint64_t val = 0;
            if (print_eval_parseFirstNumber(line, &val)) {
                current->count = (int64_t)val;
                current->hasCount = 1;
            }
            continue;
        }
        if (strstr(line, "DW_AT_location")) {
            uint64_t addr = 0;
            if (print_eval_parseLocationAddr(line, &addr)) {
                current->addr = addr;
                current->hasAddr = 1;
            }
            continue;
        }
    }
    pclose(fp);
    return 1;
}

static uint32_t
print_eval_lookupSymbolAddr(print_index_t *index, const char *name, int *found)
{
    if (found) {
        *found = 0;
    }
    if (!index || !name) {
        return 0;
    }
    for (int i = 0; i < index->symbolCount; ++i) {
        if (strcmp(index->symbols[i].name, name) == 0) {
            if (found) {
                *found = 1;
            }
            return index->symbols[i].addr;
        }
    }
    return 0;
}

static void
print_eval_buildVariables(print_index_t *index)
{
    if (!index) {
        return;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        print_dwarf_node_t *node = &index->nodes[i];
        if (node->tag != print_dwarf_tag_variable) {
            continue;
        }
        if (!node->name || !node->hasTypeRef) {
            continue;
        }
        uint32_t addr = 0;
        int hasAddr = 0;
        if (node->hasAddr) {
            addr = (uint32_t)(node->addr & 0x00ffffffu);
            hasAddr = 1;
        }
        if (!hasAddr) {
            addr = print_eval_lookupSymbolAddr(index, node->name, &hasAddr);
        }
        if (!hasAddr) {
            continue;
        }
        print_eval_addVariable(index, node->name, addr, node->typeRef);
    }
}

static print_type_t *
print_eval_findType(print_index_t *index, uint32_t offset)
{
    if (!index) {
        return NULL;
    }
    for (int i = 0; i < index->typeCount; ++i) {
        if (index->types[i] && index->types[i]->dieOffset == offset) {
            return index->types[i];
        }
    }
    return NULL;
}

static print_type_t *
print_eval_addType(print_index_t *index, uint32_t offset)
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
    type->dieOffset = offset;
    index->types[index->typeCount++] = type;
    return type;
}

static print_type_t *
print_eval_getType(print_index_t *index, uint32_t offset);

static size_t
print_eval_arrayCountFromNode(print_index_t *index, print_dwarf_node_t *node)
{
    if (!index || !node) {
        return 0;
    }
    for (int i = 0; i < index->nodeCount; ++i) {
        print_dwarf_node_t *child = &index->nodes[i];
        if (child->parentOffset != node->offset) {
            continue;
        }
        if (child->tag != print_dwarf_tag_subrange_type) {
            continue;
        }
        if (child->hasCount) {
            return (size_t)child->count;
        }
        if (child->hasUpperBound) {
            return (size_t)(child->upperBound + 1);
        }
    }
    return 0;
}

static int
print_eval_collectMembers(print_index_t *index, print_dwarf_node_t *node, print_type_t *type)
{
    if (!index || !node || !type) {
        return 0;
    }
    int memberCount = 0;
    for (int i = 0; i < index->nodeCount; ++i) {
        if (index->nodes[i].parentOffset == node->offset && index->nodes[i].tag == print_dwarf_tag_member) {
            ++memberCount;
        }
    }
    if (memberCount <= 0) {
        return 1;
    }
    type->members = (print_member_t *)alloc_calloc((size_t)memberCount, sizeof(*type->members));
    if (!type->members) {
        return 0;
    }
    type->memberCount = 0;
    for (int i = 0; i < index->nodeCount; ++i) {
        print_dwarf_node_t *child = &index->nodes[i];
        if (child->parentOffset != node->offset || child->tag != print_dwarf_tag_member) {
            continue;
        }
        print_member_t *member = &type->members[type->memberCount++];
        member->name = print_eval_strdup(child->name ? child->name : "<anon>");
        member->offset = child->hasMemberOffset ? (uint32_t)child->memberOffset : 0;
        member->type = child->hasTypeRef ? print_eval_getType(index, child->typeRef) : NULL;
    }
    return 1;
}

static print_type_t *
print_eval_getType(print_index_t *index, uint32_t offset)
{
    if (!index || offset == 0) {
        return NULL;
    }
    print_type_t *existing = print_eval_findType(index, offset);
    if (existing) {
        return existing;
    }
    print_dwarf_node_t *node = print_eval_findNode(index, offset);
    if (!node) {
        return NULL;
    }
    print_type_t *type = print_eval_addType(index, offset);
    if (!type) {
        return NULL;
    }
    type->name = print_eval_strdup(node->name ? node->name : "");
    switch (node->tag) {
        case print_dwarf_tag_base_type:
            type->kind = print_type_base;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 0;
            type->encoding = node->encoding;
            break;
        case print_dwarf_tag_pointer_type:
            type->kind = print_type_pointer;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 4;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_structure_type:
            type->kind = print_type_struct;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 0;
            print_eval_collectMembers(index, node, type);
            break;
        case print_dwarf_tag_array_type:
            type->kind = print_type_array;
            type->arrayCount = print_eval_arrayCountFromNode(index, node);
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_typedef:
            type->kind = print_type_typedef;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_const_type:
            type->kind = print_type_const;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_volatile_type:
            type->kind = print_type_volatile;
            if (node->hasTypeRef) {
                type->targetType = print_eval_getType(index, node->typeRef);
            }
            break;
        case print_dwarf_tag_enumeration_type:
            type->kind = print_type_enum;
            type->byteSize = node->hasByteSize ? (size_t)node->byteSize : 4;
            type->encoding = print_base_encoding_signed;
            break;
        default:
            type->kind = print_type_invalid;
            break;
    }
    return type;
}

static print_type_t *
print_eval_resolveType(print_type_t *type)
{
    print_type_t *cur = type;
    while (cur) {
        if (cur->kind == print_type_typedef || cur->kind == print_type_const || cur->kind == print_type_volatile) {
            cur = cur->targetType;
            continue;
        }
        break;
    }
    return cur;
}

static print_type_t *
print_eval_defaultU32(print_index_t *index)
{
    if (!index) {
        return NULL;
    }
    if (index->defaultU32) {
        return index->defaultU32;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->kind = print_type_base;
    type->byteSize = 4;
    type->encoding = print_base_encoding_unsigned;
    type->name = print_eval_strdup("uint32_t");
    index->defaultU32 = type;
    if (index->typeCount >= index->typeCap) {
        int next = index->typeCap ? index->typeCap * 2 : 128;
        print_type_t **nextTypes = (print_type_t **)alloc_realloc(index->types, sizeof(*nextTypes) * (size_t)next);
        if (!nextTypes) {
            return type;
        }
        index->types = nextTypes;
        index->typeCap = next;
    }
    index->types[index->typeCount++] = type;
    return type;
}

static int
print_eval_loadIndex(print_index_t *index)
{
    const char *elfPath = debugger.config.elfPath;
    if (!elfPath || !*elfPath || !index) {
        return 0;
    }
    if (index->elfPath[0] != '\0' && strcmp(index->elfPath, elfPath) == 0) {
        return 1;
    }
    print_eval_clearIndex(index);
    strncpy(index->elfPath, elfPath, sizeof(index->elfPath) - 1);
    index->elfPath[sizeof(index->elfPath) - 1] = '\0';
    print_eval_parseSymbols(elfPath, index);
    print_eval_parseDwarfInfo(elfPath, index);
    print_eval_buildVariables(index);
    print_eval_defaultU32(index);
    return 1;
}

static print_variable_t *
print_eval_findVariable(print_index_t *index, const char *name)
{
    if (!index || !name) {
        return NULL;
    }
    for (int i = 0; i < index->varCount; ++i) {
        if (strcmp(index->vars[i].name, name) == 0) {
            return &index->vars[i];
        }
    }
    return NULL;
}

static print_symbol_t *
print_eval_findSymbol(print_index_t *index, const char *name)
{
    if (!index || !name) {
        return NULL;
    }
    for (int i = 0; i < index->symbolCount; ++i) {
        if (strcmp(index->symbols[i].name, name) == 0) {
            return &index->symbols[i];
        }
    }
    return NULL;
}

static int
print_eval_readMemory(uint32_t addr, void *out, size_t size)
{
    if (!out || size == 0) {
        return 0;
    }
    if (libretro_host_debugReadMemory(addr, out, size)) {
        return 1;
    }
    size_t ramSize = 0;
    const uint8_t *ram = (const uint8_t *)libretro_host_getMemory(RETRO_MEMORY_SYSTEM_RAM, &ramSize);
    if (!ram || ramSize == 0) {
        return 0;
    }
    const uint32_t ramBase = 0x00100000u;
    const uint32_t ramEnd = 0x001fffffu;
    for (size_t i = 0; i < size; ++i) {
        uint32_t cur = addr + (uint32_t)i;
        if (cur < ramBase || cur > ramEnd) {
            return 0;
        }
        uint32_t offset = cur & 0xffffu;
        if (offset >= ramSize) {
            return 0;
        }
        ((uint8_t *)out)[i] = ram[offset];
    }
    return 1;
}

static uint64_t
print_eval_readUnsigned(uint32_t addr, size_t size, int *ok)
{
    uint8_t buf[16];
    if (ok) {
        *ok = 0;
    }
    if (size == 0 || size > sizeof(buf)) {
        return 0;
    }
    if (!print_eval_readMemory(addr, buf, size)) {
        return 0;
    }
    uint64_t val = 0;
    for (size_t i = 0; i < size; ++i) {
        val = (val << 8) | (uint64_t)buf[i];
    }
    if (ok) {
        *ok = 1;
    }
    return val;
}

static int64_t
print_eval_signExtend(uint64_t value, size_t size)
{
    if (size == 0 || size >= 8) {
        return (int64_t)value;
    }
    size_t bits = size * 8;
    uint64_t signBit = 1ull << (bits - 1);
    if (value & signBit) {
        uint64_t mask = (~0ull) << bits;
        return (int64_t)(value | mask);
    }
    return (int64_t)value;
}

static void
print_eval_printLine(int indent, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    int pad = indent > 0 ? indent : 0;
    if (pad > 120) {
        pad = 120;
    }
    char line[1152];
    int offset = 0;
    if (pad > 0) {
        memset(line, ' ', (size_t)pad);
        offset = pad;
    }
    size_t remaining = sizeof(line) - (size_t)offset - 1;
    strncpy(line + offset, msg, remaining);
    line[offset + remaining] = '\0';
    debug_printf("%s", line);
}

static void
print_eval_dumpValueAt(print_type_t *type, uint32_t addr, int indent, const char *label)
{
    print_type_t *resolved = print_eval_resolveType(type);
    if (!resolved) {
        print_eval_printLine(indent, "%s: 0x%06X", label ? label : "<value>", addr);
        return;
    }
    switch (resolved->kind) {
        case print_type_base: {
            int ok = 0;
            size_t size = resolved->byteSize > 0 ? resolved->byteSize : 4;
            uint64_t val = print_eval_readUnsigned(addr, size, &ok);
            if (!ok) {
                print_eval_printLine(indent, "%s: <unreadable>", label ? label : "<value>");
                return;
            }
            if (resolved->encoding == print_base_encoding_float) {
                if (size == 4) {
                    uint32_t tmp = (uint32_t)val;
                    float f = 0.0f;
                    memcpy(&f, &tmp, sizeof(f));
                    print_eval_printLine(indent, "%s: %f", label ? label : "<value>", (double)f);
                } else if (size == 8) {
                    uint64_t tmp = val;
                    double d = 0.0;
                    memcpy(&d, &tmp, sizeof(d));
                    print_eval_printLine(indent, "%s: %f", label ? label : "<value>", d);
                } else {
                    print_eval_printLine(indent, "%s: 0x%llX", label ? label : "<value>", (unsigned long long)val);
                }
                return;
            }
            if (resolved->encoding == print_base_encoding_signed) {
                int64_t s = print_eval_signExtend(val, size);
                print_eval_printLine(indent, "%s: %lld (0x%llX)", label ? label : "<value>",
                                    (long long)s, (unsigned long long)val);
                return;
            }
            if (resolved->encoding == print_base_encoding_boolean) {
                print_eval_printLine(indent, "%s: %s", label ? label : "<value>", val ? "true" : "false");
                return;
            }
            print_eval_printLine(indent, "%s: %llu (0x%llX)", label ? label : "<value>",
                                (unsigned long long)val, (unsigned long long)val);
            return;
        }
        case print_type_pointer: {
            int ok = 0;
            size_t size = resolved->byteSize > 0 ? resolved->byteSize : 4;
            uint64_t ptrVal = print_eval_readUnsigned(addr, size, &ok);
            if (!ok) {
                print_eval_printLine(indent, "%s: <unreadable>", label ? label : "<value>");
                return;
            }
            print_eval_printLine(indent, "%s: 0x%08llX", label ? label : "<value>", (unsigned long long)ptrVal);
            return;
        }
        case print_type_struct: {
            print_eval_printLine(indent, "%s:", label ? label : (resolved->name && *resolved->name ? resolved->name : "<struct>"));
            for (int i = 0; i < resolved->memberCount; ++i) {
                print_member_t *member = &resolved->members[i];
                uint32_t memberAddr = addr + member->offset;
                const char *memberName = member->name ? member->name : "<member>";
                print_eval_dumpValueAt(member->type, memberAddr, indent + 2, memberName);
            }
            return;
        }
        case print_type_array: {
            size_t count = resolved->arrayCount;
            print_eval_printLine(indent, "%s:", label ? label : "<array>");
            if (!resolved->targetType || count == 0) {
                return;
            }
            size_t elemSize = resolved->targetType->byteSize > 0 ? resolved->targetType->byteSize : 1;
            for (size_t i = 0; i < count; ++i) {
                uint32_t elemAddr = addr + (uint32_t)(i * elemSize);
                char nameBuf[64];
                snprintf(nameBuf, sizeof(nameBuf), "[%zu]", i);
                print_eval_dumpValueAt(resolved->targetType, elemAddr, indent + 2, nameBuf);
            }
            return;
        }
        case print_type_enum: {
            int ok = 0;
            size_t size = resolved->byteSize > 0 ? resolved->byteSize : 4;
            uint64_t val = print_eval_readUnsigned(addr, size, &ok);
            if (!ok) {
                print_eval_printLine(indent, "%s: <unreadable>", label ? label : "<value>");
                return;
            }
            int64_t s = print_eval_signExtend(val, size);
            print_eval_printLine(indent, "%s: %lld (0x%llX)", label ? label : "<value>",
                                (long long)s, (unsigned long long)val);
            return;
        }
        default:
            print_eval_printLine(indent, "%s: <unsupported>", label ? label : "<value>");
            return;
    }
}

static int
print_eval_readPointerValue(const print_value_t *value, uint32_t *outAddr)
{
    if (!value || !outAddr) {
        return 0;
    }
    if (value->hasImmediate) {
        *outAddr = (uint32_t)value->immediate;
        return 1;
    }
    if (!value->hasAddress) {
        return 0;
    }
    size_t size = 4;
    if (value->type && value->type->byteSize > 0) {
        size = value->type->byteSize;
    }
    int ok = 0;
    uint64_t val = print_eval_readUnsigned(value->address, size, &ok);
    if (!ok) {
        return 0;
    }
    *outAddr = (uint32_t)val;
    return 1;
}

static void
print_eval_skipSpace(const char **cursor)
{
    if (!cursor || !*cursor) {
        return;
    }
    const char *p = *cursor;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    *cursor = p;
}

static int
print_eval_parseIdentifier(const char **cursor, char *out, size_t cap)
{
    if (!cursor || !*cursor || !out || cap == 0) {
        return 0;
    }
    const char *p = *cursor;
    if (!isalpha((unsigned char)*p) && *p != '_') {
        return 0;
    }
    size_t len = 0;
    while ((isalnum((unsigned char)*p) || *p == '_') && len + 1 < cap) {
        out[len++] = *p++;
    }
    out[len] = '\0';
    *cursor = p;
    return (int)len;
}

static int
print_eval_parseNumber(const char **cursor, uint64_t *out)
{
    if (!cursor || !*cursor || !out) {
        return 0;
    }
    const char *p = *cursor;
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    uint64_t val = strtoull(p, &end, 0);
    if (errno != 0 || !end || end == p) {
        return 0;
    }
    *out = val;
    *cursor = end;
    return 1;
}

static print_value_t
print_eval_makeAddressValue(print_type_t *type, uint32_t addr)
{
    print_value_t val;
    memset(&val, 0, sizeof(val));
    val.type = type;
    val.address = addr;
    val.hasAddress = 1;
    return val;
}

static print_value_t
print_eval_makeImmediateValue(print_type_t *type, uint64_t immediate)
{
    print_value_t val;
    memset(&val, 0, sizeof(val));
    val.type = type;
    val.immediate = immediate;
    val.hasImmediate = 1;
    return val;
}

static print_type_t *
print_eval_makeTempPointerType(print_temp_type_t **list, print_type_t *target)
{
    if (!list) {
        return NULL;
    }
    print_type_t *type = (print_type_t *)alloc_calloc(1, sizeof(*type));
    if (!type) {
        return NULL;
    }
    type->kind = print_type_pointer;
    type->byteSize = 4;
    type->targetType = target;
    type->name = print_eval_strdup("");
    print_temp_type_t *node = (print_temp_type_t *)alloc_calloc(1, sizeof(*node));
    if (!node) {
        alloc_free(type);
        return NULL;
    }
    node->type = type;
    node->next = *list;
    *list = node;
    return type;
}

static void
print_eval_freeTempTypes(print_temp_type_t *list)
{
    while (list) {
        print_temp_type_t *next = list->next;
        if (list->type) {
            print_eval_freeString(list->type->name);
            alloc_free(list->type);
        }
        alloc_free(list);
        list = next;
    }
}

static int
print_eval_parseExpression(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly);

static int
print_eval_parsePrimary(const char **cursor, print_index_t *index, print_value_t *out, int typeOnly)
{
    print_eval_skipSpace(cursor);
    if (!cursor || !*cursor || !out || !index) {
        return 0;
    }
    if (**cursor == '(') {
        ++(*cursor);
        if (!print_eval_parseExpression(cursor, index, out, NULL, typeOnly)) {
            return 0;
        }
        print_eval_skipSpace(cursor);
        if (**cursor == ')') {
            ++(*cursor);
        }
        return 1;
    }
    char ident[256];
    if (print_eval_parseIdentifier(cursor, ident, sizeof(ident))) {
        print_variable_t *var = print_eval_findVariable(index, ident);
        if (var) {
            print_type_t *type = print_eval_getType(index, var->typeRef);
            *out = print_eval_makeAddressValue(type, var->addr);
            return 1;
        }
        print_symbol_t *sym = print_eval_findSymbol(index, ident);
        if (sym) {
            print_type_t *type = print_eval_defaultU32(index);
            *out = print_eval_makeAddressValue(type, sym->addr);
            return 1;
        }
        unsigned long regValue = 0;
        if (machine_findReg(&debugger.machine, ident, &regValue)) {
            print_type_t *type = print_eval_defaultU32(index);
            *out = print_eval_makeImmediateValue(type, (uint64_t)regValue);
            return 1;
        }
        return 0;
    }
    uint64_t number = 0;
    if (print_eval_parseNumber(cursor, &number)) {
        print_type_t *type = print_eval_defaultU32(index);
        *out = print_eval_makeImmediateValue(type, number);
        return 1;
    }
    return 0;
}

static int
print_eval_parseUnary(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    print_eval_skipSpace(cursor);
    if (**cursor == '&') {
        ++(*cursor);
        print_value_t inner;
        if (!print_eval_parseUnary(cursor, index, &inner, tempTypes, typeOnly)) {
            return 0;
        }
        if (!inner.hasAddress && !typeOnly) {
            return 0;
        }
        print_type_t *ptrType = print_eval_makeTempPointerType(tempTypes, inner.type);
        if (typeOnly) {
            *out = print_eval_makeImmediateValue(ptrType, 0);
            out->hasImmediate = 0;
        } else {
            *out = print_eval_makeImmediateValue(ptrType, inner.address);
        }
        return 1;
    }
    if (**cursor == '*') {
        ++(*cursor);
        print_value_t inner;
        if (!print_eval_parseUnary(cursor, index, &inner, tempTypes, typeOnly)) {
            return 0;
        }
        print_type_t *resolved = print_eval_resolveType(inner.type);
        if (resolved && resolved->kind == print_type_pointer) {
            if (typeOnly) {
                *out = print_eval_makeAddressValue(resolved->targetType, 0);
                out->hasAddress = 0;
            } else {
                uint32_t addr = 0;
                if (!print_eval_readPointerValue(&inner, &addr)) {
                    return 0;
                }
                *out = print_eval_makeAddressValue(resolved->targetType, addr);
            }
            return 1;
        }
        if (typeOnly) {
            *out = print_eval_makeAddressValue(print_eval_defaultU32(index), 0);
            out->hasAddress = 0;
            return 1;
        }
        uint32_t addr = 0;
        if (inner.hasImmediate) {
            addr = (uint32_t)inner.immediate;
        } else if (inner.hasAddress) {
            addr = inner.address;
        } else {
            return 0;
        }
        *out = print_eval_makeAddressValue(print_eval_defaultU32(index), addr);
        return 1;
    }
    return print_eval_parsePrimary(cursor, index, out, typeOnly);
}

static int
print_eval_parsePostfix(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    if (!print_eval_parseUnary(cursor, index, out, tempTypes, typeOnly)) {
        return 0;
    }
    for (;;) {
        print_eval_skipSpace(cursor);
        if (**cursor == '.' || (**cursor == '-' && (*cursor)[1] == '>')) {
            int isArrow = 0;
            if (**cursor == '-') {
                isArrow = 1;
                *cursor += 2;
            } else {
                ++(*cursor);
            }
            print_eval_skipSpace(cursor);
            char memberName[256];
            if (!print_eval_parseIdentifier(cursor, memberName, sizeof(memberName))) {
                return 0;
            }
            print_type_t *resolved = print_eval_resolveType(out->type);
            uint32_t baseAddr = 0;
            if (isArrow) {
                if (!resolved || resolved->kind != print_type_pointer) {
                    return 0;
                }
                if (!typeOnly) {
                    print_value_t ptrVal = *out;
                    ptrVal.type = resolved;
                    if (!print_eval_readPointerValue(&ptrVal, &baseAddr)) {
                        return 0;
                    }
                }
                resolved = print_eval_resolveType(resolved->targetType);
            } else {
                if (!out->hasAddress && !typeOnly) {
                    return 0;
                }
                baseAddr = out->address;
            }
            if (!resolved || resolved->kind != print_type_struct) {
                return 0;
            }
            print_member_t *member = NULL;
            for (int i = 0; i < resolved->memberCount; ++i) {
                if (resolved->members[i].name && strcmp(resolved->members[i].name, memberName) == 0) {
                    member = &resolved->members[i];
                    break;
                }
            }
            if (!member) {
                return 0;
            }
            if (typeOnly) {
                *out = print_eval_makeAddressValue(member->type, 0);
                out->hasAddress = 0;
            } else {
                uint32_t memberAddr = baseAddr + member->offset;
                *out = print_eval_makeAddressValue(member->type, memberAddr);
            }
            continue;
        }
        if (**cursor == '[') {
            ++(*cursor);
            print_eval_skipSpace(cursor);
            uint64_t indexVal = 0;
            if (!print_eval_parseNumber(cursor, &indexVal)) {
                return 0;
            }
            print_eval_skipSpace(cursor);
            if (**cursor == ']') {
                ++(*cursor);
            }
            print_type_t *resolved = print_eval_resolveType(out->type);
            if (!resolved) {
                return 0;
            }
            print_type_t *elemType = NULL;
            uint32_t baseAddr = 0;
            if (resolved->kind == print_type_array) {
                elemType = resolved->targetType;
                if (!out->hasAddress && !typeOnly) {
                    return 0;
                }
                baseAddr = out->address;
            } else if (resolved->kind == print_type_pointer) {
                elemType = resolved->targetType;
                if (!typeOnly && !print_eval_readPointerValue(out, &baseAddr)) {
                    return 0;
                }
            } else {
                return 0;
            }
            size_t elemSize = elemType && elemType->byteSize > 0 ? elemType->byteSize : 1;
            if (typeOnly) {
                *out = print_eval_makeAddressValue(elemType, 0);
                out->hasAddress = 0;
            } else {
                uint32_t elemAddr = baseAddr + (uint32_t)(indexVal * elemSize);
                *out = print_eval_makeAddressValue(elemType, elemAddr);
            }
            continue;
        }
        break;
    }
    return 1;
}

static int
print_eval_parseExpression(const char **cursor, print_index_t *index, print_value_t *out, print_temp_type_t **tempTypes, int typeOnly)
{
    return print_eval_parsePostfix(cursor, index, out, tempTypes, typeOnly);
}

static int
print_eval_resolveTypeFromExpression(const char *expr, print_index_t *index, print_type_t **outType)
{
    if (!expr || !outType) {
        return 0;
    }
    const char *cursor = expr;
    print_temp_type_t *tempTypes = NULL;
    print_value_t value;
    int ok = print_eval_parseExpression(&cursor, index, &value, &tempTypes, 1);
    if (ok && outType) {
        *outType = print_eval_resolveType(value.type);
    }
    print_eval_freeTempTypes(tempTypes);
    return ok;
}

static int
print_eval_completeMembers(print_index_t *index, const char *baseExpr, const char *prefix, const char *sep, char ***outList, int *outCount)
{
    if (!index || !baseExpr || !sep || !outList || !outCount) {
        return 0;
    }
    print_type_t *baseType = NULL;
    if (!print_eval_resolveTypeFromExpression(baseExpr, index, &baseType)) {
        return 0;
    }
    print_type_t *resolved = baseType;
    if (resolved && resolved->kind == print_type_pointer) {
        resolved = print_eval_resolveType(resolved->targetType);
    }
    if (!resolved || resolved->kind != print_type_struct) {
        return 0;
    }
    int cap = 0;
    char **items = NULL;
    int count = 0;
    for (int i = 0; i < resolved->memberCount; ++i) {
        const char *name = resolved->members[i].name;
        if (!name) {
            continue;
        }
        if (prefix && *prefix && strncmp(name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        char fullName[512];
        snprintf(fullName, sizeof(fullName), "%s%s%s", baseExpr, sep, name);
        if (count >= cap) {
            int next = cap ? cap * 2 : 32;
            char **nextItems = (char **)alloc_realloc(items, sizeof(*nextItems) * (size_t)next);
            if (!nextItems) {
                break;
            }
            items = nextItems;
            cap = next;
        }
        items[count++] = print_eval_strdup(fullName);
    }
    if (count == 0) {
        alloc_free(items);
        return 0;
    }
    *outList = items;
    *outCount = count;
    return 1;
}

static int
print_eval_completeGlobals(print_index_t *index, const char *prefix, char ***outList, int *outCount)
{
    if (!index || !outList || !outCount) {
        return 0;
    }
    int cap = 0;
    char **items = NULL;
    int count = 0;
    for (int i = 0; i < index->varCount; ++i) {
        const char *name = index->vars[i].name;
        if (!name) {
            continue;
        }
        if (prefix && *prefix && strncmp(name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        if (count >= cap) {
            int next = cap ? cap * 2 : 64;
            char **nextItems = (char **)alloc_realloc(items, sizeof(*nextItems) * (size_t)next);
            if (!nextItems) {
                break;
            }
            items = nextItems;
            cap = next;
        }
        items[count++] = print_eval_strdup(name);
    }
    for (int i = 0; i < index->symbolCount; ++i) {
        const char *name = index->symbols[i].name;
        if (!name) {
            continue;
        }
        if (prefix && *prefix && strncmp(name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        int duplicate = 0;
        for (int j = 0; j < count; ++j) {
            if (items[j] && strcmp(items[j], name) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (count >= cap) {
            int next = cap ? cap * 2 : 64;
            char **nextItems = (char **)alloc_realloc(items, sizeof(*nextItems) * (size_t)next);
            if (!nextItems) {
                break;
            }
            items = nextItems;
            cap = next;
        }
        items[count++] = print_eval_strdup(name);
    }
    if (count == 0) {
        alloc_free(items);
        return 0;
    }
    *outList = items;
    *outCount = count;
    return 1;
}

int
print_eval_complete(const char *prefix, char ***outList, int *outCount)
{
    if (outList) {
        *outList = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    if (!outList || !outCount) {
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        return 0;
    }
    const char *dot = prefix ? strrchr(prefix, '.') : NULL;
    const char *arrow = NULL;
    if (prefix) {
        for (const char *p = prefix; p[0] && p[1]; ++p) {
            if (p[0] == '-' && p[1] == '>') {
                arrow = p;
            }
        }
    }
    const char *sep = NULL;
    int sepLen = 0;
    if (arrow && (!dot || arrow > dot)) {
        sep = arrow;
        sepLen = 2;
    } else if (dot) {
        sep = dot;
        sepLen = 1;
    }
    if (sep) {
        char baseExpr[512];
        size_t baseLen = (size_t)(sep - prefix);
        if (baseLen >= sizeof(baseExpr)) {
            baseLen = sizeof(baseExpr) - 1;
        }
        memcpy(baseExpr, prefix, baseLen);
        baseExpr[baseLen] = '\0';
        const char *memberPrefix = sep + sepLen;
        char sepBuf[3];
        if (sepLen >= (int)sizeof(sepBuf)) {
            sepLen = (int)sizeof(sepBuf) - 1;
        }
        memcpy(sepBuf, sep, (size_t)sepLen);
        sepBuf[sepLen] = '\0';
        return print_eval_completeMembers(&print_eval_index, baseExpr, memberPrefix, sepBuf, outList, outCount);
    }
    return print_eval_completeGlobals(&print_eval_index, prefix, outList, outCount);
}

void
print_eval_freeCompletions(char **list, int count)
{
    if (!list || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        alloc_free(list[i]);
    }
    alloc_free(list);
}

int
print_eval_resolveSymbol(const char *name, uint32_t *outAddr, size_t *outSize)
{
    if (!name || !*name || !outAddr || !outSize) {
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        return 0;
    }
    print_variable_t *var = print_eval_findVariable(&print_eval_index, name);
    if (!var) {
        return 0;
    }
    print_type_t *type = print_eval_getType(&print_eval_index, var->typeRef);
    print_type_t *resolved = print_eval_resolveType(type);
    size_t size = 0;
    if (resolved && resolved->byteSize > 0) {
        size = resolved->byteSize;
    }
    if (size == 0) {
        size = 4;
    }
    *outAddr = var->addr;
    *outSize = size;
    return 1;
}

int
print_eval_resolveAddress(const char *expr, uint32_t *outAddr, size_t *outSize)
{
    if (!expr || !*expr || !outAddr || !outSize) {
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        return 0;
    }
    print_temp_type_t *tempTypes = NULL;
    print_value_t value;
    const char *cursor = expr;
    if (!print_eval_parseExpression(&cursor, &print_eval_index, &value, &tempTypes, 0)) {
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    if (!value.hasAddress) {
        print_eval_freeTempTypes(tempTypes);
        return 0;
    }
    print_type_t *resolved = print_eval_resolveType(value.type);
    size_t size = 0;
    if (resolved && resolved->byteSize > 0) {
        size = resolved->byteSize;
    }
    if (size == 0) {
        size = 4;
    }
    *outAddr = value.address;
    *outSize = size;
    print_eval_freeTempTypes(tempTypes);
    return 1;
}

int
print_eval_print(const char *expr)
{
    if (!expr || !*expr) {
        debug_error("print: missing expression");
        return 0;
    }
    if (!print_eval_loadIndex(&print_eval_index)) {
        debug_error("print: failed to load symbols (check --elf)");
        return 0;
    }
    print_temp_type_t *tempTypes = NULL;
    print_value_t value;
    const char *cursor = expr;
    if (!print_eval_parseExpression(&cursor, &print_eval_index, &value, &tempTypes, 0)) {
        print_eval_freeTempTypes(tempTypes);
        debug_error("print: failed to parse '%s'", expr);
        return 0;
    }
    if (value.hasAddress) {
        print_eval_dumpValueAt(value.type, value.address, 0, expr);
    } else if (value.hasImmediate) {
        print_type_t *resolved = print_eval_resolveType(value.type);
        if (!resolved) {
            print_eval_printLine(0, "%s: %llu (0x%llX)", expr,
                                (unsigned long long)value.immediate,
                                (unsigned long long)value.immediate);
        } else if (resolved->kind == print_type_pointer) {
            print_eval_printLine(0, "%s: 0x%08llX", expr, (unsigned long long)value.immediate);
        } else if (resolved->kind == print_type_base || resolved->kind == print_type_enum) {
            print_eval_printLine(0, "%s: %llu (0x%llX)", expr,
                                (unsigned long long)value.immediate,
                                (unsigned long long)value.immediate);
        } else {
            print_eval_printLine(0, "%s: 0x%llX", expr, (unsigned long long)value.immediate);
        }
    } else {
        debug_error("print: no value");
    }
    print_eval_freeTempTypes(tempTypes);
    return 1;
}
