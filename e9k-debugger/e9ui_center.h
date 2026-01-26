/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"

e9ui_component_t *
e9ui_center_make(e9ui_component_t *child);

void
e9ui_center_setSize(e9ui_component_t *center, int width_px, int height_px);


