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
e9ui_hstack_make(void);

void
e9ui_hstack_addFixed(e9ui_component_t *stack, e9ui_component_t *child, int width_px);

void
e9ui_hstack_addFlex(e9ui_component_t *stack, e9ui_component_t *child);


