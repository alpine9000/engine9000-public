/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"
#include <stddef.h>
#include "e9ui_context.h"
#include "e9ui.h"

typedef void (*seek_bar_change_cb_t)(float percent, void *user);
typedef void (*seek_bar_drag_cb_t)(int dragging, float percent, void *user);
typedef void (*seek_bar_tooltip_cb_t)(float percent, char *out, size_t cap, void *user);

e9ui_component_t *seek_bar_make(void);

void seek_bar_setMargins(e9ui_component_t *comp, int left, int right, int bottom);
void seek_bar_setHeight(e9ui_component_t *comp, int height);
void seek_bar_setHoverMargin(e9ui_component_t *comp, int margin);
int seek_bar_getHoverMargin(e9ui_component_t *comp);
void seek_bar_setCallback(e9ui_component_t *comp, seek_bar_change_cb_t cb, void *user);
void seek_bar_setDragCallback(e9ui_component_t *comp, seek_bar_drag_cb_t cb, void *user);
void seek_bar_setPercent(e9ui_component_t *comp, float percent);
void seek_bar_setVisible(e9ui_component_t *comp, int visible);
void seek_bar_setTooltipPrefix(e9ui_component_t *comp, const char *prefix);
void seek_bar_setTooltipUnit(e9ui_component_t *comp, const char *unit);
void seek_bar_setTooltipScale(e9ui_component_t *comp, float scale);
void seek_bar_setTooltipOffset(e9ui_component_t *comp, float offset);
void seek_bar_setTooltipCallback(e9ui_component_t *comp, seek_bar_tooltip_cb_t cb, void *user);

void seek_bar_layoutInParent(e9ui_component_t *comp, e9ui_context_t *ctx, e9ui_rect_t parent);


