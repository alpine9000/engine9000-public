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

int
addr2line_start(const char *elf_path);

void
addr2line_stop(void);

int
addr2line_resolve(uint64_t addr, char *out_file, size_t file_cap, int *out_line);

