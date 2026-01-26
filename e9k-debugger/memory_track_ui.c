/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "memory_track_ui.h"

#include "alloc.h"
#include "debug.h"
#include "debugger.h"
#include "e9ui.h"
#include "e9ui_button.h"
#include "e9ui_hstack.h"
#include "e9ui_scroll.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_text.h"
#include "e9ui_text_cache.h"
#include "e9ui_textbox.h"
#include "e9ui_theme.h"
#include "libretro_host.h"
#include "protect.h"
#include "state_buffer.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MEMORY_TRACK_UI_TITLE "ENGINE9000 DEBUGGER - MEMORY TRACKER"
#define MEMORY_TRACK_UI_REGION_BASE 0x00100000u
#define MEMORY_TRACK_UI_REGION_SIZE 0x10000u

typedef struct memory_track_entry {
    uint32_t address;
    uint32_t value;
} memory_track_entry_t;

typedef struct memory_track_frame_data {
    uint64_t frameNo;
    memory_track_entry_t *entries;
    size_t entryCount;
} memory_track_frame_data_t;

typedef struct memory_track_ui memory_track_ui_t;

typedef struct memory_track_table_state {
    memory_track_ui_t *ui;
} memory_track_table_state_t;

struct memory_track_ui {
    int open;
    int closeRequested;
    SDL_Window *window;
    SDL_Renderer *renderer;
    uint32_t windowId;
    e9ui_context_t ctx;
    e9ui_component_t *root;
    e9ui_component_t *pendingRemove;
    e9ui_component_t *protectModal;
    e9ui_component_t *protectCbBlock;
    e9ui_component_t *protectCbSet;
    e9ui_component_t *protectValueBox;
    uint32_t protectAddress;
    int protectAccessSize;
    int protectRadioUpdating;
    e9ui_component_t *fullscreen;
    e9ui_component_t *headerRow;
    e9ui_component_t *scroll;
    e9ui_component_t *table;
    e9ui_component_t *modeBtn8;
    e9ui_component_t *modeBtn16;
    e9ui_component_t *modeBtn32;
    e9ui_component_t *filterBtn;
    e9ui_component_t **frameInputs;
    size_t frameInputsCap;
    size_t frameInputsCount;
    char **frameTexts;
    size_t frameTextsCap;
    size_t frameTextsCount;
    e9ui_component_t **filterInputs;
    size_t filterInputsCap;
    size_t filterInputsCount;
    char **filterTexts;
    size_t filterTextsCap;
    size_t filterTextsCount;
    e9ui_component_t **addressLinks;
    size_t addressLinksCap;
    size_t addressLinksCount;
    memory_track_frame_data_t *frames;
    size_t frameCount;
    uint32_t *addresses;
    size_t addressCount;
    size_t addressCap;
    size_t *frameIndices;
    size_t frameIndicesCap;
    int columnCount;
    int columnWidth;
    int addressWidth;
    int columnGap;
    int modeButtonWidth;
    int modeButtonGap;
    int modeWidth;
    int filterButtonWidth;
    int filterButtonGap;
    int padding;
    int rowHeight;
    int headerHeight;
    int contentHeight;
    int hasActiveFrames;
    int accessSize;
    int requireAllColumns;
    int needsRebuild;
    int needsRefresh;
    char error[128];
};

static memory_track_ui_t memory_track_ui_state = {0};

static void
memory_track_ui_clearAddressLinks(memory_track_ui_t *ui);

static void
memory_track_ui_clearFrameMarkersInternal(memory_track_ui_t *ui);

static void
memory_track_ui_clearFrameMarkers(e9ui_context_t *ctx, void *user);

static void
memory_track_ui_refocusMain(void)
{
    SDL_Window *mainWin = debugger.ui.ctx.window;
    if (!mainWin) {
        return;
    }
    SDL_ShowWindow(mainWin);
    SDL_RaiseWindow(mainWin);
    SDL_SetWindowInputFocus(mainWin);
    e9ui_component_t *geo = e9ui_findById(debugger.ui.root, "geo_view");
    if (geo) {
        e9ui_setFocus(&debugger.ui.ctx, geo);
    }
}

static void
memory_track_ui_setError(memory_track_ui_t *ui, const char *fmt, ...)
{
    if (!ui) {
        return;
    }
    ui->error[0] = '\0';
    if (!fmt || !*fmt) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->error, sizeof(ui->error), fmt, ap);
    va_end(ap);
    ui->error[sizeof(ui->error) - 1] = '\0';
}

static int
memory_track_ui_parseFrameText(memory_track_ui_t *ui, const char *text, uint64_t *outFrame, int *outEmpty)
{
    if (!ui || !outFrame) {
        return 0;
    }
    if (!text || !*text) {
        if (outEmpty) {
            *outEmpty = 1;
        }
        return 1;
    }
    if (outEmpty) {
        *outEmpty = 0;
    }
    char *end = NULL;
    unsigned long long val = strtoull(text, &end, 0);
    if (!end || end == text) {
        memory_track_ui_setError(ui, "Invalid frame: \"%s\"", text);
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end) {
        memory_track_ui_setError(ui, "Invalid frame: \"%s\"", text);
        return 0;
    }
    *outFrame = (uint64_t)val;
    return 1;
}

static int
memory_track_ui_readFrameBytes(uint64_t frameNo, uint32_t base, uint8_t *out, size_t size)
{
    if (!state_buffer_restoreFrameNo(frameNo)) {
        return 0;
    }
    if (!libretro_host_debugReadMemory(base, out, size)) {
        return 0;
    }
    return 1;
}

static uint32_t
memory_track_ui_readValueBE(const uint8_t *data, int size)
{
    if (!data || size <= 0) {
        return 0;
    }
    if (size == 1) {
        return (uint32_t)data[0];
    }
    if (size == 2) {
        return ((uint32_t)data[0] << 8) | (uint32_t)data[1];
    }
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void
memory_track_ui_clearData(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    memory_track_ui_clearAddressLinks(ui);
    if (ui->frames) {
        for (size_t frameIndex = 0; frameIndex < ui->frameCount; ++frameIndex) {
            alloc_free(ui->frames[frameIndex].entries);
        }
        alloc_free(ui->frames);
    }
    alloc_free(ui->addresses);
    alloc_free(ui->frameIndices);
    ui->frames = NULL;
    ui->frameCount = 0;
    ui->addresses = NULL;
    ui->addressCount = 0;
    ui->addressCap = 0;
    ui->frameIndices = NULL;
    ui->frameIndicesCap = 0;
    ui->hasActiveFrames = 0;
}

static int
memory_track_ui_isEmptyText(const char *text)
{
    return (!text || !text[0]) ? 1 : 0;
}

static int
memory_track_ui_ensureFrameTextsCap(memory_track_ui_t *ui, size_t count)
{
    if (!ui) {
        return 0;
    }
    if (count <= ui->frameTextsCap) {
        return 1;
    }
    size_t newCap = ui->frameTextsCap ? ui->frameTextsCap : 4;
    while (newCap < count) {
        newCap *= 2;
    }
    char **next = (char**)alloc_realloc(ui->frameTexts, newCap * sizeof(char*));
    if (!next) {
        return 0;
    }
    ui->frameTexts = next;
    ui->frameTextsCap = newCap;
    return 1;
}

static int
memory_track_ui_ensureFilterTextsCap(memory_track_ui_t *ui, size_t count)
{
    if (!ui) {
        return 0;
    }
    if (count <= ui->filterTextsCap) {
        return 1;
    }
    size_t newCap = ui->filterTextsCap ? ui->filterTextsCap : 4;
    while (newCap < count) {
        newCap *= 2;
    }
    char **next = (char**)alloc_realloc(ui->filterTexts, newCap * sizeof(char*));
    if (!next) {
        return 0;
    }
    ui->filterTexts = next;
    ui->filterTextsCap = newCap;
    return 1;
}

static void
memory_track_ui_setStoredFrameText(memory_track_ui_t *ui, size_t index, const char *text)
{
    if (!ui) {
        return;
    }
    size_t needed = index + 1;
    if (!memory_track_ui_ensureFrameTextsCap(ui, needed)) {
        return;
    }
    if (needed > ui->frameTextsCount) {
        for (size_t i = ui->frameTextsCount; i < needed; ++i) {
            ui->frameTexts[i] = NULL;
        }
        ui->frameTextsCount = needed;
    }
    if (index < ui->frameTextsCount) {
        alloc_free(ui->frameTexts[index]);
        ui->frameTexts[index] = NULL;
        if (text && text[0]) {
            ui->frameTexts[index] = alloc_strdup(text);
        }
    }
}

static void
memory_track_ui_storeFrameTexts(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    size_t count = (ui->frameInputs && ui->frameInputsCount) ? ui->frameInputsCount : 0;
    if (count == 0) {
        return;
    }
    if (!memory_track_ui_ensureFrameTextsCap(ui, count)) {
        return;
    }
    if (count > ui->frameTextsCount) {
        for (size_t textIndex = ui->frameTextsCount; textIndex < count; ++textIndex) {
            ui->frameTexts[textIndex] = NULL;
        }
        ui->frameTextsCount = count;
    }
    for (size_t textIndex = 0; textIndex < count; ++textIndex) {
        const char *text = e9ui_textbox_getText(ui->frameInputs[textIndex]);
        alloc_free(ui->frameTexts[textIndex]);
        ui->frameTexts[textIndex] = text ? alloc_strdup(text) : NULL;
    }
}

static void
memory_track_ui_storeFilterTexts(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    size_t count = (ui->filterInputs && ui->filterInputsCount) ? ui->filterInputsCount : 0;
    if (count == 0) {
        return;
    }
    if (!memory_track_ui_ensureFilterTextsCap(ui, count)) {
        return;
    }
    if (count > ui->filterTextsCount) {
        for (size_t textIndex = ui->filterTextsCount; textIndex < count; ++textIndex) {
            ui->filterTexts[textIndex] = NULL;
        }
        ui->filterTextsCount = count;
    }
    for (size_t textIndex = 0; textIndex < count; ++textIndex) {
        const char *text = e9ui_textbox_getText(ui->filterInputs[textIndex]);
        alloc_free(ui->filterTexts[textIndex]);
        ui->filterTexts[textIndex] = text ? alloc_strdup(text) : NULL;
    }
}

static size_t
memory_track_ui_findEmptyFrameIndex(memory_track_ui_t *ui)
{
    if (!ui) {
        return 0;
    }
    if (ui->frameInputs && ui->frameInputsCount) {
        for (size_t i = 0; i < ui->frameInputsCount; ++i) {
            const char *text = e9ui_textbox_getText(ui->frameInputs[i]);
            if (memory_track_ui_isEmptyText(text)) {
                return i;
            }
        }
    }
    for (size_t i = 0; i < ui->frameTextsCount; ++i) {
        if (memory_track_ui_isEmptyText(ui->frameTexts[i])) {
            return i;
        }
    }
    return ui->frameTextsCount;
}

static void
memory_track_ui_setFrameTextAtIndex(memory_track_ui_t *ui, size_t index, const char *text)
{
    if (!ui) {
        return;
    }
    if (ui->frameInputs && index < ui->frameInputsCount && ui->frameInputs[index]) {
        e9ui_textbox_setText(ui->frameInputs[index], text ? text : "");
    }
    memory_track_ui_setStoredFrameText(ui, index, text);
}

static void
memory_track_ui_clearFrameMarkersInternal(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    size_t count = ui->frameInputsCount;
    if (ui->frameTextsCount > count) {
        count = ui->frameTextsCount;
    }
    for (size_t i = 0; i < count; ++i) {
        memory_track_ui_setFrameTextAtIndex(ui, i, "");
    }
    ui->needsRefresh = 1;
}

static void
memory_track_ui_clearFrameMarkers(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_clearFrameMarkersInternal((memory_track_ui_t*)user);
}

static void
memory_track_ui_clearAddressLinks(memory_track_ui_t *ui)
{
    if (!ui || !ui->addressLinks) {
        if (ui) {
            ui->addressLinksCount = 0;
        }
        return;
    }
    for (size_t i = 0; i < ui->addressLinksCount; ++i) {
        if (ui->addressLinks[i]) {
            e9ui_childDestroy(ui->addressLinks[i], &ui->ctx);
            ui->addressLinks[i] = NULL;
        }
    }
    ui->addressLinksCount = 0;
}

static void
memory_track_ui_protectClose(memory_track_ui_t *ui)
{
    if (!ui || !ui->protectModal) {
        return;
    }
    e9ui_setHidden(ui->protectModal, 1);
    ui->pendingRemove = ui->protectModal;
    ui->protectModal = NULL;
    ui->protectCbBlock = NULL;
    ui->protectCbSet = NULL;
    ui->protectValueBox = NULL;
}

static void
memory_track_ui_protectCancel(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_protectClose((memory_track_ui_t*)user);
}

static void
memory_track_ui_protectClosed(e9ui_component_t *modal, void *user)
{
    (void)modal;
    memory_track_ui_protectClose((memory_track_ui_t*)user);
}

static int
memory_track_ui_parseU32Strict(const char *s, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    if (!s || !*s || !out) {
        return 0;
    }
    if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) {
        return 0;
    }
    const char *p = s + 2;
    if (!*p) {
        return 0;
    }
    while (*p) {
        if (!isxdigit((unsigned char)*p)) {
            return 0;
        }
        ++p;
    }
    errno = 0;
    unsigned long long v = strtoull(s, NULL, 16);
    if (errno != 0 || v > 0xffffffffULL) {
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static void
memory_track_ui_protectRadioChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui || ui->protectRadioUpdating) {
        return;
    }
    ui->protectRadioUpdating = 1;
    if (self == ui->protectCbBlock) {
        if (ui->protectCbSet) {
            e9ui_checkbox_setSelected(ui->protectCbSet, selected ? 0 : 1, ctx);
        }
    } else if (self == ui->protectCbSet) {
        if (ui->protectCbBlock) {
            e9ui_checkbox_setSelected(ui->protectCbBlock, selected ? 0 : 1, ctx);
        }
    }
    ui->protectRadioUpdating = 0;
}

static void
memory_track_ui_protectApply(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    uint32_t size_bits = 0;
    if (ui->protectAccessSize == 1) {
        size_bits = 8;
    } else if (ui->protectAccessSize == 2) {
        size_bits = 16;
    } else if (ui->protectAccessSize == 4) {
        size_bits = 32;
    }
    if (size_bits == 0) {
        debug_error("protect: invalid size");
        return;
    }
    int mode_block = 0;
    int mode_set = 0;
    if (ui->protectCbBlock) {
        mode_block = e9ui_checkbox_isSelected(ui->protectCbBlock);
    }
    if (ui->protectCbSet) {
        mode_set = e9ui_checkbox_isSelected(ui->protectCbSet);
    }
    if (mode_block == mode_set) {
        debug_error("protect: choose either block or set");
        return;
    }
    uint32_t addr = ui->protectAddress & 0x00ffffffu;
    int ok = 0;
    if (mode_block) {
        ok = protect_addBlock(addr, size_bits);
    } else {
        const char *text = NULL;
        if (ui->protectValueBox) {
            text = e9ui_textbox_getText(ui->protectValueBox);
        }
        uint32_t value = 0;
        if (!memory_track_ui_parseU32Strict(text, &value)) {
            debug_error("protect: invalid set value '%s' (expected 0x...)", text ? text : "");
            return;
        }
        ok = protect_addSet(addr, value, size_bits);
    }
    if (!ok) {
        debug_error("protect: failed (core protect API missing?)");
        return;
    }
    debug_printf("protect: added\n");
    memory_track_ui_protectClose(ui);
}

static void
memory_track_ui_showProtectModal(memory_track_ui_t *ui, uint32_t address)
{
    if (!ui || !ui->root) {
        return;
    }
    if (ui->protectModal) {
        memory_track_ui_protectClose(ui);
    }
    int winW = 0;
    int winH = 0;
    if (ui->renderer) {
        SDL_GetRendererOutputSize(ui->renderer, &winW, &winH);
    }
    int modalW = e9ui_scale_px(&ui->ctx, 520);
    int modalH = e9ui_scale_px(&ui->ctx, 240);
    if (modalW < 1) {
        modalW = 1;
    }
    if (modalH < 1) {
        modalH = 1;
    }
    int x = (winW - modalW) / 2;
    int y = (winH - modalH) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    char title[80];
    int bits = 0;
    if (ui->accessSize == 1) {
        bits = 8;
    } else if (ui->accessSize == 2) {
        bits = 16;
    } else if (ui->accessSize == 4) {
        bits = 32;
    }
    snprintf(title, sizeof(title), "PROTECT - %06X (%d bits)", address & 0x00FFFFFFu, bits);
    e9ui_rect_t rect = { x, y, modalW, modalH };
    ui->protectAddress = address;
    ui->protectAccessSize = ui->accessSize;
    e9ui_component_t *modal = e9ui_modal_make(title, rect, memory_track_ui_protectClosed, ui);
    if (!modal) {
        return;
    }
    ui->protectModal = modal;
    if (ui->root->name && strcmp(ui->root->name, "e9ui_stack") == 0) {
        e9ui_stack_addFixed(ui->root, modal);
    } else {
        e9ui_child_add(ui->root, modal, alloc_strdup("protect_modal"));
    }

    e9ui_component_t *cb_block = e9ui_checkbox_make("Block", 1, memory_track_ui_protectRadioChanged, ui);
    e9ui_component_t *cb_set = e9ui_checkbox_make("Set Value", 0, memory_track_ui_protectRadioChanged, ui);
    e9ui_component_t *value_box = e9ui_textbox_make(16, NULL, NULL, NULL);
    ui->protectCbBlock = cb_block;
    ui->protectCbSet = cb_set;
    ui->protectValueBox = value_box;
    if (value_box) {
        e9ui_textbox_setPlaceholder(value_box, "Value");
    }
    e9ui_component_t *set_row = e9ui_hstack_make();
    int checkboxW = e9ui_scale_px(&ui->ctx, 140);
    int valueW = e9ui_scale_px(&ui->ctx, 140);
    int rowGap = e9ui_scale_px(&ui->ctx, 8);
    if (set_row) {
        if (cb_set) {
            e9ui_hstack_addFixed(set_row, cb_set, checkboxW);
        }
        e9ui_hstack_addFixed(set_row, e9ui_spacer_make(rowGap), rowGap);
        if (value_box) {
            e9ui_hstack_addFixed(set_row, value_box, valueW);
        }
        e9ui_hstack_addFlex(set_row, e9ui_spacer_make(1));
    }

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    if (cb_block) {
        e9ui_stack_addFixed(stack, cb_block);
    }
    e9ui_stack_addFixed(stack, e9ui_vspacer_make(8));
    if (set_row) {
        e9ui_stack_addFixed(stack, set_row);
    }
    e9ui_component_t *content_box = e9ui_box_make(stack);
    e9ui_box_setPadding(content_box, 12);
    e9ui_component_t *center = e9ui_center_make(content_box);
    if (center) {
        e9ui_center_setSize(center, 420, 120);
    }

    e9ui_component_t *btn_protect = e9ui_button_make("Protect", memory_track_ui_protectApply, ui);
    e9ui_component_t *btn_cancel = e9ui_button_make("Cancel", memory_track_ui_protectCancel, ui);
    e9ui_component_t *footer = e9ui_flow_make();
    e9ui_flow_setPadding(footer, 0);
    e9ui_flow_setSpacing(footer, 8);
    e9ui_flow_setWrap(footer, 0);
    if (btn_protect) {
        e9ui_button_setTheme(btn_protect, e9ui_theme_button_preset_green());
        e9ui_button_setGlowPulse(btn_protect, 1);
        e9ui_flow_add(footer, btn_protect);
    }
    if (btn_cancel) {
        e9ui_button_setTheme(btn_cancel, e9ui_theme_button_preset_red());
        e9ui_button_setGlowPulse(btn_cancel, 1);
        e9ui_flow_add(footer, btn_cancel);
    }
    e9ui_component_t *overlay = e9ui_overlay_make(center, footer);
    e9ui_overlay_setAnchor(overlay, e9ui_anchor_bottom_right);
    e9ui_overlay_setMargin(overlay, 12);
    e9ui_modal_setBodyChild(modal, overlay, &ui->ctx);
}

static void
memory_track_ui_addressLinkClicked(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    uint32_t address = (uint32_t)(uintptr_t)user;
    memory_track_ui_showProtectModal(&memory_track_ui_state, address);
}

static void
memory_track_ui_updateModeButtons(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();
    if (ui->modeBtn8) {
        if (ui->accessSize == 1) {
            e9ui_button_setTheme(ui->modeBtn8, activeTheme);
        } else {
            e9ui_button_clearTheme(ui->modeBtn8);
        }
    }
    if (ui->modeBtn16) {
        if (ui->accessSize == 2) {
            e9ui_button_setTheme(ui->modeBtn16, activeTheme);
        } else {
            e9ui_button_clearTheme(ui->modeBtn16);
        }
    }
    if (ui->modeBtn32) {
        if (ui->accessSize == 4) {
            e9ui_button_setTheme(ui->modeBtn32, activeTheme);
        } else {
            e9ui_button_clearTheme(ui->modeBtn32);
        }
    }
}

static void
memory_track_ui_updateFilterButton(memory_track_ui_t *ui)
{
    if (!ui || !ui->filterBtn) {
        return;
    }
    const e9k_theme_button_t *activeTheme = e9ui_theme_button_preset_profile_active();
    if (ui->requireAllColumns) {
        e9ui_button_setTheme(ui->filterBtn, activeTheme);
    } else {
        e9ui_button_clearTheme(ui->filterBtn);
    }
}

static void
memory_track_ui_setAccessSize(memory_track_ui_t *ui, int size)
{
    if (!ui) {
        return;
    }
    if (size != 1 && size != 2 && size != 4) {
        return;
    }
    if (ui->accessSize == size) {
        return;
    }
    ui->accessSize = size;
    memory_track_ui_updateModeButtons(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_toggleRequireAll(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    ui->requireAllColumns = ui->requireAllColumns ? 0 : 1;
    memory_track_ui_updateFilterButton(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_access8(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setAccessSize((memory_track_ui_t*)user, 1);
}

static void
memory_track_ui_access16(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setAccessSize((memory_track_ui_t*)user, 2);
}

static void
memory_track_ui_access32(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_setAccessSize((memory_track_ui_t*)user, 4);
}

static int
memory_track_ui_compareU32(const void *a, const void *b)
{
    uint32_t va = *(const uint32_t*)a;
    uint32_t vb = *(const uint32_t*)b;
    if (va < vb) {
        return -1;
    }
    if (va > vb) {
        return 1;
    }
    return 0;
}

static int
memory_track_ui_appendAddresses(memory_track_ui_t *ui, const memory_track_entry_t *entries, size_t entryCount)
{
    if (!ui || !entries || entryCount == 0) {
        return 1;
    }
    size_t needed = ui->addressCount + entryCount;
    if (needed > ui->addressCap) {
        size_t newCap = ui->addressCap ? ui->addressCap : 256;
        while (newCap < needed) {
            newCap *= 2;
        }
        uint32_t *next = (uint32_t*)alloc_realloc(ui->addresses, newCap * sizeof(uint32_t));
        if (!next) {
            return 0;
        }
        ui->addresses = next;
        ui->addressCap = newCap;
    }
    for (size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        ui->addresses[ui->addressCount++] = entries[entryIndex].address;
    }
    return 1;
}

static int
memory_track_ui_buildAddressLinks(memory_track_ui_t *ui)
{
    if (!ui) {
        return 0;
    }
    memory_track_ui_clearAddressLinks(ui);
    if (ui->addressCount == 0) {
        return 1;
    }
    if (ui->addressCount > ui->addressLinksCap) {
        size_t newCap = ui->addressLinksCap ? ui->addressLinksCap : 64;
        while (newCap < ui->addressCount) {
            newCap *= 2;
        }
        e9ui_component_t **next = (e9ui_component_t**)alloc_realloc(ui->addressLinks,
                                                                     newCap * sizeof(e9ui_component_t*));
        if (!next) {
            return 0;
        }
        ui->addressLinks = next;
        ui->addressLinksCap = newCap;
    }
    for (size_t i = 0; i < ui->addressCount; ++i) {
        char text[16];
        uint32_t address = ui->addresses[i];
        snprintf(text, sizeof(text), "0x%06X", address & 0x00FFFFFFu);
        e9ui_component_t *link = e9ui_link_make(text, memory_track_ui_addressLinkClicked,
                                                (void*)(uintptr_t)address);
        ui->addressLinks[i] = link;
        if (!link) {
            ui->addressLinksCount = i;
            return 0;
        }
    }
    ui->addressLinksCount = ui->addressCount;
    return 1;
}

static int
memory_track_ui_collectData(memory_track_ui_t *ui, int columnCount)
{
    if (!ui || columnCount <= 0) {
        return 0;
    }
    memory_track_ui_clearData(ui);
    ui->frames = (memory_track_frame_data_t*)alloc_calloc((size_t)columnCount, sizeof(*ui->frames));
    if (!ui->frames) {
        memory_track_ui_setError(ui, "Out of memory");
        return 0;
    }

    uint64_t restoreFrame = state_buffer_getCurrentFrameNo();
    uint8_t *refBytes = NULL;
    uint8_t *baseBytes = (uint8_t*)alloc_alloc(MEMORY_TRACK_UI_REGION_SIZE);
    uint8_t *curBytes = (uint8_t*)alloc_alloc(MEMORY_TRACK_UI_REGION_SIZE);
    uint64_t *frameNos = (uint64_t*)alloc_calloc((size_t)columnCount, sizeof(uint64_t));
    int *frameActive = (int*)alloc_calloc((size_t)columnCount, sizeof(int));
    if (!baseBytes || !curBytes || !frameNos || !frameActive) {
        memory_track_ui_setError(ui, "Out of memory");
        alloc_free(frameNos);
        alloc_free(frameActive);
        alloc_free(refBytes);
        alloc_free(baseBytes);
        alloc_free(curBytes);
        memory_track_ui_clearData(ui);
        return 0;
    }
    int success = 0;
    int hasActive = 0;
    int accessSize = ui->accessSize > 0 ? ui->accessSize : 1;
    uint64_t prevActiveFrameNo = 0;
    int hasPrevActive = 0;
    for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
        ui->frames[frameIndex].frameNo = 0;
        ui->frames[frameIndex].entries = NULL;
        ui->frames[frameIndex].entryCount = 0;
        if (!ui->frameInputs || frameIndex >= (int)ui->frameInputsCount) {
            continue;
        }
        const char *text = e9ui_textbox_getText(ui->frameInputs[frameIndex]);
        uint64_t frameNo = 0;
        int empty = 0;
        if (!memory_track_ui_parseFrameText(ui, text, &frameNo, &empty)) {
            goto cleanup;
        }
        if (empty) {
            continue;
        }
        if (!state_buffer_hasFrameNo(frameNo)) {
            memory_track_ui_setError(ui, "Frame %llu not in state buffer", (unsigned long long)frameNo);
            goto cleanup;
        }
        frameActive[frameIndex] = 1;
        frameNos[frameIndex] = frameNo;
        ui->frames[frameIndex].frameNo = frameNo;
    }

    if (frameActive[0]) {
        refBytes = (uint8_t*)alloc_alloc(MEMORY_TRACK_UI_REGION_SIZE);
        if (!refBytes) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        if (!memory_track_ui_readFrameBytes(frameNos[0], MEMORY_TRACK_UI_REGION_BASE, refBytes,
                                            MEMORY_TRACK_UI_REGION_SIZE)) {
            memory_track_ui_setError(ui, "Failed to read frame %llu",
                                     (unsigned long long)frameNos[0]);
            goto cleanup;
        }
        prevActiveFrameNo = frameNos[0];
        hasPrevActive = 1;
    }

    for (int frameIndex = 1; frameIndex < columnCount; ++frameIndex) {
        if (!frameActive[frameIndex]) {
            continue;
        }
        uint64_t frameNo = frameNos[frameIndex];
        uint64_t baseFrameNo = 0;
        if (hasPrevActive) {
            baseFrameNo = prevActiveFrameNo;
        } else {
            if (frameNo == 0 || !state_buffer_hasFrameNo(frameNo - 1)) {
                memory_track_ui_setError(ui, "Previous frame %llu not in state buffer",
                                         (unsigned long long)(frameNo - 1));
                goto cleanup;
            }
            baseFrameNo = frameNo - 1;
        }
        if (!memory_track_ui_readFrameBytes(baseFrameNo, MEMORY_TRACK_UI_REGION_BASE, baseBytes,
                                            MEMORY_TRACK_UI_REGION_SIZE)) {
            memory_track_ui_setError(ui, "Failed to read frame %llu",
                                     (unsigned long long)baseFrameNo);
            goto cleanup;
        }
        if (!memory_track_ui_readFrameBytes(frameNo, MEMORY_TRACK_UI_REGION_BASE, curBytes,
                                            MEMORY_TRACK_UI_REGION_SIZE)) {
            memory_track_ui_setError(ui, "Failed to read frame %llu", (unsigned long long)frameNo);
            goto cleanup;
        }
        size_t diffCount = 0;
        for (size_t offset = 0; offset + (size_t)accessSize <= MEMORY_TRACK_UI_REGION_SIZE; offset += (size_t)accessSize) {
            uint32_t baseValue = memory_track_ui_readValueBE(baseBytes + offset, accessSize);
            uint32_t curValue = memory_track_ui_readValueBE(curBytes + offset, accessSize);
            if (baseValue != curValue) {
                diffCount++;
            }
        }
        memory_track_entry_t *entries = NULL;
        if (diffCount > 0) {
            entries = (memory_track_entry_t*)alloc_alloc(diffCount * sizeof(*entries));
            if (!entries) {
                memory_track_ui_setError(ui, "Out of memory");
                goto cleanup;
            }
            size_t entryIndex = 0;
            for (size_t offset = 0; offset + (size_t)accessSize <= MEMORY_TRACK_UI_REGION_SIZE; offset += (size_t)accessSize) {
                uint32_t baseValue = memory_track_ui_readValueBE(baseBytes + offset, accessSize);
                uint32_t curValue = memory_track_ui_readValueBE(curBytes + offset, accessSize);
                if (baseValue != curValue) {
                    entries[entryIndex].address = MEMORY_TRACK_UI_REGION_BASE + (uint32_t)offset;
                    entries[entryIndex].value = curValue;
                    entryIndex++;
                }
            }
        }
        ui->frames[frameIndex].frameNo = frameNo;
        ui->frames[frameIndex].entries = entries;
        ui->frames[frameIndex].entryCount = diffCount;
        if (!memory_track_ui_appendAddresses(ui, entries, diffCount)) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        hasActive = 1;
        prevActiveFrameNo = frameNo;
        hasPrevActive = 1;
    }

    if (ui->addressCount > 1) {
        qsort(ui->addresses, ui->addressCount, sizeof(uint32_t), memory_track_ui_compareU32);
        size_t writeIndex = 0;
        for (size_t readIndex = 0; readIndex < ui->addressCount; ++readIndex) {
            if (writeIndex == 0 || ui->addresses[readIndex] != ui->addresses[writeIndex - 1]) {
                ui->addresses[writeIndex++] = ui->addresses[readIndex];
            }
        }
        ui->addressCount = writeIndex;
    }
    if (ui->requireAllColumns && ui->addressCount > 0) {
        size_t activeColumns = 0;
        for (int frameIndex = 1; frameIndex < columnCount; ++frameIndex) {
            if (frameActive[frameIndex]) {
                activeColumns++;
            }
        }
        if (activeColumns == 0) {
            ui->addressCount = 0;
        } else {
            size_t *indices = (size_t*)alloc_calloc((size_t)columnCount, sizeof(size_t));
            uint32_t *filtered = (uint32_t*)alloc_alloc(ui->addressCount * sizeof(uint32_t));
            if (!indices || !filtered) {
                alloc_free(indices);
                alloc_free(filtered);
                memory_track_ui_setError(ui, "Out of memory");
                goto cleanup;
            }
            size_t filteredCount = 0;
            for (size_t addrIndex = 0; addrIndex < ui->addressCount; ++addrIndex) {
                uint32_t address = ui->addresses[addrIndex];
                int allMatch = 1;
                for (int frameIndex = 1; frameIndex < columnCount; ++frameIndex) {
                    if (!frameActive[frameIndex]) {
                        continue;
                    }
                    memory_track_frame_data_t *frame = &ui->frames[frameIndex];
                    size_t idx = indices[frameIndex];
                    while (idx < frame->entryCount && frame->entries[idx].address < address) {
                        idx++;
                    }
                    indices[frameIndex] = idx;
                    if (idx >= frame->entryCount || frame->entries[idx].address != address) {
                        allMatch = 0;
                        break;
                    }
                }
                if (allMatch) {
                    filtered[filteredCount++] = address;
                }
            }
            memcpy(ui->addresses, filtered, filteredCount * sizeof(uint32_t));
            ui->addressCount = filteredCount;
            alloc_free(indices);
            alloc_free(filtered);
        }
    }

    if (frameActive[0] && ui->addressCount > 0) {
        size_t entryCount = ui->addressCount;
        memory_track_entry_t *entries = (memory_track_entry_t*)alloc_alloc(entryCount * sizeof(*entries));
        if (!entries) {
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        for (size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
            uint32_t address = ui->addresses[entryIndex];
            uint32_t offset = address - MEMORY_TRACK_UI_REGION_BASE;
            entries[entryIndex].address = address;
            entries[entryIndex].value = memory_track_ui_readValueBE(refBytes + offset, accessSize);
        }
        ui->frames[0].entries = entries;
        ui->frames[0].entryCount = entryCount;
    }

    if (ui->addressCount > 0) {
        uint32_t *filterValues = (uint32_t*)alloc_calloc((size_t)columnCount, sizeof(uint32_t));
        int *filterActive = (int*)alloc_calloc((size_t)columnCount, sizeof(int));
        if (!filterValues || !filterActive) {
            alloc_free(filterValues);
            alloc_free(filterActive);
            memory_track_ui_setError(ui, "Out of memory");
            goto cleanup;
        }
        int anyFilter = 0;
        for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
            const char *text = NULL;
            if (ui->filterInputs && frameIndex < (int)ui->filterInputsCount && ui->filterInputs[frameIndex]) {
                text = e9ui_textbox_getText(ui->filterInputs[frameIndex]);
            } else if (ui->filterTexts && frameIndex < (int)ui->filterTextsCount) {
                text = ui->filterTexts[frameIndex];
            }
            if (!text || !text[0]) {
                continue;
            }
            while (*text && isspace((unsigned char)*text)) {
                text++;
            }
            if (text[0] == '\0') {
                continue;
            }
            if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
                text += 2;
            }
            char *end = NULL;
            unsigned long long val = strtoull(text, &end, 16);
            if (!end || end == text) {
                memory_track_ui_setError(ui, "Invalid filter: \"%s\"", text);
                alloc_free(filterValues);
                alloc_free(filterActive);
                goto cleanup;
            }
            while (*end && isspace((unsigned char)*end)) {
                end++;
            }
            if (*end) {
                memory_track_ui_setError(ui, "Invalid filter: \"%s\"", text);
                alloc_free(filterValues);
                alloc_free(filterActive);
                goto cleanup;
            }
            filterValues[frameIndex] = (uint32_t)val;
            filterActive[frameIndex] = 1;
            anyFilter = 1;
        }
        if (anyFilter) {
            size_t *indices = (size_t*)alloc_calloc((size_t)columnCount, sizeof(size_t));
            uint32_t *filtered = (uint32_t*)alloc_alloc(ui->addressCount * sizeof(uint32_t));
            if (!indices || !filtered) {
                alloc_free(indices);
                alloc_free(filtered);
                alloc_free(filterValues);
                alloc_free(filterActive);
                memory_track_ui_setError(ui, "Out of memory");
                goto cleanup;
            }
            size_t filteredCount = 0;
            for (size_t addrIndex = 0; addrIndex < ui->addressCount; ++addrIndex) {
                uint32_t address = ui->addresses[addrIndex];
                int matches = 1;
                for (int frameIndex = 0; frameIndex < columnCount; ++frameIndex) {
                    if (!filterActive[frameIndex]) {
                        continue;
                    }
                    memory_track_frame_data_t *frame = &ui->frames[frameIndex];
                    size_t idx = indices[frameIndex];
                    while (idx < frame->entryCount && frame->entries[idx].address < address) {
                        idx++;
                    }
                    indices[frameIndex] = idx;
                    if (idx >= frame->entryCount || frame->entries[idx].address != address) {
                        matches = 0;
                        break;
                    }
                    if (frame->entries[idx].value != filterValues[frameIndex]) {
                        matches = 0;
                        break;
                    }
                }
                if (matches) {
                    filtered[filteredCount++] = address;
                }
            }
            memcpy(ui->addresses, filtered, filteredCount * sizeof(uint32_t));
            ui->addressCount = filteredCount;
            alloc_free(indices);
            alloc_free(filtered);
        }
        alloc_free(filterValues);
        alloc_free(filterActive);
    }
    if (!memory_track_ui_buildAddressLinks(ui)) {
        memory_track_ui_setError(ui, "Out of memory");
        goto cleanup;
    }

    ui->frameCount = (size_t)columnCount;
    ui->hasActiveFrames = hasActive;
    memory_track_ui_setError(ui, NULL);
    success = 1;

cleanup:
    alloc_free(frameNos);
    alloc_free(frameActive);
    alloc_free(refBytes);
    alloc_free(baseBytes);
    alloc_free(curBytes);
    (void)state_buffer_restoreFrameNo(restoreFrame);
    debugger.frameCounter = restoreFrame;
    state_buffer_setCurrentFrameNo(restoreFrame);
    if (!success) {
        memory_track_ui_clearData(ui);
    }
    return success;
}

static int
memory_track_ui_measureTextWidth(e9ui_context_t *ctx, const char *text)
{
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : (ctx ? ctx->font : NULL);
    if (!font || !text) {
        return 0;
    }
    int textW = 0;
    int textH = 0;
    if (TTF_SizeUTF8(font, text, &textW, &textH) != 0) {
        return 0;
    }
    return textW;
}

static void
memory_track_ui_updateMetrics(memory_track_ui_t *ui, int winW)
{
    if (!ui) {
        return;
    }
    int prevColumns = ui->columnCount;
    int prevAddressWidth = ui->addressWidth;
    int prevColumnWidth = ui->columnWidth;
    int pad = e9ui_scale_px(&ui->ctx, 8);
    int gap = e9ui_scale_px(&ui->ctx, 10);
    int addrW = memory_track_ui_measureTextWidth(&ui->ctx, "0x00FFFF");
    int frameW = memory_track_ui_measureTextWidth(&ui->ctx, "F000000");
    if (addrW <= 0) {
        addrW = e9ui_scale_px(&ui->ctx, 96);
    }
    if (frameW <= 0) {
        frameW = e9ui_scale_px(&ui->ctx, 60);
    }
    ui->padding = pad;
    ui->columnGap = gap;
    ui->addressWidth = addrW + e9ui_scale_px(&ui->ctx, 12);
    int minColumnW = e9ui_scale_px(&ui->ctx, 72);
    int desiredColumnW = frameW + e9ui_scale_px(&ui->ctx, 10);
    ui->columnWidth = desiredColumnW > minColumnW ? desiredColumnW : minColumnW;

    ui->modeButtonWidth = e9ui_scale_px(&ui->ctx, 34);
    ui->modeButtonGap = e9ui_scale_px(&ui->ctx, 4);
    ui->modeWidth = ui->modeButtonWidth * 3 + ui->modeButtonGap * 2;
    int filterW = memory_track_ui_measureTextWidth(&ui->ctx, "Show All");
    if (filterW <= 0) {
        filterW = e9ui_scale_px(&ui->ctx, 60);
    }
    ui->filterButtonWidth = filterW + e9ui_scale_px(&ui->ctx, 16);
    ui->filterButtonGap = e9ui_scale_px(&ui->ctx, 6);

    int availableW = winW - pad * 2 - ui->addressWidth - ui->modeWidth - ui->filterButtonWidth -
                     ui->filterButtonGap - gap;
    int columns = 1;
    if (availableW > ui->columnWidth) {
        columns = 1 + (availableW - ui->columnWidth) / (ui->columnWidth + gap);
    }
    if (columns < 1) {
        columns = 1;
    }
    ui->columnCount = columns;
    if (columns != prevColumns || ui->addressWidth != prevAddressWidth || ui->columnWidth != prevColumnWidth) {
        ui->needsRebuild = 1;
        ui->needsRefresh = 1;
    }
}

static void
memory_track_ui_updateContentHeight(memory_track_ui_t *ui, TTF_Font *font)
{
    if (!ui) {
        return;
    }
    int lineHeight = font ? TTF_FontHeight(font) : 0;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }
    ui->rowHeight = lineHeight + e9ui_scale_px(&ui->ctx, 2);
    ui->headerHeight = 0;
    if (ui->error[0]) {
        ui->contentHeight = ui->padding * 2 + ui->rowHeight * 2;
    } else if (ui->addressCount == 0) {
        ui->contentHeight = ui->padding * 2 + ui->rowHeight * 2;
    } else {
        ui->contentHeight = ui->padding * 2 + (int)(ui->rowHeight * (int)ui->addressCount);
    }
    if (ui->scroll) {
        e9ui_scroll_setContentHeightPx(ui->scroll, ui->contentHeight);
    }
}

static int
memory_track_ui_tablePreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)ctx;
    (void)availW;
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    if (!st || !st->ui) {
        return 0;
    }
    return st->ui->contentHeight;
}

static void
memory_track_ui_tableLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->addressLinks || ui->addressLinksCount == 0) {
        return;
    }
    int pad = ui->padding;
    int startX = bounds.x + pad;
    int startY = bounds.y + pad;
    size_t linkCount = ui->addressLinksCount;
    for (size_t rowIndex = 0; rowIndex < linkCount; ++rowIndex) {
        e9ui_component_t *link = ui->addressLinks[rowIndex];
        if (link && link->layout) {
            e9ui_rect_t r = { startX, startY, ui->addressWidth, ui->rowHeight };
            link->layout(link, ctx, r);
        }
        startY += ui->rowHeight;
    }
}

static void
memory_track_ui_tableRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui) {
        return;
    }
    SDL_Rect rect = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 18, 18, 18, 255);
    SDL_RenderFillRect(ctx->renderer, &rect);

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font) {
        return;
    }
    SDL_Color addrColor = { 180, 200, 180, 255 };
    SDL_Color valueColor = { 200, 220, 200, 255 };
    SDL_Color errorColor = { 220, 80, 80, 255 };

    int pad = ui->padding;
    int startX = rect.x + pad;
    int startY = rect.y + pad;
    if (ui->error[0]) {
        int textW = 0;
        int textH = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, ui->error, errorColor,
                                                   &textW, &textH);
        if (tex) {
            SDL_Rect tr = { startX, startY, textW, textH };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &tr);
        }
        return;
    }

    int columnsX = startX + ui->addressWidth + ui->modeWidth + ui->filterButtonWidth +
                   ui->filterButtonGap + ui->columnGap;
    if (ui->addressCount == 0) {
        int textW = 0;
        int textH = 0;
        const char *emptyText = ui->hasActiveFrames ? "No changes in selected frames"
                                                    : "Enter frame numbers above";
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, emptyText,
                                                   addrColor, &textW, &textH);
        if (tex) {
            SDL_Rect tr = { startX, startY, textW, textH };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &tr);
        }
        return;
    }

    if (ui->frameCount > ui->frameIndicesCap) {
        size_t newCap = ui->frameIndicesCap ? ui->frameIndicesCap : 8;
        while (newCap < ui->frameCount) {
            newCap *= 2;
        }
        size_t *next = (size_t*)alloc_realloc(ui->frameIndices, newCap * sizeof(size_t));
        if (!next) {
            return;
        }
        ui->frameIndices = next;
        ui->frameIndicesCap = newCap;
    }
    for (size_t frameIndex = 0; frameIndex < ui->frameCount; ++frameIndex) {
        ui->frameIndices[frameIndex] = 0;
    }

    for (size_t rowIndex = 0; rowIndex < ui->addressCount; ++rowIndex) {
        uint32_t address = ui->addresses[rowIndex];
        if (ui->addressLinks && rowIndex < ui->addressLinksCount) {
            e9ui_component_t *link = ui->addressLinks[rowIndex];
            if (link && link->render) {
                link->render(link, ctx);
            }
        } else {
            char addrText[16];
            snprintf(addrText, sizeof(addrText), "0x%06X", address & 0x00FFFFFFu);
            int addrW = 0;
            int addrH = 0;
            SDL_Texture *addrTex = e9ui_text_cache_getText(ctx->renderer, font, addrText, addrColor,
                                                           &addrW, &addrH);
            if (addrTex) {
                SDL_Rect tr = { startX, startY, addrW, addrH };
                SDL_RenderCopy(ctx->renderer, addrTex, NULL, &tr);
            }
        }

        for (size_t colIndex = 0; colIndex < ui->frameCount; ++colIndex) {
            memory_track_frame_data_t *frame = &ui->frames[colIndex];
            size_t idx = ui->frameIndices[colIndex];
            while (idx < frame->entryCount && frame->entries[idx].address < address) {
                idx++;
            }
            ui->frameIndices[colIndex] = idx;
            if (idx < frame->entryCount && frame->entries[idx].address == address) {
                char valText[16];
                unsigned digits = (unsigned)(ui->accessSize * 2);
                if (digits < 2) {
                    digits = 2;
                }
                snprintf(valText, sizeof(valText), "%0*X", (int)digits, frame->entries[idx].value);
                int valW = 0;
                int valH = 0;
                SDL_Texture *valTex = e9ui_text_cache_getText(ctx->renderer, font, valText, valueColor,
                                                              &valW, &valH);
                if (valTex) {
                    int colX = columnsX + (int)colIndex * (ui->columnWidth + ui->columnGap);
                    SDL_Rect tr = { colX, startY, valW, valH };
                    SDL_RenderCopy(ctx->renderer, valTex, NULL, &tr);
                }
            }
        }
        startY += ui->rowHeight;
    }
}

static int
memory_track_ui_tableHandleEvent(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    memory_track_ui_t *ui = st ? st->ui : NULL;
    if (!ui || !ui->addressLinks || ui->addressLinksCount == 0) {
        return 0;
    }
    for (size_t i = 0; i < ui->addressLinksCount; ++i) {
        e9ui_component_t *link = ui->addressLinks[i];
        if (link && e9ui_event_process(link, ctx, ev)) {
            return 1;
        }
    }
    return 0;
}

static void
memory_track_ui_tableDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self) {
        return;
    }
    memory_track_table_state_t *st = (memory_track_table_state_t*)self->state;
    alloc_free(st);
    self->state = NULL;
}

static e9ui_component_t *
memory_track_ui_tableMake(memory_track_ui_t *ui)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    memory_track_table_state_t *st = (memory_track_table_state_t*)alloc_calloc(1, sizeof(*st));
    if (!comp || !st) {
        alloc_free(comp);
        alloc_free(st);
        return NULL;
    }
    st->ui = ui;
    comp->name = "memory_track_table";
    comp->state = st;
    comp->preferredHeight = memory_track_ui_tablePreferredHeight;
    comp->layout = memory_track_ui_tableLayout;
    comp->render = memory_track_ui_tableRender;
    comp->handleEvent = memory_track_ui_tableHandleEvent;
    comp->dtor = memory_track_ui_tableDtor;
    return comp;
}

static void
memory_track_ui_onFrameSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    memory_track_ui_storeFrameTexts(ui);
    ui->needsRefresh = 1;
}

static void
memory_track_ui_onFilterSubmit(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    memory_track_ui_t *ui = (memory_track_ui_t*)user;
    if (!ui) {
        return;
    }
    memory_track_ui_storeFilterTexts(ui);
    ui->needsRefresh = 1;
}

static e9ui_component_t *
memory_track_ui_buildFrameRow(memory_track_ui_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    if (!row) {
        return NULL;
    }
    e9ui_component_t *label = e9ui_text_make("Address");
    if (label) {
        SDL_Color labelColor = { 200, 200, 200, 255 };
        e9ui_text_setColor(label, labelColor);
    }
    int gap = ui->columnGap;
    if (label) {
        e9ui_hstack_addFixed(row, label, ui->addressWidth);
    }
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);

    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->modeWidth), ui->modeWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->filterButtonWidth), ui->filterButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->filterButtonGap), ui->filterButtonGap);

    if (ui->columnCount > (int)ui->frameInputsCap) {
        size_t newCap = ui->frameInputsCap ? ui->frameInputsCap : 4;
        while (newCap < (size_t)ui->columnCount) {
            newCap *= 2;
        }
        e9ui_component_t **next = (e9ui_component_t**)alloc_realloc(ui->frameInputs,
                                                                     newCap * sizeof(e9ui_component_t*));
        if (!next) {
            e9ui_childDestroy(row, &ui->ctx);
            return NULL;
        }
        ui->frameInputs = next;
        ui->frameInputsCap = newCap;
    }
    ui->frameInputsCount = (size_t)ui->columnCount;

    for (int columnIndex = 0; columnIndex < ui->columnCount; ++columnIndex) {
        e9ui_component_t *textbox = e9ui_textbox_make(16, memory_track_ui_onFrameSubmit, NULL, ui);
        if (textbox) {
            e9ui_textbox_setPlaceholder(textbox, "Frame");
            e9ui_textbox_setNumericOnly(textbox, 1);
            if (ui->frameTexts && columnIndex < (int)ui->frameTextsCount && ui->frameTexts[columnIndex]) {
                e9ui_textbox_setText(textbox, ui->frameTexts[columnIndex]);
            }
        }
        ui->frameInputs[columnIndex] = textbox;
        if (textbox) {
            e9ui_hstack_addFixed(row, textbox, ui->columnWidth);
        } else {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->columnWidth), ui->columnWidth);
        }
        if (columnIndex + 1 < ui->columnCount) {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
        }
    }
    e9ui_hstack_addFlex(row, e9ui_spacer_make(1));
    memory_track_ui_updateModeButtons(ui);
    memory_track_ui_updateFilterButton(ui);
    return row;
}

static e9ui_component_t *
memory_track_ui_buildControlRow(memory_track_ui_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    if (!row) {
        return NULL;
    }
    int gap = ui->columnGap;
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->addressWidth), ui->addressWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);

    ui->modeBtn8 = e9ui_button_make("8", memory_track_ui_access8, ui);
    ui->modeBtn16 = e9ui_button_make("16", memory_track_ui_access16, ui);
    ui->modeBtn32 = e9ui_button_make("32", memory_track_ui_access32, ui);
    if (ui->modeBtn8) {
        e9ui_hstack_addFixed(row, ui->modeBtn8, ui->modeButtonWidth);
    }
    if (ui->modeBtn16 || ui->modeBtn32) {
        e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->modeButtonGap), ui->modeButtonGap);
    }
    if (ui->modeBtn16) {
        e9ui_hstack_addFixed(row, ui->modeBtn16, ui->modeButtonWidth);
    }
    if (ui->modeBtn32) {
        e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->modeButtonGap), ui->modeButtonGap);
    }
    if (ui->modeBtn32) {
        e9ui_hstack_addFixed(row, ui->modeBtn32, ui->modeButtonWidth);
    }
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);

    ui->filterBtn = e9ui_button_make("Show All", memory_track_ui_toggleRequireAll, ui);
    if (ui->filterBtn) {
        e9ui_hstack_addFixed(row, ui->filterBtn, ui->filterButtonWidth);
    }
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->filterButtonGap), ui->filterButtonGap);
    e9ui_component_t *btn_reset = e9ui_button_make("Reset", memory_track_ui_clearFrameMarkers, ui);
    if (btn_reset) {
        int resetW = e9ui_scale_px(&ui->ctx, 80);
        e9ui_hstack_addFixed(row, btn_reset, resetW);
    }
    e9ui_hstack_addFlex(row, e9ui_spacer_make(1));
    memory_track_ui_updateModeButtons(ui);
    memory_track_ui_updateFilterButton(ui);
    return row;
}

static e9ui_component_t *
memory_track_ui_buildFilterRow(memory_track_ui_t *ui)
{
    if (!ui) {
        return NULL;
    }
    e9ui_component_t *row = e9ui_hstack_make();
    if (!row) {
        return NULL;
    }
    int gap = ui->columnGap;
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->addressWidth), ui->addressWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->modeWidth), ui->modeWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->filterButtonWidth), ui->filterButtonWidth);
    e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->filterButtonGap), ui->filterButtonGap);

    if (ui->columnCount > (int)ui->filterInputsCap) {
        size_t newCap = ui->filterInputsCap ? ui->filterInputsCap : 4;
        while (newCap < (size_t)ui->columnCount) {
            newCap *= 2;
        }
        e9ui_component_t **next = (e9ui_component_t**)alloc_realloc(ui->filterInputs,
                                                                     newCap * sizeof(e9ui_component_t*));
        if (!next) {
            e9ui_childDestroy(row, &ui->ctx);
            return NULL;
        }
        ui->filterInputs = next;
        ui->filterInputsCap = newCap;
    }
    ui->filterInputsCount = (size_t)ui->columnCount;

    for (int columnIndex = 0; columnIndex < ui->columnCount; ++columnIndex) {
        e9ui_component_t *textbox = e9ui_textbox_make(16, memory_track_ui_onFilterSubmit, NULL, ui);
        if (textbox) {
            e9ui_textbox_setPlaceholder(textbox, "Filter");
            if (ui->filterTexts && columnIndex < (int)ui->filterTextsCount && ui->filterTexts[columnIndex]) {
                e9ui_textbox_setText(textbox, ui->filterTexts[columnIndex]);
            }
        }
        ui->filterInputs[columnIndex] = textbox;
        if (textbox) {
            e9ui_hstack_addFixed(row, textbox, ui->columnWidth);
        } else {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(ui->columnWidth), ui->columnWidth);
        }
        if (columnIndex + 1 < ui->columnCount) {
            e9ui_hstack_addFixed(row, e9ui_spacer_make(gap), gap);
        }
    }
    e9ui_hstack_addFlex(row, e9ui_spacer_make(1));
    return row;
}

static e9ui_component_t *
memory_track_ui_buildRoot(memory_track_ui_t *ui)
{
    e9ui_component_t *stack = e9ui_stack_makeVertical();
    if (!stack) {
        return NULL;
    }
    e9ui_component_t *controls = memory_track_ui_buildControlRow(ui);
    if (!controls) {
        e9ui_childDestroy(stack, &ui->ctx);
        return NULL;
    }
    e9ui_component_t *controls_box = e9ui_box_make(controls);
    e9ui_box_setPadding(controls_box, ui->padding);
    e9ui_component_t *filters = memory_track_ui_buildFilterRow(ui);
    if (!filters) {
        e9ui_childDestroy(controls_box, &ui->ctx);
        e9ui_childDestroy(stack, &ui->ctx);
        return NULL;
    }
    e9ui_component_t *frames = memory_track_ui_buildFrameRow(ui);
    if (!frames) {
        e9ui_childDestroy(controls_box, &ui->ctx);
        e9ui_childDestroy(filters, &ui->ctx);
        e9ui_childDestroy(stack, &ui->ctx);
        return NULL;
    }

    e9ui_component_t *table = memory_track_ui_tableMake(ui);
    if (!table) {
        e9ui_childDestroy(controls_box, &ui->ctx);
        e9ui_childDestroy(filters, &ui->ctx);
        e9ui_childDestroy(frames, &ui->ctx);
        e9ui_childDestroy(stack, &ui->ctx);
        return NULL;
    }
    ui->table = table;
    ui->scroll = e9ui_scroll_make(table);
    if (!ui->scroll) {
        e9ui_childDestroy(table, &ui->ctx);
        e9ui_childDestroy(controls_box, &ui->ctx);
        e9ui_childDestroy(filters, &ui->ctx);
        e9ui_childDestroy(frames, &ui->ctx);
        e9ui_childDestroy(stack, &ui->ctx);
        return NULL;
    }
    ui->headerRow = controls_box;
    e9ui_stack_addFixed(stack, controls_box);
    e9ui_stack_addFixed(stack, filters);
    e9ui_stack_addFixed(stack, frames);
    e9ui_stack_addFlex(stack, ui->scroll);
    return stack;
}

static void
memory_track_ui_rebuildRoot(memory_track_ui_t *ui)
{
    if (!ui) {
        return;
    }
    memory_track_ui_storeFrameTexts(ui);
    memory_track_ui_storeFilterTexts(ui);
    if (ui->root) {
        e9ui_childDestroy(ui->root, &ui->ctx);
    }
    ui->root = NULL;
    ui->headerRow = NULL;
    ui->scroll = NULL;
    ui->table = NULL;
    ui->root = memory_track_ui_buildRoot(ui);
    ui->needsRebuild = 0;
    ui->needsRefresh = 1;
}

static float
memory_track_ui_computeDpiScale(const e9ui_context_t *ctx)
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

int
memory_track_ui_init(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (ui->open) {
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow(MEMORY_TRACK_UI_TITLE,
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       900, 600,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        debug_error("memory track ui: SDL_CreateWindow failed: %s", SDL_GetError());
        return 0;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        SDL_DestroyWindow(win);
        debug_error("memory track ui: SDL_CreateRenderer failed: %s", SDL_GetError());
        return 0;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    ui->window = win;
    ui->renderer = ren;
    ui->windowId = SDL_GetWindowID(win);
    ui->ctx.window = win;
    ui->ctx.renderer = ren;
    ui->ctx.font = debugger.ui.ctx.font;
    ui->ctx.dpiScale = memory_track_ui_computeDpiScale(&ui->ctx);
    ui->closeRequested = 0;
    ui->needsRefresh = 1;
    ui->accessSize = 2;
    ui->requireAllColumns = 1;
    {
        int winW = 0;
        int winH = 0;
        SDL_GetRendererOutputSize(ui->renderer, &winW, &winH);
        memory_track_ui_updateMetrics(ui, winW);
    }
    ui->root = memory_track_ui_buildRoot(ui);
    if (!ui->root) {
        memory_track_ui_shutdown();
        return 0;
    }
    ui->needsRebuild = 0;
    memory_track_ui_setError(ui, NULL);
    ui->open = 1;
    return 1;
}

void
memory_track_ui_shutdown(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (!ui->open) {
        return;
    }
    if (ui->frameInputs && ui->frameInputsCount) {
        memory_track_ui_storeFrameTexts(ui);
    }
    if (ui->filterInputs && ui->filterInputsCount) {
        memory_track_ui_storeFilterTexts(ui);
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
    memory_track_ui_clearData(ui);
    alloc_free(ui->frameInputs);
    ui->frameInputs = NULL;
    ui->frameInputsCap = 0;
    ui->frameInputsCount = 0;
    alloc_free(ui->filterInputs);
    ui->filterInputs = NULL;
    ui->filterInputsCap = 0;
    ui->filterInputsCount = 0;
    alloc_free(ui->addressLinks);
    ui->addressLinks = NULL;
    ui->addressLinksCap = 0;
    ui->addressLinksCount = 0;
    ui->open = 0;
    ui->closeRequested = 0;
    ui->windowId = 0;
    ui->headerRow = NULL;
    ui->scroll = NULL;
    ui->table = NULL;
    ui->columnCount = 0;
    ui->needsRebuild = 0;
    ui->needsRefresh = 0;
    memset(&ui->ctx, 0, sizeof(ui->ctx));
    memory_track_ui_refocusMain();
}

int
memory_track_ui_isOpen(void)
{
    return memory_track_ui_state.open ? 1 : 0;
}

uint32_t
memory_track_ui_getWindowId(void)
{
    return memory_track_ui_state.windowId;
}

void
memory_track_ui_handleEvent(SDL_Event *ev)
{
    if (!ev || !memory_track_ui_state.open) {
        return;
    }
    memory_track_ui_t *ui = &memory_track_ui_state;
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
        int mouseX = 0;
        int mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);
        int scaledX = e9ui_scale_coord(&ui->ctx, mouseX);
        int scaledY = e9ui_scale_coord(&ui->ctx, mouseY);
        ui->ctx.mouseX = scaledX;
        ui->ctx.mouseY = scaledY;
    } else if (ev->type == SDL_WINDOWEVENT) {
        if (ev->window.event == SDL_WINDOWEVENT_RESIZED ||
            ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            ui->ctx.dpiScale = memory_track_ui_computeDpiScale(&ui->ctx);
            ui->needsRefresh = 1;
        }
    } else if (ev->type == SDL_KEYDOWN) {
        if (ev->key.keysym.sym == SDLK_ESCAPE) {
            ui->closeRequested = 1;
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
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT &&
        !ui->ctx.focusClickHandled) {
        e9ui_setFocus(&ui->ctx, NULL);
    }
}

void
memory_track_ui_render(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (!ui->open || !ui->renderer || !ui->root) {
        return;
    }
    if (ui->closeRequested) {
        memory_track_ui_shutdown();
        return;
    }
    if (ui->pendingRemove && ui->root) {
        e9ui_childRemove(ui->root, ui->pendingRemove, &ui->ctx);
        ui->pendingRemove = NULL;
    }
    ui->ctx.font = debugger.ui.ctx.font;
    ui->ctx.window = ui->window;
    ui->ctx.renderer = ui->renderer;

    SDL_SetRenderDrawColor(ui->renderer, 12, 12, 12, 255);
    SDL_RenderClear(ui->renderer);
    int winW = 0;
    int winH = 0;
    SDL_GetRendererOutputSize(ui->renderer, &winW, &winH);
    ui->ctx.winW = winW;
    ui->ctx.winH = winH;

    memory_track_ui_updateMetrics(ui, winW);
    if (ui->needsRebuild) {
        memory_track_ui_rebuildRoot(ui);
        ui->needsRebuild = 0;
        if (!ui->root) {
            memory_track_ui_shutdown();
            return;
        }
    }
    if (ui->needsRefresh) {
        (void)memory_track_ui_collectData(ui, ui->columnCount);
        ui->needsRefresh = 0;
    }
    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ui->ctx.font;
    memory_track_ui_updateContentHeight(ui, font);

    e9ui_component_t *root = ui->fullscreen ? ui->fullscreen : ui->root;
    if (root && root->layout) {
        e9ui_rect_t full = (e9ui_rect_t){ 0, 0, winW, winH };
        root->layout(root, &ui->ctx, full);
    }
    if (root && root->render) {
        root->render(root, &ui->ctx);
    }
    SDL_RenderPresent(ui->renderer);
}

void
memory_track_ui_addFrameMarker(uint64_t frameNo)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    char text[64];
    snprintf(text, sizeof(text), "%llu", (unsigned long long)frameNo);
    size_t index = memory_track_ui_findEmptyFrameIndex(ui);
    memory_track_ui_setFrameTextAtIndex(ui, index, text);
    ui->needsRefresh = 1;
}

void
memory_track_ui_clearMarkers(void)
{
    memory_track_ui_clearFrameMarkersInternal(&memory_track_ui_state);
}

size_t
memory_track_ui_getMarkerCount(void)
{
    memory_track_ui_t *ui = &memory_track_ui_state;
    if (!ui) {
        return 0;
    }
    size_t count = 0;
    if (ui->frameInputs && ui->frameInputsCount) {
        for (size_t i = 0; i < ui->frameInputsCount; ++i) {
            const char *text = e9ui_textbox_getText(ui->frameInputs[i]);
            if (!text || !*text) {
                continue;
            }
            char *end = NULL;
            unsigned long long val = strtoull(text, &end, 0);
            if (!end || end == text) {
                continue;
            }
            while (*end && isspace((unsigned char)*end)) {
                ++end;
            }
            if (*end) {
                continue;
            }
            if (val > 0) {
                count++;
            }
        }
        return count;
    }
    for (size_t i = 0; i < ui->frameTextsCount; ++i) {
        const char *text = ui->frameTexts[i];
        if (!text || !*text) {
            continue;
        }
        char *end = NULL;
        unsigned long long val = strtoull(text, &end, 0);
        if (!end || end == text) {
            continue;
        }
        while (*end && isspace((unsigned char)*end)) {
            ++end;
        }
        if (*end) {
            continue;
        }
        if (val > 0) {
            count++;
        }
    }
    return count;
}
