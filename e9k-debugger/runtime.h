/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "debugger.h"

void
runtime_executeFrame(debugger_run_mode_t mode, int restoreFrame);

void
runtime_onVblank(void *user);

void
runtime_runLoop(void);
