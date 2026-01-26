/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once
#include "alloc.h"
#include "e9ui_component.h"
#include "e9ui_context.h"
#include "e9ui_stack.h"
#include "e9ui_split.h"
#include "e9ui_split_stack.h"
#include "e9ui_console.h"
#include "e9ui_image.h"
#include "e9ui_box.h"
#include "e9ui_hstack.h"
#include "e9ui_button.h"
#include "e9ui_flow.h"
#include "e9ui_spacer.h"
#include "e9ui_vspacer.h"
#include "e9ui_textbox.h"
#include "e9ui_separator.h"
#include "e9ui_overlay.h"
#include "e9ui_center.h"
#include "e9ui_fileselect.h"
#include "e9ui_labeled_textbox.h"
#include "e9ui_labeled_checkbox.h"
#include "e9ui_modal.h"
#include "e9ui_event.h"
#include "e9ui_text_cache.h"
#include "e9ui_scale.h"
#include "e9ui_link.h"
#include "e9ui_checkbox.h"
#include "e9ui_theme_defaults.h"
#include "e9ui_theme_presets.h"
#include "e9ui_theme.h"

typedef struct {
  list_t* _cursor;                       
  e9ui_component_t* child;               
  void* meta;                            
  e9ui_component_child_t* container;     
} e9ui_child_iterator;

typedef struct {
  list_t* head;
  list_t* cursor;
  e9ui_component_t* child;
  void* meta;
  e9ui_component_child_t* container;
} e9ui_child_reverse_iterator;

void
e9ui_loadLayoutComponents(void);

void
e9ui_saveLayout(void);

void
e9ui_loadLayoutWindow(void);

void
e9ui_toggleSrcMode(e9ui_context_t *ctx, void *user);

e9ui_component_t *
e9ui_findById(e9ui_component_t *root, const char *id);

void
e9ui_debugDrawBounds(e9ui_component_t *c, e9ui_context_t *ctx, int depth);

void
e9ui_renderFrame(void);

void
e9ui_renderFrameNoLayout(void);

void
e9ui_renderFrameNoLayoutNoPresent(void);

void
e9ui_renderFrameNoLayoutNoPresentFade(int fadeAlpha);

void
e9ui_renderFrameNoLayoutNoPresentNoClear(void);

int
e9ui_processEvents(void);

void
e9ui_setFullscreenComponent(e9ui_component_t *comp);

void
e9ui_clearFullscreenComponent(void);

void
e9ui_showTransientMessage(const char *message);

e9ui_component_t *
e9ui_getFullscreenComponent(void);

int
e9ui_isFullscreenComponent(const e9ui_component_t *comp);

int
e9ui_ctor(void);

void
e9ui_shutdown(void);

void
e9ui_setDisableVariable(e9ui_component_t *comp, const int *stateFlag, int disableWhenTrue);

void
e9ui_setHiddenVariable(e9ui_component_t *comp, const int *var, int hideWhenTrue);

void
e9ui_setHidden(e9ui_component_t *comp, int visible);

void
e9ui_setAutoHide(e9ui_component_t *comp, int enable, int margin_px);

void
e9ui_setAutoHideClip(e9ui_component_t *comp, const e9ui_rect_t *rect);

void
e9ui_setFocusTarget(e9ui_component_t *comp, e9ui_component_t *target);

void
e9ui_child_add(e9ui_component_t *comp, e9ui_component_t *child, void *meta);

void
e9ui_setTooltip(e9ui_component_t *comp, const char *tooltip);

e9ui_child_iterator*
e9ui_child_interateNext(e9ui_child_iterator* iter);

e9ui_child_iterator*
e9ui_child_iterateChildren(e9ui_component_t* self, e9ui_child_iterator *iter);

e9ui_child_reverse_iterator*
e9ui_child_iterateChildrenReverse(e9ui_component_t* self, e9ui_child_reverse_iterator *iter);

e9ui_child_reverse_iterator*
e9ui_child_iteratePrev(e9ui_child_reverse_iterator* iter);

void
e9ui_childRemove(e9ui_component_t *self, e9ui_component_t *child,  e9ui_context_t *ctx);

void
e9ui_child_destroyChildren(e9ui_component_t *self, e9ui_context_t *ctx);

void
e9ui_childDestroy(e9ui_component_t *self, e9ui_context_t *ctx);

int    
e9ui_child_enumerateREMOVETHIS(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_component_t **out, int cap);

e9ui_component_child_t *
e9ui_child_findContainer(e9ui_component_t *self, void* meta);

e9ui_component_t *
e9ui_child_find(e9ui_component_t *self, void* meta);

#define e9ui_getHidden(comp) ( comp && (comp->_hidden+0))
