/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef struct e9ui_split_state {
  e9ui_component_t *a;
  e9ui_component_t *b;
  e9ui_orient_t     orient;
  float               ratio;
  int                 grip;
  int                 dragging;
  int                 hover;
  SDL_Rect            rectA;
  SDL_Rect            rectB;
  SDL_Rect            rectGrip;
  SDL_Rect            rectGrab; // hit area (includes margin)
  int                 hitMargin;
  int                 prevCollapsedTop;
  int                 prevCollapsedBottom;
  float               savedRatio;
} e9ui_split_state_t;

// Lazy-created system cursors for resize and arrow
static SDL_Cursor *s_cursor_ns = NULL; // vertical sizing (splitter between stacked panes)
static SDL_Cursor *s_cursor_ew = NULL; // horizontal sizing (splitter between side-by-side panes)
static SDL_Cursor *s_cursor_arrow = NULL;

void
e9ui_split_resetCursors(void)
{
  if (s_cursor_ns) {
    SDL_FreeCursor(s_cursor_ns);
    s_cursor_ns = NULL;
  }
  if (s_cursor_ew) {
    SDL_FreeCursor(s_cursor_ew);
    s_cursor_ew = NULL;
  }
  if (s_cursor_arrow) {
    SDL_FreeCursor(s_cursor_arrow);
    s_cursor_arrow = NULL;
  }
}

static void
ensure_cursors_init(void)
{
  if (!s_cursor_ns) {
    s_cursor_ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
  }
  if (!s_cursor_ew) {
    s_cursor_ew = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
  }
  if (!s_cursor_arrow) {
    s_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  }
}

static int
e9ui_split_clampInt(int value, int min, int max)
{
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

static void
e9ui_split_updateGrabArea(e9ui_split_state_t *s, e9ui_rect_t bounds, int margin)
{
  if (!s) {
    return;
  }
  s->rectGrab = s->rectGrip;
  if (margin <= 0) {
    return;
  }
  if (s->orient == e9ui_orient_vertical) {
    int newY = s->rectGrab.y - margin;
    int newH = s->rectGrab.h + margin * 2;
    if (newY < bounds.y) {
      newH -= bounds.y - newY;
      newY = bounds.y;
    }
    if (newY + newH > bounds.y + bounds.h) {
      newH = bounds.y + bounds.h - newY;
    }
    if (newH < 0) {
      newH = 0;
    }
    s->rectGrab.y = newY;
    s->rectGrab.h = newH;
  } else {
    int newX = s->rectGrab.x - margin;
    int newW = s->rectGrab.w + margin * 2;
    if (newX < bounds.x) {
      newW -= bounds.x - newX;
      newX = bounds.x;
    }
    if (newX + newW > bounds.x + bounds.w) {
      newW = bounds.x + bounds.w - newX;
    }
    if (newW < 0) {
      newW = 0;
    }
    s->rectGrab.x = newX;
    s->rectGrab.w = newW;
  }
}

static int
e9ui_split_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
  (void)self; (void)ctx; (void)availW;
  // Flexible; let container size this by remaining space
  return 0;
}

static void
e9ui_split_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
  (void)ctx;
  self->bounds = bounds;
  e9ui_split_state_t *s = (e9ui_split_state_t*)self->state;
  int grip = e9ui_scale_px(ctx, s->grip);
  int margin = e9ui_scale_px(ctx, s->hitMargin);
  if (grip < 0) {
    grip = 0;
  }
  int total = (s->orient == e9ui_orient_vertical) ? bounds.h : bounds.w;
  int topCollapsed = s->a && s->a->collapsed;
  int bottomCollapsed = s->b && s->b->collapsed;
  if ((topCollapsed != s->prevCollapsedTop) || (bottomCollapsed != s->prevCollapsedBottom)) {
    if (topCollapsed || bottomCollapsed) {
      s->savedRatio = s->ratio;
    } else if (s->prevCollapsedTop || s->prevCollapsedBottom) {
      if (s->savedRatio > 0.0f && s->savedRatio < 1.0f) {
	s->ratio = s->savedRatio;
      }
    }
    s->prevCollapsedTop = topCollapsed;
    s->prevCollapsedBottom = bottomCollapsed;
  }
  int usedGrip = (topCollapsed || bottomCollapsed) ? 0 : grip;
  if (usedGrip > total) {
    usedGrip = total;
  }
  
  int primarySize = 0;
  int secondarySize = 0;
  if (topCollapsed || bottomCollapsed) {
    int available = total - usedGrip;
    if (available < 0) {
      available = 0;
    }
    if (topCollapsed && !bottomCollapsed) {
      int collapsed = s->a->collapsedHeight;
      primarySize = e9ui_split_clampInt(collapsed, 0, available);
      secondarySize = available - primarySize;
      if (secondarySize < 0) {
	secondarySize = 0;
	primarySize = available;
      }
    } else if (!topCollapsed && bottomCollapsed) {
      int collapsed = s->b->collapsedHeight;
      secondarySize = e9ui_split_clampInt(collapsed, 0, available);
      primarySize = available - secondarySize;
      if (primarySize < 0) {
	primarySize = 0;
	secondarySize = available;
      }
    } else if (topCollapsed && bottomCollapsed) {
      int collapsedTop = s->a->collapsedHeight;
      primarySize = e9ui_split_clampInt(collapsedTop, 0, available);
      int remaining = available - primarySize;
      if (remaining < 0) {
	remaining = 0;
      }
      int collapsedBottom = s->b->collapsedHeight;
      secondarySize = e9ui_split_clampInt(collapsedBottom, 0, remaining);
      int leftover = remaining - secondarySize;
      if (leftover > 0) {
	primarySize = e9ui_split_clampInt(primarySize + leftover, 0, available);
      }
    }
  } else {
    if (s->orient == e9ui_orient_vertical) {
      primarySize = (int)((float)total * s->ratio) - grip/2;
    } else {
      primarySize = (int)((float)total * s->ratio) - grip/2;
    }
    if (primarySize < 0) {
      primarySize = 0;
    }
    secondarySize = total - primarySize - grip;
    if (secondarySize < 0) {
      secondarySize = 0;
    }
  }
  
  if (s->orient == e9ui_orient_vertical) {
    if (primarySize + secondarySize + usedGrip > bounds.h) {
      secondarySize = bounds.h - primarySize - usedGrip;
      if (secondarySize < 0) {
	secondarySize = 0;
      }
    }
    s->rectA = (SDL_Rect){ bounds.x, bounds.y, bounds.w, primarySize };
    s->rectGrip = (SDL_Rect){ bounds.x, bounds.y + primarySize, bounds.w, usedGrip };
    int secondY = s->rectGrip.y + s->rectGrip.h;
    int secondH = bounds.h - primarySize - usedGrip;
    if (secondH < 0) {
      secondH = 0;
    }
    s->rectB = (SDL_Rect){ bounds.x, secondY, bounds.w, secondH };
  } else {
    if (primarySize + secondarySize + usedGrip > bounds.w) {
      secondarySize = bounds.w - primarySize - usedGrip;
      if (secondarySize < 0) {
	secondarySize = 0;
      }
    }
    s->rectA = (SDL_Rect){ bounds.x, bounds.y, primarySize, bounds.h };
    s->rectGrip = (SDL_Rect){ bounds.x + primarySize, bounds.y, usedGrip, bounds.h };
    int secondX = s->rectGrip.x + s->rectGrip.w;
    int secondW = bounds.w - primarySize - usedGrip;
    if (secondW < 0) {
      secondW = 0;
    }
    s->rectB = (SDL_Rect){ secondX, bounds.y, secondW, bounds.h };
  }
  e9ui_split_updateGrabArea(s, bounds, margin);
  if (s->a && s->a->layout) {
    s->a->layout(s->a, ctx, (e9ui_rect_t){ s->rectA.x, s->rectA.y, s->rectA.w, s->rectA.h });
  }
  if (s->b && s->b->layout) {
    s->b->layout(s->b, ctx, (e9ui_rect_t){ s->rectB.x, s->rectB.y, s->rectB.w, s->rectB.h });
  }
}

static void
e9ui_split_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
  e9ui_split_state_t *s = (e9ui_split_state_t*)self->state;
  if (s->a && s->a->render) {
    s->a->render(s->a, ctx);
  }
  if (s->b && s->b->render) {
    s->b->render(s->b, ctx);
  }
  if (e9ui->transition.inTransition > 0) {
    return;
  }
  // Draw grip
  Uint8 fillC = (s->hover || s->dragging) ? 60 : 40;
  Uint8 lineC = (s->hover || s->dragging) ? 140 : 90;
  if (e9ui->transition.inTransition < 0) {
    float scale = 1.0f + (float)(-e9ui->transition.inTransition) / 100.0f;
    int fill = (int)((float)fillC * scale);
    int line = (int)((float)lineC * scale);
    fillC = (Uint8)(fill > 255 ? 255 : fill);
    lineC = (Uint8)(line > 255 ? 255 : line);
  }
  SDL_SetRenderDrawColor(ctx->renderer, fillC, fillC, fillC, 255);
  SDL_RenderFillRect(ctx->renderer, &s->rectGrip);
  SDL_SetRenderDrawColor(ctx->renderer, lineC, lineC, lineC, 255);
  if (s->orient == e9ui_orient_vertical) {
    int y = s->rectGrip.y + s->rectGrip.h/2;
    SDL_RenderDrawLine(ctx->renderer, s->rectGrip.x, y, s->rectGrip.x + s->rectGrip.w, y);
  } else {
    int x = s->rectGrip.x + s->rectGrip.w/2;
    SDL_RenderDrawLine(ctx->renderer, x, s->rectGrip.y, x, s->rectGrip.y + s->rectGrip.h);
  }
}

static int
e9ui_split_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
  e9ui_split_state_t *s = (e9ui_split_state_t*)self->state;
  if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
    int mx = ev->button.x, my = ev->button.y;
    if (mx >= s->rectGrab.x && mx < s->rectGrab.x + s->rectGrab.w &&
	my >= s->rectGrab.y && my < s->rectGrab.y + s->rectGrab.h) {
      s->dragging = 1;
      s->hover = 1;
      return 1;
    }
  } else if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
    if (s->dragging) {
      s->dragging = 0;
      return 1;
    }
  } else if (ev->type == SDL_MOUSEMOTION) {
    // Update cursor shape based on hover over hit area
    ensure_cursors_init();
    int mx = ev->motion.x, my = ev->motion.y;
    int over = (mx >= s->rectGrab.x && mx < s->rectGrab.x + s->rectGrab.w &&
		my >= s->rectGrab.y && my < s->rectGrab.y + s->rectGrab.h);
    s->hover = over;
    if (over || s->dragging) {
      if (ctx) {
	ctx->cursorOverride = 1;
      }
      SDL_Cursor *cur = (s->orient == e9ui_orient_vertical) ? s_cursor_ns : s_cursor_ew;
      if (cur) {
	SDL_SetCursor(cur);
      }
    } else if (!ctx || !ctx->cursorOverride) {
      if (s_cursor_arrow) {
	SDL_SetCursor(s_cursor_arrow);
      }
    }
    if (s->dragging) {
      int mx = ev->motion.x, my = ev->motion.y;
      if (s->orient == e9ui_orient_vertical) {
	int rel = my - self->bounds.y;
	if (rel < 0) {
	  rel = 0;
	}
	if (rel > self->bounds.h) {
	  rel = self->bounds.h;
	}
	s->ratio = (float)rel / (float)(self->bounds.h > 0 ? self->bounds.h : 1);
	if (s->ratio < 0.05f) {
	  s->ratio = 0.05f;
	}
	if (s->ratio > 0.95f) {
	  s->ratio = 0.95f;
	}
      } else {
	int rel = mx - self->bounds.x;
	if (rel < 0) {
	  rel = 0;
	}
	if (rel > self->bounds.w) {
	  rel = self->bounds.w;
	}
	s->ratio = (float)rel / (float)(self->bounds.w > 0 ? self->bounds.w : 1);
	if (s->ratio < 0.05f) {
	  s->ratio = 0.05f;
	}
	if (s->ratio > 0.95f) {
	  s->ratio = 0.95f;
	}
      }
      // Re-layout children
      e9ui_split_layout(self, ctx, self->bounds);
      // Notify root of ratio change
      if (ctx && ctx->onSplitChanged) {
	ctx->onSplitChanged(ctx, self, s->ratio);
      }
      return 1;
    }
  }
  // Pass to children (hit-testing could be added here)
  return 0;
}


e9ui_component_t *
e9ui_split_makeComponent(e9ui_component_t *a,
                           e9ui_component_t *b,
                           e9ui_orient_t orient,
                           float ratio,
                           int grip_px)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    e9ui_split_state_t *st = (e9ui_split_state_t*)alloc_calloc(1, sizeof(*st));
    int grip = grip_px > 0 ? grip_px : 6;
    st->a = a;
    st->b = b;
    e9ui_child_add(comp, a, 0);
    e9ui_child_add(comp, b, 0);
    st->orient = orient;
    st->ratio = ratio;
    st->grip = grip;
    st->hitMargin = grip * 2;
    st->prevCollapsedTop = 0;
    st->prevCollapsedBottom = 0;
    st->savedRatio = ratio;
    comp->name = "e9ui_split";
    comp->state = st;
    comp->preferredHeight = e9ui_split_preferredHeight;
    comp->layout = e9ui_split_layout;
    comp->render = e9ui_split_render;
    comp->handleEvent = e9ui_split_handleEvent;
    // Persistence and traversal hooks
    extern void e9ui_split_persistSave(e9ui_component_t*, e9ui_context_t*, FILE*);
    extern void e9ui_split_persistLoad(e9ui_component_t*, e9ui_context_t*, const char*, const char*);
    comp->persistSave = e9ui_split_persistSave;
    comp->persistLoad = e9ui_split_persistLoad;
    return comp;
}

float
e9ui_split_getRatio(e9ui_component_t *split)
{
    if (!split || !split->state) {
        return 0.5f;
    }
    e9ui_split_state_t *s = (e9ui_split_state_t*)split->state;
    return s->ratio;
}

void
e9ui_split_setRatio(e9ui_component_t *split, float ratio)
{
    if (!split || !split->state) {
        return;
    }
    if (ratio < 0.05f) {
        ratio = 0.05f;
    }
    if (ratio > 0.95f) {
        ratio = 0.95f;
    }
    e9ui_split_state_t *s = (e9ui_split_state_t*)split->state;
    s->ratio = ratio;
}

void
e9ui_split_setId(e9ui_component_t *split, const char *id)
{
    if (!split) {
        return;
    }
    split->persist_id = id;
}


void
e9ui_split_persistSave(e9ui_component_t *self, e9ui_context_t *ctx, FILE *f)
{
    (void)ctx;
    if (!self || !self->state || !self->persist_id || !f) {
        return;
    }
    e9ui_split_state_t *s = (e9ui_split_state_t*)self->state;
    fprintf(f, "comp.%s.ratio=%.4f\n", self->persist_id, s->ratio);
    fprintf(f, "comp.%s.saved_ratio=%.4f\n", self->persist_id, s->savedRatio);
}

void
e9ui_split_persistLoad(e9ui_component_t *self, e9ui_context_t *ctx, const char *key, const char *value)
{
    (void)ctx;
    if (!self || !self->state || !key || !value) {
        return;
    }
    if (strcmp(key, "ratio") == 0) {
        float v = (float)atof(value);
        e9ui_split_setRatio(self, v);
    } else if (strcmp(key, "saved_ratio") == 0) {
        float v = (float)atof(value);
        e9ui_split_state_t *s = (e9ui_split_state_t*)self->state;
        s->savedRatio = v;
    }
}
