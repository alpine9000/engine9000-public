/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>

int
elfutil_getTextBounds(const char *elf_path, uint64_t *out_lo, uint64_t *out_hi);

