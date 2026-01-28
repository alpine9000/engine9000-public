#pragma once

#include <stddef.h>
#include <stdint.h>

#define GEO_WATCHPOINT_COUNT 64

#define GEO_WATCH_OP_READ                 (1u << 0)
#define GEO_WATCH_OP_WRITE                (1u << 1)
#define GEO_WATCH_OP_VALUE_NEQ_OLD        (1u << 2)
#define GEO_WATCH_OP_VALUE_EQ             (1u << 3)
#define GEO_WATCH_OP_OLD_VALUE_EQ         (1u << 4)
#define GEO_WATCH_OP_ACCESS_SIZE          (1u << 5)
#define GEO_WATCH_OP_ADDR_COMPARE_MASK    (1u << 6)

#define GEO_WATCH_ACCESS_READ             1u
#define GEO_WATCH_ACCESS_WRITE            2u

typedef struct geo_debug_watchpoint
{
    uint32_t addr;
    uint32_t op_mask;
    uint32_t diff_operand;
    uint32_t value_operand;
    uint32_t old_value_operand;
    uint32_t size_operand;
    uint32_t addr_mask_operand;
} geo_debug_watchpoint_t;

typedef struct geo_debug_watchbreak
{
    uint32_t index;

    uint32_t watch_addr;
    uint32_t op_mask;
    uint32_t diff_operand;
    uint32_t value_operand;
    uint32_t old_value_operand;
    uint32_t size_operand;
    uint32_t addr_mask_operand;

    uint32_t access_addr;
    uint32_t access_kind;
    uint32_t access_size;
    uint32_t value;
    uint32_t old_value;
    uint32_t old_value_valid;
} geo_debug_watchbreak_t;

