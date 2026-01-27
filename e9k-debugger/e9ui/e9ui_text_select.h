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

void
e9ui_text_select_beginFrame(e9ui_context_t *ctx);

void
e9ui_text_select_endFrame(e9ui_context_t *ctx);

int
e9ui_text_select_handleEvent(e9ui_context_t *ctx, const e9ui_event_t *ev);

int
e9ui_text_select_hasSelection(void);

int
e9ui_text_select_isSelecting(void);

void
e9ui_text_select_copyToClipboard(void);

void
e9ui_text_select_clear(void);

void
e9ui_text_select_shutdown(void);

void
e9ui_text_select_drawText(e9ui_context_t *ctx,
                          e9ui_component_t *owner,
                          TTF_Font *font,
                          const char *text,
                          SDL_Color color,
                          int x,
                          int y,
                          int lineHeight,
                          int hitW,
                          void *bucket,
                          int dragOnly,
                          int selectable);
