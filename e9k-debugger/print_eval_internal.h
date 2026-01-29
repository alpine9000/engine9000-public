/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef enum print_dwarf_tag {
    print_dwarf_tag_unknown = 0,
    print_dwarf_tag_compile_unit,
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
    print_dwarf_tag_subprogram,
    print_dwarf_tag_lexical_block,
    print_dwarf_tag_inlined_subroutine,
    print_dwarf_tag_formal_parameter,
    print_dwarf_tag_variable
} print_dwarf_tag_t;

typedef enum print_base_encoding {
    print_base_encoding_unknown = 0,
    print_base_encoding_signed,
    print_base_encoding_unsigned,
    print_base_encoding_float,
    print_base_encoding_boolean
} print_base_encoding_t;

typedef enum print_dwarf_location_kind {
    print_dwarf_location_none = 0,
    print_dwarf_location_addr,
    print_dwarf_location_const,
    print_dwarf_location_fbreg,
    print_dwarf_location_breg,
    print_dwarf_location_reg,
    print_dwarf_location_cfa
} print_dwarf_location_kind_t;

typedef struct print_dwarf_node {
    uint32_t offset;
    uint32_t parentOffset;
    uint32_t altOffset;
    uint32_t altOffset2;
    print_dwarf_tag_t tag;
    char *name;
    uint32_t abstractOrigin;
    uint32_t typeRef;
    uint64_t byteSize;
    uint64_t addr;
    uint64_t lowPc;
    uint64_t highPc;
    uint64_t constValue;
    print_dwarf_location_kind_t frameBaseKind;
    int32_t frameBaseOffset;
    uint8_t frameBaseReg;
    int64_t memberOffset;
    int64_t upperBound;
    int64_t count;
    print_base_encoding_t encoding;
    print_dwarf_location_kind_t locationKind;
    int32_t locationOffset;
    uint8_t locationReg;
    uint8_t depth;
    int hasTypeRef;
    int hasAbstractOrigin;
    int hasByteSize;
    int hasAddr;
    int hasLowPc;
    int hasHighPc;
    int highPcIsOffset;
    int hasConstValue;
    int hasFrameBase;
    int hasMemberOffset;
    int hasUpperBound;
    int hasCount;
    int hasAltOffset;
    int hasAltOffset2;
} print_dwarf_node_t;

typedef struct print_symbol {
    char *name;
    uint32_t addr;
} print_symbol_t;

typedef struct print_variable {
    char *name;
    uint32_t addr;
    uint32_t typeRef;
    size_t byteSize;
    int hasByteSize;
} print_variable_t;

typedef struct print_cfi_row {
    uint32_t loc;
    uint8_t cfaReg;
    int32_t cfaOffset;
} print_cfi_row_t;

typedef struct print_cfi_fde {
    uint32_t pcStart;
    uint32_t pcEnd;
    uint8_t defaultCfaReg;
    int32_t defaultCfaOffset;
    print_cfi_row_t *rows;
    int rowCount;
    int rowCap;
} print_cfi_fde_t;

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
    uint32_t cacheTextBaseAddr;
    uint32_t cacheDataBaseAddr;
    uint32_t cacheBssBaseAddr;
    print_dwarf_node_t *nodes;
    int nodeCount;
    int nodeCap;
    print_cfi_fde_t *fdes;
    int fdeCount;
    int fdeCap;
    print_symbol_t *symbols;
    int symbolCount;
    int symbolCap;
    uint32_t *symbolLookup;
    uint32_t symbolLookupMask;
    print_variable_t *vars;
    int varCount;
    int varCap;
    print_type_t **types;
    int typeCount;
    int typeCap;
    print_type_t *defaultU8;
    print_type_t *defaultU16;
    print_type_t *defaultU32;
    print_type_t *defaultU64;
} print_index_t;
