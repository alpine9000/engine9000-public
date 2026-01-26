/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"

#define PROFILE_LIST_MAX_ENTRIES 512
#define PROFILE_LIST_PADDING_X 8
#define PROFILE_LIST_PADDING_Y 4


e9ui_component_t *
profile_list_makeComponent(void);

void
profile_list_notifyUpdate(void);

void
profile_list_freeChildMeta(e9ui_component_t *self);


