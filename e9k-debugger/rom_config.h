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
rom_config_loadSettingsForSelectedRom(void);

void
rom_config_loadRuntimeStateOnBoot(void);

void
rom_config_saveOnExit(void);

void
rom_config_saveSettingsForRom(const char *saveDir, const char *romPath,
                              const char *elfPath, const char *sourceDir,
                              const char *toolchainPrefix);

int
rom_config_loadSettingsForRom(const char *saveDir, const char *romPath,
                              char *outElfPath, size_t elfCap,
                              char *outSourceDir, size_t sourceCap,
                              char *outToolchainPrefix, size_t toolchainCap,
                              int *outHasElf, int *outHasSource, int *outHasToolchain);
