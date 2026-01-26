/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>
#include <stddef.h>

#ifdef _WIN32
#include <sys/types.h>

ssize_t w64_getline(char **lineptr, size_t *n, FILE *stream);
int w64_getExeDir(char *out, size_t cap);
#endif
