/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>

#include "geo_watchpoint.h"
#include "geo_protect.h"

void
protect_clear(void);

int
protect_addBlock(uint32_t addr24, uint32_t sizeBits);

int
protect_addSet(uint32_t addr24, uint32_t value, uint32_t sizeBits);

int
protect_remove(uint32_t addr24, uint32_t sizeBits);

int
protect_handleWatchbreak(const geo_debug_watchbreak_t *wb);

void
protect_debugList(void);


