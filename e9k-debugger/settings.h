/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"

void
settings_uiOpen(e9ui_context_t *ctx, void *user);

void
settings_cancelModal(void);

int
settings_configIsOk(void);

void
settings_updateButton(int settingsOk);

void
settings_applyToolbarMode(void);
