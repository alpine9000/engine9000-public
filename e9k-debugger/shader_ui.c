/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>

#include "shader_ui.h"
#include "alloc.h"
#include "crt.h"
#include "debug.h"
#include "debugger.h"
#include "config.h"
#include "e9ui.h"
#include "e9ui_labeled_checkbox.h"
#include "e9ui_vspacer.h"
#include "e9ui_text_cache.h"
#include "seek_bar.h"
#include "e9ui_button.h"
#include "e9ui_theme.h"


#define SHADER_UI_LABEL_W 185
#define SHADER_UI_GAP 12
#define SHADER_UI_ROW_PAD 6
#define SHADER_UI_BAR_H 12
#define SHADER_UI_RIGHT_MARGIN 12

typedef struct shader_ui_slider_binding {
    float minValue;
    float maxValue;
    float (*getValue)(void);
    void (*setValue)(float value);
} shader_ui_slider_binding_t;

typedef struct shader_ui_checkbox_binding {
    int (*getValue)(void);
    void (*setValue)(int enabled);
} shader_ui_checkbox_binding_t;

typedef struct shader_ui_slider {
    e9ui_component_t *bar;
    shader_ui_slider_binding_t binding;
    const char *tooltipLabel;
    const char *tooltipUnit;
    int tooltipPrecision;
} shader_ui_slider_t;

typedef struct shader_ui_checkbox {
    e9ui_component_t *checkbox;
    shader_ui_checkbox_binding_t binding;
} shader_ui_checkbox_t;

typedef struct shader_ui_slider_row_state {
    char *label;
    e9ui_component_t *bar;
    int labelWidth;
    int gap;
    int barHeight;
    int rowPadding;
} shader_ui_slider_row_state_t;

typedef struct shader_ui_column_state {
    int rowGap;
} shader_ui_column_state_t;

typedef struct shader_ui_action_row_state {
    e9ui_component_t *defaultsButton;
    e9ui_component_t *cancelButton;
    e9ui_component_t *applyButton;
    int gap;
    int padRight;
} shader_ui_action_row_state_t;

typedef struct e9k_shader_ui {
    int open;
    int closeRequested;
    SDL_Window *window;
    SDL_Renderer *renderer;
    uint32_t windowId;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *fullscreen;
    shader_ui_checkbox_t crtEnabled;
    shader_ui_checkbox_t geometryEnabled;
    shader_ui_checkbox_t bloomEnabled;
    shader_ui_checkbox_t halationEnabled;
    shader_ui_checkbox_t maskEnabled;
    shader_ui_checkbox_t gammaEnabled;
    shader_ui_checkbox_t chromaEnabled;
    shader_ui_checkbox_t grilleEnabled;
    shader_ui_slider_t scanStrength;
    shader_ui_slider_t halationStrength;
    shader_ui_slider_t halationThreshold;
    shader_ui_slider_t halationRadius;
    shader_ui_slider_t maskStrength;
    shader_ui_slider_t maskScale;
    shader_ui_slider_t beamStrength;
    shader_ui_slider_t beamWidth;
    shader_ui_slider_t curvature;
    shader_ui_slider_t overscan;
    shader_ui_slider_t scanlineBorder;
    int snapshotReady;
    int snapshotCrtEnabled;
    int snapshotGeometryEnabled;
    int snapshotBloomEnabled;
    int snapshotHalationEnabled;
    int snapshotMaskEnabled;
    int snapshotGammaEnabled;
    int snapshotChromaEnabled;
    int snapshotGrilleEnabled;
    float snapshotScanStrength;
    float snapshotHalationStrength;
    float snapshotHalationThreshold;
    float snapshotHalationRadius;
    float snapshotMaskStrength;
    float snapshotMaskScale;
    float snapshotBeamStrength;
    float snapshotBeamWidth;
    float snapshotCurvature;
    float snapshotOverscan;
    float snapshotScanlineBorder;
} e9k_shader_ui_t;

static e9k_shader_ui_t shader_ui_state = {0};

static void
shader_ui_refocusMain(void)
{
    SDL_Window *main_win = e9ui->ctx.window;
    if (!main_win) {
        return;
    }
    SDL_ShowWindow(main_win);
    SDL_RaiseWindow(main_win);
    SDL_SetWindowInputFocus(main_win);
    e9ui_component_t *geo = e9ui_findById(e9ui->root, "geo_view");
    if (geo) {
        e9ui_setFocus(&e9ui->ctx, geo);
    }
}


static int
shader_ui_getCrtEnabled(void)
{
    return crt_isEnabled();
}

static void
shader_ui_setCrtEnabled(int enabled)
{
    crt_setEnabled(enabled ? 1 : 0);
    debugger.config.crtEnabled = crt_isEnabled() ? 1 : 0;
}

static int
shader_ui_getGeometryEnabled(void)
{
    return crt_isGeometryEnabled();
}

static void
shader_ui_setGeometryEnabled(int enabled)
{
    crt_setGeometryEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getBloomEnabled(void)
{
    return crt_isBloomEnabled();
}

static void
shader_ui_setBloomEnabled(int enabled)
{
    crt_setBloomEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getHalationEnabled(void)
{
    return crt_isHalationEnabled();
}

static void
shader_ui_setHalationEnabled(int enabled)
{
    crt_setHalationEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getMaskEnabled(void)
{
    return crt_isMaskEnabled();
}

static void
shader_ui_setMaskEnabled(int enabled)
{
    crt_setMaskEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getGammaEnabled(void)
{
    return crt_isGammaEnabled();
}

static void
shader_ui_setGammaEnabled(int enabled)
{
    crt_setGammaEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getChromaEnabled(void)
{
    return crt_isChromaEnabled();
}

static void
shader_ui_setChromaEnabled(int enabled)
{
    crt_setChromaEnabled(enabled ? 1 : 0);
}

static int
shader_ui_getGrilleEnabled(void)
{
    return crt_isGrilleEnabled();
}

static void
shader_ui_setGrilleEnabled(int enabled)
{
    crt_setGrilleEnabled(enabled ? 1 : 0);
}

static void
shader_ui_snapshot(e9k_shader_ui_t *ui)
{
    if (!ui) {
        return;
    }
    ui->snapshotCrtEnabled = crt_isEnabled();
    ui->snapshotGeometryEnabled = crt_isGeometryEnabled();
    ui->snapshotBloomEnabled = crt_isBloomEnabled();
    ui->snapshotHalationEnabled = crt_isHalationEnabled();
    ui->snapshotMaskEnabled = crt_isMaskEnabled();
    ui->snapshotGammaEnabled = crt_isGammaEnabled();
    ui->snapshotChromaEnabled = crt_isChromaEnabled();
    ui->snapshotGrilleEnabled = crt_isGrilleEnabled();
    ui->snapshotScanStrength = crt_getScanStrength();
    ui->snapshotHalationStrength = crt_getHalationStrength();
    ui->snapshotHalationThreshold = crt_getHalationThreshold();
    ui->snapshotHalationRadius = crt_getHalationRadius();
    ui->snapshotMaskStrength = crt_getMaskStrength();
    ui->snapshotMaskScale = crt_getMaskScale();
    ui->snapshotBeamStrength = crt_getBeamStrength();
    ui->snapshotBeamWidth = crt_getBeamWidth();
    ui->snapshotCurvature = crt_getCurvatureK();
    ui->snapshotOverscan = crt_getOverscan();
    ui->snapshotScanlineBorder = crt_getScanlineBorder();
    ui->snapshotReady = 1;
}

static void
shader_ui_restoreSnapshot(const e9k_shader_ui_t *ui)
{
    if (!ui || !ui->snapshotReady) {
        return;
    }

    shader_ui_setCrtEnabled(ui->snapshotCrtEnabled);
    crt_setGeometryEnabled(ui->snapshotGeometryEnabled);
    crt_setBloomEnabled(ui->snapshotBloomEnabled);
    crt_setHalationEnabled(ui->snapshotHalationEnabled);
    crt_setMaskEnabled(ui->snapshotMaskEnabled);
    crt_setGammaEnabled(ui->snapshotGammaEnabled);
    crt_setChromaEnabled(ui->snapshotChromaEnabled);
    crt_setGrilleEnabled(ui->snapshotGrilleEnabled);
    crt_setScanStrength(ui->snapshotScanStrength);
    crt_setHalationStrength(ui->snapshotHalationStrength);
    crt_setHalationThreshold(ui->snapshotHalationThreshold);
    crt_setHalationRadius(ui->snapshotHalationRadius);
    crt_setMaskStrength(ui->snapshotMaskStrength);
    crt_setMaskScale(ui->snapshotMaskScale);
    crt_setBeamStrength(ui->snapshotBeamStrength);
    crt_setBeamWidth(ui->snapshotBeamWidth);
    crt_setCurvatureK(ui->snapshotCurvature);
    crt_setOverscan(ui->snapshotOverscan);
    crt_setScanlineBorder(ui->snapshotScanlineBorder);
}

static int
shader_ui_sliderRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    shader_ui_slider_row_state_t *st = (shader_ui_slider_row_state_t*)self->state;
    int barH = e9ui_scale_px(ctx, st->barHeight);
    if (barH <= 0) {
        barH = e9ui_scale_px(ctx, SHADER_UI_BAR_H);
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    int textH = font ? TTF_FontHeight(font) : barH;
    if (textH < barH) {
        textH = barH;
    }
    int pad = e9ui_scale_px(ctx, st->rowPadding);
    return textH + pad * 2;
}

static void
shader_ui_sliderRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !self->state || !ctx) {
        return;
    }
    shader_ui_slider_row_state_t *st = (shader_ui_slider_row_state_t*)self->state;
    self->bounds = bounds;
    if (!st->bar) {
        return;
    }
    int labelW = e9ui_scale_px(ctx, st->labelWidth);
    int gap = e9ui_scale_px(ctx, st->gap);
    int barH = e9ui_scale_px(ctx, st->barHeight);
    if (barH <= 0) {
        barH = e9ui_scale_px(ctx, SHADER_UI_BAR_H);
    }
    int knobR = barH / 2;
    if (knobR < 6) {
        knobR = 6;
    }
    int inset = knobR;
    int rightMargin = e9ui_scale_px(ctx, SHADER_UI_RIGHT_MARGIN);
    int barW = bounds.w - labelW - gap - inset * 2 - rightMargin;
    if (barW < 0) {
        barW = 0;
    }
    int barX = bounds.x + labelW + gap + inset;
    int barY = bounds.y + (bounds.h - barH) / 2;
    st->bar->bounds.x = barX;
    st->bar->bounds.y = barY;
    st->bar->bounds.w = barW;
    st->bar->bounds.h = barH;
}

static void
shader_ui_sliderRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }
    shader_ui_slider_row_state_t *st = (shader_ui_slider_row_state_t*)self->state;
    if (st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        if (font) {
            SDL_Color color = (SDL_Color){220, 220, 220, 255};
            int tw = 0;
            int th = 0;
            SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->label, color, &tw, &th);
            if (tex) {
                int pad = e9ui_scale_px(ctx, SHADER_UI_RIGHT_MARGIN);
                int labelW = e9ui_scale_px(ctx, st->labelWidth) - pad;
                if (labelW < 0) {
                    labelW = 0;
                }
                int textX = self->bounds.x + pad;
                if (labelW > tw) {
                    textX = self->bounds.x + pad + labelW - tw;
                }
                int textY = self->bounds.y + (self->bounds.h - th) / 2;
                SDL_Rect dst = { textX, textY, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }
    if (st->bar && st->bar->render) {
        st->bar->render(st->bar, ctx);
    }
}

static void
shader_ui_sliderRowDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    shader_ui_slider_row_state_t *st = (shader_ui_slider_row_state_t*)self->state;
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
}

static int
shader_ui_columnPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    if (!self || !ctx) {
        return 0;
    }
    shader_ui_column_state_t *st = (shader_ui_column_state_t*)self->state;
    if (!st) {
        return 0;
    }
    int gap = e9ui_scale_px(ctx, st->rowGap);
    int total = 0;
    int visibleCount = 0;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        int h = child->preferredHeight ? child->preferredHeight(child, ctx, availW) : 0;
        total += h;
        visibleCount++;
    }
    if (visibleCount > 1) {
        total += gap * (visibleCount - 1);
    }
    return total;
}

static void
shader_ui_columnLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx) {
        return;
    }
    shader_ui_column_state_t *st = (shader_ui_column_state_t*)self->state;
    if (!st) {
        return;
    }
    self->bounds = bounds;
    int gap = e9ui_scale_px(ctx, st->rowGap);
    int y = bounds.y;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        int h = child->preferredHeight ? child->preferredHeight(child, ctx, bounds.w) : 0;
        if (child->layout) {
            e9ui_rect_t row = (e9ui_rect_t){ bounds.x, y, bounds.w, h };
            child->layout(child, ctx, row);
        }
        y += h + gap;
    }
}

static void
shader_ui_columnRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(self, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (child && child->render) {
            child->render(child, ctx);
        }
    }
}

static e9ui_component_t *
shader_ui_columnMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    shader_ui_column_state_t *st = (shader_ui_column_state_t*)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->rowGap = 4;
    comp->name = "shader_ui_column";
    comp->state = st;
    comp->preferredHeight = shader_ui_columnPreferredHeight;
    comp->layout = shader_ui_columnLayout;
    comp->render = shader_ui_columnRender;
    return comp;
}

static void
shader_ui_actionRowLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)self->state;
    self->bounds = bounds;
    int gap = e9ui_scale_px(ctx, st->gap);
    int padRight = e9ui_scale_px(ctx, st->padRight);
    int barH = e9ui_scale_px(ctx, SHADER_UI_BAR_H);
    int inset = barH / 2;
    if (inset < 6) {
        inset = 6;
    }
    padRight += inset;
    int wApply = 0;
    int hApply = 0;
    int wDefaults = 0;
    int hDefaults = 0;
    int wCancel = 0;
    int hCancel = 0;
    if (st->applyButton) {
        e9ui_button_measure(st->applyButton, ctx, &wApply, &hApply);
    }
    if (st->defaultsButton) {
        e9ui_button_measure(st->defaultsButton, ctx, &wDefaults, &hDefaults);
    }
    if (st->cancelButton) {
        e9ui_button_measure(st->cancelButton, ctx, &wCancel, &hCancel);
    }
    int totalW = 0;
    if (st->applyButton) {
        totalW += wApply;
    }
    if (st->defaultsButton) {
        totalW += wDefaults;
    }
    if (st->cancelButton) {
        totalW += wCancel;
    }
    int gapCount = 0;
    if (st->defaultsButton) {
        gapCount++;
    }
    if (st->cancelButton) {
        gapCount++;
    }
    if (st->applyButton) {
        gapCount++;
    }
    if (gapCount > 1) {
        totalW += gap;
    }
    int x = bounds.x + bounds.w - padRight - totalW;
    int y = bounds.y;
    int rowH = bounds.h;
    if (st->applyButton) {
        int bh = hApply > 0 ? hApply : rowH;
        st->applyButton->bounds.x = x;
        st->applyButton->bounds.y = y + (rowH - bh) / 2;
        st->applyButton->bounds.w = wApply;
        st->applyButton->bounds.h = bh;
        x += wApply + gap;
    }
    if (st->defaultsButton) {
        int bh = hDefaults > 0 ? hDefaults : rowH;
        st->defaultsButton->bounds.x = x;
        st->defaultsButton->bounds.y = y + (rowH - bh) / 2;
        st->defaultsButton->bounds.w = wDefaults;
        st->defaultsButton->bounds.h = bh;
        x += wDefaults + gap;
    }
    if (st->cancelButton) {
        int bh = hCancel > 0 ? hCancel : rowH;
        st->cancelButton->bounds.x = x;
        st->cancelButton->bounds.y = y + (rowH - bh) / 2;
        st->cancelButton->bounds.w = wCancel;
        st->cancelButton->bounds.h = bh;
    }
}

static void
shader_ui_actionRowRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !self->state) {
        return;
    }
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)self->state;
    if (st->defaultsButton && st->defaultsButton->render) {
        st->defaultsButton->render(st->defaultsButton, ctx);
    }
    if (st->cancelButton && st->cancelButton->render) {
        st->cancelButton->render(st->cancelButton, ctx);
    }
    if (st->applyButton && st->applyButton->render) {
        st->applyButton->render(st->applyButton, ctx);
    }
}

static int
shader_ui_actionRowPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !ctx || !self->state) {
        return 0;
    }
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)self->state;
    int hDefaults = 0;
    int hCancel = 0;
    int hApply = 0;
    if (st->defaultsButton) {
        int w = 0;
        e9ui_button_measure(st->defaultsButton, ctx, &w, &hDefaults);
    }
    if (st->cancelButton) {
        int w = 0;
        e9ui_button_measure(st->cancelButton, ctx, &w, &hCancel);
    }
    if (st->applyButton) {
        int w = 0;
        e9ui_button_measure(st->applyButton, ctx, &w, &hApply);
    }
    int max = hDefaults > hCancel ? hDefaults : hCancel;
    return hApply > max ? hApply : max;
}

static e9ui_component_t *
shader_ui_actionRowMake(e9ui_component_t *defaultsButton, e9ui_component_t *cancelButton,
                        e9ui_component_t *applyButton)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    shader_ui_action_row_state_t *st = (shader_ui_action_row_state_t*)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    st->defaultsButton = defaultsButton;
    st->cancelButton = cancelButton;
    st->applyButton = applyButton;
    st->gap = 10;
    st->padRight = SHADER_UI_RIGHT_MARGIN;
    comp->name = "shader_ui_action_row";
    comp->state = st;
    comp->preferredHeight = shader_ui_actionRowPreferredHeight;
    comp->layout = shader_ui_actionRowLayout;
    comp->render = shader_ui_actionRowRender;
    if (defaultsButton) {
        e9ui_child_add(comp, defaultsButton, NULL);
    }
    if (cancelButton) {
        e9ui_child_add(comp, cancelButton, NULL);
    }
    if (applyButton) {
        e9ui_child_add(comp, applyButton, NULL);
    }
    return comp;
}

static e9ui_component_t *
shader_ui_sliderRowMake(const char *label, e9ui_component_t **outBar)
{
    e9ui_component_t *row = (e9ui_component_t*)alloc_calloc(1, sizeof(*row));
    shader_ui_slider_row_state_t *st = (shader_ui_slider_row_state_t*)alloc_calloc(1, sizeof(*st));
    if (!row || !st) {
        if (row) {
            alloc_free(row);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }
    if (label && *label) {
        st->label = alloc_strdup(label);
    }
    st->labelWidth = SHADER_UI_LABEL_W;
    st->gap = SHADER_UI_GAP;
    st->barHeight = SHADER_UI_BAR_H;
    st->rowPadding = SHADER_UI_ROW_PAD;
    st->bar = seek_bar_make();
    if (st->bar) {
        seek_bar_setMargins(st->bar, 0, 0, 0);
    }
    row->name = "shader_ui_slider_row";
    row->state = st;
    row->preferredHeight = shader_ui_sliderRowPreferredHeight;
    row->layout = shader_ui_sliderRowLayout;
    row->render = shader_ui_sliderRowRender;
    row->dtor = shader_ui_sliderRowDtor;
    if (st->bar) {
        e9ui_child_add(row, st->bar, NULL);
    }
    if (outBar) {
        *outBar = st->bar;
    }
    return row;
}

static void
shader_ui_checkboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    shader_ui_checkbox_binding_t *binding = (shader_ui_checkbox_binding_t*)user;
    if (!binding || !binding->setValue) {
        return;
    }
    binding->setValue(selected ? 1 : 0);
}

static void
shader_ui_sliderChanged(float percent, void *user)
{
    shader_ui_slider_binding_t *binding = (shader_ui_slider_binding_t*)user;
    if (!binding || !binding->setValue) {
        return;
    }
    float range = binding->maxValue - binding->minValue;
    float value = binding->minValue + percent * range;
    binding->setValue(value);
}

static void
shader_ui_sliderTooltip(float percent, char *out, size_t cap, void *user)
{
    shader_ui_slider_t *slider = (shader_ui_slider_t*)user;
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!slider) {
        return;
    }
    float range = slider->binding.maxValue - slider->binding.minValue;
    float value = slider->binding.minValue + percent * range;
    int precision = slider->tooltipPrecision > 0 ? slider->tooltipPrecision : 2;
    const char *label = slider->tooltipLabel ? slider->tooltipLabel : "Value";
    const char *unit = slider->tooltipUnit ? slider->tooltipUnit : "";
    snprintf(out, cap, "%s %.2f%s", label, value, unit);
    if (precision != 2) {
        char fmt[32];
        snprintf(fmt, sizeof(fmt), "%%s %%.%df%%s", precision);
        snprintf(out, cap, fmt, label, value, unit);
    }
}

static float
shader_ui_clampPercent(float percent)
{
    if (percent < 0.0f) {
        return 0.0f;
    }
    if (percent > 1.0f) {
        return 1.0f;
    }
    return percent;
}

static void
shader_ui_syncCheckbox(shader_ui_checkbox_t *checkbox, e9ui_context_t *ctx)
{
    if (!checkbox || !checkbox->checkbox || !checkbox->binding.getValue) {
        return;
    }
    int selected = checkbox->binding.getValue();
    e9ui_labeled_checkbox_setSelected(checkbox->checkbox, selected, ctx);
}

static void
shader_ui_syncSlider(shader_ui_slider_t *slider)
{
    if (!slider || !slider->bar || !slider->binding.getValue) {
        return;
    }
    float range = slider->binding.maxValue - slider->binding.minValue;
    if (range <= 0.0f) {
        return;
    }
    float value = slider->binding.getValue();
    float percent = (value - slider->binding.minValue) / range;
    seek_bar_setPercent(slider->bar, shader_ui_clampPercent(percent));
}

static e9ui_component_t *
shader_ui_makeCheckbox(const char *label, shader_ui_checkbox_t *slot)
{
    if (!slot) {
        return NULL;
    }
    int selected = slot->binding.getValue ? slot->binding.getValue() : 0;
    e9ui_component_t *comp = e9ui_labeled_checkbox_make(label, SHADER_UI_LABEL_W, 0,
                                                        selected, shader_ui_checkboxChanged,
                                                        &slot->binding);
    slot->checkbox = comp;
    return comp;
}

static e9ui_component_t *
shader_ui_makeSlider(const char *label, shader_ui_slider_t *slot)
{
    if (!slot) {
        return NULL;
    }
    e9ui_component_t *bar = NULL;
    e9ui_component_t *row = shader_ui_sliderRowMake(label, &bar);
    slot->bar = bar;
    if (!slot->tooltipLabel) {
        slot->tooltipLabel = label;
    }
    if (slot->tooltipPrecision <= 0) {
        slot->tooltipPrecision = 2;
    }
    if (bar) {
        seek_bar_setCallback(bar, shader_ui_sliderChanged, &slot->binding);
        seek_bar_setTooltipCallback(bar, shader_ui_sliderTooltip, slot);
        seek_bar_setHoverMargin(bar, 6);
    }
    return row;
}

static void
shader_ui_syncState(e9k_shader_ui_t *ui)
{
    if (!ui) {
        return;
    }
    shader_ui_syncCheckbox(&ui->crtEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->geometryEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->bloomEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->halationEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->maskEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->gammaEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->chromaEnabled, &ui->ctx);
    shader_ui_syncCheckbox(&ui->grilleEnabled, &ui->ctx);
    shader_ui_syncSlider(&ui->scanStrength);
    shader_ui_syncSlider(&ui->halationStrength);
    shader_ui_syncSlider(&ui->halationThreshold);
    shader_ui_syncSlider(&ui->halationRadius);
    shader_ui_syncSlider(&ui->maskStrength);
    shader_ui_syncSlider(&ui->maskScale);
    shader_ui_syncSlider(&ui->beamStrength);
    shader_ui_syncSlider(&ui->beamWidth);
    shader_ui_syncSlider(&ui->curvature);
    shader_ui_syncSlider(&ui->overscan);
    shader_ui_syncSlider(&ui->scanlineBorder);
}

static float
shader_ui_computeDpiScale(const e9ui_context_t *ctx)
{
    if (!ctx || !ctx->window || !ctx->renderer) {
        return 1.0f;
    }
    int winW = 0;
    int winH = 0;
    int renW = 0;
    int renH = 0;
    SDL_GetWindowSize(ctx->window, &winW, &winH);
    SDL_GetRendererOutputSize(ctx->renderer, &renW, &renH);
    if (winW <= 0 || winH <= 0) {
        return 1.0f;
    }
    float scaleX = (float)renW / (float)winW;
    float scaleY = (float)renH / (float)winH;
    float scale = scaleX > scaleY ? scaleX : scaleY;
    return scale < 1.0f ? 1.0f : scale;
}

static void
shader_ui_buildBindings(e9k_shader_ui_t *ui)
{
    ui->crtEnabled.binding.getValue = shader_ui_getCrtEnabled;
    ui->crtEnabled.binding.setValue = shader_ui_setCrtEnabled;
    ui->geometryEnabled.binding.getValue = shader_ui_getGeometryEnabled;
    ui->geometryEnabled.binding.setValue = shader_ui_setGeometryEnabled;
    ui->bloomEnabled.binding.getValue = shader_ui_getBloomEnabled;
    ui->bloomEnabled.binding.setValue = shader_ui_setBloomEnabled;
    ui->halationEnabled.binding.getValue = shader_ui_getHalationEnabled;
    ui->halationEnabled.binding.setValue = shader_ui_setHalationEnabled;
    ui->maskEnabled.binding.getValue = shader_ui_getMaskEnabled;
    ui->maskEnabled.binding.setValue = shader_ui_setMaskEnabled;
    ui->gammaEnabled.binding.getValue = shader_ui_getGammaEnabled;
    ui->gammaEnabled.binding.setValue = shader_ui_setGammaEnabled;
    ui->chromaEnabled.binding.getValue = shader_ui_getChromaEnabled;
    ui->chromaEnabled.binding.setValue = shader_ui_setChromaEnabled;
    ui->grilleEnabled.binding.getValue = shader_ui_getGrilleEnabled;
    ui->grilleEnabled.binding.setValue = shader_ui_setGrilleEnabled;

    ui->scanStrength.binding.minValue = 0.0f;
    ui->scanStrength.binding.maxValue = 1.0f;
    ui->scanStrength.binding.getValue = crt_getScanStrength;
    ui->scanStrength.binding.setValue = crt_setScanStrength;
    ui->scanStrength.tooltipLabel = "Scan Strength";

    ui->halationStrength.binding.minValue = 0.0f;
    ui->halationStrength.binding.maxValue = 1.0f;
    ui->halationStrength.binding.getValue = crt_getHalationStrength;
    ui->halationStrength.binding.setValue = crt_setHalationStrength;
    ui->halationStrength.tooltipLabel = "Halation Strength";

    ui->halationThreshold.binding.minValue = 0.0f;
    ui->halationThreshold.binding.maxValue = 1.0f;
    ui->halationThreshold.binding.getValue = crt_getHalationThreshold;
    ui->halationThreshold.binding.setValue = crt_setHalationThreshold;
    ui->halationThreshold.tooltipLabel = "Halation Threshold";

    ui->halationRadius.binding.minValue = 0.0f;
    ui->halationRadius.binding.maxValue = 64.0f;
    ui->halationRadius.binding.getValue = crt_getHalationRadius;
    ui->halationRadius.binding.setValue = crt_setHalationRadius;
    ui->halationRadius.tooltipLabel = "Halation Radius";
    ui->halationRadius.tooltipUnit = "px";

    ui->maskStrength.binding.minValue = 0.0f;
    ui->maskStrength.binding.maxValue = 1.0f;
    ui->maskStrength.binding.getValue = crt_getMaskStrength;
    ui->maskStrength.binding.setValue = crt_setMaskStrength;
    ui->maskStrength.tooltipLabel = "Mask Strength";

    ui->maskScale.binding.minValue = 0.25f;
    ui->maskScale.binding.maxValue = 32.0f;
    ui->maskScale.binding.getValue = crt_getMaskScale;
    ui->maskScale.binding.setValue = crt_setMaskScale;
    ui->maskScale.tooltipLabel = "Mask Scale";
    ui->maskScale.tooltipUnit = "x";

    ui->beamStrength.binding.minValue = 0.0f;
    ui->beamStrength.binding.maxValue = 1.0f;
    ui->beamStrength.binding.getValue = crt_getBeamStrength;
    ui->beamStrength.binding.setValue = crt_setBeamStrength;
    ui->beamStrength.tooltipLabel = "Beam Strength";

    ui->beamWidth.binding.minValue = 0.25f;
    ui->beamWidth.binding.maxValue = 4.0f;
    ui->beamWidth.binding.getValue = crt_getBeamWidth;
    ui->beamWidth.binding.setValue = crt_setBeamWidth;
    ui->beamWidth.tooltipLabel = "Beam Width";

    ui->curvature.binding.minValue = 0.0f;
    ui->curvature.binding.maxValue = 0.20f;
    ui->curvature.binding.getValue = crt_getCurvatureK;
    ui->curvature.binding.setValue = crt_setCurvatureK;
    ui->curvature.tooltipLabel = "Curvature";
    ui->curvature.tooltipPrecision = 3;

    ui->overscan.binding.minValue = 0.50f;
    ui->overscan.binding.maxValue = 1.50f;
    ui->overscan.binding.getValue = crt_getOverscan;
    ui->overscan.binding.setValue = crt_setOverscan;
    ui->overscan.tooltipLabel = "Overscan";
    ui->overscan.tooltipUnit = "x";

    ui->scanlineBorder.binding.minValue = 0.0f;
    ui->scanlineBorder.binding.maxValue = 0.45f;
    ui->scanlineBorder.binding.getValue = crt_getScanlineBorder;
    ui->scanlineBorder.binding.setValue = crt_setScanlineBorder;
    ui->scanlineBorder.tooltipLabel = "Scanline Border";
}

static void
shader_ui_cancel(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9k_shader_ui_t *ui = (e9k_shader_ui_t*)user;
    if (!ui) {
        return;
    }
    shader_ui_restoreSnapshot(ui);
    ui->closeRequested = 1;
}

static void
shader_ui_defaults(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    crt_setAdvancedDefaults();
    debugger.config.crtEnabled = crt_isEnabled() ? 1 : 0;
}

static void
shader_ui_apply(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9k_shader_ui_t *ui = (e9k_shader_ui_t*)user;
    if (!ui) {
        return;
    }
    config_saveConfig();
    ui->closeRequested = 1;
}

static e9ui_component_t *
shader_ui_buildRoot(e9k_shader_ui_t *ui)
{
    e9ui_component_t *stack = e9ui_stack_makeVertical();
    if (!stack) {
        return NULL;
    }

    e9ui_stack_addFixed(stack, e9ui_vspacer_make(SHADER_UI_RIGHT_MARGIN));

    e9ui_component_t *checkboxRow = e9ui_hstack_make();
    e9ui_component_t *leftCol = shader_ui_columnMake();
    e9ui_component_t *rightCol = shader_ui_columnMake();
    if (checkboxRow && leftCol && rightCol) {
        e9ui_component_t *row = shader_ui_makeCheckbox("CRT Enabled", &ui->crtEnabled);
        if (row) {
            e9ui_child_add(leftCol, row, NULL);
        }
        row = shader_ui_makeCheckbox("Geometry", &ui->geometryEnabled);
        if (row) {
            e9ui_child_add(leftCol, row, NULL);
        }
        row = shader_ui_makeCheckbox("Mask", &ui->maskEnabled);
        if (row) {
            e9ui_child_add(leftCol, row, NULL);
        }
        row = shader_ui_makeCheckbox("Bloom", &ui->bloomEnabled);
        if (row) {
            e9ui_child_add(rightCol, row, NULL);
        }
        row = shader_ui_makeCheckbox("Halation", &ui->halationEnabled);
        if (row) {
            e9ui_child_add(rightCol, row, NULL);
        }
        row = shader_ui_makeCheckbox("Gamma", &ui->gammaEnabled);
        if (row) {
            e9ui_child_add(rightCol, row, NULL);
        }
        row = shader_ui_makeCheckbox("Chroma", &ui->chromaEnabled);
        if (row) {
            e9ui_child_add(rightCol, row, NULL);
        }
        row = shader_ui_makeCheckbox("Grille", &ui->grilleEnabled);
        if (row) {
            e9ui_child_add(leftCol, row, NULL);
        }
        e9ui_hstack_addFlex(checkboxRow, leftCol);
        e9ui_hstack_addFixed(checkboxRow, e9ui_spacer_make(24), 24);
        e9ui_hstack_addFlex(checkboxRow, rightCol);
        e9ui_stack_addFixed(stack, checkboxRow);
    }

    e9ui_stack_addFixed(stack, e9ui_vspacer_make(10));

    e9ui_component_t *row = shader_ui_makeSlider("Scan Strength", &ui->scanStrength);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Mask Strength", &ui->maskStrength);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Mask Scale", &ui->maskScale);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Beam Strength", &ui->beamStrength);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Beam Width", &ui->beamWidth);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Curvature", &ui->curvature);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Overscan", &ui->overscan);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Scanline Border", &ui->scanlineBorder);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Halation Strength", &ui->halationStrength);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Halation Threshold", &ui->halationThreshold);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }
    row = shader_ui_makeSlider("Halation Radius", &ui->halationRadius);
    if (row) {
        e9ui_stack_addFixed(stack, row);
    }

    e9ui_stack_addFixed(stack, e9ui_vspacer_make(SHADER_UI_RIGHT_MARGIN));
    e9ui_component_t *apply = e9ui_button_make("Apply", shader_ui_apply, ui);
    e9ui_component_t *defaults = e9ui_button_make("Defaults", shader_ui_defaults, ui);
    e9ui_component_t *cancel = e9ui_button_make("Cancel", shader_ui_cancel, ui);
    if (apply) {
        e9ui_button_setTheme(apply, e9ui_theme_button_preset_green());
    }
    if (cancel) {
        e9ui_button_setTheme(cancel, e9ui_theme_button_preset_red());
    }
    e9ui_component_t *actions = shader_ui_actionRowMake(defaults, cancel, apply);
    if (actions) {
        e9ui_stack_addFixed(stack, actions);
    }
    e9ui_stack_addFlex(stack, e9ui_vspacer_make(6));
    return stack;
}

static int
shader_ui_measureRootHeight(e9ui_component_t *root, e9ui_context_t *ctx, int availW)
{
    if (!root || !ctx) {
        return 0;
    }
    int innerW = availW;
    int total = 0;
    e9ui_child_iterator iter;
    e9ui_child_iterator *it = e9ui_child_iterateChildren(root, &iter);
    while (e9ui_child_interateNext(it)) {
        e9ui_component_t *child = it->child;
        if (!child || e9ui_getHidden(child)) {
            continue;
        }
        if (child->preferredHeight) {
            total += child->preferredHeight(child, ctx, innerW);
        }
    }
    return total;
}

int
shader_ui_init(void)
{
    e9k_shader_ui_t *ui = &shader_ui_state;
    if (ui->open) {
        return 1;
    }
    shader_ui_buildBindings(ui);

    SDL_Window *win = SDL_CreateWindow("ENGINE9000 DEBUGGER - CRT SETTINGS",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       520, 720,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        debug_error("shader ui: SDL_CreateWindow failed: %s", SDL_GetError());
        return 0;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        SDL_DestroyWindow(win);
        debug_error("shader ui: SDL_CreateRenderer failed: %s", SDL_GetError());
        return 0;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    ui->window = win;
    ui->renderer = ren;
    ui->windowId = SDL_GetWindowID(win);
    ui->ctx.window = win;
    ui->ctx.renderer = ren;
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.dpiScale = shader_ui_computeDpiScale(&ui->ctx);
    shader_ui_snapshot(ui);
    ui->closeRequested = 0;

    ui->root = shader_ui_buildRoot(ui);
    if (!ui->root) {
        shader_ui_shutdown();
        return 0;
    }
    {
        int winW = 0;
        int winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        int renderW = (int)((float)winW * ui->ctx.dpiScale + 0.5f);
        int desiredRenderH = shader_ui_measureRootHeight(ui->root, &ui->ctx, renderW);
        if (desiredRenderH > 0 && ui->ctx.dpiScale > 0.0f) {
            int desiredWinH = (int)((float)desiredRenderH / ui->ctx.dpiScale + 0.5f);
            if (desiredWinH > 0 && desiredWinH != winH) {
                SDL_SetWindowSize(win, winW, desiredWinH);
            }
        }
    }
    ui->open = 1;
    return 1;
}

void
shader_ui_shutdown(void)
{
    e9k_shader_ui_t *ui = &shader_ui_state;
    if (!ui->open) {
        return;
    }
    if (ui->root) {
        e9ui_childDestroy(ui->root, &ui->ctx);
        ui->root = NULL;
    }
    e9ui_text_cache_clearRenderer(ui->renderer);
    if (ui->renderer) {
        SDL_DestroyRenderer(ui->renderer);
        ui->renderer = NULL;
    }
    if (ui->window) {
        SDL_DestroyWindow(ui->window);
        ui->window = NULL;
    }
    ui->open = 0;
    ui->closeRequested = 0;
    ui->windowId = 0;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
    shader_ui_refocusMain();
}

int
shader_ui_isOpen(void)
{
    return shader_ui_state.open ? 1 : 0;
}

uint32_t
shader_ui_getWindowId(void)
{
    return shader_ui_state.windowId;
}

void
shader_ui_handleEvent(SDL_Event *ev)
{
    if (!ev || !shader_ui_state.open) {
        return;
    }
    e9k_shader_ui_t *ui = &shader_ui_state;
    if (ui->closeRequested) {
        return;
    }
    e9ui_component_t *root = ui->fullscreen ? ui->fullscreen : ui->root;
    ui->ctx.focusClickHandled = 0;
    ui->ctx.cursorOverride = 0;

    if (ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_CLOSE) {
        ui->closeRequested = 1;
        return;
    }

    if (ev->type == SDL_MOUSEMOTION) {
        int prevX = ui->ctx.mouseX;
        int prevY = ui->ctx.mouseY;
        ui->ctx.mousePrevX = prevX;
        ui->ctx.mousePrevY = prevY;
        int scaledX = e9ui_scale_coord(&ui->ctx, ev->motion.x);
        int scaledY = e9ui_scale_coord(&ui->ctx, ev->motion.y);
        ev->motion.x = scaledX;
        ev->motion.y = scaledY;
        ev->motion.xrel = scaledX - prevX;
        ev->motion.yrel = scaledY - prevY;
        ui->ctx.mouseX = scaledX;
        ui->ctx.mouseY = scaledY;
    } else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
        int scaledX = e9ui_scale_coord(&ui->ctx, ev->button.x);
        int scaledY = e9ui_scale_coord(&ui->ctx, ev->button.y);
        ev->button.x = scaledX;
        ev->button.y = scaledY;
        ui->ctx.mouseX = scaledX;
        ui->ctx.mouseY = scaledY;
    } else if (ev->type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        int scaledX = e9ui_scale_coord(&ui->ctx, mx);
        int scaledY = e9ui_scale_coord(&ui->ctx, my);
        ui->ctx.mouseX = scaledX;
        ui->ctx.mouseY = scaledY;
    } else if (ev->type == SDL_WINDOWEVENT) {
        if (ev->window.event == SDL_WINDOWEVENT_RESIZED ||
            ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            ui->ctx.dpiScale = shader_ui_computeDpiScale(&ui->ctx);
        }
    } else if (ev->type == SDL_KEYDOWN) {
        if (ev->key.keysym.sym == SDLK_ESCAPE) {
            shader_ui_cancel(&ui->ctx, ui);
            return;
        }
        int consumed = 0;
        if (e9ui_getFocus(&ui->ctx) && e9ui_getFocus(&ui->ctx)->handleEvent) {
            consumed = e9ui_getFocus(&ui->ctx)->handleEvent(e9ui_getFocus(&ui->ctx), &ui->ctx, ev);
        }
        if (!consumed && root && root->handleEvent) {
            root->handleEvent(root, &ui->ctx, ev);
        }
        return;
    } else if (ev->type == SDL_TEXTINPUT) {
        if (e9ui_getFocus(&ui->ctx) && e9ui_getFocus(&ui->ctx)->handleEvent) {
            e9ui_getFocus(&ui->ctx)->handleEvent(e9ui_getFocus(&ui->ctx), &ui->ctx, ev);
        }
        return;
    }

    if (root) {
        e9ui_event_process(root, &ui->ctx, ev);
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT && !ui->ctx.focusClickHandled) {
        e9ui_setFocus(&ui->ctx, NULL);
    }
}

void
shader_ui_render(void)
{
    e9k_shader_ui_t *ui = &shader_ui_state;
    if (!ui->open || !ui->renderer || !ui->root) {
        return;
    }
    if (ui->closeRequested) {
        shader_ui_shutdown();
        return;
    }
    ui->ctx.font = e9ui->ctx.font;
    ui->ctx.window = ui->window;
    ui->ctx.renderer = ui->renderer;
    shader_ui_syncState(ui);

    SDL_SetRenderDrawColor(ui->renderer, 12, 12, 12, 255);
    SDL_RenderClear(ui->renderer);
    int w = 0;
    int h = 0;
    SDL_GetRendererOutputSize(ui->renderer, &w, &h);
    ui->ctx.winW = w;
    ui->ctx.winH = h;

    e9ui_component_t *root = ui->fullscreen ? ui->fullscreen : ui->root;
    if (root && root->layout) {
        e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
        root->layout(root, &ui->ctx, full);
    }
    if (root && root->render) {
        root->render(root, &ui->ctx);
    }
    SDL_RenderPresent(ui->renderer);
}
