/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct dasm_iface {
    uint32_t flags;
    void (*init)(void);
    void (*shutdown)(void);
    int (*preloadText)(void);
    int (*getTotal)(void);
    int (*getAddrHexWidth)(void);
    int (*findIndexForAddr)(uint64_t addr, int *out_index);
    int (*getRangeByIndex)(int start_index, int end_index,
                           const char ***out_lines,
                           const uint64_t **out_addrs,
                           int *out_first_index,
                           int *out_count);
} dasm_iface_t;

enum {
    DASM_IFACE_FLAG_STREAMING = 1u << 0,
    DASM_IFACE_FLAG_FINITE_TOTAL = 1u << 1,
};

extern const dasm_iface_t dasm_geo_iface;
extern const dasm_iface_t dasm_ami_iface;

void
dasm_init(void);

void
dasm_shutdown(void);

int
dasm_preloadText(void);

uint32_t
dasm_getFlags(void);

int
dasm_getTotal(void);

int
dasm_getAddrHexWidth(void);

int
dasm_findIndexForAddr(uint64_t addr, int *out_index);

int
dasm_getRangeByIndex(int start_index, int end_index,
                     const char ***out_lines,
                     const uint64_t **out_addrs,
                     int *out_first_index,
                     int *out_count);

