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

void
train_clearIgnoreList(void);

int
train_addIgnoreAddr(uint32_t addr24);

int
train_isIgnoredAddr(uint32_t addr24);

void
train_setLastWatchbreak(const geo_debug_watchbreak_t *wb);

void
train_setWatchIndex(uint32_t index);

int
train_isActive(void);

int
train_hasLastWatchbreak(void);

int
train_getLastWatchbreakAddr(uint32_t *out_addr24);


