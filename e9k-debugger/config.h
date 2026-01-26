/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdio.h>

void
config_persistConfig(FILE *f);

void
config_saveConfig(void);

void
config_loadConfig(void);
