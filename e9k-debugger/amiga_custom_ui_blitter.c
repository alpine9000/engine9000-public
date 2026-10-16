/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>

#include "amiga_blittervis.h"
#include "amiga_custom_ui_internal.h"
#include "e9ui_labeled_textbox.h"
#include "e9ui_seek_bar.h"
#include "e9ui_textbox.h"
#include "libretro_host.h"

float
amiga_custom_ui_blitter_visDecayToPercent(int decay)
{
    int clamped = decay;
    if (clamped < 1) {
        clamped = 1;
    }
    if (clamped > AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX) {
        clamped = AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX;
    }
    if (AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX <= 1) {
        return 1.0f;
    }
    return (float)(clamped - 1) / (float)(AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX - 1);
}


int
amiga_custom_ui_blitter_visDecayFromPercent(float percent)
{
    float clamped = percent;
    if (clamped < 0.0f) {
        clamped = 0.0f;
    }
    if (clamped > 1.0f) {
        clamped = 1.0f;
    }
    int decay = 1 + (int)(clamped * (float)(AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX - 1) + 0.5f);
    if (decay < 1) {
        decay = 1;
    }
    if (decay > AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX) {
        decay = AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX;
    }
    return decay;
}


void
amiga_custom_ui_blitter_syncVisDecaySeekBar(amiga_custom_ui_state_t *ui)
{
    if (!ui || !ui->blitterVisDecaySeekBar) {
        return;
    }
    e9ui_seek_bar_setPercent(ui->blitterVisDecaySeekBar, amiga_custom_ui_blitter_visDecayToPercent(ui->blitterVisDecay));
}


void
amiga_custom_ui_blitter_statsChartSetValues(e9ui_component_t *comp,
                                     int hasStats,
                                     uint32_t wordsFrame,
                                     uint32_t maxWordsEstimateFrame,
                                     uint32_t blitsFrame)
{
    if (!comp || !comp->state) {
        return;
    }
    amiga_custom_ui_blitter_stats_chart_state_t *st = (amiga_custom_ui_blitter_stats_chart_state_t *)comp->state;
    st->hasStats = hasStats ? 1 : 0;
    st->wordsFrame = wordsFrame;
    st->maxWordsEstimateFrame = maxWordsEstimateFrame;
    st->blitsFrame = blitsFrame;
}


static int
amiga_custom_ui_blitter_statsChartPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !self->state || !ctx) {
        return 0;
    }
    amiga_custom_ui_blitter_stats_chart_state_t *st = (amiga_custom_ui_blitter_stats_chart_state_t *)self->state;
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);
    int barH = amiga_custom_ui_common_textboxLikeHeight(ctx);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int rowGap = e9ui_scale_px(ctx, st->rowGap);
    if (fontH <= 0) {
        fontH = 12;
    }
    int rowH = fontH > barH ? fontH : barH;
    return topPad + rowH + rowGap + rowH + bottomPad;
}


static void
amiga_custom_ui_blitter_statsChartLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}


void
amiga_custom_ui_blitter_statsChartRenderBarRow(e9ui_context_t *ctx,
                                 int contentX,
                                 int barX,
                                 int barW,
                                 int barHeight,
                                 int labelGap,
                                 int rowY,
                                 int rowH,
                                 int fontH,
                                 const char *labelText,
                                 int hasData,
                                 uint32_t valueUsed,
                                 uint32_t valueMax,
                                 const char *valueSuffix,
                                 int useGradientFill,
                                 SDL_Color solidFillColor,
                                 int useTextShadow,
                                 int useTextClip)
{
    SDL_Color labelColor = amiga_custom_ui_common_blitterStatsChartLabelColor;
    SDL_Color textColor = amiga_custom_ui_common_blitterStatsChartTextColor;
    SDL_Color textShadow = amiga_custom_ui_common_blitterStatsChartTextShadowColor;
    int rowLabelY = rowY + (rowH - fontH) / 2;
    int rowBarY = rowY + (rowH - barHeight) / 2;

    {
        TTF_Font *labelFont = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        int labelTextW = 0;
        if (labelFont) {
            TTF_SizeText(labelFont, labelText, &labelTextW, NULL);
        }
        int labelX = barX - labelGap - labelTextW;
        if (labelX < contentX) {
            labelX = contentX;
        }
        amiga_custom_ui_common_blitterStatsChartDrawText(ctx, labelFont, labelText, labelColor, labelX, rowLabelY);
    }

    SDL_Rect trackRect = { barX, rowBarY, barW, barHeight };
    SDL_Rect innerRect = trackRect;
    if (innerRect.w > 2) {
        innerRect.x += 1;
        innerRect.w -= 2;
    }
    if (innerRect.h > 2) {
        innerRect.y += 1;
        innerRect.h -= 2;
    }

    SDL_SetRenderDrawColor(ctx->renderer, 34, 40, 46, 255);
    SDL_RenderFillRect(ctx->renderer, &trackRect);
    if (innerRect.w > 0 && innerRect.h > 0) {
        SDL_SetRenderDrawColor(ctx->renderer, 22, 26, 31, 255);
        SDL_RenderFillRect(ctx->renderer, &innerRect);
    }

    int fillW = 0;
    if (hasData && valueMax > 0u && innerRect.w > 0) {
        uint64_t scaled = (uint64_t)valueUsed * (uint64_t)(uint32_t)innerRect.w;
        fillW = (int)((scaled + (uint64_t)(valueMax / 2u)) / (uint64_t)valueMax);
        if (fillW < 0) {
            fillW = 0;
        }
        if (fillW > innerRect.w) {
            fillW = innerRect.w;
        }
    }
    if (fillW > 0 && innerRect.h > 0) {
        SDL_Rect fillRect = { innerRect.x, innerRect.y, fillW, innerRect.h };
        if (useGradientFill) {
            amiga_custom_ui_common_blitterStatsChartFillGradient(ctx, &fillRect, innerRect.x, innerRect.w);
        } else {
            SDL_SetRenderDrawColor(ctx->renderer, solidFillColor.r, solidFillColor.g, solidFillColor.b, 255);
            SDL_RenderFillRect(ctx->renderer, &fillRect);
        }
    }

    SDL_SetRenderDrawColor(ctx->renderer, 64, 72, 82, 255);
    SDL_RenderDrawRect(ctx->renderer, &trackRect);

    if (ctx->font) {
        int tw = 0;
        int th = 0;
        if (!hasData) {
            amiga_custom_ui_common_statsChartMeasureText(ctx, ctx->font, "n/a", textColor, &tw, &th);
        } else {
            amiga_custom_ui_common_statsChartMeasureValueText(ctx,
                                                              ctx->font,
                                                              valueUsed,
                                                              valueSuffix,
                                                              textColor,
                                                              &tw,
                                                              &th);
        }
        if (tw > 0 && th > 0) {
            int tx = barX + (barW - tw) / 2;
            int ty = rowBarY + (barHeight - th) / 2;
            if (tx < barX + 2) {
                tx = barX + 2;
            }
            if (ty < rowBarY) {
                ty = rowBarY;
            }
            SDL_bool hadPrevClip = SDL_FALSE;
            SDL_Rect prevClip;
            if (useTextClip) {
                hadPrevClip = SDL_RenderIsClipEnabled(ctx->renderer);
                if (hadPrevClip) {
                    SDL_RenderGetClipRect(ctx->renderer, &prevClip);
                }
                int clipPad = e9ui_scale_px(ctx, 4);
                if (clipPad < 1) {
                    clipPad = 1;
                }
                SDL_Rect textClip = { barX + clipPad, rowBarY, barW - clipPad * 2, barHeight };
                if (textClip.w < 1) {
                    textClip.x = barX;
                    textClip.w = barW;
                }
                if (textClip.h < 1) {
                    textClip.y = rowBarY;
                    textClip.h = barHeight;
                }
                SDL_RenderSetClipRect(ctx->renderer, &textClip);
            }
            if (!hasData) {
                if (useTextShadow) {
                    amiga_custom_ui_common_blitterStatsChartDrawText(ctx, NULL, "n/a", textShadow, tx + 1, ty + 1);
                }
                amiga_custom_ui_common_blitterStatsChartDrawText(ctx, NULL, "n/a", textColor, tx, ty);
            } else {
                if (useTextShadow) {
                    amiga_custom_ui_common_statsChartDrawValueText(ctx,
                                                                   ctx->font,
                                                                   valueUsed,
                                                                   valueSuffix,
                                                                   textShadow,
                                                                   tx + 1,
                                                                   ty + 1);
                }
                amiga_custom_ui_common_statsChartDrawValueText(ctx,
                                                               ctx->font,
                                                               valueUsed,
                                                               valueSuffix,
                                                               textColor,
                                                               tx,
                                                               ty);
            }
            if (useTextClip) {
                if (hadPrevClip) {
                    SDL_RenderSetClipRect(ctx->renderer, &prevClip);
                } else {
                    SDL_RenderSetClipRect(ctx->renderer, NULL);
                }
            }
        }
    }
}


static void
amiga_custom_ui_blitter_statsChartRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) {
        return;
    }

    amiga_custom_ui_blitter_stats_chart_state_t *st = (amiga_custom_ui_blitter_stats_chart_state_t *)self->state;
    int leftInset = e9ui_scale_px(ctx, st->leftInset);
    int rightInset = e9ui_scale_px(ctx, st->rightInset);
    int topPad = e9ui_scale_px(ctx, st->topPadding);
    int bottomPad = e9ui_scale_px(ctx, st->bottomPadding);
    int rowGap = e9ui_scale_px(ctx, st->rowGap);
    int labelWidth = e9ui_scale_px(ctx, st->labelWidth);
    int labelGap = e9ui_scale_px(ctx, st->labelGap);
    int barHeight = amiga_custom_ui_common_textboxLikeHeight(ctx);
    int fontH = ctx->font ? TTF_FontHeight(ctx->font) : e9ui_scale_px(ctx, 12);

    if (fontH <= 0) {
        fontH = 12;
    }
    int contentX = self->bounds.x + leftInset;
    int contentY = self->bounds.y + topPad;
    int contentW = self->bounds.w - leftInset - rightInset;
    if (contentW < 1) {
        contentW = 1;
    }

    int rowH = fontH > barHeight ? fontH : barHeight;

    int barX = contentX + labelWidth + labelGap;
    int barW = contentW - labelWidth - labelGap;
    if (barW < 1) {
        barW = 1;
    }
    int rowY = contentY;
    uint32_t blitsMax = 300u;
    amiga_custom_ui_blitter_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     rowY,
                                     rowH,
                                     fontH,
                                     "B/W",
                                     st->hasStats,
                                     st->wordsFrame,
                                     st->maxWordsEstimateFrame,
                                     "words/frame",
                                     1,
                                     amiga_custom_ui_common_dmaColorBlitter,
                                     1,
                                     1);
    rowY += rowH + rowGap;
    amiga_custom_ui_blitter_statsChartRenderBarRow(ctx,
                                     contentX,
                                     barX,
                                     barW,
                                     barHeight,
                                     labelGap,
                                     rowY,
                                     rowH,
                                     fontH,
                                     "Blits",
                                     st->hasStats,
                                     st->blitsFrame,
                                     blitsMax,
                                     "blits/frame",
                                     1,
                                     amiga_custom_ui_common_dmaColorBlitter,
                                     1,
                                     1);
    (void)bottomPad;
}


static void
amiga_custom_ui_blitter_statsChartDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    if (self->state) {
        alloc_free(self->state);
        self->state = NULL;
    }
}


e9ui_component_t *
amiga_custom_ui_blitter_statsChartMake(void)
{
    e9ui_component_t *comp = (e9ui_component_t *)alloc_calloc(1, sizeof(*comp));
    amiga_custom_ui_blitter_stats_chart_state_t *st =
        (amiga_custom_ui_blitter_stats_chart_state_t *)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        if (comp) {
            alloc_free(comp);
        }
        if (st) {
            alloc_free(st);
        }
        return NULL;
    }

    st->leftInset = 0;
    st->rightInset = 14;
    st->topPadding = 2;
    st->bottomPadding = 2;
    st->rowGap = 4;
    st->labelWidth = 78;
    st->labelGap = 8;
    st->barHeight = 0;

    comp->name = "amiga_custom_ui_blitter_stats_chart";
    comp->state = st;
    comp->preferredHeight = amiga_custom_ui_blitter_statsChartPreferredHeight;
    comp->layout = amiga_custom_ui_blitter_statsChartLayout;
    comp->render = amiga_custom_ui_blitter_statsChartRender;
    comp->dtor = amiga_custom_ui_blitter_statsChartDtor;
    return comp;
}


void
amiga_custom_ui_blitter_updateStatsChart(amiga_custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }

    e9k_debug_ami_blitter_vis_stats_t stats;
    int hasStats = 0;

    if (libretro_host_amiga_readBlitterVisStats(&stats)) {
        hasStats = 1;
    }

    if (ui->blitterVisStatsChart) {
        amiga_custom_ui_blitter_statsChartSetValues(ui->blitterVisStatsChart,
                                             hasStats,
                                             hasStats ? stats.writesThisFrame : 0u,
                                             hasStats ? (stats.writeBytesMaxEstimateFrame / 2u) : 0u,
                                             hasStats ? stats.blitsThisFrame : 0u);
    }
}


void
amiga_custom_ui_blitter_applyOption(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    amiga_custom_ui_common_applyOption(E9K_DEBUG_OPTION_AMIGA_BLITTER, ui->blitterEnabled ? 1u : 0u);
}


void
amiga_custom_ui_blitter_applyVisDecayOption(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    amiga_custom_ui_common_applyOption(E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_DECAY, (uint32_t)ui->blitterVisDecay);
}


void
amiga_custom_ui_blitter_applyVisModeOption(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    amiga_custom_ui_common_applyOption(E9K_DEBUG_OPTION_AMIGA_BLITTER_VIS_MODE, (uint32_t)ui->blitterVisMode);
}


void
amiga_custom_ui_blitter_applyDebugOption(void)
{
    amiga_custom_ui_state_t *ui = &amiga_custom_ui_state;
    (void)libretro_host_amiga_setBlitterDebug(ui->blitterDebugEnabled ? 1 : 0);
}


void
amiga_custom_ui_blitter_syncDebugSuboptions(amiga_custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int disabled = ui->blitterDebugEnabled ? 0 : 1;
    amiga_custom_ui_common_setComponentDisabled(ui->blitterVisCollectCheckbox, disabled);
    amiga_custom_ui_common_setComponentDisabled(ui->blitterVisDecayRow, disabled);
    amiga_custom_ui_common_setComponentDisabled(ui->blitterVisDecayTextbox, disabled);
    amiga_custom_ui_common_setComponentDisabled(ui->blitterVisDecaySeekRow, disabled);
    amiga_custom_ui_common_setComponentDisabled(ui->blitterVisDecaySeekBar, disabled);
}


void
amiga_custom_ui_blitter_syncDebugCheckbox(amiga_custom_ui_state_t *ui)
{
    if (!ui) {
        return;
    }
    int enabled = 0;
    if (!libretro_host_amiga_getBlitterDebug(&enabled)) {
        return;
    }
    ui->blitterDebugEnabled = enabled ? 1 : 0;
    if (ui->blitterDebugCheckbox) {
        ui->suppressBlitterDebugCallbacks = 1;
        e9ui_checkbox_setSelected(ui->blitterDebugCheckbox, ui->blitterDebugEnabled, &ui->ctx);
        ui->suppressBlitterDebugCallbacks = 0;
    }
    amiga_custom_ui_blitter_syncDebugSuboptions(ui);
}


void
amiga_custom_ui_blitter_changed(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    amiga_custom_ui_state_t *ui = (amiga_custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    ui->blitterEnabled = selected ? 1 : 0;
    amiga_custom_ui_blitter_applyOption();
}


void
amiga_custom_ui_blitter_debugChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    amiga_custom_ui_state_t *ui = (amiga_custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (ui->suppressBlitterDebugCallbacks) {
        return;
    }
    ui->blitterDebugEnabled = selected ? 1 : 0;
    amiga_custom_ui_blitter_applyDebugOption();
    amiga_custom_ui_blitter_syncDebugSuboptions(ui);
}


void
amiga_custom_ui_blitter_visCollectChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    amiga_custom_ui_state_t *ui = (amiga_custom_ui_state_t*)user;
    if (!ui) {
        return;
    }
    if (!ui->blitterDebugEnabled) {
        return;
    }
    if (selected) {
        ui->blitterVisMode |= AMIGA_CUSTOM_UI_BLITTER_VIS_MODE_COLLECT;
    } else {
        ui->blitterVisMode &= ~AMIGA_CUSTOM_UI_BLITTER_VIS_MODE_COLLECT;
    }
    amiga_custom_ui_blitter_applyVisModeOption();
}


void
amiga_custom_ui_blitter_visDecayChanged(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)ctx;
    (void)comp;
    (void)value;
    (void)user;
}


void
amiga_custom_ui_blitter_visDecaySeekChanged(float percent, void *user)
{
    amiga_custom_ui_state_t *ui = (amiga_custom_ui_state_t*)user;
    if (!ui || !ui->blitterDebugEnabled) {
        return;
    }
    int nextDecay = amiga_custom_ui_blitter_visDecayFromPercent(percent);
    if (nextDecay == ui->blitterVisDecay) {
        return;
    }
    ui->blitterVisDecay = nextDecay;
    amiga_custom_ui_blitter_applyVisDecayOption();
    if (ui->blitterVisDecayRow) {
        char decayText[16];
        snprintf(decayText, sizeof(decayText), "%d", ui->blitterVisDecay);
        e9ui_labeled_textbox_setText(ui->blitterVisDecayRow, decayText);
    }
    amiga_custom_ui_blitter_syncVisDecaySeekBar(ui);
}


void
amiga_custom_ui_blitter_visDecaySeekTooltip(float percent, char *out, size_t cap, void *user)
{
    (void)user;
    if (!out || cap == 0) {
        return;
    }
    int decay = amiga_custom_ui_blitter_visDecayFromPercent(percent);
    snprintf(out, cap, "Decay %d", decay);
}


static void
amiga_custom_ui_blitter_commitVisDecayTextbox(amiga_custom_ui_state_t *ui)
{
    if (!ui || !ui->blitterVisDecayTextbox) {
        return;
    }
    if (!ui->blitterDebugEnabled) {
        return;
    }

    const char *value = e9ui_textbox_getText(ui->blitterVisDecayTextbox);
    int nextDecay = 0;
    if (!value || sscanf(value, "%d", &nextDecay) != 1) {
        return;
    }
    if (nextDecay <= 0 || nextDecay > AMIGA_CUSTOM_UI_BLITTER_VIS_DECAY_MAX) {
        return;
    }
    if (nextDecay == ui->blitterVisDecay) {
        return;
    }
    ui->blitterVisDecay = nextDecay;
    amiga_custom_ui_blitter_applyVisDecayOption();
    amiga_custom_ui_blitter_syncVisDecaySeekBar(ui);
}


int
amiga_custom_ui_blitter_visDecayTextboxKey(e9ui_context_t *ctx, SDL_Keycode key, SDL_Keymod mods, void *user)
{
    (void)ctx;
    (void)mods;
    amiga_custom_ui_state_t *ui = (amiga_custom_ui_state_t*)user;
    if (!ui || !ui->blitterVisDecayTextbox) {
        return 0;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        amiga_custom_ui_blitter_commitVisDecayTextbox(ui);
    }
    return 0;
}


void
amiga_custom_ui_blitter_tickVisDecayTextbox(amiga_custom_ui_state_t *ui)
{
    if (!ui || !ui->blitterVisDecayTextbox) {
        return;
    }
    int hasFocus = e9ui_getFocus(&ui->ctx) == ui->blitterVisDecayTextbox ? 1 : 0;
    if (ui->blitterVisDecayTextboxHadFocus && !hasFocus) {
        amiga_custom_ui_blitter_commitVisDecayTextbox(ui);
    }
    ui->blitterVisDecayTextboxHadFocus = hasFocus;
}
