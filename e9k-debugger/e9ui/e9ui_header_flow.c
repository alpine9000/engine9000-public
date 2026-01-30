/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef struct header_flow_item {
    int w;
    int h;
    int x;
    int y;
} header_flow_item_t;

typedef struct e9ui_header_flow_state {
    int pad;
    int gap;
    int nowrap;

    int headerHeight_px;
    int leftWidth_px;
    int rightWidth_px;

    e9ui_component_t *left;
    e9ui_component_t *right;

    int lastAvailW;
    int lastPrefH;
    int lastRow1Count;
} e9ui_header_flow_state_t;

static int
header_flow_childHidden(e9ui_component_t *child)
{
    return child && e9ui_getHidden(child);
}

static void
header_flow_measureChildren(e9ui_component_t* self, e9ui_context_t *ctx)
{
    e9ui_child_iterator it;
    e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t* child = p->child;
        header_flow_item_t* meta = (header_flow_item_t*)p->meta;

        if (!child || !meta) {
            continue;
        }

        int w = 80;
        int h = 24;
        if (child->name && strcmp(child->name, "e9ui_button") == 0) {
            e9ui_button_measure(child, ctx, &w, &h);
        } else if (child->name && strcmp(child->name, "e9ui_separator") == 0) {
            e9ui_separator_measure(child, ctx, &w, &h);
        } else if (child->preferredHeight) {
            h = child->preferredHeight(child, ctx, 100);
            w = 100;
        }

        if (header_flow_childHidden(child)) {
            w = 0;
            h = 0;
        }

        meta->w = w;
        meta->h = h;
    }
}

static int
header_flow_computeRow1(e9ui_component_t *self,
                        e9ui_context_t *ctx,
                        int availW,
                        int leftW,
                        int rightW,
                        int pad,
                        int gap,
                        int *outRow1Count,
                        int *outRow1MaxH)
{
    if (outRow1Count) {
        *outRow1Count = 0;
    }
    if (outRow1MaxH) {
        *outRow1MaxH = 0;
    }
    if (!self || !ctx) {
        return 0;
    }

    int leftGap = leftW > 0 ? gap : 0;
    int rightGap = rightW > 0 ? gap : 0;
    int usableW = availW - leftW - rightW - pad * 2 - leftGap - rightGap;
    if (usableW < 0) {
        usableW = 0;
    }

    int x = 0;
    int count = 0;
    int maxH = 0;

    e9ui_child_iterator it;
    e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t* child = p->child;
        header_flow_item_t* meta = (header_flow_item_t*)p->meta;
        if (!child || !meta) {
            continue;
        }
        if (header_flow_childHidden(child)) {
            continue;
        }

        int w = meta->w;
        int h = meta->h;

        if (count == 0) {
            if (w > usableW && usableW > 0) {
                break;
            }
            if (w > usableW && usableW == 0) {
                break;
            }
            x = w;
        } else {
            if (x + gap + w > usableW) {
                break;
            }
            x += gap + w;
        }

        if (h > maxH) {
            maxH = h;
        }
        count++;
    }

    if (outRow1Count) {
        *outRow1Count = count;
    }
    if (outRow1MaxH) {
        *outRow1MaxH = maxH;
    }
    return count;
}

static int
e9ui_header_flow_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !ctx || !self->state) {
        return 0;
    }

    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)self->state;
    header_flow_measureChildren(self, ctx);

    int pad = e9ui_scale_px(ctx, st->pad);
    int gap = e9ui_scale_px(ctx, st->gap);
    int headerH = e9ui_scale_px(ctx, st->headerHeight_px);
    int leftW = st->left ? e9ui_scale_px(ctx, st->leftWidth_px) : 0;
    int rightW = st->right ? e9ui_scale_px(ctx, st->rightWidth_px) : 0;

    int row1Count = 0;
    (void)header_flow_computeRow1(self, ctx, availW, leftW, rightW, pad, gap, &row1Count, NULL);
    st->lastRow1Count = row1Count;

    if (st->nowrap) {
        st->lastAvailW = availW;
        st->lastPrefH = headerH + pad * 2;
        return headerH + pad * 2;
    }

    int overflowUsableW = availW - pad * 2;
    if (overflowUsableW < 0) {
        overflowUsableW = 0;
    }

    int totalOverflowH = 0;
    int rowH = 0;
    int x = 0;
    int rowCount = 0;
    int idx = 0;

    e9ui_child_iterator it;
    e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t* child = p->child;
        header_flow_item_t* meta = (header_flow_item_t*)p->meta;
        if (!child || !meta) {
            continue;
        }
        if (header_flow_childHidden(child)) {
            continue;
        }

        if (idx < row1Count) {
            idx++;
            continue;
        }

        int w = meta->w;
        int h = meta->h;

        if (x > 0 && x + gap + w > overflowUsableW) {
            totalOverflowH += rowH + gap;
            rowH = 0;
            x = 0;
            rowCount++;
        }

        if (h > rowH) {
            rowH = h;
        }

        if (x == 0) {
            x = w;
        } else {
            x += gap + w;
        }
        if (rowCount == 0) {
            rowCount = 1;
        }
    }

    if (rowCount > 0) {
        totalOverflowH += rowH;
    }

    int totalH = headerH + pad * 2;
    if (totalOverflowH > 0) {
        totalH += gap + totalOverflowH;
    }

    st->lastAvailW = availW;
    st->lastPrefH = totalH;
    return totalH;
}

static void
e9ui_header_flow_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx || !self->state) {
        return;
    }

    self->bounds = bounds;

    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)self->state;
    header_flow_measureChildren(self, ctx);

    int pad = e9ui_scale_px(ctx, st->pad);
    int gap = e9ui_scale_px(ctx, st->gap);
    int headerH = e9ui_scale_px(ctx, st->headerHeight_px);
    int leftW = st->left ? e9ui_scale_px(ctx, st->leftWidth_px) : 0;
    int rightW = st->right ? e9ui_scale_px(ctx, st->rightWidth_px) : 0;
    int leftGap = leftW > 0 ? gap : 0;
    int innerLeft = bounds.x + pad;
    int innerRight = bounds.x + bounds.w - pad;

    if (st->left && st->left->layout) {
        st->left->layout(st->left, ctx, (e9ui_rect_t){ innerLeft, bounds.y + pad, leftW, headerH });
    }
    if (st->right && st->right->layout) {
        st->right->layout(st->right, ctx, (e9ui_rect_t){ innerRight - rightW, bounds.y + pad, rightW, headerH });
    }

    int row1Count = 0;
    int row1MaxH = 0;
    header_flow_computeRow1(self, ctx, bounds.w, leftW, rightW, pad, gap, &row1Count, &row1MaxH);
    st->lastRow1Count = row1Count;

    int yRow1 = bounds.y + pad;
    if (row1MaxH > 0 && headerH > row1MaxH) {
        yRow1 = bounds.y + pad + (headerH - row1MaxH) / 2;
    }

    int x1 = innerLeft + leftW + leftGap;

    int idx = 0;
    e9ui_child_iterator it;
    e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t* child = p->child;
        header_flow_item_t* meta = (header_flow_item_t*)p->meta;
        if (!child || !meta) {
            continue;
        }
        if (header_flow_childHidden(child)) {
            continue;
        }
        if (idx >= row1Count) {
            break;
        }
        meta->x = x1;
        meta->y = yRow1;
        if (child->layout) {
            child->layout(child, ctx, (e9ui_rect_t){ meta->x, meta->y, meta->w, meta->h });
        }
        x1 += meta->w + gap;
        idx++;
    }

    if (st->nowrap) {
        return;
    }

    int x2Start = innerLeft;
    int rightLimit2 = innerRight;
    int x2 = x2Start;
    int y2 = bounds.y + pad + headerH;
    int rowH = 0;
    int placedAny = 0;
    if (idx < row1Count) {
        idx = row1Count;
    }

    if (idx < row1Count) {
        idx = row1Count;
    }

    if (st->lastPrefH == 0 || st->lastAvailW != bounds.w) {
        (void)e9ui_header_flow_preferredHeight(self, ctx, bounds.w);
    }

    int total = 0;
    int current = 0;
    e9ui_child_iterator itCount;
    e9ui_child_iterator* pc = e9ui_child_iterateChildren(self, &itCount);
    while (e9ui_child_interateNext(pc)) {
        if (pc->child && !header_flow_childHidden(pc->child)) {
            total++;
        }
    }

    current = 0;
    e9ui_child_iterator it2;
    e9ui_child_iterator* p2 = e9ui_child_iterateChildren(self, &it2);
    while (e9ui_child_interateNext(p2)) {
        e9ui_component_t* child = p2->child;
        header_flow_item_t* meta = (header_flow_item_t*)p2->meta;
        if (!child || !meta) {
            continue;
        }
        if (header_flow_childHidden(child)) {
            continue;
        }

        if (current < row1Count) {
            current++;
            continue;
        }

        if (!placedAny) {
            y2 += gap;
            placedAny = 1;
        }

        if (x2 > x2Start && x2 + meta->w > rightLimit2) {
            x2 = x2Start;
            y2 += rowH + gap;
            rowH = 0;
        }

        meta->x = x2;
        meta->y = y2;
        if (meta->h > rowH) {
            rowH = meta->h;
        }

        if (child->layout) {
            child->layout(child, ctx, (e9ui_rect_t){ meta->x, meta->y, meta->w, meta->h });
        }

        x2 += meta->w + gap;
        current++;
    }

    (void)total;
}

static void
e9ui_header_flow_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (ctx && ctx->renderer && e9ui->transition.inTransition <= 0) {
        SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(ctx->renderer, &bg);
    }

    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)self->state;
    if (st && st->left && st->left->render) {
        st->left->render(st->left, ctx);
    }
    if (st && st->right && st->right->render) {
        st->right->render(st->right, ctx);
    }

    e9ui_child_iterator it;
    e9ui_child_iterator* p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_component_t* c = p->child;
        if (!c) {
            continue;
        }
        if (header_flow_childHidden(c)) {
            continue;
        }
        if (c->render) {
            c->render(c, ctx);
        }
    }
}

static void
e9ui_header_flow_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state) {
        return;
    }
    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)self->state;
    if (st->left) {
        e9ui_childDestroy(st->left, ctx);
        st->left = NULL;
    }
    if (st->right) {
        e9ui_childDestroy(st->right, ctx);
        st->right = NULL;
    }
}

e9ui_component_t *
e9ui_header_flow_make(e9ui_component_t *left,
                      int leftWidth_px,
                      e9ui_component_t *right,
                      int rightWidth_px,
                      int headerHeight_px)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)alloc_calloc(1, sizeof(*st));
    st->pad = 0;
    st->gap = 8;
    st->nowrap = 0;
    st->headerHeight_px = headerHeight_px > 0 ? headerHeight_px : 48;
    st->leftWidth_px = leftWidth_px > 0 ? leftWidth_px : 0;
    st->rightWidth_px = rightWidth_px > 0 ? rightWidth_px : 0;
    st->left = left;
    st->right = right;

    c->name = "e9ui_header_flow";
    c->state = st;
    c->preferredHeight = e9ui_header_flow_preferredHeight;
    c->layout = e9ui_header_flow_layout;
    c->render = e9ui_header_flow_render;
    c->dtor = e9ui_header_flow_dtor;
    return c;
}

void
e9ui_header_flow_setPadding(e9ui_component_t *flow, int pad_px)
{
    if (!flow || !flow->state) {
        return;
    }
    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)flow->state;
    st->pad = pad_px >= 0 ? pad_px : 0;
}

void
e9ui_header_flow_setSpacing(e9ui_component_t *flow, int gap_px)
{
    if (!flow || !flow->state) {
        return;
    }
    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)flow->state;
    st->gap = gap_px >= 0 ? gap_px : 0;
}

void
e9ui_header_flow_setWrap(e9ui_component_t *flow, int wrap)
{
    if (!flow || !flow->state) {
        return;
    }
    e9ui_header_flow_state_t *st = (e9ui_header_flow_state_t*)flow->state;
    st->nowrap = wrap ? 0 : 1;
}

void
e9ui_header_flow_add(e9ui_component_t *flow, e9ui_component_t *child)
{
    if (!flow || !child) {
        return;
    }
    header_flow_item_t* meta = (header_flow_item_t*)alloc_alloc(sizeof(*meta));
    meta->w = 0;
    meta->h = 0;
    meta->x = 0;
    meta->y = 0;
    e9ui_child_add(flow, child, meta);
}
