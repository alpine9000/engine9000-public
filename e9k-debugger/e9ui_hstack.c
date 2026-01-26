// e9ui horizontal stack container
#include "e9ui.h"
#include "debugger.h"
#include <stdlib.h>

typedef struct e9ui_hstack_item {
    int                 isFlex; // 0 = fixed width, 1 = flex
    int                 fixedW; // pixels
    int                 calcW;  // cached width during layout
} e9ui_hstack_item_t;


static int
e9ui_hstack_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
  int fixedTotal = 0; int flexCount = 0;  
  e9ui_child_iterator iter;
  e9ui_child_iterator* ptr =  e9ui_child_iterateChildren(self, &iter);
  while (e9ui_child_interateNext(ptr)) {
    e9ui_hstack_item_t* meta = ptr->meta;
    if (!ptr->child) {
      continue;
    }
    if (meta->isFlex) {
      flexCount++;
    } else {
      fixedTotal += meta->fixedW;
    }
  }
  
  int rem = availW - fixedTotal; if (rem < 0) rem = 0;
  int eachFlex = (flexCount > 0) ? (rem / flexCount) : 0;
  int maxH = 0;
  ptr =  e9ui_child_iterateChildren(self, &iter);
  while (e9ui_child_interateNext(ptr)) {
    e9ui_hstack_item_t* meta = ptr->meta;  
    e9ui_component_t *c = ptr->child;
    if (!c) continue;
    int w = meta->isFlex ? eachFlex : meta->fixedW;
    if (w < 0) w = 0;
    if (c->preferredHeight) {
      int h = c->preferredHeight(c, ctx, w);
      if (h > maxH) maxH = h;
    }
  }

  return maxH;
}

static void
e9ui_hstack_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
  self->bounds = bounds;
  int fixedTotal = 0; int flexCount = 0;
  e9ui_child_iterator iter;
  e9ui_child_iterator* ptr =  e9ui_child_iterateChildren(self, &iter);
  while (e9ui_child_interateNext(ptr)) {
    e9ui_hstack_item_t* meta = ptr->meta;      
    if (!ptr->child) {
      continue;
    }
    if (meta->isFlex) {
      flexCount++;
    } else {
      fixedTotal += meta->fixedW;
    }
  }

  if (fixedTotal > bounds.w) fixedTotal = bounds.w;
  int rem = bounds.w - fixedTotal; if (rem < 0) rem = 0;
  int eachFlex = (flexCount > 0) ? (rem / flexCount) : 0;
  int x = bounds.x;
  ptr =  e9ui_child_iterateChildren(self, &iter);
  while (e9ui_child_interateNext(ptr)) {
    e9ui_hstack_item_t* meta = ptr->meta;        
    e9ui_component_t *c = ptr->child;
    if (!c) {
      continue;
    }
    int w = meta->isFlex ? eachFlex : meta->fixedW;
    meta->calcW = w;
    e9ui_rect_t r = (e9ui_rect_t){ x, bounds.y, w, bounds.h };
    if (c->layout) {
      c->layout(c, ctx, r);
    }
    x += w;
  }
}

static void
e9ui_hstack_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
  if (ctx && ctx->renderer && debugger.inTransition <= 0) {
    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);
  }
  e9ui_child_iterator iter;
  e9ui_child_iterator* ptr =  e9ui_child_iterateChildren(self, &iter);
  while (e9ui_child_interateNext(ptr)) {
    e9ui_component_t *c = ptr->child;
    if (c && c->render) {
      c->render(c, ctx);
    }
  }
}

static void
e9ui_hstack_addItem(e9ui_component_t *stack, e9ui_component_t *child, int isFlex, int w)
{
  e9ui_hstack_item_t* meta = alloc_alloc(sizeof(*meta));    
  meta->isFlex = isFlex;
  meta->fixedW = w;
  meta->calcW = 0;
  e9ui_child_add(stack, child, meta);
}

e9ui_component_t *
e9ui_hstack_make(void)
{
  e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
  comp->name = "e9ui_hstack";
  comp->state = 0;
  comp->preferredHeight = e9ui_hstack_preferredHeight;
  comp->layout = e9ui_hstack_layout;
  comp->render = e9ui_hstack_render;
  return comp;
}

void
e9ui_hstack_addFixed(e9ui_component_t *stack, e9ui_component_t *child, int width_px)
{
  if (width_px < 0) {
    width_px = 0;
  }
  e9ui_hstack_addItem(stack, child, 0, width_px);
}

void
e9ui_hstack_addFlex(e9ui_component_t *stack, e9ui_component_t *child)
{
  e9ui_hstack_addItem(stack, child, 1, 0);
}
