/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdint.h>

void
ui_build(void);

void
ui_updateSourceTitle(void);

void
ui_refreshOnPause(void);

void
ui_centerSourceOnAddress(uint32_t addr);

void
ui_applySourcePaneElfMode(void);

void
ui_copyFramebufferToClipboard(void);

void
ui_refreshSpeedButton(void);
