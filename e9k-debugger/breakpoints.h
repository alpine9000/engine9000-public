/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"
#include "machine.h"

typedef struct breakpoints_list_state breakpoints_list_state_t;

e9ui_component_t *
breakpoints_makeComponent(void);

void
breakpoints_markDirty(void);

void
breakpoints_registerListState(breakpoints_list_state_t *state);

void
breakpoints_unregisterListState(breakpoints_list_state_t *state);

void
breakpoints_resolveLocation(machine_breakpoint_t *bp);


