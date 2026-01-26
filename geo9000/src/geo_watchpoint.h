#ifndef GEO_WATCHPOINT_H
#define GEO_WATCHPOINT_H

#include <stdint.h>
#include <stddef.h>

#define GEO_WATCHPOINT_COUNT 64

// Watchpoint operation bits.
// These can be combined; operands are stored separately per watchpoint.
#define GEO_WATCH_OP_READ                 (1u << 0) // (1) Read
#define GEO_WATCH_OP_WRITE                (1u << 1) // (2) Write
#define GEO_WATCH_OP_VALUE_NEQ_OLD        (1u << 2) // (3) Value != existing value (write-only)
#define GEO_WATCH_OP_VALUE_EQ             (1u << 3) // (4) Value == operand
#define GEO_WATCH_OP_OLD_VALUE_EQ         (1u << 4) // (5) Existing value == operand
#define GEO_WATCH_OP_ACCESS_SIZE          (1u << 5) // (6) Access size (operand: 8/16/32 bits)
#define GEO_WATCH_OP_ADDR_COMPARE_MASK    (1u << 6) // (7) Address compare mask (operand: mask)

// Access kind for watchbreak reporting.
#define GEO_WATCH_ACCESS_READ             1u
#define GEO_WATCH_ACCESS_WRITE            2u

typedef struct geo_debug_watchpoint
{
    uint32_t addr;
    uint32_t op_mask;
    uint32_t diff_operand;      // (3) operand value
    uint32_t value_operand;     // (4) operand value
    uint32_t old_value_operand; // (5) operand value
    uint32_t size_operand;      // (6) operand size, 8/16/32 (bits)
    uint32_t addr_mask_operand; // (7) operand mask, 0 => always match
} geo_debug_watchpoint_t;

typedef struct geo_debug_watchbreak
{
    uint32_t index;             // 0..GEO_WATCHPOINT_COUNT-1

    // Snapshot of the triggering watchpoint.
    uint32_t watch_addr;
    uint32_t op_mask;
    uint32_t diff_operand;
    uint32_t value_operand;
    uint32_t old_value_operand;
    uint32_t size_operand;      // 8/16/32 (bits)
    uint32_t addr_mask_operand;

    // Access details.
    uint32_t access_addr;       // address used for the access (base)
    uint32_t access_kind;       // GEO_WATCH_ACCESS_*
    uint32_t access_size;       // 8/16/32 (bits)
    uint32_t value;             // value read/written (size-truncated)
    uint32_t old_value;         // existing value (if known; for reads, equals value)
    uint32_t old_value_valid;   // 1 if old_value is valid
} geo_debug_watchbreak_t;

#endif // GEO_WATCHPOINT_H
