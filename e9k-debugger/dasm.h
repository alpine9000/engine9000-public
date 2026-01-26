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

void
dasm_init(void);

void
dasm_shutdown(void);

int
dasm_preloadText(void);

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


