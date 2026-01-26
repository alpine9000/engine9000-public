/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"

e9ui_component_t *
e9ui_scroll_make(e9ui_component_t *child);

void
e9ui_scroll_setContentHeightPx(e9ui_component_t *scroll, int contentHeight_px);


