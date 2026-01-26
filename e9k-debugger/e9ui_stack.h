/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"

typedef struct e9ui_stack e9ui_stack_t;

e9ui_component_t *
e9ui_stack_makeVertical(void);

void
e9ui_stack_addFixed(e9ui_component_t *stack, e9ui_component_t *child);

void
e9ui_stack_addFlex(e9ui_component_t *stack, e9ui_component_t *child);

void
e9ui_stack_remove(e9ui_component_t *stack, e9ui_context_t *ctx, e9ui_component_t *child);

void
e9ui_stack_removeAll(e9ui_component_t *stack, e9ui_context_t *ctx);


