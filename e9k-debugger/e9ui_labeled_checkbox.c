/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include "debugger.h"

typedef struct e9ui_labeled_checkbox_state {
    char *label;
    int labelWidth_px;
    int totalWidth_px;
    e9ui_component_t *checkbox;
    e9ui_labeled_checkbox_cb_t cb;
    void *user;
    e9ui_component_t *self;
} e9ui_labeled_checkbox_state_t;

static void
labeled_checkbox_notify(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)user;
    if (!st || !st->cb) {
        return;
    }
    st->cb(self, ctx, selected, st->user);
}

static int
labeled_checkbox_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st || !st->checkbox) {
        return 0;
    }
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    int gap = e9ui_scale_px(ctx, 8);
    int totalW = availW;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int checkboxW = totalW - labelW - gap;
    if (checkboxW < 0) {
        checkboxW = 0;
    }
    return st->checkbox->preferredHeight ? st->checkbox->preferredHeight(st->checkbox, ctx, checkboxW) : 0;
}

static void
labeled_checkbox_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st || !st->checkbox) {
        return;
    }
    int gap = e9ui_scale_px(ctx, 8);
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    if (labelW == 0 && st->label && *st->label) {
        TTF_Font *font = debugger.theme.text.prompt ? debugger.theme.text.prompt : ctx->font;
        if (font) {
            int textW = 0;
            TTF_SizeText(font, st->label, &textW, NULL);
            labelW = textW + gap;
        }
    }
    int totalW = bounds.w;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int checkboxW = totalW - labelW - gap;
    if (checkboxW < 0) {
        checkboxW = 0;
    }
    int checkboxH = st->checkbox->preferredHeight ? st->checkbox->preferredHeight(st->checkbox, ctx, checkboxW) : 0;
    int rowX = bounds.x + (bounds.w - totalW) / 2;
    int rowY = bounds.y + (bounds.h - checkboxH) / 2;
    e9ui_rect_t checkboxRect = { rowX + labelW + gap, rowY, checkboxW, checkboxH };
    st->checkbox->layout(st->checkbox, ctx, checkboxRect);
}

static void
labeled_checkbox_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->label && *st->label) {
        TTF_Font *font = debugger.theme.text.prompt ? debugger.theme.text.prompt : ctx->font;
        if (font) {
            SDL_Color color = (SDL_Color){220, 220, 220, 255};
            int tw = 0;
            int th = 0;
            SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->label, color, &tw, &th);
            if (tex) {
                int gap = e9ui_scale_px(ctx, 8);
                int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : tw + gap;
                int totalW = self->bounds.w;
                if (st->totalWidth_px > 0) {
                    int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
                    if (scaled < totalW) {
                        totalW = scaled;
                    }
                }
                int rowX = self->bounds.x + (self->bounds.w - totalW) / 2;
                int rowY = self->bounds.y + (self->bounds.h - th) / 2;
                int textX = rowX + labelW - tw;
                SDL_Rect dst = { textX, rowY, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }
    if (st->checkbox && st->checkbox->render) {
        st->checkbox->render(st->checkbox, ctx);
    }
}

static void
labeled_checkbox_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
}

e9ui_component_t *
e9ui_labeled_checkbox_make(const char *label, int labelWidth_px, int totalWidth_px,
                           int selected, e9ui_labeled_checkbox_cb_t cb, void *user)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)alloc_calloc(1, sizeof(*st));
    st->labelWidth_px = labelWidth_px;
    st->totalWidth_px = totalWidth_px;
    if (label && *label) {
        st->label = alloc_strdup(label);
    }
    st->checkbox = e9ui_checkbox_make("", selected, labeled_checkbox_notify, st);
    st->cb = cb;
    st->user = user;
    c->name = "e9ui_labeledCheckbox";
    c->state = st;
    c->preferredHeight = labeled_checkbox_preferredHeight;
    c->layout = labeled_checkbox_layout;
    c->render = labeled_checkbox_render;
    c->dtor = labeled_checkbox_dtor;
    st->self = c;
    if (st->checkbox) {
        e9ui_child_add(c, st->checkbox, 0);
    }
    return c;
}

void
e9ui_labeled_checkbox_setLabelWidth(e9ui_component_t *comp, int labelWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    st->labelWidth_px = labelWidth_px;
}

void
e9ui_labeled_checkbox_setTotalWidth(e9ui_component_t *comp, int totalWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    st->totalWidth_px = totalWidth_px;
}

void
e9ui_labeled_checkbox_setSelected(e9ui_component_t *comp, int selected, e9ui_context_t *ctx)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    if (!st || !st->checkbox) {
        return;
    }
    e9ui_checkbox_setSelected(st->checkbox, selected, ctx);
}

int
e9ui_labeled_checkbox_isSelected(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return 0;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    if (!st || !st->checkbox) {
        return 0;
    }
    return e9ui_checkbox_isSelected(st->checkbox);
}

e9ui_component_t *
e9ui_labeled_checkbox_getCheckbox(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_checkbox_state_t *st = (const e9ui_labeled_checkbox_state_t*)comp->state;
    return st ? st->checkbox : NULL;
}
