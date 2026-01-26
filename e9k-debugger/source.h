/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>

void
source_init(void);

void
source_shutdown(void);

int
source_getTotalLines(const char *filename);

int
source_getRange(const char *filename, int start_line, int end_line,
                const char ***out_lines, int *out_count, int *out_first, int *out_total);

