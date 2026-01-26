/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"
typedef struct trainer_list_state trainer_list_state_t;

e9ui_component_t *
trainer_makeComponent(void);

void
trainer_markDirty(void);

void
trainer_registerListState(trainer_list_state_t *state);

void
trainer_unregisterListState(trainer_list_state_t *state);
