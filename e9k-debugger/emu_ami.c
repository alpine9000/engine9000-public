/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "debugger.h"
#include "debugger_input_bindings.h"
#include "e9ui.h"
#include "e9ui_range_bar.h"
#include "amiga_blittervis.h"
#include "amiga_memview.h"
#include "amiga_custom.h"
#include "amiga_custom_log.h"
#include "amiga_custom_ui.h"
#include "amiga_custom_regs.h"
#include "profile_checkpoints.h"
#include "libretro.h"
#include "libretro_host.h"
#include "ui.h"


#define EMU_AMI_SPRITE_VIS_POINTS_CAP_DEFAULT (2304u * 1620u)
#define EMU_AMI_DMA_DEBUG_ALPHA 0xa0u
#define EMU_AMI_DMA_RECORD_REFRESH 1u
#define EMU_AMI_DMA_RECORD_CPU 2u
#define EMU_AMI_DMA_RECORD_COPPER 3u
#define EMU_AMI_DMA_RECORD_AUDIO 4u
#define EMU_AMI_DMA_RECORD_BLITTER 5u
#define EMU_AMI_DMA_RECORD_BITPLANE 6u
#define EMU_AMI_DMA_RECORD_SPRITE 7u
#define EMU_AMI_DMA_RECORD_DISK 8u
#define EMU_AMI_DMA_RECORD_CONFLICT 9u
#define EMU_AMI_COPPER_HIT_MARGIN_SCALE 3

typedef struct emu_ami_sprite_vis_cache {
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    uint32_t *pixels;
    size_t pixelsCap;
    uint8_t *spriteIds;
    size_t spriteIdsCap;
    uint32_t normalSpriteMask;
    uint32_t attachedPairMask;
    int texWidth;
    int texHeight;
    e9k_debug_ami_sprite_vis_point_t *points;
    size_t pointsCap;
} emu_ami_sprite_vis_cache_t;

typedef struct emu_ami_dma_debug_cache {
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    uint32_t *pixels;
    size_t pixelsCap;
    int texWidth;
    int texHeight;
    int frameNumber;
} emu_ami_dma_debug_cache_t;

typedef struct emu_ami_copper_debug_cache {
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    uint32_t *pixels;
    size_t pixelsCap;
    int texWidth;
    int texHeight;
    int frameNumber;
} emu_ami_copper_debug_cache_t;

typedef struct emu_ami_copper_legend_entry {
    const char *label;
    uint32_t color;
} emu_ami_copper_legend_entry_t;

static emu_ami_sprite_vis_cache_t emu_ami_spriteVisCache = {0};
static emu_ami_dma_debug_cache_t emu_ami_dmaDebugCache = {0};
static emu_ami_copper_debug_cache_t emu_ami_copperDebugCache = {0};
static int emu_ami_copperDebugForcedDma = 0;
static int emu_ami_copperDebugPrevDmaMode = 0;
static int emu_ami_customLogCallbackBound = 0;
static SDL_Rect emu_ami_copperLegendOuterDst = { 0, 0, 0, 0 };
static int emu_ami_legendReservedHeight = 0;

static uint32_t
emu_ami_spriteVisColorFromIndex(uint32_t spriteIndex);

static uint32_t
emu_ami_spriteVisAttachedColorFromIndex(uint32_t spriteIndex);

static int
emu_ami_isCopperDebugEnabled(void);

static int
emu_ami_isPaletteRegOffset(uint16_t regOffset)
{
    uint16_t normalized = (uint16_t)(regOffset & 0x01feu);
    return normalized >= 0x0180u && normalized <= 0x01beu;
}

static SDL_Color
emu_ami_paletteSwatchColor(uint16_t value)
{
    uint8_t r = (uint8_t)(((value >> 8) & 0x0fu) * 17u);
    uint8_t g = (uint8_t)(((value >> 4) & 0x0fu) * 17u);
    uint8_t b = (uint8_t)((value & 0x0fu) * 17u);
    SDL_Color color = { r, g, b, 255 };
    return color;
}

static int
emu_ami_measureTextWidth(TTF_Font *font, const char *text)
{
    int width = 0;

    if (!font || !text || !*text) {
        return 0;
    }
    if (TTF_SizeUTF8(font, text, &width, NULL) != 0) {
        return 0;
    }
    return width;
}

static int
emu_ami_copperLegendReserveHeight(int height)
{
    int reserve = 0;

    if (height <= 0) {
        return 0;
    }
    reserve = height / 5;
    if (reserve < 44) {
        reserve = 44;
    }
    if (reserve > 96) {
        reserve = 96;
    }
    if (reserve >= height / 2) {
        reserve = height / 2;
    }
    return reserve;
}

static int
emu_ami_isSpriteVisEnabled(void)
{
    int enabled = 0;

    return libretro_host_amiga_getSpriteVis(&enabled) && enabled ? 1 : 0;
}

static int
emu_ami_hasAttachedSpriteLegend(void)
{
    return emu_ami_isSpriteVisEnabled() && emu_ami_spriteVisCache.attachedPairMask != 0u;
}

static int
emu_ami_getLegendSlotCount(void)
{
    int count = 0;

    if (emu_ami_isCopperDebugEnabled()) {
        count++;
    }
    if (emu_ami_isSpriteVisEnabled()) {
        count++;
    }
    if (emu_ami_hasAttachedSpriteLegend()) {
        count++;
    }
    return count;
}

static int
emu_ami_getLegendReserveHeight(int height)
{
    int slotCount = emu_ami_getLegendSlotCount();
    int reserve = 0;

    if (slotCount <= 0) {
        return 0;
    }
    reserve = emu_ami_copperLegendReserveHeight(height) * slotCount;
    if (reserve >= height / 2) {
        reserve = height / 2;
    }
    return reserve;
}

static void
emu_ami_drawText(e9ui_context_t *ctx, TTF_Font *font, const char *text, SDL_Color color, int x, int y)
{
    int lineHeight = 0;

    if (!ctx || !font || !text || !*text) {
        return;
    }
    lineHeight = TTF_FontHeight(font);
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    e9ui_drawSelectableText(ctx, NULL, font, text, color, x, y, lineHeight, 0, NULL, 0, 0);
}

static uint32_t
emu_ami_copperColorForMoveOffset(uint16_t regOffset)
{
    const char *regName = amiga_custom_regs_nameForOffset(regOffset);

    if (!regName || !*regName) {
        return 0x00000000u;
    }
    if (strncmp(regName, "SPR", 3) == 0) {
        return 0x00ffffffu;
    }
    if (strncmp(regName, "BLT", 3) == 0) {
        return 0x000000ffu;
    }
    if (strncmp(regName, "BPL", 3) == 0 && strstr(regName, "PT")) {
        return 0x00ff00ffu;
    }
    if (strncmp(regName, "COLOR", 5) == 0) {
        return 0x0000ff00u;
    }
    return 0x00ffff00u;
}

static uint32_t
emu_ami_copperDebugArgb(uint32_t rgb)
{
    return 0xff000000u | (rgb & 0x00ffffffu);
}

static uint32_t
emu_ami_copperColorForRecord(const e9k_debug_ami_copper_debug_raw_record_t *rec)
{
    uint32_t insn = 0;
    uint32_t insnType = 0;

    if (!rec) {
        return 0x00000000u;
    }
    insn = ((uint32_t)rec->w1 << 16) | (uint32_t)rec->w2;
    insnType = insn & 0x00010001u;
    if (insnType == 0x00010000u || insnType == 0x00010001u) {
        return 0x00ff0000u;
    }
    return emu_ami_copperColorForMoveOffset((uint16_t)(rec->w1 & 0x01feu));
}

static int
emu_ami_isCopperPaletteOffset(uint16_t regOffset)
{
    const char *regName = amiga_custom_regs_nameForOffset(regOffset);

    if (!regName || !*regName) {
        return 0;
    }
    return strncmp(regName, "COLOR", 5) == 0 ? 1 : 0;
}

static void
emu_ami_renderLegend(e9ui_context_t *ctx,
                     const SDL_Rect *videoDst,
                     const emu_ami_copper_legend_entry_t *entries,
                     size_t count,
                     int slotIndex,
                     int slotCount)
{
    TTF_Font *font = NULL;
    int lineHeight = 0;
    int pad = 0;
    int swatch = 0;
    int gap = 0;
    int rowGap = 0;
    int legendTop = 0;
    int legendHeight = 0;
    int legendWidth = 0;
    int legendX = 0;
    int maxWidth = 0;
    int maxRowWidth = 0;
    int outerPad = 0;
    int rowCount = 1;
    int rowWidths[8] = { 0 };
    int rowItems[8] = { 0 };
    int itemWidths[8] = { 0 };
    SDL_Color textColor = { 232, 232, 236, 255 };
    int footerTop = 0;
    int footerBottom = 0;
    int footerHeight = 0;
    int slotTop = 0;
    int slotBottom = 0;
    int slotHeight = 0;

    if (!ctx || !ctx->renderer || !videoDst || !entries || count == 0u) {
        return;
    }
    if (slotCount <= 0 || slotIndex < 0 || slotIndex >= slotCount) {
        return;
    }

    footerTop = videoDst->y + videoDst->h + e9ui_scale_px(ctx, 6);
    footerBottom = emu_ami_copperLegendOuterDst.y + emu_ami_copperLegendOuterDst.h;
    if (footerTop >= footerBottom) {
        int overlayPad = e9ui_scale_px(ctx, 8);
        int overlayHeight = emu_ami_copperLegendReserveHeight(videoDst->h) * slotCount;

        if (overlayHeight >= videoDst->h / 2) {
            overlayHeight = videoDst->h / 2;
        }
        if (overlayHeight <= 0) {
            return;
        }
        footerBottom = videoDst->y + videoDst->h - overlayPad;
        footerTop = footerBottom - overlayHeight;
        if (footerTop < videoDst->y + overlayPad) {
            footerTop = videoDst->y + overlayPad;
        }
        if (footerTop >= footerBottom) {
            return;
        }
    }
    footerHeight = footerBottom - footerTop;
    slotTop = footerTop + (footerHeight * slotIndex) / slotCount;
    slotBottom = footerTop + (footerHeight * (slotIndex + 1)) / slotCount;
    slotHeight = slotBottom - slotTop;
    if (slotHeight <= 0) {
        return;
    }

    font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    lineHeight = font ? TTF_FontHeight(font) : 16;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    pad = e9ui_scale_px(ctx, 6);
    swatch = lineHeight - e9ui_scale_px(ctx, 4);
    if (swatch < e9ui_scale_px(ctx, 8)) {
        swatch = e9ui_scale_px(ctx, 8);
    }
    gap = e9ui_scale_px(ctx, 12);
    rowGap = e9ui_scale_px(ctx, 4);
    outerPad = e9ui_scale_px(ctx, 6);
    maxWidth = emu_ami_copperLegendOuterDst.w - outerPad * 2 - pad * 2;
    if (maxWidth <= 0) {
        return;
    }

    for (size_t i = 0; i < count && i < (sizeof(itemWidths) / sizeof(itemWidths[0])); ++i) {
        int textWidth = emu_ami_measureTextWidth(font, entries[i].label);
        int itemWidth = swatch + e9ui_scale_px(ctx, 6) + textWidth;

        itemWidths[i] = itemWidth;
        if (rowWidths[rowCount - 1] > 0 && rowWidths[rowCount - 1] + gap + itemWidth > maxWidth && rowCount < 8) {
            rowCount++;
        }
        if (rowWidths[rowCount - 1] > 0) {
            rowWidths[rowCount - 1] += gap;
        }
        rowWidths[rowCount - 1] += itemWidth;
        rowItems[rowCount - 1]++;
    }
    for (int row = 0; row < rowCount; ++row) {
        if (rowWidths[row] > maxRowWidth) {
            maxRowWidth = rowWidths[row];
        }
    }

    legendHeight = pad * 2 + rowCount * lineHeight + (rowCount - 1) * rowGap;
    if (legendHeight > slotHeight) {
        legendHeight = slotHeight;
    }
    if (legendHeight <= lineHeight) {
        return;
    }
    legendWidth = maxRowWidth + pad * 2;
    if (legendWidth > emu_ami_copperLegendOuterDst.w - outerPad * 2) {
        legendWidth = emu_ami_copperLegendOuterDst.w - outerPad * 2;
    }
    if (legendWidth <= 0) {
        return;
    }
    legendX = emu_ami_copperLegendOuterDst.x + (emu_ami_copperLegendOuterDst.w - legendWidth) / 2;
    if (legendX < emu_ami_copperLegendOuterDst.x + outerPad) {
        legendX = emu_ami_copperLegendOuterDst.x + outerPad;
    }
    legendTop = slotTop + (slotHeight - legendHeight) / 2;
    if (legendTop < slotTop) {
        legendTop = slotTop;
    }

    SDL_Rect bg = { legendX, legendTop, legendWidth, legendHeight };
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 18, 216);
    SDL_RenderFillRect(ctx->renderer, &bg);
    SDL_SetRenderDrawColor(ctx->renderer, 72, 72, 80, 255);
    SDL_RenderDrawRect(ctx->renderer, &bg);

    int itemIndex = 0;
    int contentBottom = bg.y + bg.h - pad;
    int y = legendTop + pad;
    for (int row = 0; row < rowCount; ++row) {
        if (y + lineHeight > contentBottom) {
            break;
        }
        int x = legendX + (legendWidth - rowWidths[row]) / 2;
        if (x < legendX + pad) {
            x = legendX + pad;
        }
        for (int col = 0; col < rowItems[row] && itemIndex < (int)count; ++col, ++itemIndex) {
            SDL_Rect swatchRect = { x, y + (lineHeight - swatch) / 2, swatch, swatch };
            uint32_t color = entries[itemIndex].color;

            SDL_SetRenderDrawColor(ctx->renderer,
                                   (uint8_t)((color >> 16) & 0xffu),
                                   (uint8_t)((color >> 8) & 0xffu),
                                   (uint8_t)(color & 0xffu),
                                   220);
            SDL_RenderFillRect(ctx->renderer, &swatchRect);
            SDL_SetRenderDrawColor(ctx->renderer, 220, 220, 220, 255);
            SDL_RenderDrawRect(ctx->renderer, &swatchRect);

            emu_ami_drawText(ctx,
                             font,
                             entries[itemIndex].label,
                             textColor,
                             x + swatch + e9ui_scale_px(ctx, 6),
                             y);
            x += itemWidths[itemIndex] + gap;
        }
        y += lineHeight + rowGap;
    }
}

static void
emu_ami_renderCopperLegend(e9ui_context_t *ctx, const SDL_Rect *videoDst)
{
    static const emu_ami_copper_legend_entry_t entries[] = {
        { "WAIT/SKIP", 0x00ff0000u },
        { "MOVE SPR*", 0x00ffffffu },
        { "MOVE BLT*", 0x000000ffu },
        { "MOVE BPL*PT*", 0x00ff00ffu },
        { "MOVE COLOR*", 0x0000ff00u },
        { "MOVE OTHER", 0x00ffff00u }
    };

    if (!ctx || !ctx->renderer || !videoDst || !emu_ami_isCopperDebugEnabled()) {
        return;
    }
    emu_ami_renderLegend(ctx,
                         videoDst,
                         entries,
                         sizeof(entries) / sizeof(entries[0]),
                         0,
                         emu_ami_getLegendSlotCount());
}

static void
emu_ami_renderSpriteLegend(e9ui_context_t *ctx, const SDL_Rect *videoDst)
{
    emu_ami_copper_legend_entry_t entries[8];
    char labels[8][8];
    emu_ami_copper_legend_entry_t attachedEntries[4];
    char attachedLabels[4][16];
    uint32_t attachedPairMask = emu_ami_spriteVisCache.attachedPairMask;
    int slotIndex = emu_ami_isCopperDebugEnabled() ? 1 : 0;
    size_t attachedCount = 0u;

    if (!ctx || !ctx->renderer || !videoDst || !emu_ami_isSpriteVisEnabled()) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        snprintf(labels[i], sizeof(labels[i]), "SPR%d", i);
        entries[i].label = labels[i];
        entries[i].color = emu_ami_spriteVisColorFromIndex((uint32_t)i) & 0x00ffffffu;
    }
    emu_ami_renderLegend(ctx, videoDst, entries, 8u, slotIndex, emu_ami_getLegendSlotCount());
    if (!attachedPairMask) {
        return;
    }
    for (int pairIndex = 0; pairIndex < 4; ++pairIndex) {
        int spriteIndex = pairIndex * 2;
        snprintf(attachedLabels[attachedCount], sizeof(attachedLabels[attachedCount]), "ATT %d+%d", spriteIndex, spriteIndex + 1);
        attachedEntries[attachedCount].label = attachedLabels[attachedCount];
        attachedEntries[attachedCount].color = emu_ami_spriteVisAttachedColorFromIndex((uint32_t)spriteIndex) & 0x00ffffffu;
        attachedCount++;
    }
    emu_ami_renderLegend(ctx, videoDst, attachedEntries, attachedCount, slotIndex + 1, emu_ami_getLegendSlotCount());
}

static const char *
emu_ami_mouseCaptureOptionKey(void)
{
    return "e9k_debugger_amiga_mouse_capture";
}

static int
emu_ami_percentToVideoLine(float percent, int lineCount)
{
    float clamped = percent;

    if (lineCount <= 1) {
        return 0;
    }
    if (clamped < 0.0f) {
        clamped = 0.0f;
    }
    if (clamped > 1.0f) {
        clamped = 1.0f;
    }
    return (int)(clamped * (float)(lineCount - 1) + 0.5f);
}

static float
emu_ami_videoLineToPercent(int videoLine, int lineCount)
{
    int clamped = videoLine;

    if (lineCount <= 1) {
        return 0.0f;
    }
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped >= lineCount) {
        clamped = lineCount - 1;
    }
    return (float)clamped / (float)(lineCount - 1);
}

static int
emu_ami_rangeBarGetCoreRangeFromPercent(float startPercent,
                                        float endPercent,
                                        int *outStartLine,
                                        int *outEndLine)
{
    int lineCount = 0;
    int startVideoLine = 0;
    int endVideoLine = 0;
    int startLine = -1;
    int endLine = -1;

    if (!libretro_host_amiga_getVideoLineCount(&lineCount) || lineCount <= 0) {
        return 0;
    }
    startVideoLine = emu_ami_percentToVideoLine(startPercent, lineCount);
    endVideoLine = emu_ami_percentToVideoLine(endPercent, lineCount);
    if (!libretro_host_amiga_videoLineToCoreLine(startVideoLine, &startLine)) {
        return 0;
    }
    if (!libretro_host_amiga_videoLineToCoreLine(endVideoLine, &endLine)) {
        return 0;
    }
    if (endLine < startLine) {
        int temp = startLine;
        startLine = endLine;
        endLine = temp;
    }
    if (outStartLine) {
        *outStartLine = startLine;
    }
    if (outEndLine) {
        *outEndLine = endLine;
    }
    return 1;
}

static int
emu_ami_rangeBarSetPercentFromCoreLines(e9ui_component_t *bar, int startLine, int endLine)
{
    int lineCount = 0;
    int startVideoLine = -1;
    int endVideoLine = -1;
    float startPercent = 0.0f;
    float endPercent = 0.0f;

    if (!bar) {
        return 0;
    }
    if (!libretro_host_amiga_getVideoLineCount(&lineCount) || lineCount <= 0) {
        return 0;
    }
    if (!libretro_host_amiga_coreLineToVideoLine(startLine, &startVideoLine)) {
        return 0;
    }
    if (!libretro_host_amiga_coreLineToVideoLine(endLine, &endVideoLine)) {
        return 0;
    }
    startPercent = emu_ami_videoLineToPercent(startVideoLine, lineCount);
    endPercent = emu_ami_videoLineToPercent(endVideoLine, lineCount);
    if (endPercent < startPercent) {
        float temp = startPercent;
        startPercent = endPercent;
        endPercent = temp;
    }
    e9ui_range_bar_setRangePercent(bar, startPercent, endPercent);
    return 1;
}

int
emu_ami_mouseCaptureOnClickEnabled(void)
{
    const char *settingValue = NULL;
    const char *customValue = NULL;
    const char *coreValue = NULL;

    if (!target) {
        return 0;
    }
    if (target->romConfigGetActiveCustomOptionValue) {
        customValue = target->romConfigGetActiveCustomOptionValue(emu_ami_mouseCaptureOptionKey());
        settingValue = customValue;
    }
    if (!settingValue && target->coreOptionGetValue) {
        coreValue = target->coreOptionGetValue(emu_ami_mouseCaptureOptionKey());
        settingValue = coreValue;
    }
    if (settingValue && strcmp(settingValue, "disabled") == 0) {
        return 0;
    }
    return 1;
}

size_t
emu_ami_rangeBarCount(void)
{
    return 2;
}

int
emu_ami_rangeBarDescribe(size_t index, emu_range_bar_desc_t *outDesc)
{
    if (!outDesc) {
        return 0;
    }
    memset(outDesc, 0, sizeof(*outDesc));
    if (index == 0) {
        outDesc->metaKey = "range_bar_left";
        outDesc->side = (int)e9ui_range_bar_sideLeft;
    } else if (index == 1) {
        outDesc->metaKey = "range_bar_right";
        outDesc->side = (int)e9ui_range_bar_sideRight;
    } else {
        return 0;
    }
    outDesc->marginTop = 10;
    outDesc->marginBottom = 10;
    outDesc->marginSide = 10;
    outDesc->width = 12;
    outDesc->hoverMargin = 18;
    return 1;
}

void
emu_ami_rangeBarChanged(size_t index, float startPercent, float endPercent)
{
    int startLine = -1;
    int endLine = -1;

    if (!emu_ami_rangeBarGetCoreRangeFromPercent(startPercent, endPercent, &startLine, &endLine)) {
        return;
    }
    if (index == 1) {
        amiga_custom_ui_setCopperLimitRange(startLine, endLine);
        return;
    }
    if (index == 0) {
        amiga_custom_ui_setBplptrLineLimitRange(startLine, endLine);
    }
}

void
emu_ami_rangeBarDragging(size_t index, int dragging, float startPercent, float endPercent)
{
    (void)index;
    (void)dragging;
    (void)startPercent;
    (void)endPercent;
}

void
emu_ami_rangeBarTooltip(size_t index, float startPercent, float endPercent, char *out, size_t cap)
{
    int startLine = -1;
    int endLine = -1;
    const char *label = "BPLPTR";

    if (!out || cap == 0) {
        return;
    }
    if (!emu_ami_rangeBarGetCoreRangeFromPercent(startPercent, endPercent, &startLine, &endLine)) {
        return;
    }
    if (index == 1) {
        label = "Copper";
    }
    snprintf(out, cap, "%s %d..%d", label, startLine, endLine);
}

int
emu_ami_rangeBarSync(size_t index, e9ui_component_t *bar)
{
    int enabled = 0;
    int startLine = 0;
    int endLine = 0;

    if (!bar) {
        return 0;
    }
    if (!amiga_custom_ui_isOpen()) {
        e9ui_setHidden(bar, 1);
        return 0;
    }
    if (index == 1) {
        enabled = amiga_custom_ui_getCopperLimitEnabled();
        if (!amiga_custom_ui_getCopperLimitRange(&startLine, &endLine)) {
            enabled = 0;
        }
    } else if (index == 0) {
        enabled = amiga_custom_ui_getBplptrBlockEnabled();
        if (!amiga_custom_ui_getBplptrLineLimitRange(&startLine, &endLine)) {
            enabled = 0;
        }
    } else {
        enabled = 0;
    }
    if (!enabled) {
        e9ui_setHidden(bar, 1);
        return 0;
    }
    if (!emu_ami_rangeBarSetPercentFromCoreLines(bar, startLine, endLine)) {
        e9ui_setHidden(bar, 1);
        return 0;
    }
    return 1;
}

static void
emu_ami_onCustomLogFrame(const e9k_debug_ami_custom_log_entry_t *entries,
                         size_t count,
                         uint32_t dropped,
                         uint64_t frameNo,
                         void *user)
{
    (void)user;
    amiga_custom_log_captureFrame(entries, count, dropped, frameNo);
}

static void
emu_ami_tryBindCustomLogFrameCallback(void)
{
    if (emu_ami_customLogCallbackBound) {
        return;
    }
    if (libretro_host_amiga_setCustomLogFrameCallback(emu_ami_onCustomLogFrame, NULL)) {
        emu_ami_customLogCallbackBound = 1;
    }
}

static void
emu_ami_destroy(void)
{
    amiga_blittervis_destroy();
    if (emu_ami_spriteVisCache.texture) {
        SDL_DestroyTexture(emu_ami_spriteVisCache.texture);
        emu_ami_spriteVisCache.texture = NULL;
    }
    free(emu_ami_spriteVisCache.pixels);
    free(emu_ami_spriteVisCache.spriteIds);
    free(emu_ami_spriteVisCache.points);
    memset(&emu_ami_spriteVisCache, 0, sizeof(emu_ami_spriteVisCache));
    if (emu_ami_dmaDebugCache.texture) {
        SDL_DestroyTexture(emu_ami_dmaDebugCache.texture);
        emu_ami_dmaDebugCache.texture = NULL;
    }
    free(emu_ami_dmaDebugCache.pixels);
    memset(&emu_ami_dmaDebugCache, 0, sizeof(emu_ami_dmaDebugCache));
    if (emu_ami_copperDebugCache.texture) {
        SDL_DestroyTexture(emu_ami_copperDebugCache.texture);
        emu_ami_copperDebugCache.texture = NULL;
    }
    free(emu_ami_copperDebugCache.pixels);
    memset(&emu_ami_copperDebugCache, 0, sizeof(emu_ami_copperDebugCache));
    emu_ami_customLogCallbackBound = 0;
}

static uint32_t
emu_ami_spriteVisColorFromIndex(uint32_t spriteIndex)
{
    static const uint32_t palette[] = {
        0x00ff4d4du,
        0x004dc3ffu,
        0x006dff4du,
        0x00ffe14du,
        0x00ff4dbeu,
        0x00ff8a33u,
        0x008c4dffu,
        0x004dffd9u
    };
    uint32_t rgb = palette[spriteIndex & 7u];

    return (uint32_t)(0xb0u << 24) | rgb;
}

static uint32_t
emu_ami_spriteVisAttachedColorFromIndex(uint32_t spriteIndex)
{
    static const uint32_t palette[] = {
        0x00f5f5f5u,
        0x00ff9f1cu,
        0x002ec4b6u,
        0x00e71d36u
    };
    uint32_t rgb = palette[(spriteIndex >> 1) & 3u];

    return (uint32_t)(0xb0u << 24) | rgb;
}

static uint32_t
emu_ami_dmaDebugArgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)(EMU_AMI_DMA_DEBUG_ALPHA << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t
emu_ami_dmaDebugColorForRecord(const e9k_debug_ami_dma_debug_raw_record_t *rec)
{
    if (!rec) {
        return 0u;
    }
    if (rec->cf_reg != 0xffffu) {
        return emu_ami_dmaDebugArgb(0xffu, 0xb8u, 0x40u);
    }
    switch ((uint16_t)rec->type) {
    case EMU_AMI_DMA_RECORD_REFRESH:
        return emu_ami_dmaDebugArgb(0x44u, 0x44u, 0x44u);
    case EMU_AMI_DMA_RECORD_CPU:
        if (rec->extra == 1u) {
            return emu_ami_dmaDebugArgb(0xadu, 0x98u, 0xd6u);
        }
        return emu_ami_dmaDebugArgb(0xa2u, 0x53u, 0x42u);
    case EMU_AMI_DMA_RECORD_COPPER:
        if ((rec->extra & 7u) == 1u) {
            return emu_ami_dmaDebugArgb(0xaau, 0xaau, 0x22u);
        }
        if ((rec->extra & 7u) == 2u) {
            return emu_ami_dmaDebugArgb(0x66u, 0x66u, 0x44u);
        }
        return emu_ami_dmaDebugArgb(0xeeu, 0xeeu, 0x00u);
    case EMU_AMI_DMA_RECORD_AUDIO:
        return emu_ami_dmaDebugArgb(0xffu, 0x00u, 0x00u);
    case EMU_AMI_DMA_RECORD_BLITTER:
        if ((rec->extra & 0x20u) != 0u) {
            return emu_ami_dmaDebugArgb(0x00u, 0xffu, 0x00u);
        }
        if ((rec->extra & 0x10u) != 0u) {
            return emu_ami_dmaDebugArgb(0x00u, 0x88u, 0xffu);
        }
        if ((rec->extra & 7u) == 3u) {
            return emu_ami_dmaDebugArgb(0x00u, 0xaau, 0x88u);
        }
        return emu_ami_dmaDebugArgb(0x00u, 0x88u, 0x88u);
    case EMU_AMI_DMA_RECORD_BITPLANE:
        return emu_ami_dmaDebugArgb(0x00u, 0x00u, 0xffu);
    case EMU_AMI_DMA_RECORD_SPRITE:
        return emu_ami_dmaDebugArgb(0xffu, 0x00u, 0xffu);
    case EMU_AMI_DMA_RECORD_DISK:
        return emu_ami_dmaDebugArgb(0xffu, 0xffu, 0xffu);
    case EMU_AMI_DMA_RECORD_CONFLICT:
        return emu_ami_dmaDebugArgb(0xffu, 0xb8u, 0x40u);
    default:
        break;
    }
    return 0u;
}

static int
emu_ami_scaleAxis(int value, int srcExtent, int dstExtent)
{
    if (srcExtent <= 0 || dstExtent <= 0 || value == 0) {
        return 0;
    }
    return (int)(((int64_t)value * (int64_t)dstExtent + (int64_t)(srcExtent / 2)) / (int64_t)srcExtent);
}

static int
emu_ami_dmaDebugEnsureTexture(emu_ami_dma_debug_cache_t *cache,
                              e9ui_context_t *ctx,
                              int textureWidth,
                              int textureHeight)
{
    if (!cache || !ctx || !ctx->renderer || textureWidth <= 0 || textureHeight <= 0) {
        return 0;
    }
    if (cache->renderer != ctx->renderer) {
        if (cache->texture) {
            SDL_DestroyTexture(cache->texture);
            cache->texture = NULL;
        }
        cache->renderer = ctx->renderer;
        cache->texWidth = 0;
        cache->texHeight = 0;
        cache->frameNumber = -1;
    }
    if (!cache->texture ||
        cache->texWidth != textureWidth ||
        cache->texHeight != textureHeight) {
        if (cache->texture) {
            SDL_DestroyTexture(cache->texture);
            cache->texture = NULL;
        }
        cache->texture = SDL_CreateTexture(ctx->renderer,
                                           SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           textureWidth,
                                           textureHeight);
        if (!cache->texture) {
            return 0;
        }
        cache->texWidth = textureWidth;
        cache->texHeight = textureHeight;
        cache->frameNumber = -1;
    }
    size_t pixelCount = (size_t)textureWidth * (size_t)textureHeight;
    if (pixelCount > cache->pixelsCap) {
        uint32_t *nextPixels = (uint32_t *)realloc(cache->pixels, pixelCount * sizeof(*nextPixels));
        if (!nextPixels) {
            return 0;
        }
        cache->pixels = nextPixels;
        cache->pixelsCap = pixelCount;
    }
    return 1;
}

static int
emu_ami_copperDebugEnsureTexture(emu_ami_copper_debug_cache_t *cache,
                                 e9ui_context_t *ctx,
                                 int textureWidth,
                                 int textureHeight)
{
    if (!cache || !ctx || !ctx->renderer || textureWidth <= 0 || textureHeight <= 0) {
        return 0;
    }
    if (cache->renderer != ctx->renderer) {
        if (cache->texture) {
            SDL_DestroyTexture(cache->texture);
            cache->texture = NULL;
        }
        cache->renderer = ctx->renderer;
        cache->texWidth = 0;
        cache->texHeight = 0;
        cache->frameNumber = -1;
    }
    if (!cache->texture ||
        cache->texWidth != textureWidth ||
        cache->texHeight != textureHeight) {
        if (cache->texture) {
            SDL_DestroyTexture(cache->texture);
            cache->texture = NULL;
        }
        cache->texture = SDL_CreateTexture(ctx->renderer,
                                           SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           textureWidth,
                                           textureHeight);
        if (!cache->texture) {
            return 0;
        }
        cache->texWidth = textureWidth;
        cache->texHeight = textureHeight;
        cache->frameNumber = -1;
    }
    size_t pixelCount = (size_t)textureWidth * (size_t)textureHeight;
    if (pixelCount > cache->pixelsCap) {
        uint32_t *nextPixels = (uint32_t *)realloc(cache->pixels, pixelCount * sizeof(*nextPixels));
        if (!nextPixels) {
            return 0;
        }
        cache->pixels = nextPixels;
        cache->pixelsCap = pixelCount;
    }
    return 1;
}

static void
emu_ami_renderSpriteVisOverlay(e9ui_context_t *ctx, SDL_Rect *dst)
{
    int enabled = 0;
    uint32_t srcWidth = 0;
    uint32_t srcHeight = 0;

    if (!libretro_host_amiga_getSpriteVis(&enabled) || !enabled) {
        return;
    }
    if (!emu_ami_spriteVisCache.pointsCap) {
        emu_ami_spriteVisCache.pointsCap = EMU_AMI_SPRITE_VIS_POINTS_CAP_DEFAULT;
        emu_ami_spriteVisCache.points =
            (e9k_debug_ami_sprite_vis_point_t *)realloc(emu_ami_spriteVisCache.points,
                                                        emu_ami_spriteVisCache.pointsCap * sizeof(*emu_ami_spriteVisCache.points));
        if (!emu_ami_spriteVisCache.points) {
            emu_ami_spriteVisCache.pointsCap = 0u;
            return;
        }
    }

    size_t fetchedCount = libretro_host_amiga_readSpriteVisPoints(emu_ami_spriteVisCache.points,
                                                                    emu_ami_spriteVisCache.pointsCap,
                                                                    &srcWidth,
                                                                    &srcHeight);
    if (fetchedCount > emu_ami_spriteVisCache.pointsCap) {
        e9k_debug_ami_sprite_vis_point_t *nextPoints =
            (e9k_debug_ami_sprite_vis_point_t *)realloc(emu_ami_spriteVisCache.points,
                                                        fetchedCount * sizeof(*nextPoints));
        if (!nextPoints) {
            return;
        }
        emu_ami_spriteVisCache.points = nextPoints;
        emu_ami_spriteVisCache.pointsCap = fetchedCount;
        fetchedCount = libretro_host_amiga_readSpriteVisPoints(emu_ami_spriteVisCache.points,
                                                                 emu_ami_spriteVisCache.pointsCap,
                                                                 &srcWidth,
                                                                 &srcHeight);
    }
    if (!srcWidth || !srcHeight) {
        return;
    }

    int textureWidth = (int)srcWidth;
    int textureHeight = (int)srcHeight;
    if (textureWidth <= 0 || textureHeight <= 0) {
        return;
    }
    if (emu_ami_spriteVisCache.renderer != ctx->renderer) {
        if (emu_ami_spriteVisCache.texture) {
            SDL_DestroyTexture(emu_ami_spriteVisCache.texture);
            emu_ami_spriteVisCache.texture = NULL;
        }
        emu_ami_spriteVisCache.renderer = ctx->renderer;
        emu_ami_spriteVisCache.texWidth = 0;
        emu_ami_spriteVisCache.texHeight = 0;
    }
    if (!emu_ami_spriteVisCache.texture ||
        emu_ami_spriteVisCache.texWidth != textureWidth ||
        emu_ami_spriteVisCache.texHeight != textureHeight) {
        if (emu_ami_spriteVisCache.texture) {
            SDL_DestroyTexture(emu_ami_spriteVisCache.texture);
            emu_ami_spriteVisCache.texture = NULL;
        }
        emu_ami_spriteVisCache.texture = SDL_CreateTexture(ctx->renderer,
                                                           SDL_PIXELFORMAT_ARGB8888,
                                                           SDL_TEXTUREACCESS_STREAMING,
                                                           textureWidth,
                                                           textureHeight);
        if (!emu_ami_spriteVisCache.texture) {
            return;
        }
        emu_ami_spriteVisCache.texWidth = textureWidth;
        emu_ami_spriteVisCache.texHeight = textureHeight;
    }

    size_t pixelCount = (size_t)textureWidth * (size_t)textureHeight;
    if (pixelCount > emu_ami_spriteVisCache.pixelsCap) {
        uint32_t *nextPixels =
            (uint32_t *)realloc(emu_ami_spriteVisCache.pixels, pixelCount * sizeof(*nextPixels));
        if (!nextPixels) {
            return;
        }
        emu_ami_spriteVisCache.pixels = nextPixels;
        emu_ami_spriteVisCache.pixelsCap = pixelCount;
    }
    if (pixelCount > emu_ami_spriteVisCache.spriteIdsCap) {
        uint8_t *nextSpriteIds =
            (uint8_t *)realloc(emu_ami_spriteVisCache.spriteIds, pixelCount * sizeof(*nextSpriteIds));
        if (!nextSpriteIds) {
            return;
        }
        emu_ami_spriteVisCache.spriteIds = nextSpriteIds;
        emu_ami_spriteVisCache.spriteIdsCap = pixelCount;
    }

    emu_ami_spriteVisCache.normalSpriteMask = 0u;
    emu_ami_spriteVisCache.attachedPairMask = 0u;
    memset(emu_ami_spriteVisCache.pixels, 0, pixelCount * sizeof(*emu_ami_spriteVisCache.pixels));
    memset(emu_ami_spriteVisCache.spriteIds, 0, pixelCount * sizeof(*emu_ami_spriteVisCache.spriteIds));
    for (size_t i = 0; i < fetchedCount; ++i) {
        uint32_t x = emu_ami_spriteVisCache.points[i].x;
        uint32_t y = emu_ami_spriteVisCache.points[i].y;
        if (x >= srcWidth || y >= srcHeight) {
            continue;
        }
        uint32_t spriteIndex = emu_ami_spriteVisCache.points[i].spriteIndex;
        int attachedPair =
            (emu_ami_spriteVisCache.points[i].flags & E9K_DEBUG_AMI_SPRITE_VIS_FLAG_ATTACHED) != 0u ? 1 : 0;
        size_t pixelIndex = (size_t)y * (size_t)srcWidth + (size_t)x;
        if (attachedPair) {
            emu_ami_spriteVisCache.attachedPairMask |= 1u << ((spriteIndex >> 1) & 3u);
            emu_ami_spriteVisCache.pixels[pixelIndex] = emu_ami_spriteVisAttachedColorFromIndex(spriteIndex);
            emu_ami_spriteVisCache.spriteIds[pixelIndex] = (uint8_t)((spriteIndex + 1u) | 0x10u);
        } else {
            emu_ami_spriteVisCache.normalSpriteMask |= 1u << (spriteIndex & 7u);
            emu_ami_spriteVisCache.pixels[pixelIndex] = emu_ami_spriteVisColorFromIndex(spriteIndex);
            emu_ami_spriteVisCache.spriteIds[pixelIndex] = (uint8_t)(spriteIndex + 1u);
        }
    }

    SDL_UpdateTexture(emu_ami_spriteVisCache.texture,
                      NULL,
                      emu_ami_spriteVisCache.pixels,
                      textureWidth * (int)sizeof(*emu_ami_spriteVisCache.pixels));
    SDL_SetTextureBlendMode(emu_ami_spriteVisCache.texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(ctx->renderer, emu_ami_spriteVisCache.texture, NULL, dst);
}

static int
emu_ami_dmaDebugNormalizeLineDhpos(int dhpos, int lineStartDhpos, int dhposWrap)
{
    if (dhposWrap > 0 && dhpos < lineStartDhpos) {
        return dhpos + dhposWrap;
    }
    return dhpos;
}

static const e9k_debug_ami_dma_debug_frame_view_t *
emu_ami_getDmaDebugFrameView(void)
{
    const e9k_debug_ami_dma_debug_frame_view_t *frame =
        libretro_host_amiga_getDmaDebugFrameView(E9K_DEBUG_AMI_DMA_DEBUG_FRAME_LATEST_COMPLETE);
    if (!frame ||
        !frame->records ||
        frame->info.frameNumber < 0 ||
        frame->info.recordCount == 0u ||
        frame->info.hposCount <= 0 ||
        frame->renderWidth <= 0 ||
        frame->renderHeight <= 0 ||
        frame->visibleWidth <= 0 ||
        frame->visibleHeight <= 0 ||
        frame->dhposScale <= 0) {
        return NULL;
    }

    return frame;
}

static int
emu_ami_isVideoSyncDmaEnabled(void)
{
    int *debugDma = debugger.amigaDebug.debugDma;
    return (debugDma && *debugDma == (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_VIDEO_SYNC) ? 1 : 0;
}

static int
emu_ami_isCopperDebugEnabled(void)
{
    int *debugCopper = debugger.amigaDebug.debugCopper;
    return (debugCopper && *debugCopper != 0) ? 1 : 0;
}

int
emu_ami_getCopperDebugEnabled(void)
{
    return emu_ami_isCopperDebugEnabled();
}

static const e9k_debug_ami_dma_debug_frame_view_t *
emu_ami_getScaledDmaDebugFrameView(void)
{
    if (!emu_ami_isVideoSyncDmaEnabled() && !emu_ami_isCopperDebugEnabled()) {
        return NULL;
    }
    return emu_ami_getDmaDebugFrameView();
}

static void
emu_ami_getDmaDebugOverlayDst(const e9k_debug_ami_dma_debug_frame_view_t *frame, const SDL_Rect *dst, SDL_Rect *out)
{
    if (!frame || !dst || !out) {
        return;
    }

    *out = *dst;
    out->x = dst->x - emu_ami_scaleAxis(frame->visibleOffsetX, frame->visibleWidth, dst->w);
    out->y = dst->y - emu_ami_scaleAxis(frame->visibleOffsetY, frame->visibleHeight, dst->h);
    out->w = emu_ami_scaleAxis(frame->renderWidth, frame->visibleWidth, dst->w);
    out->h = emu_ami_scaleAxis(frame->renderHeight, frame->visibleHeight, dst->h);
}

static void
emu_ami_adjustVideoBounds(e9ui_rect_t *bounds)
{
    if (!bounds || bounds->w <= 0 || bounds->h <= 0) {
        emu_ami_legendReservedHeight = 0;
        return;
    }

    emu_ami_legendReservedHeight = emu_ami_getLegendReserveHeight(bounds->h);
    if (emu_ami_legendReservedHeight > 0 && emu_ami_legendReservedHeight < bounds->h) {
        bounds->h -= emu_ami_legendReservedHeight;
    } else {
        emu_ami_legendReservedHeight = 0;
    }
}

static void
emu_ami_adjustVideoDst(SDL_Rect *dst)
{
    if (!dst || dst->w <= 0 || dst->h <= 0) {
        return;
    }

    const e9k_debug_ami_dma_debug_frame_view_t *frame = emu_ami_getScaledDmaDebugFrameView();
    if (!frame) {
        emu_ami_copperLegendOuterDst = *dst;
        emu_ami_copperLegendOuterDst.h += emu_ami_legendReservedHeight;
        return;
    }

    int adjustedWidth = emu_ami_scaleAxis(frame->visibleWidth, frame->renderWidth, dst->w);
    int adjustedHeight = emu_ami_scaleAxis(frame->visibleHeight, frame->renderHeight, dst->h);
    int adjustedOffsetX = emu_ami_scaleAxis(frame->visibleOffsetX, frame->renderWidth, dst->w);
    int adjustedOffsetY = emu_ami_scaleAxis(frame->visibleOffsetY, frame->renderHeight, dst->h);

    if (adjustedWidth <= 0 || adjustedHeight <= 0) {
        return;
    }

    dst->x += adjustedOffsetX;
    dst->y += adjustedOffsetY;
    dst->w = adjustedWidth;
    dst->h = adjustedHeight;
    emu_ami_copperLegendOuterDst = *dst;
    emu_ami_copperLegendOuterDst.h += emu_ami_legendReservedHeight;
}

static void
emu_ami_renderDmaDebugOverlay(e9ui_context_t *ctx, SDL_Rect *dst)
{
    if (!ctx || !ctx->renderer || !dst || dst->w <= 0 || dst->h <= 0) {
        return;
    }

    if (!emu_ami_isVideoSyncDmaEnabled()) {
        return;
    }

    const e9k_debug_ami_dma_debug_frame_view_t *frame = emu_ami_getDmaDebugFrameView();
    if (!frame) {
        return;
    }

    if (!emu_ami_dmaDebugEnsureTexture(&emu_ami_dmaDebugCache, ctx, frame->renderWidth, frame->renderHeight)) {
        return;
    }

    size_t pixelCount = (size_t)frame->renderWidth * (size_t)frame->renderHeight;
    memset(emu_ami_dmaDebugCache.pixels, 0, pixelCount * sizeof(*emu_ami_dmaDebugCache.pixels));

    int stride = frame->info.hposCount;
    int rowCount = 0;
    if (stride > 0) {
        rowCount = (int)(frame->info.recordCount / (uint32_t)stride);
    }
    if (rowCount > frame->renderHeight) {
        rowCount = frame->renderHeight;
    }
    if (frame->info.vposCount > 0 && rowCount > frame->info.vposCount) {
        rowCount = frame->info.vposCount;
    }
    if (rowCount < 0) {
        rowCount = 0;
    }

    for (int row = 0; row < rowCount; ++row) {
        size_t rowBase = (size_t)row * (size_t)stride;
        int lineStartDhpos = -1;
        for (int x = 0; x < stride; ++x) {
            const e9k_debug_ami_dma_debug_raw_record_t *rec = &frame->records[rowBase + (size_t)x];
            if (rec->end) {
                break;
            }
            if (rec->dhpos > 0) {
                lineStartDhpos = rec->dhpos;
                break;
            }
        }
        if (lineStartDhpos < 0) {
            continue;
        }
        uint32_t *pixelRow = emu_ami_dmaDebugCache.pixels + ((size_t)row * (size_t)frame->renderWidth);
        for (int x = 0; x < stride; ++x) {
            const e9k_debug_ami_dma_debug_raw_record_t *rec = &frame->records[rowBase + (size_t)x];
            if (rec->end) {
                break;
            }
            if (rec->reg == 0xffffu && rec->cf_reg == 0xffffu) {
                continue;
            }
            uint32_t color = emu_ami_dmaDebugColorForRecord(rec);
            if (!color) {
                continue;
            }
            int dhposOrdered = emu_ami_dmaDebugNormalizeLineDhpos(rec->dhpos, lineStartDhpos, frame->dhposWrap);
            int nextDhposOrdered = dhposOrdered + 2;
            if (x + 1 < stride) {
                const e9k_debug_ami_dma_debug_raw_record_t *next = &frame->records[rowBase + (size_t)x + 1u];
                if (!next->end) {
                    nextDhposOrdered = emu_ami_dmaDebugNormalizeLineDhpos(next->dhpos, lineStartDhpos, frame->dhposWrap);
                }
            }
            int x0 = (dhposOrdered - lineStartDhpos) * frame->dhposScale;
            int x1 = (nextDhposOrdered - lineStartDhpos) * frame->dhposScale;
            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            if (x1 <= 0 || x0 >= frame->renderWidth) {
                continue;
            }
            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 > frame->renderWidth) {
                x1 = frame->renderWidth;
            }
            for (int px = x0; px < x1; ++px) {
                pixelRow[px] = color;
            }
        }
    }

    SDL_UpdateTexture(emu_ami_dmaDebugCache.texture,
                      NULL,
                      emu_ami_dmaDebugCache.pixels,
                      frame->renderWidth * (int)sizeof(*emu_ami_dmaDebugCache.pixels));
    SDL_SetTextureBlendMode(emu_ami_dmaDebugCache.texture, SDL_BLENDMODE_BLEND);

    SDL_Rect overlayDst;
    emu_ami_getDmaDebugOverlayDst(frame, dst, &overlayDst);
    if (overlayDst.w <= 0 || overlayDst.h <= 0) {
        return;
    }
    SDL_RenderCopy(ctx->renderer, emu_ami_dmaDebugCache.texture, NULL, &overlayDst);
}

static void
emu_ami_setCopperDebugDmaAssist(int enabled)
{
    int *debugDma = debugger.amigaDebug.debugDma;
    if (!debugDma) {
        emu_ami_copperDebugForcedDma = 0;
        emu_ami_copperDebugPrevDmaMode = 0;
        return;
    }

    if (enabled) {
        if (*debugDma == 0) {
            emu_ami_copperDebugPrevDmaMode = *debugDma;
            *debugDma = (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY;
            emu_ami_copperDebugForcedDma = 1;
        } else {
            emu_ami_copperDebugForcedDma = 0;
            emu_ami_copperDebugPrevDmaMode = *debugDma;
        }
        return;
    }

    if (emu_ami_copperDebugForcedDma && *debugDma == (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY) {
        *debugDma = emu_ami_copperDebugPrevDmaMode;
    }
    emu_ami_copperDebugForcedDma = 0;
    emu_ami_copperDebugPrevDmaMode = 0;
}

static int
emu_ami_getDmaLineStartDhpos(const e9k_debug_ami_dma_debug_frame_view_t *frame, int row, int *cache, int cacheCount)
{
    if (!frame || !cache || row < 0 || row >= cacheCount) {
        return -1;
    }
    if (cache[row] != -2) {
        return cache[row];
    }

    int stride = frame->info.hposCount;
    if (stride <= 0) {
        cache[row] = -1;
        return -1;
    }

    size_t rowBase = (size_t)row * (size_t)stride;
    for (int x = 0; x < stride; ++x) {
        const e9k_debug_ami_dma_debug_raw_record_t *rec = &frame->records[rowBase + (size_t)x];
        if (rec->end) {
            break;
        }
        if (rec->dhpos > 0) {
            cache[row] = rec->dhpos;
            return cache[row];
        }
    }

    cache[row] = -1;
    return -1;
}

static const e9k_debug_ami_dma_debug_raw_record_t *
emu_ami_findDmaRecordForHpos(const e9k_debug_ami_dma_debug_frame_view_t *frame, int row, int hpos, int *outIndex)
{
    if (outIndex) {
        *outIndex = -1;
    }
    if (!frame || row < 0 || hpos < 0) {
        return NULL;
    }

    int stride = frame->info.hposCount;
    if (stride <= 0) {
        return NULL;
    }

    size_t rowBase = (size_t)row * (size_t)stride;
    int directIndex = hpos + frame->info.dmaHoffset;
    if (directIndex >= 0 && directIndex < stride) {
        const e9k_debug_ami_dma_debug_raw_record_t *direct = &frame->records[rowBase + (size_t)directIndex];
        if (!direct->end && direct->hpos == hpos) {
            if (outIndex) {
                *outIndex = directIndex;
            }
            return direct;
        }
    }

    for (int x = 0; x < stride; ++x) {
        const e9k_debug_ami_dma_debug_raw_record_t *rec = &frame->records[rowBase + (size_t)x];
        if (rec->end) {
            break;
        }
        if (rec->hpos == hpos) {
            if (outIndex) {
                *outIndex = x;
            }
            return rec;
        }
    }

    return NULL;
}

static int
emu_ami_getCopperSlotTextureRect(const e9k_debug_ami_dma_debug_frame_view_t *frame,
                                 const e9k_debug_ami_copper_debug_raw_record_t *rec,
                                 int *lineStartCache,
                                 int cacheCount,
                                 SDL_Rect *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!frame || !rec || !out || rec->vpos < 0) {
        return 0;
    }

    int lineStartDhpos = emu_ami_getDmaLineStartDhpos(frame, rec->vpos, lineStartCache, cacheCount);
    if (lineStartDhpos < 0) {
        return 0;
    }

    int dmaIndex = -1;
    const e9k_debug_ami_dma_debug_raw_record_t *dmaRec =
        emu_ami_findDmaRecordForHpos(frame, rec->vpos, rec->hpos, &dmaIndex);
    if (!dmaRec || dmaIndex < 0) {
        return 0;
    }

    int dhposOrdered = emu_ami_dmaDebugNormalizeLineDhpos(dmaRec->dhpos, lineStartDhpos, frame->dhposWrap);
    int nextDhposOrdered = dhposOrdered + 2;
    int stride = frame->info.hposCount;
    if (dmaIndex + 1 < stride) {
        const e9k_debug_ami_dma_debug_raw_record_t *next = &frame->records[(size_t)rec->vpos * (size_t)stride + (size_t)dmaIndex + 1u];
        if (!next->end) {
            nextDhposOrdered = emu_ami_dmaDebugNormalizeLineDhpos(next->dhpos, lineStartDhpos, frame->dhposWrap);
        }
    }

    out->x = (dhposOrdered - lineStartDhpos) * frame->dhposScale;
    out->y = rec->vpos;
    out->w = (nextDhposOrdered - dhposOrdered) * frame->dhposScale;
    out->h = 1;
    if (out->w <= 0) {
        out->w = 1;
    }
    return 1;
}

static void
emu_ami_appendCopperTooltipText(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!out || cap == 0 || !pos || !text) {
        return;
    }

    while (*text && *pos + 1 < cap) {
        char ch = *text++;
        if (ch == '\r') {
            continue;
        }
        if (ch == '\t') {
            ch = ' ';
        }
        out[*pos] = ch;
        (*pos)++;
    }
    out[*pos] = '\0';
}

static void
emu_ami_appendCopperTooltipLine(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!text || !*text) {
        return;
    }
    if (*pos > 0 && *pos + 1 < cap && out[*pos - 1] != '\n') {
        out[*pos] = '\n';
        (*pos)++;
        out[*pos] = '\0';
    }
    emu_ami_appendCopperTooltipText(out, cap, pos, text);
}

static void
emu_ami_appendCopperTooltipFormat(char *out, size_t cap, size_t *pos, const char *fmt, ...)
{
    if (!out || cap == 0 || !pos || !fmt) {
        return;
    }

    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (written <= 0) {
        return;
    }
    emu_ami_appendCopperTooltipLine(out, cap, pos, line);
}

static void
emu_ami_appendCopperTooltipInlineFormat(char *out, size_t cap, size_t *pos, const char *fmt, ...)
{
    if (!out || cap == 0 || !pos || !fmt) {
        return;
    }

    char text[256];
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (written <= 0) {
        return;
    }
    emu_ami_appendCopperTooltipText(out, cap, pos, text);
}

static void
emu_ami_appendCopperBeamPosition(char *out, size_t cap, size_t *pos, const char *label, int vpos, int hpos)
{
    emu_ami_appendCopperTooltipFormat(out, cap, pos, "%s LINE %03d HPOS %03d", label, vpos, hpos);
}

static void
emu_ami_appendCopperWaitHeadlineCore(uint32_t insn, char *out, size_t cap, size_t *pos)
{
    int vp = (int)((insn & 0xff000000u) >> 24);
    int hp = (int)((insn & 0x00fe0000u) >> 16);
    int ve = (int)((insn & 0x00007f00u) >> 8);
    int he = (int)(insn & 0x000000feu);
    int bfd = (int)((insn & 0x00008000u) >> 15);
    int vMask = vp & (ve | 0x80);
    int hMask = hp & he;
    int didOutput = 0;

    if (vMask > 0) {
        didOutput = 1;
        emu_ami_appendCopperTooltipText(out, cap, pos, "vpos ");
        if (ve != 0x7fu) {
            emu_ami_appendCopperTooltipInlineFormat(out, cap, pos, "& 0x%02x ", ve);
        }
        emu_ami_appendCopperTooltipInlineFormat(out, cap, pos, ">= 0x%02x", vMask);
    }
    if (he > 0) {
        if (vMask > 0) {
            emu_ami_appendCopperTooltipText(out, cap, pos, " and");
        }
        emu_ami_appendCopperTooltipText(out, cap, pos, " hpos ");
        if (he != 0xfeu) {
            emu_ami_appendCopperTooltipInlineFormat(out, cap, pos, "& 0x%02x ", he);
        }
        emu_ami_appendCopperTooltipInlineFormat(out, cap, pos, ">= 0x%02x", hMask);
    } else {
        if (didOutput) {
            emu_ami_appendCopperTooltipText(out, cap, pos, ", ");
        }
        emu_ami_appendCopperTooltipText(out, cap, pos, "ignore horizontal");
    }

    emu_ami_appendCopperTooltipText(out, cap, pos, "\n");
    emu_ami_appendCopperTooltipInlineFormat(out,
                                            cap,
                                            pos,
                                            "VP %02x, VE %02x, HP %02x, HE %02x, BFD %d",
                                            vp,
                                            ve,
                                            hp,
                                            he,
                                            bfd);
}

static void
emu_ami_appendCopperHeadline(const e9k_debug_ami_copper_debug_raw_record_t *rec,
                             uint32_t insn,
                             uint32_t insnType,
                             char *out,
                             size_t cap,
                             size_t *pos)
{
    if (!rec || !out || cap == 0 || !pos) {
        return;
    }

    if (insnType == 0x00010000u || insnType == 0x00010001u) {
        int waitsForBlitterIdle = (rec->w2 & 0x8000u) == 0u;
        int isWaitForever = rec->w1 == 0xffffu && rec->w2 == 0xfffeu;

        if (isWaitForever) {
            emu_ami_appendCopperTooltipLine(out, cap, pos, insnType == 0x00010001u ? "SKIP FOREVER" : "WAIT FOREVER");
            return;
        }

        if (insnType == 0x00010001u) {
            emu_ami_appendCopperTooltipText(out, cap, pos, "Skip if ");
        } else {
            emu_ami_appendCopperTooltipText(out, cap, pos, "WAIT for ");
        }
        emu_ami_appendCopperWaitHeadlineCore(insn, out, cap, pos);
        if (waitsForBlitterIdle) {
            emu_ami_appendCopperTooltipText(out, cap, pos, " ; BLITTER MUST BE IDLE");
        }
        if (insnType == 0x00010000u && insn == 0xfffffffeu) {
            emu_ami_appendCopperTooltipText(out, cap, pos, " ; End of Copperlist");
        }
        return;
    }

    uint16_t regOffset = (uint16_t)(rec->w1 & 0x01feu);
    const char *regName = amiga_custom_regs_nameForOffset(regOffset);
    if (!regName || !regName[0]) {
        emu_ami_appendCopperTooltipFormat(out, cap, pos, "%03X := %04X", (unsigned)regOffset, (unsigned)rec->w2);
        return;
    }

    emu_ami_appendCopperTooltipFormat(out, cap, pos, "%s := %04X", regName, (unsigned)rec->w2);
}

static void
emu_ami_appendCopperWaitDescription(const e9k_debug_ami_copper_debug_raw_record_t *rec,
                                    char *out,
                                    size_t cap,
                                    size_t *pos,
                                    int isSkip)
{
    uint8_t verticalMask = (uint8_t)(((rec->w2 >> 8) & 0x7fu) | 0x80u);
    uint8_t horizontalMask = (uint8_t)(rec->w2 & 0xfeu);
    int targetVpos = (int)(((rec->w1 >> 8) & 0xffu) & verticalMask);
    int targetHpos = (int)((rec->w1 & 0xfeu) & horizontalMask);
    int waitsForBlitterIdle = (rec->w2 & 0x8000u) == 0u;
    int isWaitForever = rec->w1 == 0xffffu && rec->w2 == 0xfffeu;

    if (isWaitForever) {
        emu_ami_appendCopperTooltipLine(out, cap, pos, "TARGET NEVER MATCHES");
    } else if (isSkip) {
        emu_ami_appendCopperBeamPosition(out, cap, pos, "TEST", targetVpos, targetHpos);
        emu_ami_appendCopperTooltipLine(out, cap, pos, "SKIPS NEXT OP IF BEAM IS AT OR PAST TEST");
    }

    if (waitsForBlitterIdle) {
        emu_ami_appendCopperTooltipLine(out, cap, pos, "ALSO WAITS FOR BLITTER IDLE");
        if (rec->bvpos >= 0 && rec->bhpos >= 0) {
            emu_ami_appendCopperBeamPosition(out, cap, pos, "BLIT DONE", rec->bvpos, rec->bhpos);
        }
    } else {
        emu_ami_appendCopperTooltipLine(out, cap, pos, "BLITTER BUSY IS IGNORED");
    }

    if (verticalMask != 0xffu || horizontalMask != 0xfeu) {
        emu_ami_appendCopperTooltipFormat(out, cap, pos,
                                          "MASKED COMPARE VMASK %02X HMASK %02X",
                                          (unsigned)verticalMask,
                                          (unsigned)horizontalMask);
    }
}

void
emu_ami_setCopperDebugEnabled(int enabled)
{
    int *debugCopper = debugger.amigaDebug.debugCopper;
    int nextEnabled = enabled ? 1 : 0;

    if (!debugCopper) {
        return;
    }
    *debugCopper = nextEnabled;
    emu_ami_setCopperDebugDmaAssist(nextEnabled);
}

static void
emu_ami_buildCopperTooltip(const e9k_debug_ami_copper_debug_raw_record_t *rec, char *out, size_t cap)
{
    int skipMoveDetails = 0;

    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!rec) {
        return;
    }

    size_t pos = 0;
    uint32_t insn = ((uint32_t)rec->w1 << 16) | (uint32_t)rec->w2;
    uint32_t insnType = insn & 0x00010001u;

    emu_ami_appendCopperHeadline(rec, insn, insnType, out, cap, &pos);

    if (insnType == 0x00010000u || insnType == 0x00010001u) {
        emu_ami_appendCopperWaitDescription(rec, out, cap, &pos, insnType == 0x00010001u);
    } else {
        uint16_t regOffset = (uint16_t)(rec->w1 & 0x01feu);
        if (emu_ami_isCopperPaletteOffset(regOffset)) {
            skipMoveDetails = 1;
        }
        if (!skipMoveDetails) {
            const char *regDesc = amiga_custom_regs_descriptionForOffset(regOffset);
            const char *valueTip = amiga_custom_regs_valueTooltipForOffset(regOffset, rec->w2);
            if (regDesc && *regDesc) {
                emu_ami_appendCopperTooltipLine(out, cap, &pos, regDesc);
            }
            if (valueTip && *valueTip) {
                emu_ami_appendCopperTooltipText(out, cap, &pos, "\n");
                emu_ami_appendCopperTooltipText(out, cap, &pos, valueTip);
            }
        }
    }

    emu_ami_appendCopperTooltipFormat(out,
                                      cap,
                                      &pos,
                                      "COP %06X",
                                      (unsigned)(rec->addr & 0x00ffffffu));
    if (rec->nextaddr != 0u && rec->nextaddr != 0xffffffffu && rec->nextaddr != rec->addr + 4u) {
        emu_ami_appendCopperTooltipFormat(out, cap, &pos, "NEXT %06X", (unsigned)(rec->nextaddr & 0x00ffffffu));
    }
    if (rec->bvpos >= 0 && rec->bhpos >= 0) {
        emu_ami_appendCopperTooltipFormat(out, cap, &pos, "BLITWAIT %03d %03d", rec->bvpos, rec->bhpos);
    }
}

static void
emu_ami_renderCopperTooltip(e9ui_context_t *ctx,
                            const SDL_Rect *anchor,
                            const char *text,
                            int showSwatch,
                            SDL_Color swatchColor)
{
    if (!ctx || !ctx->renderer || !ctx->font || !anchor || !text || !*text) {
        return;
    }

    enum { EMU_AMI_TOOLTIP_MAX_LINES = 64 };
    const char *lines[EMU_AMI_TOOLTIP_MAX_LINES];
    int lineCount = 0;
    const char *cursor = text;
    while (*cursor && lineCount < EMU_AMI_TOOLTIP_MAX_LINES) {
        lines[lineCount++] = cursor;
        const char *next = strchr(cursor, '\n');
        if (!next) {
            break;
        }
        cursor = next + 1;
    }
    if (lineCount <= 0) {
        return;
    }

    int lineHeight = TTF_FontLineSkip(ctx->font);
    if (lineHeight <= 0) {
        lineHeight = TTF_FontHeight(ctx->font);
    }
    if (lineHeight <= 0) {
        return;
    }

    int lineWidths[EMU_AMI_TOOLTIP_MAX_LINES];
    int maxTextW = 0;
    char lineBuf[1024];
    for (int i = 0; i < lineCount; ++i) {
        const char *lineStart = lines[i];
        const char *lineEnd = strchr(lineStart, '\n');
        size_t lineLen = lineEnd ? (size_t)(lineEnd - lineStart) : strlen(lineStart);
        while (lineLen > 0u && lineStart[lineLen - 1u] == '\r') {
            lineLen--;
        }
        if (lineLen >= sizeof(lineBuf)) {
            lineLen = sizeof(lineBuf) - 1u;
        }
        memcpy(lineBuf, lineStart, lineLen);
        lineBuf[lineLen] = '\0';

        int lineW = 0;
        int lineH = 0;
        if (lineBuf[0] != '\0') {
            TTF_SizeText(ctx->font, lineBuf, &lineW, &lineH);
        }
        lineWidths[i] = lineW;
        if (lineW > maxTextW) {
            maxTextW = lineW;
        }
    }

    int pad = e9ui_scale_px(ctx, 6);
    int offset = e9ui_scale_px(ctx, 8);
    int swatchGap = e9ui_scale_px(ctx, 6);
    int swatchSize = lineHeight - e9ui_scale_px(ctx, 4);
    if (showSwatch && lineCount > 0 && swatchSize > 0) {
        int swatchLineWidth = lineWidths[0] + swatchGap + swatchSize;
        if (swatchLineWidth > maxTextW) {
            maxTextW = swatchLineWidth;
        }
    }
    if (maxTextW <= 0) {
        maxTextW = e9ui_scale_px(ctx, 8);
    }

    int bgW = maxTextW + pad * 2;
    int bgH = lineHeight * lineCount + pad * 2;
    if (bgW <= 0 || bgH <= 0) {
        return;
    }

    int x = anchor->x + anchor->w + offset;
    int y = anchor->y + offset;
    int maxX = ctx->winW > 8 ? ctx->winW - 4 : 4;
    int maxY = ctx->winH > 8 ? ctx->winH - 4 : 4;
    if (x + bgW > maxX) {
        x = maxX - bgW;
    }
    if (y + bgH > maxY) {
        y = maxY - bgH;
    }
    if (x < 4) {
        x = 4;
    }
    if (y < 4) {
        y = 4;
    }

    SDL_Rect bg = { x, y, bgW, bgH };
    SDL_Color background = { 16, 16, 16, 220 };
    SDL_Color border = { 170, 170, 170, 255 };
    SDL_Color textColor = { 235, 235, 235, 255 };
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, background.r, background.g, background.b, background.a);
    SDL_RenderFillRect(ctx->renderer, &bg);
    SDL_SetRenderDrawColor(ctx->renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(ctx->renderer, &bg);

    for (int i = 0; i < lineCount; ++i) {
        const char *lineStart = lines[i];
        const char *lineEnd = strchr(lineStart, '\n');
        size_t lineLen = lineEnd ? (size_t)(lineEnd - lineStart) : strlen(lineStart);
        while (lineLen > 0u && lineStart[lineLen - 1u] == '\r') {
            lineLen--;
        }
        if (lineLen >= sizeof(lineBuf)) {
            lineLen = sizeof(lineBuf) - 1u;
        }
        memcpy(lineBuf, lineStart, lineLen);
        lineBuf[lineLen] = '\0';
        if (lineBuf[0] == '\0') {
            continue;
        }
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, ctx->font, lineBuf, textColor, &tw, &th);
        if (!tex) {
            continue;
        }
        int lineY = y + pad + i * lineHeight + ((lineHeight - th) / 2);
        SDL_Rect textRect = { x + pad, lineY, tw, th };
        SDL_RenderCopy(ctx->renderer, tex, NULL, &textRect);
    }

    if (showSwatch && lineCount > 0 && swatchSize > 0) {
        int swatchX = x + pad + lineWidths[0] + swatchGap;
        int swatchY = y + pad + (lineHeight - swatchSize) / 2;
        SDL_Rect swatchRect = { swatchX, swatchY, swatchSize, swatchSize };
        SDL_SetRenderDrawColor(ctx->renderer, swatchColor.r, swatchColor.g, swatchColor.b, 255);
        SDL_RenderFillRect(ctx->renderer, &swatchRect);
        SDL_SetRenderDrawColor(ctx->renderer, 220, 220, 220, 255);
        SDL_RenderDrawRect(ctx->renderer, &swatchRect);
    }
}

static int
emu_ami_buildCopperDebugOverlayCache(e9ui_context_t *ctx,
                                     const e9k_debug_ami_copper_debug_frame_view_t *copperFrame,
                                     const e9k_debug_ami_dma_debug_frame_view_t *dmaFrame,
                                     const SDL_Rect *overlayDst)
{
    int lineStartDhposCache[2048];
    int cacheCount = 0;

    if (!ctx || !ctx->renderer || !copperFrame || !copperFrame->records || !dmaFrame || !overlayDst) {
        return 0;
    }
    if (overlayDst->w <= 0 || overlayDst->h <= 0) {
        return 0;
    }
    if (!emu_ami_copperDebugEnsureTexture(&emu_ami_copperDebugCache, ctx, overlayDst->w, overlayDst->h)) {
        return 0;
    }
    if (emu_ami_copperDebugCache.frameNumber == (int)copperFrame->info.frameNumber) {
        return 1;
    }

    size_t pixelCount = (size_t)overlayDst->w * (size_t)overlayDst->h;
    memset(emu_ami_copperDebugCache.pixels, 0, pixelCount * sizeof(*emu_ami_copperDebugCache.pixels));

    cacheCount = dmaFrame->info.vposCount;
    if (cacheCount <= 0) {
        cacheCount = 0;
    }
    if (cacheCount > (int)(sizeof(lineStartDhposCache) / sizeof(lineStartDhposCache[0]))) {
        cacheCount = (int)(sizeof(lineStartDhposCache) / sizeof(lineStartDhposCache[0]));
    }
    for (int i = 0; i < cacheCount; ++i) {
        lineStartDhposCache[i] = -2;
    }

    for (uint32_t i = 0; i < copperFrame->info.recordCount; ++i) {
        const e9k_debug_ami_copper_debug_raw_record_t *rec = &copperFrame->records[i];
        SDL_Rect texRect;

        if (!emu_ami_getCopperSlotTextureRect(dmaFrame, rec, lineStartDhposCache, cacheCount, &texRect)) {
            continue;
        }

        int x0 = emu_ami_scaleAxis(texRect.x, dmaFrame->renderWidth, overlayDst->w);
        int x1 = emu_ami_scaleAxis(texRect.x + texRect.w, dmaFrame->renderWidth, overlayDst->w);
        int y0 = emu_ami_scaleAxis(texRect.y, dmaFrame->renderHeight, overlayDst->h);
        int y1 = emu_ami_scaleAxis(texRect.y + 1, dmaFrame->renderHeight, overlayDst->h);
        if (x1 <= x0) {
            x1 = x0 + 1;
        }
        if (y1 <= y0) {
            y1 = y0 + 1;
        } else if (y1 - y0 > 1) {
            y1 -= 1;
        }
        if (x1 <= 0 || x0 >= overlayDst->w || y1 <= 0 || y0 >= overlayDst->h) {
            continue;
        }
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > overlayDst->w) {
            x1 = overlayDst->w;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (y1 > overlayDst->h) {
            y1 = overlayDst->h;
        }

        uint32_t color = emu_ami_copperDebugArgb(emu_ami_copperColorForRecord(rec));
        for (int py = y0; py < y1; ++py) {
            uint32_t *pixelRow = emu_ami_copperDebugCache.pixels + ((size_t)py * (size_t)overlayDst->w);
            for (int px = x0; px < x1; ++px) {
                pixelRow[px] = color;
            }
        }
    }

    SDL_UpdateTexture(emu_ami_copperDebugCache.texture,
                      NULL,
                      emu_ami_copperDebugCache.pixels,
                      overlayDst->w * (int)sizeof(*emu_ami_copperDebugCache.pixels));
    SDL_SetTextureBlendMode(emu_ami_copperDebugCache.texture, SDL_BLENDMODE_BLEND);
    emu_ami_copperDebugCache.frameNumber = (int)copperFrame->info.frameNumber;
    return 1;
}

static int
emu_ami_copperDistanceToAxisRange(int point, int startInclusive, int endExclusive)
{
    if (point < startInclusive) {
        return startInclusive - point;
    }
    if (point >= endExclusive) {
        return point - endExclusive + 1;
    }
    return 0;
}

static unsigned
emu_ami_copperMarkerDistanceSq(SDL_Rect marker, int x, int y)
{
    int dx = emu_ami_copperDistanceToAxisRange(x, marker.x, marker.x + marker.w);
    int dy = emu_ami_copperDistanceToAxisRange(y, marker.y, marker.y + marker.h);
    return (unsigned)(dx * dx + dy * dy);
}

static const e9k_debug_ami_copper_debug_raw_record_t *
emu_ami_findCopperRecordAtPoint(const SDL_Rect *dst, int x, int y, SDL_Rect *outMarker, int isClick)
{
    const e9k_debug_ami_copper_debug_frame_view_t *copperFrame =
        libretro_host_amiga_getCopperDebugFrameView(E9K_DEBUG_AMI_COPPER_DEBUG_FRAME_LATEST_COMPLETE);
    const e9k_debug_ami_dma_debug_frame_view_t *dmaFrame = emu_ami_getDmaDebugFrameView();
    const e9k_debug_ami_copper_debug_raw_record_t *closestRec = NULL;
    SDL_Rect overlayDst;
    SDL_Rect closestMarker = { 0, 0, 0, 0 };
    unsigned closestDistanceSq = 0u;
    int lineStartDhposCache[2048];
    int cacheCount = 0;

    if (outMarker) {
        memset(outMarker, 0, sizeof(*outMarker));
    }
    if (!dst || dst->w <= 0 || dst->h <= 0 || !emu_ami_isCopperDebugEnabled()) {
        return NULL;
    }
    if (!copperFrame || !copperFrame->records || copperFrame->info.recordCount == 0u || !dmaFrame) {
        return NULL;
    }

    emu_ami_getDmaDebugOverlayDst(dmaFrame, dst, &overlayDst);
    if (overlayDst.w <= 0 || overlayDst.h <= 0) {
        return NULL;
    }

    cacheCount = dmaFrame->info.vposCount;
    if (cacheCount <= 0) {
        cacheCount = 0;
    }
    if (cacheCount > (int)(sizeof(lineStartDhposCache) / sizeof(lineStartDhposCache[0]))) {
        cacheCount = (int)(sizeof(lineStartDhposCache) / sizeof(lineStartDhposCache[0]));
    }
    for (int i = 0; i < cacheCount; ++i) {
        lineStartDhposCache[i] = -2;
    }

    for (uint32_t i = 0; i < copperFrame->info.recordCount; ++i) {
        const e9k_debug_ami_copper_debug_raw_record_t *rec = &copperFrame->records[i];
        SDL_Rect texRect;
        if (!emu_ami_getCopperSlotTextureRect(dmaFrame, rec, lineStartDhposCache, cacheCount, &texRect)) {
            continue;
        }

        int x0 = overlayDst.x + emu_ami_scaleAxis(texRect.x, dmaFrame->renderWidth, overlayDst.w);
        int x1 = overlayDst.x + emu_ami_scaleAxis(texRect.x + texRect.w, dmaFrame->renderWidth, overlayDst.w);
        int y0 = overlayDst.y + emu_ami_scaleAxis(texRect.y, dmaFrame->renderHeight, overlayDst.h);
        int y1 = overlayDst.y + emu_ami_scaleAxis(texRect.y + 1, dmaFrame->renderHeight, overlayDst.h);
        if (x1 <= x0) {
            x1 = x0 + 1;
        }
        if (y1 <= y0) {
            y1 = y0 + 1;
        } else if (y1 - y0 > 1) {
            y1 -= 1;
        }

        SDL_Rect marker = { x0, y0, x1 - x0, y1 - y0 };
        if (x >= marker.x &&
            x < marker.x + marker.w &&
            y >= marker.y &&
            y < marker.y + marker.h) {
            if (outMarker) {
                *outMarker = marker;
            }
            return rec;
        }

        int marginX = marker.w * EMU_AMI_COPPER_HIT_MARGIN_SCALE;
        int marginY = marker.h * EMU_AMI_COPPER_HIT_MARGIN_SCALE;
        int expandedLeft = marker.x - marginX;
        int expandedRight = marker.x + marker.w + marginX;
        int expandedTop = marker.y - marginY;
        int expandedBottom = marker.y + marker.h + marginY;
        if (x < expandedLeft ||
            x >= expandedRight ||
            y < expandedTop ||
            y >= expandedBottom) {
            continue;
        }

        unsigned distanceSq = emu_ami_copperMarkerDistanceSq(marker, x, y);
        if (!closestRec || distanceSq < closestDistanceSq) {
            closestRec = rec;
            closestMarker = marker;
            closestDistanceSq = distanceSq;
        }
    }

    (void)isClick;
    if (closestRec && outMarker) {
        *outMarker = closestMarker;
    }
    return closestRec;
}

static void
emu_ami_renderCopperDebugOverlay(e9ui_context_t *ctx, SDL_Rect *dst)
{
    if (!ctx || !ctx->renderer || !dst || dst->w <= 0 || dst->h <= 0) {
        return;
    }
    if (!emu_ami_isCopperDebugEnabled()) {
        return;
    }

    const e9k_debug_ami_copper_debug_frame_view_t *copperFrame =
        libretro_host_amiga_getCopperDebugFrameView(E9K_DEBUG_AMI_COPPER_DEBUG_FRAME_LATEST_COMPLETE);
    const e9k_debug_ami_dma_debug_frame_view_t *dmaFrame = emu_ami_getDmaDebugFrameView();
    if (!copperFrame || !copperFrame->records || copperFrame->info.recordCount == 0u || !dmaFrame) {
        return;
    }

    SDL_Rect overlayDst;
    emu_ami_getDmaDebugOverlayDst(dmaFrame, dst, &overlayDst);
    if (overlayDst.w <= 0 || overlayDst.h <= 0) {
        return;
    }

    if (emu_ami_copperDebugCache.frameNumber != (int)copperFrame->info.frameNumber ||
        emu_ami_copperDebugCache.texWidth != overlayDst.w ||
        emu_ami_copperDebugCache.texHeight != overlayDst.h ||
        emu_ami_copperDebugCache.renderer != ctx->renderer) {
        if (!emu_ami_buildCopperDebugOverlayCache(ctx, copperFrame, dmaFrame, &overlayDst)) {
            return;
        }
    }

    SDL_RenderCopy(ctx->renderer, emu_ami_copperDebugCache.texture, NULL, &overlayDst);
}

static void
emu_ami_renderCopperTooltipOverlay(e9ui_context_t *ctx, SDL_Rect *dst)
{
    SDL_Rect hoveredRect = { 0, 0, 0, 0 };
    const e9k_debug_ami_copper_debug_raw_record_t *hoveredRec = NULL;

    if (!ctx || !dst || dst->w <= 0 || dst->h <= 0) {
        return;
    }
    hoveredRec = emu_ami_findCopperRecordAtPoint(dst, ctx->mouseX, ctx->mouseY, &hoveredRect, 0);
    if (!hoveredRec) {
        return;
    }

    int showSwatch = 0;
    SDL_Color swatchColor = { 0, 0, 0, 255 };
    e9ui_drawFocusRingRect(ctx, hoveredRect, 1);
    char tooltip[1024];
    emu_ami_buildCopperTooltip(hoveredRec, tooltip, sizeof(tooltip));
    if (((hoveredRec->w1 & 0x0001u) == 0u) && ((hoveredRec->w1 & 0x01feu) != 0u)) {
        uint16_t regOffset = (uint16_t)(hoveredRec->w1 & 0x01feu);
        if (emu_ami_isPaletteRegOffset(regOffset)) {
            showSwatch = 1;
            swatchColor = emu_ami_paletteSwatchColor(hoveredRec->w2);
        }
    }
    emu_ami_renderCopperTooltip(ctx, &hoveredRect, tooltip, showSwatch, swatchColor);
}

static int
emu_ami_findSpriteVisPixelAtPoint(const SDL_Rect *dst,
                                  int mouseX,
                                  int mouseY,
                                  SDL_Rect *outRect,
                                  uint32_t *outSpriteIndex,
                                  int *outAttachedPair)
{
    int localX = 0;
    int localY = 0;
    int srcX = 0;
    int srcY = 0;
    int x0 = 0;
    int x1 = 0;
    int y0 = 0;
    int y1 = 0;
    size_t pixelIndex = 0u;
    uint8_t spriteId = 0u;

    if (outRect) {
        memset(outRect, 0, sizeof(*outRect));
    }
    if (outSpriteIndex) {
        *outSpriteIndex = 0u;
    }
    if (outAttachedPair) {
        *outAttachedPair = 0;
    }
    if (!dst || dst->w <= 0 || dst->h <= 0 || !emu_ami_isSpriteVisEnabled()) {
        return 0;
    }
    if (!emu_ami_spriteVisCache.spriteIds ||
        emu_ami_spriteVisCache.texWidth <= 0 ||
        emu_ami_spriteVisCache.texHeight <= 0) {
        return 0;
    }
    if (mouseX < dst->x ||
        mouseX >= dst->x + dst->w ||
        mouseY < dst->y ||
        mouseY >= dst->y + dst->h) {
        return 0;
    }

    localX = mouseX - dst->x;
    localY = mouseY - dst->y;
    srcX = (int)(((int64_t)localX * (int64_t)emu_ami_spriteVisCache.texWidth) / (int64_t)dst->w);
    srcY = (int)(((int64_t)localY * (int64_t)emu_ami_spriteVisCache.texHeight) / (int64_t)dst->h);
    if (srcX < 0 ||
        srcX >= emu_ami_spriteVisCache.texWidth ||
        srcY < 0 ||
        srcY >= emu_ami_spriteVisCache.texHeight) {
        return 0;
    }

    pixelIndex = (size_t)srcY * (size_t)emu_ami_spriteVisCache.texWidth + (size_t)srcX;
    if (pixelIndex >= emu_ami_spriteVisCache.spriteIdsCap) {
        return 0;
    }
    spriteId = emu_ami_spriteVisCache.spriteIds[pixelIndex];
    if (!spriteId) {
        return 0;
    }

    x0 = dst->x + emu_ami_scaleAxis(srcX, emu_ami_spriteVisCache.texWidth, dst->w);
    x1 = dst->x + emu_ami_scaleAxis(srcX + 1, emu_ami_spriteVisCache.texWidth, dst->w);
    y0 = dst->y + emu_ami_scaleAxis(srcY, emu_ami_spriteVisCache.texHeight, dst->h);
    y1 = dst->y + emu_ami_scaleAxis(srcY + 1, emu_ami_spriteVisCache.texHeight, dst->h);
    if (x1 <= x0) {
        x1 = x0 + 1;
    }
    if (y1 <= y0) {
        y1 = y0 + 1;
    }

    if (outRect) {
        outRect->x = x0;
        outRect->y = y0;
        outRect->w = x1 - x0;
        outRect->h = y1 - y0;
    }
    if (outSpriteIndex) {
        *outSpriteIndex = (uint32_t)((spriteId & 0x0fu) - 1u);
    }
    if (outAttachedPair) {
        *outAttachedPair = (spriteId & 0x10u) != 0u ? 1 : 0;
    }
    return 1;
}

static int
emu_ami_renderSpriteVisTooltipOverlay(e9ui_context_t *ctx, SDL_Rect *dst)
{
    SDL_Rect hoveredRect = { 0, 0, 0, 0 };
    SDL_Color swatchColor = { 0, 0, 0, 255 };
    uint32_t spriteColor = 0u;
    uint32_t spriteIndex = 0u;
    int attachedPair = 0;
    char tooltip[64];

    if (!ctx || !dst || dst->w <= 0 || dst->h <= 0) {
        return 0;
    }
    if (!emu_ami_findSpriteVisPixelAtPoint(dst, ctx->mouseX, ctx->mouseY, &hoveredRect, &spriteIndex, &attachedPair)) {
        return 0;
    }

    if (attachedPair) {
        spriteColor = emu_ami_spriteVisAttachedColorFromIndex(spriteIndex);
    } else {
        spriteColor = emu_ami_spriteVisColorFromIndex(spriteIndex);
    }
    swatchColor.r = (uint8_t)((spriteColor >> 16) & 0xffu);
    swatchColor.g = (uint8_t)((spriteColor >> 8) & 0xffu);
    swatchColor.b = (uint8_t)(spriteColor & 0xffu);
    e9ui_drawFocusRingRect(ctx, hoveredRect, 1);
    if (attachedPair) {
        snprintf(tooltip, sizeof(tooltip), "Attached %u+%u", (unsigned)spriteIndex, (unsigned)(spriteIndex + 1u));
    } else {
        snprintf(tooltip, sizeof(tooltip), "Sprite %u", (unsigned)spriteIndex);
    }
    emu_ami_renderCopperTooltip(ctx, &hoveredRect, tooltip, 1, swatchColor);
    return 1;
}

static int
emu_ami_handleOverlayEvent(e9ui_context_t *ctx, const SDL_Rect *dst, const e9ui_event_t *ev)
{
    const e9k_debug_ami_copper_debug_raw_record_t *rec = NULL;

    if (!dst || !ev) {
        return 0;
    }
    if (ev->type != SDL_MOUSEBUTTONDOWN && ev->type != SDL_MOUSEBUTTONUP) {
        return 0;
    }
    if (ev->button.button != SDL_BUTTON_LEFT) {
        return 0;
    }

    if (amiga_blittervis_handleOverlayEvent(ctx, dst, ev)) {
        return 1;
    }

    rec = emu_ami_findCopperRecordAtPoint(dst, ev->button.x, ev->button.y, NULL, 1);
    if (!rec) {
        return 0;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN) {
        ui_centerCprSourceOnAddress((uint32_t)(rec->addr & 0x00ffffffu));
    }
    return 1;
}

static int
emu_ami_mapKeyToJoypad(SDL_Keycode key, unsigned *id)
{
    return debugger_input_bindings_mapKeyToJoypad(TARGET_AMIGA,
                                                  (target && target->coreOptionGetValue)
                                                      ? target->coreOptionGetValue
                                                      : NULL,
                                                  key,
                                                  id);
}

uint16_t
emu_ami_translateModifiers(SDL_Keymod mod)
{
    uint16_t out = 0;
    if (mod & KMOD_SHIFT) {
        out |= RETROKMOD_SHIFT;
    }
    if (mod & KMOD_CTRL) {
        out |= RETROKMOD_CTRL;
    }
    if (mod & KMOD_ALT) {
        out |= RETROKMOD_ALT;
    }
    if (mod & KMOD_GUI) {
        out |= RETROKMOD_META;
    }
    if (mod & KMOD_NUM) {
        out |= RETROKMOD_NUMLOCK;
    }
    if (mod & KMOD_CAPS) {
        out |= RETROKMOD_CAPSLOCK;
    }
    return out;
}

uint32_t
emu_ami_translateCharacter(SDL_Keycode key, SDL_Keymod mod)
{
    if (key < 32 || key >= 127) {
        return 0;
    }
    int shift = (mod & KMOD_SHIFT) ? 1 : 0;
    int caps = (mod & KMOD_CAPS) ? 1 : 0;
    if (key >= 'a' && key <= 'z') {
        if (shift ^ caps) {
            return (uint32_t)toupper((int)key);
        }
        return (uint32_t)key;
    }
    if (!shift) {
        return (uint32_t)key;
    }
    switch (key) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    case '`': return '~';
    default:
        break;
    }
    return (uint32_t)key;
}

unsigned
emu_ami_translateKey(SDL_Keycode key)
{
    if (key >= 32 && key < 127) {
        if (key >= 'A' && key <= 'Z') {
            return (unsigned)tolower((int)key);
        }
        return (unsigned)key;
    }
    switch (key) {
    case SDLK_BACKSPACE: return RETROK_BACKSPACE;
    case SDLK_TAB: return RETROK_TAB;
    case SDLK_RETURN: return RETROK_RETURN;
    case SDLK_ESCAPE: return RETROK_ESCAPE;
    case SDLK_DELETE: return RETROK_DELETE;
    case SDLK_INSERT: return RETROK_INSERT;
    case SDLK_HOME: return RETROK_HOME;
    case SDLK_END: return RETROK_END;
    case SDLK_PAGEUP: return RETROK_PAGEUP;
    case SDLK_PAGEDOWN: return RETROK_PAGEDOWN;
    case SDLK_UP: return RETROK_UP;
    case SDLK_DOWN: return RETROK_DOWN;
    case SDLK_LEFT: return RETROK_LEFT;
    case SDLK_RIGHT: return RETROK_RIGHT;
    case SDLK_F1: return RETROK_F1;
    case SDLK_F2: return RETROK_F2;
    case SDLK_F3: return RETROK_F3;
    case SDLK_F4: return RETROK_F4;
    case SDLK_F5: return RETROK_F5;
    case SDLK_F6: return RETROK_F6;
    case SDLK_F7: return RETROK_F7;
    case SDLK_F8: return RETROK_F8;
    case SDLK_F9: return RETROK_F9;
    case SDLK_F10: return RETROK_F10;
    case SDLK_F11: return RETROK_F11;
    case SDLK_F12: return RETROK_F12;
    case SDLK_LSHIFT: return RETROK_LSHIFT;
    case SDLK_RSHIFT: return RETROK_RSHIFT;
    case SDLK_LCTRL: return RETROK_LCTRL;
    case SDLK_RCTRL: return RETROK_RCTRL;
    case SDLK_LALT: return RETROK_LALT;
    case SDLK_RALT: return RETROK_RALT;
    case SDLK_LGUI: return RETROK_LMETA;
    case SDLK_RGUI: return RETROK_RMETA;
    default:
        break;
    }
    return RETROK_UNKNOWN;
}

static void
emu_ami_cycleDebugDma(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    int *debugDma = debugger.amigaDebug.debugDma;
    if (!debugDma) {
        return;
    }
    switch (*debugDma) {
    case 0: *debugDma = 2; break;
    case 2: *debugDma = 3; break;
    case 3: *debugDma = 4; break;
    case 5: *debugDma = (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_VIDEO_SYNC; break;
    case 4: *debugDma = (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_VIDEO_SYNC; break;
    case (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_VIDEO_SYNC: *debugDma = (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY; break;
    case (int)E9K_DEBUG_AMI_DMA_DEBUG_MODE_COLLECT_ONLY: *debugDma = 0; break;
    default: *debugDma = 0; break;
    }
}

static void
emu_ami_toggleCustom(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (amiga_custom_ui_isOpen()) {
        amiga_custom_ui_shutdown();
    } else {
        (void)amiga_custom_ui_init();
    }
}

static void
emu_ami_toggleCustomLog(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (amiga_custom_log_isOpen()) {
        amiga_custom_log_shutdown();
    } else {
        (void)amiga_custom_log_init();
    }
}

static void
emu_ami_toggleCustomAmiga(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (amiga_custom_isOpen()) {
        amiga_custom_shutdown();
    } else {
        (void)amiga_custom_init();
    }
}

static void
emu_ami_toggleMemview(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (amiga_memview_isOpen()) {
        amiga_memview_shutdown();
    } else {
        (void)amiga_memview_init();
    }
}

static void
emu_ami_toggleCoreStatusbar(e9ui_context_t *ctx, void *user)
{
    int enabled = 0;

    (void)ctx;
    (void)user;
    if (!libretro_host_amiga_getStatusbar(&enabled)) {
        return;
    }
    (void)libretro_host_amiga_setStatusbar(!enabled);
}

static void
emu_ami_createOverlays(e9ui_component_t* comp, e9ui_component_t* button_stack)
{
    emu_ami_tryBindCustomLogFrameCallback();

    e9ui_component_t *btn = e9ui_button_make("DMA", emu_ami_cycleDebugDma, comp);
    if (btn) {
        e9ui_button_setMini(btn, 1);
        e9ui_setFocusTarget(btn, comp);
        void* dmaDebugBtnMeta = alloc_strdup("dma_debug");
        e9ui_child_add(button_stack, btn, dmaDebugBtnMeta);
    }

    e9ui_component_t *btnStatusbar = e9ui_button_make("Status", emu_ami_toggleCoreStatusbar, comp);
    if (btnStatusbar) {
        e9ui_button_setMini(btnStatusbar, 1);
        e9ui_setFocusTarget(btnStatusbar, comp);
        void *statusbarBtnMeta = alloc_strdup("statusbar");
        e9ui_child_add(button_stack, btnStatusbar, statusbarBtnMeta);
    }

    e9ui_component_t *btnCustom = e9ui_button_make("Visualisers", emu_ami_toggleCustom, comp);
    if (btnCustom) {
        e9ui_button_setMini(btnCustom, 1);
        e9ui_setFocusTarget(btnCustom, comp);
        void *customBtnMeta = alloc_strdup("custom");
        e9ui_child_add(button_stack, btnCustom, customBtnMeta);
    }

    e9ui_component_t *btnMemview = e9ui_button_make("RAM", emu_ami_toggleMemview, comp);
    if (btnMemview) {
        e9ui_button_setMini(btnMemview, 1);
        e9ui_setFocusTarget(btnMemview, comp);
        void *memviewBtnMeta = alloc_strdup("amiga_memview");
        e9ui_child_add(button_stack, btnMemview, memviewBtnMeta);
    }

    e9ui_component_t *btnCustomAmiga = e9ui_button_make("Chipset", emu_ami_toggleCustomAmiga, comp);
    if (btnCustomAmiga) {
        e9ui_button_setMini(btnCustomAmiga, 1);
        e9ui_setFocusTarget(btnCustomAmiga, comp);
        void *customAmigaBtnMeta = alloc_strdup("custom_amiga");
        e9ui_child_add(button_stack, btnCustomAmiga, customAmigaBtnMeta);
    }

    e9ui_component_t *btnCustomLog = e9ui_button_make("Chipset Log", emu_ami_toggleCustomLog, comp);
    if (btnCustomLog) {
        e9ui_button_setMini(btnCustomLog, 1);
        e9ui_setFocusTarget(btnCustomLog, comp);
        void *customLogBtnMeta = alloc_strdup("custom_log");
        e9ui_child_add(button_stack, btnCustomLog, customLogBtnMeta);
    }
}

static void
emu_ami_render(e9ui_context_t *ctx, SDL_Rect* dst, const SDL_Rect *clipRect)
{
    int rasterLineCount = 0;
    emu_ami_tryBindCustomLogFrameCallback();
    if (libretro_host_amiga_getRasterLineCount(&rasterLineCount)) {
        profile_checkpoints_renderScanlineOverlay(ctx,
                                                  dst,
                                                  clipRect,
                                                  (uint64_t)rasterLineCount,
                                                  0,
                                                  (uint64_t)rasterLineCount);
    }
    emu_ami_renderDmaDebugOverlay(ctx, dst);
    emu_ami_renderCopperDebugOverlay(ctx, dst);
    amiga_blittervis_renderOverlay(ctx, dst);
    emu_ami_renderSpriteVisOverlay(ctx, dst);
    emu_ami_renderCopperLegend(ctx, dst);
    emu_ami_renderSpriteLegend(ctx, dst);
}

static void
emu_ami_renderForeground(e9ui_context_t *ctx, SDL_Rect *dst)
{
    if (emu_ami_renderSpriteVisTooltipOverlay(ctx, dst)) {
        return;
    }
    emu_ami_renderCopperTooltipOverlay(ctx, dst);
}

const emu_system_iface_t emu_ami_iface = {
    .translateCharacter = emu_ami_translateCharacter,
    .translateModifiers = emu_ami_translateModifiers,
    .translateKey = emu_ami_translateKey,
    .mapKeyToJoypad = emu_ami_mapKeyToJoypad,
    .mouseCaptureSupported = 1,
    .mouseCaptureOnClickEnabled = emu_ami_mouseCaptureOnClickEnabled,
    .rangeBarCount = emu_ami_rangeBarCount,
    .rangeBarDescribe = emu_ami_rangeBarDescribe,
    .rangeBarChanged = emu_ami_rangeBarChanged,
    .rangeBarDragging = emu_ami_rangeBarDragging,
    .rangeBarTooltip = emu_ami_rangeBarTooltip,
    .rangeBarSync = emu_ami_rangeBarSync,
    .handleOverlayEvent = emu_ami_handleOverlayEvent,
    .adjustVideoBounds = emu_ami_adjustVideoBounds,
    .adjustVideoDst = emu_ami_adjustVideoDst,
    .createOverlays = emu_ami_createOverlays,
    .render = emu_ami_render,
    .renderForeground = emu_ami_renderForeground,
    .destroy = emu_ami_destroy,
};
