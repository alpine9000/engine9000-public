/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef struct e9ui_labeled_textbox_state {
    char *label;
    int labelWidth_px;
    int totalWidth_px;
    e9ui_component_t *textbox;
    e9ui_labeled_textbox_change_cb_t onChange;
    void *onChangeUser;
    e9ui_component_t *self;
} e9ui_labeled_textbox_state_t;

static void
e9ui_labeled_textbox_notifyChange(e9ui_context_t *ctx, e9ui_labeled_textbox_state_t *st)
{
    if (!st || !st->onChange) {
        return;
    }
    const char *text = st->textbox ? e9ui_textbox_getText(st->textbox) : NULL;
    st->onChange(ctx, st->self, text ? text : "", st->onChangeUser);
}

static void
e9ui_labeled_textbox_textChanged(e9ui_context_t *ctx, void *user)
{
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)user;
    if (!st) {
        return;
    }
    e9ui_labeled_textbox_notifyChange(ctx, st);
}

static int
e9ui_labeled_textbox_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)self->state;
    if (!st) {
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
    int textboxW = totalW - labelW - gap;
    if (textboxW < 0) {
        textboxW = 0;
    }
    int textboxH = 0;
    if (st->textbox && st->textbox->preferredHeight) {
        textboxH = st->textbox->preferredHeight(st->textbox, ctx, textboxW);
    }
    return textboxH;
}

static void
e9ui_labeled_textbox_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)self->state;
    if (!st || !st->textbox) {
        return;
    }
    int gap = e9ui_scale_px(ctx, 8);
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    if (labelW == 0 && st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
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
    int textboxW = totalW - labelW - gap;
    if (textboxW < 0) {
        textboxW = 0;
    }
    int textboxH = st->textbox->preferredHeight ? st->textbox->preferredHeight(st->textbox, ctx, textboxW) : 0;
    int rowH = textboxH;
    if (rowH < 0) {
        rowH = 0;
    }
    int rowX = bounds.x + (bounds.w - totalW) / 2;
    int rowY = bounds.y + (bounds.h - rowH) / 2;
    e9ui_rect_t textboxRect = { rowX + labelW + gap, rowY, textboxW, rowH };
    st->textbox->layout(st->textbox, ctx, textboxRect);
}

static void
e9ui_labeled_textbox_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
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
    if (st->textbox && st->textbox->render) {
        st->textbox->render(st->textbox, ctx);
    }
}

static void
e9ui_labeled_textbox_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
}

e9ui_component_t *
e9ui_labeled_textbox_make(const char *label, int labelWidth_px, int totalWidth_px,
                          e9ui_labeled_textbox_change_cb_t cb, void *user)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)alloc_calloc(1, sizeof(*st));
    st->labelWidth_px = labelWidth_px;
    st->totalWidth_px = totalWidth_px;
    if (label && *label) {
        st->label = alloc_strdup(label);
    }
    st->textbox = e9ui_textbox_make(512, NULL, e9ui_labeled_textbox_textChanged, st);
    st->onChange = cb;
    st->onChangeUser = user;
    c->name = "e9ui_labeledTextbox";
    c->state = st;
    c->preferredHeight = e9ui_labeled_textbox_preferredHeight;
    c->layout = e9ui_labeled_textbox_layout;
    c->render = e9ui_labeled_textbox_render;
    c->dtor = e9ui_labeled_textbox_dtor;
    st->self = c;
    if (st->textbox) {
        e9ui_child_add(c, st->textbox, 0);
    }
    return c;
}

void
e9ui_labeled_textbox_setLabelWidth(e9ui_component_t *comp, int labelWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)comp->state;
    st->labelWidth_px = labelWidth_px;
}

void
e9ui_labeled_textbox_setTotalWidth(e9ui_component_t *comp, int totalWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)comp->state;
    st->totalWidth_px = totalWidth_px;
}

void
e9ui_labeled_textbox_setText(e9ui_component_t *comp, const char *text)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)comp->state;
    if (!st || !st->textbox) {
        return;
    }
    e9ui_textbox_setText(st->textbox, text);
}

const char *
e9ui_labeled_textbox_getText(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_textbox_state_t *st = (const e9ui_labeled_textbox_state_t*)comp->state;
    if (!st || !st->textbox) {
        return NULL;
    }
    return e9ui_textbox_getText(st->textbox);
}

void
e9ui_labeled_textbox_setOnChange(e9ui_component_t *comp, e9ui_labeled_textbox_change_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_textbox_state_t *st = (e9ui_labeled_textbox_state_t*)comp->state;
    st->onChange = cb;
    st->onChangeUser = user;
}

e9ui_component_t *
e9ui_labeled_textbox_getTextbox(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_textbox_state_t *st = (const e9ui_labeled_textbox_state_t*)comp->state;
    return st ? st->textbox : NULL;
}
