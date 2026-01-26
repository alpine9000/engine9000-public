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
print_eval_print(const char *expr);

int
print_eval_complete(const char *prefix, char ***outList, int *outCount);

void
print_eval_freeCompletions(char **list, int count);

int
print_eval_resolveSymbol(const char *name, uint32_t *outAddr, size_t *outSize);

int
print_eval_resolveAddress(const char *expr, uint32_t *outAddr, size_t *outSize);


