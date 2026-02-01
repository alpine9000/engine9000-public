/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <SDL_ttf.h>

#include "help.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_scroll.h"
#include "e9ui_text.h"

static const char *
help_baseName(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static void
help_closeModal(void)
{
    if (!e9ui->helpModal) {
        return;
    }
    e9ui_setHidden(e9ui->helpModal, 1);
    if (!e9ui->pendingRemove) {
        e9ui->pendingRemove = e9ui->helpModal;
    }
    e9ui->helpModal = NULL;
}

static void
help_uiClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    (void)user;
    help_closeModal();
}

static void
help_uiClose(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    help_closeModal();
}

static int
help_measureKeyWidth(e9ui_context_t *ctx, const char **keys, size_t count)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    int maxW = 0;
    if (!font) {
        return e9ui_scale_px(ctx, 80);
    }
    for (size_t i = 0; i < count; ++i) {
        if (!keys[i]) {
            continue;
        }
        int w = 0;
        if (TTF_SizeText(font, keys[i], &w, NULL) == 0) {
            if (w > maxW) {
                maxW = w;
            }
        }
    }
    return maxW;
}

static int
help_measureTextWidth(e9ui_context_t *ctx, const char *text)
{
    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    if (!font || !text || !*text) {
        return 0;
    }
    int w = 0;
    if (TTF_SizeText(font, text, &w, NULL) != 0) {
        return 0;
    }
    return w;
}

static e9ui_component_t *
help_makeRow(const char *key, const char *value, int keyW, int gap, SDL_Color keyColor, SDL_Color valueColor)
{
    e9ui_component_t *row = e9ui_hstack_make();
    e9ui_component_t *keyText = e9ui_text_make(key);
    e9ui_component_t *valueText = e9ui_text_make(value);
    e9ui_text_setColor(keyText, keyColor);
    e9ui_text_setColor(valueText, valueColor);
    e9ui_hstack_addFixed(row, keyText, keyW);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    e9ui_hstack_addFlex(row, valueText);
    return row;
}

static void
help_add(e9ui_component_t *stack, e9ui_component_t *item, e9ui_context_t *ctx, int colW, int *totalH)
{
    if (!item) {
        return;
    }
    if (stack) {
        e9ui_stack_addFixed(stack, item);
        if (item->preferredHeight) {
            *totalH += item->preferredHeight(item, ctx, colW);
        }
        return;
    }
    e9ui_childDestroy(item, ctx);
}

static void
help_addSpacer(e9ui_component_t *stack, int height, e9ui_context_t *ctx, int colW, int *totalH)
{
    if (!stack) {
        return;
    }
    e9ui_component_t *spacer = e9ui_vspacer_make(height);
    if (!spacer) {
        return;
    }
    e9ui_stack_addFixed(stack, spacer);
    if (spacer->preferredHeight) {
        *totalH += spacer->preferredHeight(spacer, ctx, colW);
    }
}

void
help_cancelModal(void)
{
    help_closeModal();
}

void
help_showModal(e9ui_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (e9ui->helpModal) {
        return;
    }
    int margin = e9ui_scale_px(ctx, 32);
    int w = ctx->winW - margin * 2;
    int h = ctx->winH - margin * 2;
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }
    e9ui_rect_t rect = { margin, margin, w, h };
    e9ui->helpModal = e9ui_modal_show(ctx, "HELP", rect, help_uiClosed, NULL);
    if (!e9ui->helpModal) {
        return;
    }

    int baseText = e9ui->theme.text.fontSize > 0 ? e9ui->theme.text.fontSize : E9UI_THEME_TEXT_FONT_SIZE;
    int headingSize = baseText + 2;
    SDL_Color headingColor = (SDL_Color){235, 235, 235, 255};
    SDL_Color bodyColor = (SDL_Color){210, 210, 210, 255};

    e9ui_component_t *stackLeft = e9ui_stack_makeVertical();
    e9ui_component_t *stackRight = e9ui_stack_makeVertical();
    int gap = 10;
    int gapSmall = 6;
    int colGap = e9ui_scale_px(ctx, 16);
    int colW = e9ui_scale_px(ctx, 320);
    int columnGap = e9ui_scale_px(ctx, 32);
    int contentHLeft = 0;
    int contentHRight = 0;
    e9ui_component_t *titleShortcuts = e9ui_text_make("DEBUGGER HOTKEYS");
    e9ui_text_setBold(titleShortcuts, 1);
    e9ui_text_setFontSize(titleShortcuts, headingSize);
    e9ui_text_setColor(titleShortcuts, headingColor);

    const char *shortcutKeys[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F11", "F12", "ESC", "TAB", "C", "P", "S", "N", "I", "Ctrl+Alt+B", "Ctrl+Alt+F", "Ctrl+Alt+G", "B", ",", ".", "/" };
    const char *shortcutVals[] = { "Help",
                                   "Screenshot to clipboard",
                                   "Toggle CRT mode",
                                   "Toggle FPS overlay",
                                   "Speed toggle",
                                   "Toggle audio",
                                   "Save state",
                                   "Restore state",
				   "Toggle rolling state record",
				   "Amiga <-> Neo Geo",
                                   "Toggle fullscreen",
				   "Activate console",
                                   "Continue",
                                   "Pause",
                                   "Step",
                                   "Next",
                                   "Step inst",
                                   "Frame step back",
                                   "Frame step",
                                   "Frame continue",
                                   "Add breakpoint (current)",
                                   "Checkpoint profile toggle",
                                   "Checkpoint reset",
                                   "Checkpoint dump" };
    int shortcutKeyW = help_measureKeyWidth(ctx, shortcutKeys, sizeof(shortcutKeys) / sizeof(shortcutKeys[0]));

    e9ui_component_t *titleInputs = e9ui_text_make("NEO GEO SHORTCUTS");
    e9ui_text_setBold(titleInputs, 1);
    e9ui_text_setFontSize(titleInputs, headingSize);
    e9ui_text_setColor(titleInputs, headingColor);

    e9ui_component_t *titleKeyboard = e9ui_text_make("Keyboard");
    e9ui_text_setBold(titleKeyboard, 1);
    e9ui_text_setColor(titleKeyboard, headingColor);

    const char *kbKeys[] = { "Arrows", "L/R Alt", "L/R Ctrl", "L/R Shift", "Space", "1", "5" };
    const char *kbVals[] = { "D-pad", "A", "B", "C", "D", "Start", "Select" };
    int kbKeyW = help_measureKeyWidth(ctx, kbKeys, sizeof(kbKeys) / sizeof(kbKeys[0]));

    e9ui_component_t *titleController = e9ui_text_make("NEO GEO JOYSTICK CONTROLS");
    e9ui_text_setBold(titleController, 1);
    e9ui_text_setColor(titleController, headingColor);

    const char *padKeys[] = { "Left stick / D-pad", "A", "B", "X", "Y", "LB", "RB", "Start", "Back" };
    const char *padVals[] = { "Directions", "A", "B", "C", "D", "L", "R", "Start", "Select" };
    int padKeyW = help_measureKeyWidth(ctx, padKeys, sizeof(padKeys) / sizeof(padKeys[0]));

    help_add(stackLeft, titleShortcuts, ctx, colW, &contentHLeft);
    help_addSpacer(stackLeft, gapSmall, ctx, colW, &contentHLeft);
    for (size_t i = 0; i < sizeof(shortcutKeys) / sizeof(shortcutKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(shortcutKeys[i], shortcutVals[i], shortcutKeyW, colGap, bodyColor, bodyColor);
        help_add(stackLeft, row, ctx, colW, &contentHLeft);
    }

    help_add(stackRight, titleInputs, ctx, colW, &contentHRight);
    help_addSpacer(stackRight, gapSmall, ctx, colW, &contentHRight);
    help_add(stackRight, titleKeyboard, ctx, colW, &contentHRight);
    help_addSpacer(stackRight, gapSmall, ctx, colW, &contentHRight);
    for (size_t i = 0; i < sizeof(kbKeys) / sizeof(kbKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(kbKeys[i], kbVals[i], kbKeyW, colGap, bodyColor, bodyColor);
        help_add(stackRight, row, ctx, colW, &contentHRight);
    }

    help_addSpacer(stackRight, gap, ctx, colW, &contentHRight);
    help_add(stackRight, titleController, ctx, colW, &contentHRight);
    help_addSpacer(stackRight, gapSmall, ctx, colW, &contentHRight);
    for (size_t i = 0; i < sizeof(padKeys) / sizeof(padKeys[0]); ++i) {
        e9ui_component_t *row = help_makeRow(padKeys[i], padVals[i], padKeyW, colGap, bodyColor, bodyColor);
        help_add(stackRight, row, ctx, colW, &contentHRight);
    }

    e9ui_component_t *titleCli = e9ui_text_make("COMMAND LINE");
    e9ui_text_setBold(titleCli, 1);
    e9ui_text_setFontSize(titleCli, headingSize);
    e9ui_text_setColor(titleCli, headingColor);

    const char *prog = debugger.argv0[0] ? help_baseName(debugger.argv0) : "e9k-debugger";
    char cliCmd[PATH_MAX + 16];
    snprintf(cliCmd, sizeof(cliCmd), "%s --help", prog);
    e9ui_component_t *lineCliPrefix = e9ui_text_make("Use");
    e9ui_component_t *lineCliCmd = e9ui_text_make(cliCmd);
    e9ui_component_t *lineCliSuffix = e9ui_text_make("for options");
    e9ui_text_setColor(lineCliPrefix, bodyColor);
    e9ui_text_setColor(lineCliCmd, headingColor);
    e9ui_text_setColor(lineCliSuffix, bodyColor);

    int cliGap = e9ui_scale_px(ctx, 6);
    int prefixW = help_measureTextWidth(ctx, "Use");
    int cmdW = help_measureTextWidth(ctx, cliCmd);
    e9ui_component_t *rowCli = e9ui_hstack_make();
    e9ui_hstack_addFixed(rowCli, lineCliPrefix, prefixW);
    e9ui_hstack_addFixed(rowCli, e9ui_spacer_make(cliGap), cliGap);
    e9ui_hstack_addFixed(rowCli, lineCliCmd, cmdW);
    e9ui_hstack_addFixed(rowCli, e9ui_spacer_make(cliGap), cliGap);
    e9ui_hstack_addFlex(rowCli, lineCliSuffix);

    help_addSpacer(stackLeft, gap, ctx, colW, &contentHLeft);
    help_add(stackLeft, titleCli, ctx, colW, &contentHLeft);
    help_addSpacer(stackLeft, gapSmall, ctx, colW, &contentHLeft);
    help_add(stackLeft, rowCli, ctx, colW, &contentHLeft);

    int contentH = contentHLeft > contentHRight ? contentHLeft : contentHRight;
    e9ui_component_t *columns = e9ui_hstack_make();
    if (columns) {
        e9ui_hstack_addFixed(columns, stackLeft, colW);
        e9ui_hstack_addFixed(columns, e9ui_spacer_make(columnGap), columnGap);
        e9ui_hstack_addFixed(columns, stackRight, colW);
    } else {
        e9ui_childDestroy(stackRight, ctx);
        stackRight = NULL;
    }
    e9ui_component_t *scroll = e9ui_scroll_make(columns ? columns : stackLeft);
    e9ui_scroll_setContentHeightPx(scroll, contentH);
    e9ui_component_t *center = e9ui_center_make(scroll);
    int centerW = e9ui_unscale_px(ctx, colW * 2 + columnGap);
    e9ui_center_setSize(center, centerW, 0);
    e9ui_component_t *btnClose = e9ui_button_make("Close", help_uiClose, NULL);
    e9ui_component_t *footer = e9ui_flow_make();
    if (footer) {
        e9ui_flow_setPadding(footer, 0);
        e9ui_flow_setSpacing(footer, 8);
        e9ui_flow_setWrap(footer, 0);
        if (btnClose) {
            e9ui_button_setTheme(btnClose, e9ui_theme_button_preset_green());
            e9ui_flow_add(footer, btnClose);
        }
    }
    e9ui_component_t *overlay = e9ui_overlay_make(center, footer);
    if (overlay) {
        e9ui_overlay_setAnchor(overlay, e9ui_anchor_bottom_right);
        e9ui_overlay_setMargin(overlay, 12);
    }
    e9ui_modal_setBodyChild(e9ui->helpModal, overlay ? overlay : center, ctx);
}
