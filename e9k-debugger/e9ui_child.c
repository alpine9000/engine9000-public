/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

static void
e9ui_child_destroyTree_(e9ui_component_t *self, e9ui_context_t *ctx);

static void
e9ui_child_containerDestroyAndFree_(e9ui_component_child_t *container, e9ui_context_t *ctx)
{
  if (!container) return;

  if (container->component) {
    e9ui_child_destroyTree_(container->component, ctx);
    container->component = NULL;
  }

  if (container->meta) {
    alloc_free(container->meta);
    container->meta = NULL;
  }

  alloc_free(container);
}

static void
e9ui_child_destroyAll_(e9ui_component_t *self, e9ui_context_t *ctx)
{
  if (!self) return;

  while (self->children) {
    e9ui_component_child_t *container = (e9ui_component_child_t *)self->children->data;
    list_remove(&self->children, container, 0);
    e9ui_child_containerDestroyAndFree_(container, ctx);
  }

  self->children = NULL;
}

static void
e9ui_child_destroyTree_(e9ui_component_t *self, e9ui_context_t *ctx)
{
  if (!self) return;
  e9ui_child_destroyAll_(self, ctx);

  if (self->dtor) {
    self->dtor(self, ctx);
  }

  if (self->state) {
    alloc_free(self->state);
    self->state = NULL;
  }

  alloc_free(self);
}

void
e9ui_childDestroy(e9ui_component_t *self, e9ui_context_t *ctx)
{
  e9ui_child_destroyTree_(self, ctx);
}

void    
e9ui_childRemove(e9ui_component_t *self, e9ui_component_t *child,  e9ui_context_t *ctx)
{
  if (!self || !child) return;

  list_t *ptr = self->children;
  while (ptr && ptr->data) {
    e9ui_component_child_t *container = (e9ui_component_child_t *)ptr->data;

    if (container && container->component == child) {
      list_remove(&self->children, container, 0);
      e9ui_child_containerDestroyAndFree_(container, ctx);
      return;
    }

    ptr = ptr->next;
  }
}

void
e9ui_child_destroyChildren(e9ui_component_t *self, e9ui_context_t *ctx)
{
  if (!self) return;
  e9ui_child_destroyAll_(self, ctx);
}

e9ui_child_iterator*
e9ui_child_iterateChildren(e9ui_component_t* self, e9ui_child_iterator *iter)
{
  if (!iter) return 0;
  iter->_cursor   = self ? self->children : 0;
  iter->child     = 0;
  iter->meta      = 0;
  iter->container = 0;
  return iter;
}

e9ui_child_iterator*
e9ui_child_interateNext(e9ui_child_iterator* iter)
{
  if (!iter || !iter->_cursor) {
    return 0;
  }

  list_t* current = iter->_cursor;
  e9ui_component_child_t* container = (e9ui_component_child_t*)current->data;

  iter->container = container;
  iter->child     = container ? container->component : 0;
  iter->meta      = container ? container->meta      : 0;

  iter->_cursor = current->next;
  return iter;
}

e9ui_child_reverse_iterator*
e9ui_child_iterateChildrenReverse(e9ui_component_t* self, e9ui_child_reverse_iterator *iter)
{
  if (!iter) {
    return 0;
  }
  iter->head = 0;
  iter->cursor = 0;
  iter->child = 0;
  iter->meta = 0;
  iter->container = 0;
  if (!self) {
    return 0;
  }
  iter->head = self->children;
  iter->cursor = list_last(self->children);
  return iter->cursor ? iter : 0;
}

e9ui_child_reverse_iterator*
e9ui_child_iteratePrev(e9ui_child_reverse_iterator* iter)
{
  if (!iter || !iter->cursor) {
    return 0;
  }
  e9ui_component_child_t* container = (e9ui_component_child_t*)iter->cursor->data;
  iter->container = container;
  iter->child = container ? container->component : 0;
  iter->meta = container ? container->meta : 0;
  if (iter->cursor == iter->head) {
    iter->cursor = 0;
    return iter;
  }
  list_t* prev = iter->head;
  while (prev && prev->next && prev->next != iter->cursor) {
    prev = prev->next;
  }
  iter->cursor = prev;
  return iter;
}

int
e9ui_child_enumerateREMOVETHIS(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_component_t **out, int cap)
{
  (void)ctx;
  int n = 0;
  list_t* ptr = self->children;
  while (ptr && ptr->data) {
    e9ui_component_child_t* container = ptr->data;
    if (n < cap) {
      out[n++] = container->component;;
    }
    ptr = ptr->next;
  }
  return n;
}

e9ui_component_child_t *
e9ui_child_findContainer(e9ui_component_t *self, void* meta)
{
  list_t* ptr = self->children;
  while (ptr && ptr->data) {
    e9ui_component_child_t* container = ptr->data;
    if (container->meta == meta) {
      return container;
    }
    ptr = ptr->next;
  }
  return 0;
}

e9ui_component_t *
e9ui_child_find(e9ui_component_t *self, void* meta)
{
  e9ui_component_child_t* container = e9ui_child_findContainer(self, meta);
  if (container) {
    return container->component;
  }
  return 0;
}

void
e9ui_child_add(e9ui_component_t *comp, e9ui_component_t *child, void *meta)
{
  e9ui_component_child_t* container = alloc_alloc(sizeof(e9ui_component_child_t));
  container->component = child;
  container->meta = meta;
  list_append(&comp->children, container);
}
