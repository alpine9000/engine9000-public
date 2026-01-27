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
e9ui_text_make(const char *text);

void
e9ui_text_setText(e9ui_component_t *comp, const char *text);

void
e9ui_text_setFontSize(e9ui_component_t *comp, int fontSize_px);

void
e9ui_text_setBold(e9ui_component_t *comp, int bold);

void
e9ui_text_setColor(e9ui_component_t *comp, SDL_Color color);


