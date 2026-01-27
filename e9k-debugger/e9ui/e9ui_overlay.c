/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef enum e9ui_overlay_role {
  E9UI_OVERLAY_ROLE_CONTENT = 1,
  E9UI_OVERLAY_ROLE_OVERLAY = 2,
} e9ui_overlay_role_t;

typedef struct e9ui_overlay_meta {
  e9ui_overlay_role_t role;
} e9ui_overlay_meta_t;

typedef struct e9ui_overlay_state {
  e9ui_overlay_anchor_t anchor;
  int margin;
} e9ui_overlay_state_t;

static e9ui_component_t*
overlay_find_child(e9ui_component_t* self, e9ui_overlay_role_t role)
{
  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_overlay_meta_t* meta = (e9ui_overlay_meta_t*)p->meta;
    if (meta && meta->role == role) {
      return p->child;
    }
  }
  return 0;
}

static int
e9ui_overlay_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
  e9ui_component_t* content = overlay_find_child(self, E9UI_OVERLAY_ROLE_CONTENT);
  if (content && content->preferredHeight) {
    return content->preferredHeight(content, ctx, availW);
  }
  (void)ctx; (void)availW;
  return 0;
}

static void
e9ui_overlay_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
  self->bounds = bounds;

  e9ui_overlay_state_t *st = (e9ui_overlay_state_t*)self->state;

  e9ui_component_t* content = overlay_find_child(self, E9UI_OVERLAY_ROLE_CONTENT);
  e9ui_component_t* overlay = overlay_find_child(self, E9UI_OVERLAY_ROLE_OVERLAY);

  // Layout content first
  if (content && content->layout) {
    content->layout(content, ctx, bounds);
  }

  if (!overlay) {
    return;
  }

  int ow = 0, oh = 0;

  // Measure overlay if it is a button or flow; otherwise fallback to preferredHeight
  if (overlay->name && strcmp(overlay->name, "e9ui_button") == 0) {
    e9ui_button_measure(overlay, ctx, &ow, &oh);
  } else if (overlay->name && strcmp(overlay->name, "e9ui_flow") == 0) {
    e9ui_flow_measure(overlay, ctx, &ow, &oh);
  }
  if (oh <= 0 && overlay->preferredHeight) {
    oh = overlay->preferredHeight(overlay, ctx, bounds.w);
  }
  if (ow <= 0) {
    ow = oh * 3 / 2; // heuristic fallback
  }
  if (oh <= 0) {
    oh = e9ui_scale_px(ctx, 24);
  }

  int margin = e9ui_scale_px(ctx, st->margin);

  int x = bounds.x + margin;
  int y = bounds.y + margin;

  switch (st->anchor) {
    case e9ui_anchor_top_left:
      x = bounds.x + margin;
      y = bounds.y + margin;
      break;
    case e9ui_anchor_top_right:
      x = bounds.x + bounds.w - margin - ow;
      y = bounds.y + margin;
      break;
    case e9ui_anchor_bottom_left:
      x = bounds.x + margin;
      y = bounds.y + bounds.h - margin - oh;
      break;
    case e9ui_anchor_bottom_right:
      x = bounds.x + bounds.w - margin - ow;
      y = bounds.y + bounds.h - margin - oh;
      break;
    default:
      break;
  }

  e9ui_rect_t ob = (e9ui_rect_t){ x, y, ow, oh };
  if (overlay->layout) {
    overlay->layout(overlay, ctx, ob);
  }
}

static void
e9ui_overlay_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
  if (ctx && ctx->renderer && e9ui->transition.inTransition <= 0) {
    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);
  }
  // Render content first, overlay last
  e9ui_component_t* content = overlay_find_child(self, E9UI_OVERLAY_ROLE_CONTENT);
  if (content && content->render) {
    content->render(content, ctx);
  }

  e9ui_component_t* overlay = overlay_find_child(self, E9UI_OVERLAY_ROLE_OVERLAY);
  if (overlay && overlay->render) {
    overlay->render(overlay, ctx);
  }
}


static void
e9ui_overlay_addRole(e9ui_component_t* parent, e9ui_component_t* child, e9ui_overlay_role_t role)
{
  if (!parent || !child) return;

  e9ui_overlay_meta_t* meta = (e9ui_overlay_meta_t*)alloc_alloc(sizeof(*meta));
  meta->role = role;
  e9ui_child_add(parent, child, meta);
}

e9ui_component_t *
e9ui_overlay_make(e9ui_component_t *content, e9ui_component_t *overlay)
{
  e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
  e9ui_overlay_state_t *st = (e9ui_overlay_state_t*)alloc_calloc(1, sizeof(*st));

  st->anchor = e9ui_anchor_top_right;
  st->margin = 8;

  c->name = "e9ui_overlay";
  c->state = st;
  c->preferredHeight = e9ui_overlay_preferredHeight;
  c->layout = e9ui_overlay_layout;
  c->render = e9ui_overlay_render;

  if (content) {
    e9ui_overlay_addRole(c, content, E9UI_OVERLAY_ROLE_CONTENT);
  }
  if (overlay) {
    e9ui_overlay_addRole(c, overlay, E9UI_OVERLAY_ROLE_OVERLAY);
  }
  
  return c;
}

void
e9ui_overlay_setAnchor(e9ui_component_t *c, e9ui_overlay_anchor_t anchor)
{
  if (!c || !c->state) return;
  e9ui_overlay_state_t *st = (e9ui_overlay_state_t*)c->state;
  st->anchor = anchor;
}

void
e9ui_overlay_setMargin(e9ui_component_t *c, int margin_px)
{
  if (!c || !c->state) return;
  e9ui_overlay_state_t *st = (e9ui_overlay_state_t*)c->state;
  st->margin = margin_px;
}
