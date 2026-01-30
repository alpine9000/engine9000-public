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
neogeo_coreOptionsDirty(void);

void
neogeo_coreOptionsClear(void);

const char *
neogeo_coreOptionsGetValue(const char *key);

void
neogeo_coreOptionsSetValue(const char *key, const char *value);

int
neogeo_coreOptionsBuildPath(char *out, size_t cap,
                            const char *saveDir,
                            const char *romPath);

int
neogeo_coreOptionsLoadFromFile(const char *saveDir, const char *romPath);

int
neogeo_coreOptionsWriteToFile(const char *saveDir, const char *romPath);

int
neogeo_coreOptionsApplyFileToHost(const char *saveDir, const char *romPath);

