/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "print_eval_internal.h"

int
print_debuginfo_readelf_loadSymbols(const char *elfPath, print_index_t *index);

int
print_debuginfo_readelf_loadDwarfInfo(const char *elfPath, print_index_t *index);

