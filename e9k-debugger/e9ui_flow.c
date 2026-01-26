// e9ui horizontal flow container
#include "e9ui.h"
#include "debugger.h"

#include <stdlib.h>
#include <string.h>

typedef struct flow_item {
  int w;
  int h;
  int x;
  int y;
} flow_item_t;

typedef struct e9ui_flow_state {
  int pad;
  int gap;
  int lastAvailW;
  int lastPrefH;
  int lastRowCount;
  int baseMargin;
  int baseMarginOverride;
  int nowrap; // when non-zero, do not wrap; single row
} e9ui_flow_state_t;

static int
flow_child_hidden(e9ui_component_t *child)
{
  return child && e9ui_getHidden(child);
}

static void
flow_measure_children(e9ui_component_t* self, e9ui_context_t *ctx)
{
  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* child = p->child;
    flow_item_t* meta = (flow_item_t*)p->meta;

    if (!child || !meta) {
      continue;
    }

    int w = 80, h = 24;
    if (child->name && strcmp(child->name, "e9ui_button") == 0) {
      e9ui_button_measure(child, ctx, &w, &h);
    } else if (child->name && strcmp(child->name, "e9ui_separator") == 0) {
      e9ui_separator_measure(child, ctx, &w, &h);
    } else if (child->preferredHeight) {
      h = child->preferredHeight(child, ctx, 100);
      w = 100; // default width
    }

    if (flow_child_hidden(child)) {
      w = 0;
      h = 0;
    }

    meta->w = w;
    meta->h = h;
  }
}

void
e9ui_flow_measure(e9ui_component_t *flow, e9ui_context_t *ctx, int *outW, int *outH)
{
  if (outW) {
    *outW = 0;
  }
  if (outH) {
    *outH = 0;
  }
  if (!flow || !ctx || !flow->state) {
    return;
  }
  e9ui_flow_state_t *st = (e9ui_flow_state_t*)flow->state;
  flow_measure_children(flow, ctx);
  int pad = e9ui_scale_px(ctx, st->pad);
  int gap = e9ui_scale_px(ctx, st->gap);
  int totalW = pad * 2;
  int maxH = 0;
  int count = 0;
  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(flow, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* child = p->child;
    flow_item_t* meta = (flow_item_t*)p->meta;
    if (!child || !meta) continue;
    if (flow_child_hidden(child)) continue;
    totalW += meta->w;
    if (meta->h > maxH) {
      maxH = meta->h;
    }
    count++;
  }
  if (count > 1) {
    totalW += gap * (count - 1);
  }
  if (outW) {
    *outW = totalW;
  }
  if (outH) {
    *outH = maxH + pad * 2;
  }
}

static int
e9ui_flow_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
  e9ui_flow_state_t *st = (e9ui_flow_state_t*)self->state;
  flow_measure_children(self, ctx);

  int pad = e9ui_scale_px(ctx, st->pad);
  int gap = e9ui_scale_px(ctx, st->gap);

  int x = pad, y = pad, rowH = 0;
  int rowCount = 0;

  if (st->nowrap) {
    e9ui_child_iterator it;
    e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
      e9ui_component_t* child = p->child;
      flow_item_t* meta = (flow_item_t*)p->meta;
      if (!child || !meta) continue;
      if (flow_child_hidden(child)) continue;

      if (meta->h > rowH) {
        rowH = meta->h;
      }
    }

    int totalH = y + rowH + pad;
    st->lastAvailW = availW;
    st->lastPrefH = totalH;
    st->lastRowCount = 1;
    return totalH;
  }

  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* child = p->child;
    flow_item_t* meta = (flow_item_t*)p->meta;
    if (!child || !meta) continue;
    if (flow_child_hidden(child)) continue;

    int w = meta->w;
    int h = meta->h;

    if (x > pad && x + w > availW - pad) {
      x = pad;
      y += rowH + gap;
      rowH = 0;
      rowCount++;
    }
    if (h > rowH) {
      rowH = h;
    }
    x += w + gap;
    if (rowCount == 0) {
      rowCount = 1;
    }
  }

  int totalH = y + rowH + pad;
  int finalRows = (rowCount > 0) ? rowCount : 1;
  if (finalRows > 1 && st->baseMargin > 0) {
    int avail2 = availW - st->baseMargin * 2;
    if (avail2 < pad * 2) {
      avail2 = availW;
    }
    int x2 = pad, y2 = pad, rowH2 = 0;
    int rowCount2 = 0;
    e9ui_child_iterator it2;
    e9ui_child_iterator* p2 = e9ui_child_iterateChildren(self, &it2);
    while (e9ui_child_interateNext(p2)) {
      e9ui_component_t* child = p2->child;
      flow_item_t* meta = (flow_item_t*)p2->meta;
      if (!child || !meta) continue;
      if (flow_child_hidden(child)) continue;
      int w = meta->w;
      int h = meta->h;
      if (x2 > pad && x2 + w > avail2 - pad) {
        x2 = pad;
        y2 += rowH2 + gap;
        rowH2 = 0;
        rowCount2++;
      }
      if (h > rowH2) {
        rowH2 = h;
      }
      x2 += w + gap;
      if (rowCount2 == 0) {
        rowCount2 = 1;
      }
    }
    finalRows = (rowCount2 > 0) ? rowCount2 : 1;
    totalH = y2 + rowH2 + pad + st->baseMargin * 2;
  }
  st->lastAvailW = availW;
  st->lastPrefH = totalH;
  st->lastRowCount = finalRows;
  return totalH;
}

static void
e9ui_flow_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
  self->bounds = bounds;

  e9ui_flow_state_t *st = (e9ui_flow_state_t*)self->state;
  flow_measure_children(self, ctx);

  int pad = e9ui_scale_px(ctx, st->pad);
  int gap = e9ui_scale_px(ctx, st->gap);

  int maxRowH = 0;
  {
    e9ui_child_iterator mit;
    e9ui_child_iterator* mp = e9ui_child_iterateChildren(self, &mit);
    while (e9ui_child_interateNext(mp)) {
      e9ui_component_t* child = mp->child;
      flow_item_t* meta = (flow_item_t*)mp->meta;
      if (!child || !meta) continue;
      if (flow_child_hidden(child)) continue;
      if (meta->h > maxRowH) {
        maxRowH = meta->h;
      }
    }
  }
  if (st->lastPrefH == 0 || st->lastAvailW != bounds.w) {
    (void)e9ui_flow_preferredHeight(self, ctx, bounds.w);
  }
  int singleRowH = maxRowH + pad * 2;
  if (!st->baseMarginOverride && st->lastRowCount <= 1) {
    if (bounds.h > singleRowH) {
      st->baseMargin = (bounds.h - singleRowH) / 2;
    } else {
      st->baseMargin = 0;
    }
  }
  int contentH = st->lastPrefH;
  int extraH = bounds.h - contentH;
  if (extraH < 0) {
    extraH = 0;
  }
  int yOffset = extraH / 2;
  if (st->lastRowCount > 1 && st->baseMargin > 0) {
    yOffset = st->baseMargin;
  }
  int leftMargin = (st->lastRowCount > 1 && st->baseMargin > 0) ? st->baseMargin : 0;
  int rightMargin = leftMargin;
  int x = bounds.x + pad + leftMargin;
  int y = bounds.y + yOffset + pad;
  int rowH = 0;
  int rightLimit = bounds.x + bounds.w - pad - rightMargin;

  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* child = p->child;
    flow_item_t* meta = (flow_item_t*)p->meta;
    if (!child || !meta) continue;
    if (flow_child_hidden(child)) continue;

    if (!st->nowrap) {
      if (x > bounds.x + pad + leftMargin && x + meta->w > rightLimit) {
        x = bounds.x + pad + leftMargin;
        y += rowH + gap;
        rowH = 0;
      }
    }

    meta->x = x;
    meta->y = y;

    if (meta->h > rowH) {
      rowH = meta->h;
    }

    if (child->layout) {
      child->layout(child, ctx, (e9ui_rect_t){ meta->x, meta->y, meta->w, meta->h });
    }

    x += meta->w + gap;
  }
}

static void
e9ui_flow_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
  if (ctx && ctx->renderer && debugger.inTransition <= 0) {
    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);
  }
  e9ui_child_iterator it;
  e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
  while (e9ui_child_interateNext(p)) {
    e9ui_component_t* c = p->child;
    if (!c) continue;
    if (flow_child_hidden(c)) continue;
    if (c->render) {
      c->render(c, ctx);
    }
  }
}

e9ui_component_t *
e9ui_flow_make(void)
{
  e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
  e9ui_flow_state_t *st = (e9ui_flow_state_t*)alloc_calloc(1, sizeof(*st));
  st->pad = 0;
  st->gap = 8;
  st->nowrap = 0;
  st->baseMarginOverride = 0;

  c->name = "e9ui_flow";
  c->state = st;
  c->preferredHeight = e9ui_flow_preferredHeight;
  c->layout = e9ui_flow_layout;
  c->render = e9ui_flow_render;
  return c;
}

void
e9ui_flow_setPadding(e9ui_component_t *flow, int pad_px)
{
  e9ui_flow_state_t *st = (e9ui_flow_state_t*)flow->state;
  st->pad = pad_px >= 0 ? pad_px : 0;
}

void
e9ui_flow_setSpacing(e9ui_component_t *flow, int gap_px)
{
  e9ui_flow_state_t *st = (e9ui_flow_state_t*)flow->state;
  st->gap = gap_px >= 0 ? gap_px : 0;
}

void
e9ui_flow_setWrap(e9ui_component_t *flow, int wrap)
{
  e9ui_flow_state_t *st = (e9ui_flow_state_t*)flow->state;
  st->nowrap = wrap ? 0 : 1;
}

void
e9ui_flow_setBaseMargin(e9ui_component_t *flow, int margin_px)
{
  if (!flow) {
    return;
  }
  e9ui_flow_state_t *st = (e9ui_flow_state_t*)flow->state;
  if (!st) {
    return;
  }
  if (margin_px < 0) {
    margin_px = 0;
  }
  st->baseMargin = margin_px;
  st->baseMarginOverride = 1;
}

void
e9ui_flow_add(e9ui_component_t *flow, e9ui_component_t *child)
{
  flow_item_t* meta = (flow_item_t*)alloc_alloc(sizeof(*meta));
  meta->w = meta->h = 0;
  meta->x = meta->y = 0;
  e9ui_child_add(flow, child, meta);
}
