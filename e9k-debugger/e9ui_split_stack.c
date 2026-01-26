/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "debugger.h"

typedef struct e9ui_split_stack_panel {
    float ratio;
    const char *panelId;
    SDL_Rect rect;
} e9ui_split_stack_panel;

typedef struct e9ui_split_stack_state {
    int grip;
    int hitMargin;
    int dragging;
    int hoverIndex;
    int draggingIndex;
    SDL_Rect *gripRects;
    SDL_Rect *grabRects;
    int *gripSizes;
    e9ui_component_t **panels;
    e9ui_split_stack_panel **panelMeta;
    int *panelSizes;
    int panelCount;
    int panelCap;
} e9ui_split_stack_state;

static SDL_Cursor *e9ui_split_stack_cursorNs = NULL;
static SDL_Cursor *e9ui_split_stack_cursorArrow = NULL;

void
e9ui_split_stack_resetCursors(void)
{
    if (e9ui_split_stack_cursorNs) {
        SDL_FreeCursor(e9ui_split_stack_cursorNs);
        e9ui_split_stack_cursorNs = NULL;
    }
    if (e9ui_split_stack_cursorArrow) {
        SDL_FreeCursor(e9ui_split_stack_cursorArrow);
        e9ui_split_stack_cursorArrow = NULL;
    }
}

static void
e9ui_split_stack_ensureCursors(void)
{
    if (!e9ui_split_stack_cursorNs) {
        e9ui_split_stack_cursorNs = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    }
    if (!e9ui_split_stack_cursorArrow) {
        e9ui_split_stack_cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    }
}

static void
e9ui_split_stack_updateGrabArea(SDL_Rect *rectGrab, SDL_Rect rectGrip, e9ui_rect_t bounds, int margin)
{
    if (!rectGrab) {
        return;
    }
    *rectGrab = rectGrip;
    if (margin <= 0) {
        return;
    }
    int newY = rectGrab->y - margin;
    int newH = rectGrab->h + margin * 2;
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
    rectGrab->y = newY;
    rectGrab->h = newH;
}

static int
e9ui_split_stack_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0;
}

static int
e9ui_split_stack_collectPanels(e9ui_component_t *self, e9ui_split_stack_state *st)
{
    int count = 0;
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        if (p->child) {
            count++;
        }
    }
    if (count <= 0) {
        st->panelCount = 0;
        return 0;
    }
    if (count > st->panelCap) {
        st->panels = (e9ui_component_t**)alloc_realloc(st->panels, sizeof(*st->panels) * (size_t)count);
        st->panelMeta = (e9ui_split_stack_panel**)alloc_realloc(st->panelMeta, sizeof(*st->panelMeta) * (size_t)count);
        st->panelSizes = (int*)alloc_realloc(st->panelSizes, sizeof(*st->panelSizes) * (size_t)count);
        st->panelCap = count;
    }
    int idx = 0;
    p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        if (!p->child) {
            continue;
        }
        st->panels[idx] = p->child;
        st->panelMeta[idx] = (e9ui_split_stack_panel*)p->meta;
        st->panelSizes[idx] = 0;
        idx++;
    }
    st->panelCount = idx;
    return idx;
}

static void
e9ui_split_stack_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_split_stack_state *st = (e9ui_split_stack_state*)self->state;
    int panelCount = e9ui_split_stack_collectPanels(self, st);
    if (panelCount <= 0) {
        return;
    }

    int grip = e9ui_scale_px(ctx, st->grip);
    int margin = e9ui_scale_px(ctx, st->hitMargin);
    if (grip < 0) {
        grip = 0;
    }

    int gapCount = panelCount - 1;
    if (gapCount < 0) {
        gapCount = 0;
    }
    if (gapCount > st->panelCap) {
        gapCount = st->panelCap;
    }
    if (gapCount > 0) {
        st->gripRects = (SDL_Rect*)alloc_realloc(st->gripRects, sizeof(*st->gripRects) * (size_t)gapCount);
        st->grabRects = (SDL_Rect*)alloc_realloc(st->grabRects, sizeof(*st->grabRects) * (size_t)gapCount);
        st->gripSizes = (int*)alloc_realloc(st->gripSizes, sizeof(*st->gripSizes) * (size_t)gapCount);
    }

    int totalGrip = 0;
    for (int i = 0; i < gapCount; ++i) {
        int collapsedA = st->panels[i] && st->panels[i]->collapsed;
        int collapsedB = st->panels[i + 1] && st->panels[i + 1]->collapsed;
        int usedGrip = (collapsedA || collapsedB) ? 0 : grip;
        st->gripSizes[i] = usedGrip;
        totalGrip += usedGrip;
    }

    int remaining = bounds.h - totalGrip;
    if (remaining < 0) {
        remaining = 0;
    }

    int usedCollapsed = 0;
    int nonCollapsedCount = 0;
    float ratioSum = 0.0f;
    for (int i = 0; i < panelCount; ++i) {
        e9ui_component_t *panel = st->panels[i];
        e9ui_split_stack_panel *meta = st->panelMeta[i];
        if (!panel || !meta) {
            continue;
        }
        if (panel->collapsed) {
            int collapsedH = panel->collapsedHeight;
            if (collapsedH < 0) {
                collapsedH = 0;
            }
            int maxAvail = remaining - usedCollapsed;
            if (maxAvail < 0) {
                maxAvail = 0;
            }
            if (collapsedH > maxAvail) {
                collapsedH = maxAvail;
            }
            st->panelSizes[i] = collapsedH;
            usedCollapsed += collapsedH;
        } else {
            nonCollapsedCount++;
            ratioSum += meta->ratio;
        }
    }

    int availableFlex = remaining - usedCollapsed;
    if (availableFlex < 0) {
        availableFlex = 0;
    }

    if (nonCollapsedCount > 0 && ratioSum <= 0.0f) {
        ratioSum = (float)nonCollapsedCount;
        for (int i = 0; i < panelCount; ++i) {
            e9ui_component_t *panel = st->panels[i];
            e9ui_split_stack_panel *meta = st->panelMeta[i];
            if (panel && meta && !panel->collapsed) {
                meta->ratio = 1.0f;
            }
        }
    }

    int lastFlex = -1;
    for (int i = panelCount - 1; i >= 0; --i) {
        if (st->panels[i] && !st->panels[i]->collapsed) {
            lastFlex = i;
            break;
        }
    }

    int remainingFlex = availableFlex;
    float remainingRatio = ratioSum;
    for (int i = 0; i < panelCount; ++i) {
        e9ui_component_t *panel = st->panels[i];
        e9ui_split_stack_panel *meta = st->panelMeta[i];
        if (!panel || !meta) {
            continue;
        }
        if (panel->collapsed) {
            continue;
        }
        int size = 0;
        if (i == lastFlex) {
            size = remainingFlex;
        } else if (remainingRatio > 0.0f) {
            size = (int)((float)remainingFlex * (meta->ratio / remainingRatio));
        }
        if (size < 0) {
            size = 0;
        }
        if (size > remainingFlex) {
            size = remainingFlex;
        }
        st->panelSizes[i] = size;
        remainingFlex -= size;
        remainingRatio -= meta->ratio;
        if (remainingRatio < 0.0f) {
            remainingRatio = 0.0f;
        }
    }

    int y = bounds.y;
    for (int i = 0; i < panelCount; ++i) {
        e9ui_component_t *panel = st->panels[i];
        e9ui_split_stack_panel *meta = st->panelMeta[i];
        int h = st->panelSizes[i];
        if (h < 0) {
            h = 0;
        }
        SDL_Rect rect = (SDL_Rect){ bounds.x, y, bounds.w, h };
        if (meta) {
            meta->rect = rect;
        }
        if (panel && panel->layout) {
            panel->layout(panel, ctx, (e9ui_rect_t){ rect.x, rect.y, rect.w, rect.h });
        }
        y += h;
        if (i < gapCount) {
            int usedGrip = st->gripSizes[i];
            SDL_Rect gripRect = (SDL_Rect){ bounds.x, y, bounds.w, usedGrip };
            if (usedGrip > 0) {
                st->gripRects[i] = gripRect;
                e9ui_split_stack_updateGrabArea(&st->grabRects[i], gripRect, bounds, margin);
            } else {
                st->gripRects[i] = (SDL_Rect){ 0, 0, 0, 0 };
                st->grabRects[i] = (SDL_Rect){ 0, 0, 0, 0 };
            }
            y += usedGrip;
        }
    }
}

static void
e9ui_split_stack_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    e9ui_split_stack_state *st = (e9ui_split_stack_state*)self->state;
    int panelCount = st->panelCount;
    for (int i = 0; i < panelCount; ++i) {
        e9ui_component_t *panel = st->panels[i];
        if (panel && panel->render) {
            panel->render(panel, ctx);
        }
    }
    if (debugger.inTransition > 0) {
        return;
    }
    int gapCount = panelCount > 0 ? panelCount - 1 : 0;
    for (int i = 0; i < gapCount; ++i) {
        if (st->gripSizes[i] <= 0) {
            continue;
        }
        int active = (st->hoverIndex == i) || (st->draggingIndex == i);
        Uint8 fillC = active ? 60 : 40;
        Uint8 lineC = active ? 140 : 90;
        if (debugger.inTransition < 0) {
            float scale = 1.0f + (float)(-debugger.inTransition) / 100.0f;
            int fill = (int)((float)fillC * scale);
            int line = (int)((float)lineC * scale);
            fillC = (Uint8)(fill > 255 ? 255 : fill);
            lineC = (Uint8)(line > 255 ? 255 : line);
        }
        SDL_SetRenderDrawColor(ctx->renderer, fillC, fillC, fillC, 255);
        SDL_RenderFillRect(ctx->renderer, &st->gripRects[i]);
        SDL_SetRenderDrawColor(ctx->renderer, lineC, lineC, lineC, 255);
        int y = st->gripRects[i].y + st->gripRects[i].h / 2;
        SDL_RenderDrawLine(ctx->renderer, st->gripRects[i].x, y,
                           st->gripRects[i].x + st->gripRects[i].w, y);
    }
}

static int
e9ui_split_stack_handleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    e9ui_split_stack_state *st = (e9ui_split_stack_state*)self->state;
    int gapCount = st->panelCount > 0 ? st->panelCount - 1 : 0;
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        for (int i = 0; i < gapCount; ++i) {
            SDL_Rect grab = st->grabRects[i];
            if (mx >= grab.x && mx < grab.x + grab.w &&
                my >= grab.y && my < grab.y + grab.h) {
                st->dragging = 1;
                st->draggingIndex = i;
                st->hoverIndex = i;
                return 1;
            }
        }
    } else if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (st->dragging) {
            st->dragging = 0;
            st->draggingIndex = -1;
            return 1;
        }
    } else if (ev->type == SDL_MOUSEMOTION) {
        e9ui_split_stack_ensureCursors();
        int mx = ev->motion.x;
        int my = ev->motion.y;
        int overIndex = -1;
        for (int i = 0; i < gapCount; ++i) {
            SDL_Rect grab = st->grabRects[i];
            if (mx >= grab.x && mx < grab.x + grab.w &&
                my >= grab.y && my < grab.y + grab.h) {
                overIndex = i;
                break;
            }
        }
        st->hoverIndex = overIndex;
        if (overIndex >= 0 || st->dragging) {
            if (ctx) {
                ctx->cursorOverride = 1;
            }
            if (e9ui_split_stack_cursorNs) {
                SDL_SetCursor(e9ui_split_stack_cursorNs);
            }
        } else if (!ctx || !ctx->cursorOverride) {
            if (e9ui_split_stack_cursorArrow) {
                SDL_SetCursor(e9ui_split_stack_cursorArrow);
            }
        }
        if (st->dragging && st->draggingIndex >= 0 &&
            st->draggingIndex < gapCount) {
            int idx = st->draggingIndex;
            e9ui_split_stack_panel *metaA = st->panelMeta[idx];
            e9ui_split_stack_panel *metaB = st->panelMeta[idx + 1];
            if (!metaA || !metaB) {
                return 1;
            }
            SDL_Rect rectA = metaA->rect;
            SDL_Rect rectB = metaB->rect;
            int usedGrip = st->gripSizes[idx];
            int block = rectA.h + rectB.h + usedGrip;
            if (block <= 0) {
                return 1;
            }
            int rel = my - rectA.y;
            if (rel < 0) {
                rel = 0;
            }
            if (rel > block - usedGrip) {
                rel = block - usedGrip;
            }
            int sizeA = rel;
            int sizeB = block - usedGrip - sizeA;
            if (sizeB < 0) {
                sizeB = 0;
            }
            float pairSum = metaA->ratio + metaB->ratio;
            if (pairSum <= 0.0f) {
                pairSum = 1.0f;
            }
            if (sizeA + sizeB > 0) {
                metaA->ratio = pairSum * ((float)sizeA / (float)(sizeA + sizeB));
                metaB->ratio = pairSum - metaA->ratio;
            } else {
                metaA->ratio = pairSum * 0.5f;
                metaB->ratio = pairSum * 0.5f;
            }
            e9ui_split_stack_layout(self, ctx, self->bounds);
            if (ctx && ctx->onSplitChanged) {
                ctx->onSplitChanged(ctx, self, 0.0f);
            }
            return 1;
        }
    }
    return 0;
}

static void
e9ui_split_stack_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    e9ui_split_stack_state *st = (e9ui_split_stack_state*)self->state;
    if (st->gripRects) {
        alloc_free(st->gripRects);
        st->gripRects = NULL;
    }
    if (st->grabRects) {
        alloc_free(st->grabRects);
        st->grabRects = NULL;
    }
    if (st->gripSizes) {
        alloc_free(st->gripSizes);
        st->gripSizes = NULL;
    }
    if (st->panels) {
        alloc_free(st->panels);
        st->panels = NULL;
    }
    if (st->panelMeta) {
        alloc_free(st->panelMeta);
        st->panelMeta = NULL;
    }
    if (st->panelSizes) {
        alloc_free(st->panelSizes);
        st->panelSizes = NULL;
    }
    st->panelCap = 0;
    st->panelCount = 0;
}

e9ui_component_t *
e9ui_split_stack_make(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    e9ui_split_stack_state *st = (e9ui_split_stack_state*)alloc_calloc(1, sizeof(*st));
    st->grip = 6;
    st->hitMargin = st->grip * 2;
    st->dragging = 0;
    st->hoverIndex = -1;
    st->draggingIndex = -1;
    comp->name = "e9ui_split_stack";
    comp->state = st;
    comp->preferredHeight = e9ui_split_stack_preferredHeight;
    comp->layout = e9ui_split_stack_layout;
    comp->render = e9ui_split_stack_render;
    comp->handleEvent = e9ui_split_stack_handleEvent;
    comp->dtor = e9ui_split_stack_dtor;
    extern void e9ui_split_stack_persistSave(e9ui_component_t*, e9ui_context_t*, FILE*);
    extern void e9ui_split_stack_persistLoad(e9ui_component_t*, e9ui_context_t*, const char*, const char*);
    comp->persistSave = e9ui_split_stack_persistSave;
    comp->persistLoad = e9ui_split_stack_persistLoad;
    return comp;
}

void
e9ui_split_stack_addPanel(e9ui_component_t *stack,
                          e9ui_component_t *panel,
                          const char *panel_id,
                          float ratio)
{
    if (!stack || !stack->state || !panel) {
        return;
    }
    e9ui_split_stack_panel *meta = (e9ui_split_stack_panel*)alloc_calloc(1, sizeof(*meta));
    meta->ratio = ratio;
    meta->panelId = panel_id;
    e9ui_child_add(stack, panel, meta);
}

void
e9ui_split_stack_setId(e9ui_component_t *stack, const char *id)
{
    if (!stack) {
        return;
    }
    stack->persist_id = id;
}

void
e9ui_split_stack_persistSave(e9ui_component_t *self, e9ui_context_t *ctx, FILE *f)
{
    (void)ctx;
    if (!self || !self->state || !self->persist_id || !f) {
        return;
    }
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_split_stack_panel *meta = (e9ui_split_stack_panel*)p->meta;
        if (!meta || !meta->panelId || !meta->panelId[0]) {
            continue;
        }
        fprintf(f, "comp.%s.panel.%s.ratio=%.4f\n", self->persist_id, meta->panelId, meta->ratio);
    }
}

void
e9ui_split_stack_persistLoad(e9ui_component_t *self, e9ui_context_t *ctx, const char *key, const char *value)
{
    (void)ctx;
    if (!self || !self->state || !key || !value) {
        return;
    }
    const char *prefix = "panel.";
    size_t prefixLen = strlen(prefix);
    if (strncmp(key, prefix, prefixLen) != 0) {
        return;
    }
    const char *rest = key + prefixLen;
    const char *suffix = ".ratio";
    size_t suffixLen = strlen(suffix);
    size_t restLen = strlen(rest);
    if (restLen <= suffixLen) {
        return;
    }
    if (strcmp(rest + restLen - suffixLen, suffix) != 0) {
        return;
    }
    size_t idLen = restLen - suffixLen;
    if (idLen <= 0) {
        return;
    }
    char panelId[256];
    if (idLen >= sizeof(panelId)) {
        idLen = sizeof(panelId) - 1;
    }
    memcpy(panelId, rest, idLen);
    panelId[idLen] = '\0';
    float ratio = (float)atof(value);
    e9ui_child_iterator it;
    e9ui_child_iterator *p = e9ui_child_iterateChildren(self, &it);
    while (e9ui_child_interateNext(p)) {
        e9ui_split_stack_panel *meta = (e9ui_split_stack_panel*)p->meta;
        if (meta && meta->panelId && strcmp(meta->panelId, panelId) == 0) {
            meta->ratio = ratio;
            break;
        }
    }
}
