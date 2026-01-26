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

typedef enum smoke_test_mode {
    SMOKE_TEST_MODE_NONE = 0,
    SMOKE_TEST_MODE_RECORD = 1,
    SMOKE_TEST_MODE_COMPARE = 2
} smoke_test_mode_t;

void
smoke_test_setFolder(const char *path);

void
smoke_test_setMode(smoke_test_mode_t mode);

smoke_test_mode_t
smoke_test_getMode(void);

void
smoke_test_setOpenOnFail(int enable);

int
smoke_test_init(void);

void
smoke_test_shutdown(void);

int
smoke_test_isEnabled(void);

int
smoke_test_getRecordPath(char *out, size_t cap);

int
smoke_test_captureFrame(uint64_t frame);


