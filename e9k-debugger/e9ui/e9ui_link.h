/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"
#include "e9ui_context.h"

typedef void (*e9ui_link_cb)(e9ui_context_t *ctx, void *user);

e9ui_component_t *
e9ui_link_make(const char *text, e9ui_link_cb cb, void *user);

void
e9ui_link_setText(e9ui_component_t *link, const char *text);

void
e9ui_link_setUser(e9ui_component_t *link, void *user);

