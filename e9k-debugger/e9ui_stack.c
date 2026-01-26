/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include "e9ui.h"
#include "debugger.h"


typedef struct e9ui_stack_item {
    int isFlex;  // 0 = fixed, 1 = flex
    int fixedH;  // cached preferred height
} e9ui_stack_item_t;

typedef struct e9ui_stack_state {
  list_t* children; // (looks unused; stack uses self->children)
} e9ui_stack_state_t;

static int
e9ui_stack_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
  (void)self; (void)ctx; (void)availW;
  // Stack itself is flexible; let parent size it
  return 0;
}

static void
e9ui_stack_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
  self->bounds = bounds;

  int fixedTotal = 0;
  int flexCount  = 0;

  // Pass 1: measure fixed children and count flex
  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* child = p->child;
    e9ui_stack_item_t* meta = (e9ui_stack_item_t*)p->meta;

    if (!child || !meta) {
      continue;
    }

    if (meta->isFlex) {
      flexCount++;
      meta->fixedH = 0;
    } else {
      int h = 0;
      if (child->preferredHeight) {
        h = child->preferredHeight(child, ctx, bounds.w);
      }
      meta->fixedH = h;
      fixedTotal += h;
    }
  }

  if (fixedTotal > bounds.h) fixedTotal = bounds.h;

  int rem = bounds.h - fixedTotal;
  if (rem < 0) rem = 0;

  int eachFlex = (flexCount > 0) ? (rem / flexCount) : 0;

  int y = bounds.y;

  // Pass 2: layout
  p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* child = p->child;
    e9ui_stack_item_t* meta = (e9ui_stack_item_t*)p->meta;

    if (!child || !meta) {
      continue;
    }

    int h = meta->isFlex ? eachFlex : meta->fixedH;
    if (h < 0) h = 0;

    e9ui_rect_t r = (e9ui_rect_t){ bounds.x, y, bounds.w, h };
    if (child->layout) {
      child->layout(child, ctx, r);
    }
    y += h;
  }
}

static void
e9ui_stack_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
  if (ctx && ctx->renderer && debugger.inTransition <= 0) {
    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);
  }
  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* child = p->child;
    if (child && child->render) {
      child->render(child, ctx);
    }
  }
}


static void
e9ui_stack_addItem(e9ui_component_t *stack, e9ui_component_t *child, int isFlex)
{
  e9ui_stack_item_t* meta = (e9ui_stack_item_t*)alloc_alloc(sizeof(*meta));
  meta->isFlex = isFlex;
  meta->fixedH = 0;
  e9ui_child_add(stack, child, meta);
}

e9ui_component_t *
e9ui_stack_makeVertical(void)
{
  e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
  e9ui_stack_state_t *st = (e9ui_stack_state_t*)alloc_calloc(1, sizeof(*st));

  comp->name = "e9ui_stack";
  comp->state = st;
  comp->preferredHeight = e9ui_stack_preferredHeight;
  comp->layout = e9ui_stack_layout;
  comp->render = e9ui_stack_render;
  return comp;
}

void
e9ui_stack_remove(e9ui_component_t *stack, e9ui_context_t *ctx, e9ui_component_t *child)
{
  if (!stack || !stack->state || !child) {
    return;
  }

  e9ui_childRemove(stack, child, ctx);
}


void
e9ui_stack_removeAll(e9ui_component_t *stack, e9ui_context_t *ctx)
{
  if (!stack || !stack->state) {
    return;
  }
  e9ui_child_destroyChildren(stack, ctx);
}

void
e9ui_stack_addFixed(e9ui_component_t *stack, e9ui_component_t *child)
{
  e9ui_stack_addItem(stack, child, 0);
}

void
e9ui_stack_addFlex(e9ui_component_t *stack, e9ui_component_t *child)
{
  e9ui_stack_addItem(stack, child, 1);
}
