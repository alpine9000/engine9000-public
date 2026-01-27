/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>

int
file_getExeDir(char *out, size_t cap);

int
file_getAssetPath(const char *rel, char *out, size_t cap);

int
file_findInPath(const char *prog, char *out, size_t cap);


