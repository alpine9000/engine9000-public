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
clipboard_setPng(const void *png_data, size_t length);

int
clipboard_setImageXRGB8888(const uint8_t *data, int width, int height, size_t pitch);
