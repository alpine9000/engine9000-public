/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include <string.h>

typedef struct e9ui_labeled_select_state {
    char *label;
    int labelWidth_px;
    int totalWidth_px;
    e9ui_select_option_t *options;
    int optionCount;
    int selectedIndex;
    e9ui_component_t *button;
    e9ui_labeled_select_change_cb_t onChange;
    void *onChangeUser;
    e9ui_component_t *self;
} e9ui_labeled_select_state_t;

static const e9ui_select_option_t *
e9ui_labeled_select_currentOption(const e9ui_labeled_select_state_t *st)
{
    if (!st || !st->options || st->optionCount <= 0) {
        return NULL;
    }
    int index = st->selectedIndex;
    if (index < 0) {
        index = 0;
    }
    if (index >= st->optionCount) {
        index = st->optionCount - 1;
    }
    return &st->options[index];
}

static const char *
e9ui_labeled_select_currentValue(const e9ui_labeled_select_state_t *st)
{
    const e9ui_select_option_t *opt = e9ui_labeled_select_currentOption(st);
    return opt ? opt->value : NULL;
}

static const char *
e9ui_labeled_select_currentLabel(const e9ui_labeled_select_state_t *st)
{
    const e9ui_select_option_t *opt = e9ui_labeled_select_currentOption(st);
    if (!opt) {
        return NULL;
    }
    if (opt->label && *opt->label) {
        return opt->label;
    }
    return opt->value;
}

static int
e9ui_labeled_select_findIndex(const e9ui_labeled_select_state_t *st, const char *value)
{
    if (!st || !st->options || st->optionCount <= 0 || !value) {
        return -1;
    }
    for (int i = 0; i < st->optionCount; ++i) {
        const char *v = st->options[i].value;
        if (v && strcmp(v, value) == 0) {
            return i;
        }
    }
    return -1;
}

static void
e9ui_labeled_select_syncButtonLabel(e9ui_labeled_select_state_t *st)
{
    if (!st || !st->button) {
        return;
    }
    const char *label = e9ui_labeled_select_currentLabel(st);
    e9ui_button_setLabel(st->button, (label && *label) ? label : "");
}

static void
e9ui_labeled_select_notifyChange(e9ui_context_t *ctx, e9ui_labeled_select_state_t *st)
{
    if (!st || !st->onChange) {
        return;
    }
    const char *value = e9ui_labeled_select_currentValue(st);
    st->onChange(ctx, st->self, value ? value : "", st->onChangeUser);
}

static void
e9ui_labeled_select_clicked(e9ui_context_t *ctx, void *user)
{
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)user;
    if (!st || !st->options || st->optionCount <= 0) {
        return;
    }
    st->selectedIndex++;
    if (st->selectedIndex >= st->optionCount) {
        st->selectedIndex = 0;
    }
    e9ui_labeled_select_syncButtonLabel(st);
    e9ui_labeled_select_notifyChange(ctx, st);
}

static int
e9ui_labeled_select_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
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
    int buttonW = totalW - labelW - gap;
    if (buttonW < 0) {
        buttonW = 0;
    }
    int buttonH = 0;
    if (st->button && st->button->preferredHeight) {
        buttonH = st->button->preferredHeight(st->button, ctx, buttonW);
    }
    return buttonH;
}

static void
e9ui_labeled_select_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
    if (!st || !st->button) {
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
    int buttonW = totalW - labelW - gap;
    if (buttonW < 0) {
        buttonW = 0;
    }
    int buttonH = st->button->preferredHeight ? st->button->preferredHeight(st->button, ctx, buttonW) : 0;
    int rowH = buttonH;
    if (rowH < 0) {
        rowH = 0;
    }
    int rowX = bounds.x + (bounds.w - totalW) / 2;
    int rowY = bounds.y + (bounds.h - rowH) / 2;
    e9ui_rect_t buttonRect = { rowX + labelW + gap, rowY, buttonW, rowH };
    st->button->layout(st->button, ctx, buttonRect);
}

static void
e9ui_labeled_select_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
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
    if (st->button && st->button->render) {
        st->button->render(st->button, ctx);
    }
}

static void
e9ui_labeled_select_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->options) {
        alloc_free(st->options);
        st->options = NULL;
        st->optionCount = 0;
    }
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
}

e9ui_component_t *
e9ui_labeled_select_make(const char *label, int labelWidth_px, int totalWidth_px,
                         const e9ui_select_option_t *options, int optionCount,
                         const char *initialValue,
                         e9ui_labeled_select_change_cb_t cb, void *user)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)alloc_calloc(1, sizeof(*st));
    if (!c || !st) {
        alloc_free(c);
        alloc_free(st);
        return NULL;
    }
    st->labelWidth_px = labelWidth_px;
    st->totalWidth_px = totalWidth_px;
    if (label && *label) {
        st->label = alloc_strdup(label);
    }
    if (options && optionCount > 0) {
        st->options = (e9ui_select_option_t*)alloc_calloc((size_t)optionCount, sizeof(*st->options));
        if (!st->options) {
            alloc_free(c);
            alloc_free(st);
            return NULL;
        }
        memcpy(st->options, options, (size_t)optionCount * sizeof(*st->options));
        st->optionCount = optionCount;
    } else {
        st->options = NULL;
        st->optionCount = 0;
    }
    st->selectedIndex = 0;
    if (initialValue && *initialValue) {
        int found = e9ui_labeled_select_findIndex(st, initialValue);
        if (found >= 0) {
            st->selectedIndex = found;
        }
    }
    st->button = e9ui_button_make("", e9ui_labeled_select_clicked, st);
    e9ui_labeled_select_syncButtonLabel(st);
    st->onChange = cb;
    st->onChangeUser = user;

    c->name = "e9ui_labeledSelect";
    c->state = st;
    c->preferredHeight = e9ui_labeled_select_preferredHeight;
    c->layout = e9ui_labeled_select_layout;
    c->render = e9ui_labeled_select_render;
    c->dtor = e9ui_labeled_select_dtor;
    st->self = c;
    if (st->button) {
        e9ui_child_add(c, st->button, 0);
    }
    return c;
}

void
e9ui_labeled_select_setLabelWidth(e9ui_component_t *comp, int labelWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    st->labelWidth_px = labelWidth_px;
}

void
e9ui_labeled_select_setTotalWidth(e9ui_component_t *comp, int totalWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    st->totalWidth_px = totalWidth_px;
}

void
e9ui_labeled_select_setValue(e9ui_component_t *comp, const char *value)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    int found = e9ui_labeled_select_findIndex(st, value);
    if (found < 0) {
        return;
    }
    st->selectedIndex = found;
    e9ui_labeled_select_syncButtonLabel(st);
}

const char *
e9ui_labeled_select_getValue(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_select_state_t *st = (const e9ui_labeled_select_state_t*)comp->state;
    return e9ui_labeled_select_currentValue(st);
}

void
e9ui_labeled_select_setOnChange(e9ui_component_t *comp, e9ui_labeled_select_change_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    st->onChange = cb;
    st->onChangeUser = user;
}

e9ui_component_t *
e9ui_labeled_select_getButton(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_select_state_t *st = (const e9ui_labeled_select_state_t*)comp->state;
    return st ? st->button : NULL;
}
