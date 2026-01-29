/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "print_eval_internal.h"

// Load additional global/static symbols from STABS (.stab/.stabstr) via objdump -G.
// This is intended as a fallback when DWARF dumping is unavailable.
int
print_debuginfo_objdump_stabs_loadSymbols(const char *elfPath, print_index_t *index);

