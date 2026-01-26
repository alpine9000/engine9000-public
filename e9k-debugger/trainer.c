/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "trainer.h"
#include "e9ui.h"
#include "machine.h"
#include "libretro_host.h"
#include "debugger.h"
#include "memory_track_ui.h"
#include "protect.h"
#include "debug.h"
#include "e9ui_text.h"
#include "train.h"


typedef struct trainer_record {
    geo_debug_protect_t data;
    uint32_t            index;
    int                 enabled;
    int                 present;
} trainer_record_t;

typedef struct trainer_entry_state {
    char               primary[768];
    char               condition[768];
    int                hasCondition;
    e9ui_component_t  *checkbox;   // child component (owned via e9ui_addChild)
    trainer_record_t *record;  // non-owning pointer into list state's array
    struct trainer_list_state *list; // non-owning pointer (owner is list component)
} trainer_entry_state_t;


struct trainer_list_state {
    e9ui_component_t *entries;              // child stack (owned via e9ui_addChild)
    trainer_record_t *records;          // owned by list state
    int                  record_count;
    int                  record_cap;
    const geo_debug_protect_t *last_bps;   // non-owning snapshot pointer
    int                        last_count;
    e9ui_component_t         *markerLabel;
    e9ui_component_t         *ignoreButton;
};

static void trainer_listMarkDirty(trainer_list_state_t *st);
static void trainer_listRefreshAndMarkDirty(trainer_list_state_t *st);

static trainer_list_state_t *trainer_listState = NULL;

static void
trainer_formatSummary(const trainer_record_t *rec, char *dst, size_t cap)
{
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!rec) {
        snprintf(dst, cap, "<unknown>");
        return;
    }
    const char *mode = (rec->data.mode == GEO_PROTECT_MODE_SET) ? "set" : "block";
    snprintf(dst, cap, "#%u 0x%06X %u-bit %s%s",
             rec->index,
             rec->data.addr & 0x00ffffffu,
             rec->data.sizeBits,
             mode,
             rec->enabled ? "" : " (disabled)");
}

static void
trainer_formatDetail(const trainer_record_t *rec, char *dst, size_t cap)
{
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!rec) {
        return;
    }
    if (rec->data.mode == GEO_PROTECT_MODE_SET) {
        snprintf(dst, cap, "value = 0x%X", rec->data.value);
    } else {
        snprintf(dst, cap, "block writes");
    }
}


void
trainer_registerListState(trainer_list_state_t *state)
{
    trainer_listState = state;
}

void
trainer_unregisterListState(trainer_list_state_t *state)
{
    if (trainer_listState == state) {
        trainer_listState = NULL;
    }
}

void
trainer_markDirty(void)
{
  if (trainer_listState) {
    trainer_listRefreshAndMarkDirty(trainer_listState);
  }
}

static void
trainer_entryCheckboxCB(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;

    trainer_entry_state_t *st = (trainer_entry_state_t*)user;
    if (!st || !st->record) return;

    uint64_t mask = 0;
    if (!libretro_host_debugGetProtectEnabledMask(&mask)) {
        return;
    }
    if (selected) {
        mask |= (1ull << st->record->index);
    } else {
        mask &= ~(1ull << st->record->index);
    }
    if (!libretro_host_debugSetProtectEnabledMask(mask)) {
        return;
    }
    st->record->enabled = selected ? 1 : 0;
    if (st->list) {
        trainer_listRefreshAndMarkDirty(st->list);
    }
}

static int
trainer_entryPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;

    trainer_entry_state_t *st = (trainer_entry_state_t*)self->state;

    TTF_Font *font = debugger.theme.text.source;
    if (!font && ctx) font = ctx->font;

    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) lh = 16;

    int lines = 1 + (st && st->hasCondition ? 1 : 0);

    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) padY = E9UI_THEME_CHECKBOX_MARGIN;

    return padY + lines * lh + padY;
}

static void
trainer_entryLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    trainer_entry_state_t *st = (trainer_entry_state_t*)self->state;
    if (!st || !st->checkbox) return;

    int padX = e9ui_checkbox_getMargin(ctx);
    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) padY = E9UI_THEME_CHECKBOX_MARGIN;

    const int cbSize = 18;
    int cbHeight = bounds.h - padY * 2;
    if (cbHeight < cbSize) cbHeight = cbSize;

    int cbGap = e9ui_checkbox_getTextGap(ctx);
    if (cbGap <= 0) cbGap = E9UI_THEME_CHECKBOX_TEXT_GAP;
    int cbLeft = e9ui_checkbox_getLeftMargin(st->checkbox, ctx);

    e9ui_rect_t cb_bounds = {
        bounds.x + padX,
        bounds.y + (bounds.h - cbHeight) / 2,
        cbSize + cbGap + cbLeft,
        cbHeight
    };

    if (st->checkbox->layout) {
        st->checkbox->layout(st->checkbox, ctx, cb_bounds);
    }
}

static void
trainer_entryRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) return;

    trainer_entry_state_t *st = (trainer_entry_state_t*)self->state;

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font) return;

    if (st->checkbox && st->checkbox->render) {
        st->checkbox->render(st->checkbox, ctx);
    }

    const SDL_Color primary =
        e9ui_checkbox_isSelected(st->checkbox) ?
            (SDL_Color){200, 255, 200, 255} :
            (SDL_Color){220, 220, 220, 255};

    const SDL_Color meta = {180, 180, 210, 255};

    int padX = e9ui_checkbox_getMargin(ctx);
    if (padX <= 0) padX = 8;
    int cbLeft = e9ui_checkbox_getLeftMargin(st->checkbox, ctx);

    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) padY = E9UI_THEME_CHECKBOX_MARGIN;

    const int cbSize = 18;

    int cbGap = e9ui_checkbox_getTextGap(ctx);
    if (cbGap <= 0) cbGap = E9UI_THEME_CHECKBOX_TEXT_GAP;

    int lh = TTF_FontHeight(font);
    if (lh <= 0) lh = 16;

    int curY = self->bounds.y + padY;
    int textX = self->bounds.x + padX + cbLeft + cbSize + cbGap;

    int tw = 0, th = 0;
    SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, st->primary, primary, &tw, &th);
    if (t) {
        SDL_Rect tr = { textX, curY, tw, th };
        SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
    }

    curY += lh;

    if (st->hasCondition) {
        int cw = 0, ch = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, st->condition, meta, &cw, &ch);
        if (t) {
            SDL_Rect tr = { textX + 12, curY, cw, ch };
            SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
        }
    }
}


static e9ui_component_t *
trainer_entryMake(trainer_record_t *rec, trainer_list_state_t *list)
{
    if (!rec) return NULL;

    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    trainer_entry_state_t *st = (trainer_entry_state_t*)alloc_calloc(1, sizeof(*st));
    if (!c || !st) {
        alloc_free(c);
        alloc_free(st);
        return NULL;
    }

    c->name = "trainer_entry";
    c->state = st;

    st->record = rec;
    st->list = list;

    int checkbox_selected = rec->enabled ? 1 : 0;
    st->checkbox = e9ui_checkbox_make(NULL, checkbox_selected, trainer_entryCheckboxCB, st);
    e9ui_checkbox_setLeftMargin(st->checkbox, 8);
    if (!st->checkbox) {
        alloc_free(st);
        alloc_free(c);
        return NULL;
    }

    // ownership: entry owns checkbox as child
    e9ui_child_add(c, st->checkbox, 0);

    trainer_formatSummary(rec, st->primary, sizeof(st->primary));
    trainer_formatDetail(rec, st->condition, sizeof(st->condition));
    st->hasCondition = st->condition[0] != '\0';

    c->preferredHeight = trainer_entryPreferredHeight;
    c->layout = trainer_entryLayout;
    c->render = trainer_entryRender;

    return c;
}

static int
trainer_emptyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW;

    TTF_Font *font = debugger.theme.text.source;
    if (!font && ctx) font = ctx->font;

    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) lh = 16;

    const int padY = 4;
    return padY + lh + padY;
}

static void
trainer_emptyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
trainer_emptyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) return;

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font) return;

    const SDL_Color meta = {180, 180, 210, 255};
    const int padX = 8;
    const int padY = 4;

    int tw = 0, th = 0;
    SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, "No protects", meta, &tw, &th);
    if (t) {
        SDL_Rect tr = { self->bounds.x + padX, self->bounds.y + padY, tw, th };
        SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
    }
}

static e9ui_component_t *
trainer_emptyMake(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->name = "trainer_empty";
    c->preferredHeight = trainer_emptyPreferredHeight;
    c->layout = trainer_emptyLayout;
    c->render = trainer_emptyRender;
    return c;
}

static void
trainer_listMarkDirty(trainer_list_state_t *st)
{
    if (!st) return;
    st->last_bps = NULL;
    st->last_count = -1;
}

static void
trainer_listRefreshAndMarkDirty(trainer_list_state_t *st)
{
  if (st) trainer_listMarkDirty(st);
}

static void
trainer_updateMarkerLabel(trainer_list_state_t *st)
{
    if (!st || !st->markerLabel) {
        return;
    }
    size_t count = memory_track_ui_getMarkerCount();
    char text[64];
    snprintf(text, sizeof(text), "Markers: %zu", count);
    e9ui_text_setText(st->markerLabel, text);
}

static void
trainer_updateIgnoreButton(trainer_list_state_t *st)
{
    if (!st || !st->ignoreButton) {
        return;
    }
    int show = 0;
    if (!machine_getRunning(debugger.machine) && train_isActive() && train_hasLastWatchbreak()) {
        show = 1;
    }
    e9ui_setHidden(st->ignoreButton, show ? 0 : 1);
}

static int
trainer_recordEnsureCapacity(trainer_list_state_t *st, int minCap)
{
    if (!st) return 0;
    if (st->record_cap >= minCap) return 1;

    int newCap = st->record_cap ? st->record_cap * 2 : 8;
    while (newCap < minCap) newCap *= 2;

    trainer_record_t *newArr =
        (trainer_record_t*)alloc_realloc(st->records, (size_t)newCap * sizeof(trainer_record_t));
    if (!newArr) return 0;

    st->records = newArr;
    st->record_cap = newCap;
    return 1;
}

static trainer_record_t *
trainer_recordFind(trainer_list_state_t *st, uint32_t index)
{
    if (!st || st->record_count <= 0) return NULL;

    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].index == index) {
            return &st->records[i];
        }
    }
    return NULL;
}

static trainer_record_t *
trainer_recordAdd(trainer_list_state_t *st, const geo_debug_protect_t *protect, uint32_t index, int enabled)
{
    if (!st || !protect) return NULL;

    if (st->record_count == st->record_cap) {
        if (!trainer_recordEnsureCapacity(st, st->record_count + 1)) {
            return NULL;
        }
    }

    trainer_record_t *rec = &st->records[st->record_count++];
    memcpy(&rec->data, protect, sizeof(rec->data));
    rec->index = index;
    rec->enabled = enabled;
    rec->present = 0;
    return rec;
}

//static
int
trainer_updateRecords(trainer_list_state_t *st, const geo_debug_protect_t *protects, size_t count, uint64_t enabled_mask)
{
    if (!st) return 0;

    int changed = 0;
    int hadActive = 0;

    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].present) hadActive = 1;
        st->records[i].present = 0;
    }

    if (!protects || count == 0) {
        if (hadActive) changed = 1;
        return changed;
    }

    for (size_t i = 0; i < count; ++i) {
        const geo_debug_protect_t *protect = &protects[i];
        if (protect->sizeBits == 0) {
            continue;
        }
        trainer_record_t *rec = trainer_recordFind(st, (uint32_t)i);

        if (!rec) {
            rec = trainer_recordAdd(st, protect, (uint32_t)i, ((enabled_mask >> i) & 1ull) ? 1 : 0);
            if (!rec) continue;
            changed = 1;
        } else {
            int enabled = ((enabled_mask >> i) & 1ull) ? 1 : 0;
            if (memcmp(&rec->data, protect, sizeof(rec->data)) != 0 || rec->enabled != enabled) {
                memcpy(&rec->data, protect, sizeof(rec->data));
                rec->enabled = enabled;
                changed = 1;
            }
        }
        rec->present = 1;
    }

    return changed;
}

//static
void
trainer_rebuildEntries(trainer_list_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->entries) return;

    e9ui_stack_removeAll(st->entries, ctx);

    int presentCount = 0;
    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].present) presentCount++;
    }

    if (presentCount == 0) {
        e9ui_component_t *empty = trainer_emptyMake();
        if (empty) {
            e9ui_stack_addFlex(st->entries, empty);
        }
        return;
    }

    for (int i = 0; i < st->record_count; ++i) {
        trainer_record_t *rec = &st->records[i];
        if (!rec->present) continue;

        e9ui_component_t *entry = trainer_entryMake(rec, st);
        if (entry) {
            e9ui_stack_addFixed(st->entries, entry);
        }
    }
}

static void
trainer_listRebuild(trainer_list_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->entries) return;

    geo_debug_protect_t protects[GEO_PROTECT_COUNT];
    size_t count = 0;
    uint64_t enabled_mask = 0;
    if (!libretro_host_debugReadProtects(protects, GEO_PROTECT_COUNT, &count)) {
        count = 0;
    }
    libretro_host_debugGetProtectEnabledMask(&enabled_mask);

    int changed = 0;
    if (count != (size_t)st->last_count) {
        st->last_bps = NULL;
        st->last_count = (int)count;
        changed = 1;
    }
    if (trainer_updateRecords(st, protects, count, enabled_mask)) {
        changed = 1;
    }
    if (!changed) return;

    trainer_rebuildEntries(st, ctx);
}

static int
trainer_listPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0;
}

static void
trainer_listLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    trainer_list_state_t *st = (trainer_list_state_t*)self->state;
    if (!st || !st->entries) return;

    trainer_listRebuild(st, ctx);
    trainer_updateMarkerLabel(st);
    trainer_updateIgnoreButton(st);

    if (st->entries->layout) {
        st->entries->layout(st->entries, ctx, bounds);
    }
}

static void
trainer_listRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) return;

    trainer_list_state_t *st = (trainer_list_state_t*)self->state;
    trainer_updateMarkerLabel(st);
    trainer_updateIgnoreButton(st);

    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &r);

    if (st && st->entries && st->entries->render) {
        st->entries->render(st->entries, ctx);
    }
}

static void
trainer_listDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;

    trainer_list_state_t *st = (trainer_list_state_t*)self->state;
    if (!st) return;

    trainer_unregisterListState(st);

    if (st->records) {
        alloc_free(st->records);
        st->records = NULL;
    }
    st->record_count = 0;
    st->record_cap = 0;
}

static void
trainer_memoryTrackerCB(e9ui_context_t *ctx, void *user)
{
  (void)ctx;
  (void)user;
  e9ui_setFocus(&debugger.ui.ctx, NULL);
  if (memory_track_ui_isOpen()) {
    memory_track_ui_shutdown();
  } else {
    memory_track_ui_init();
  }
}

static void
trainer_ignoreCB(e9ui_context_t *ctx, void *user)
{
  (void)ctx;
  (void)user;

  uint32_t addr24 = 0;
  if (!train_getLastWatchbreakAddr(&addr24)) {
    debug_error("train: no watchbreak to ignore yet");
    return;
  }
  if (!train_addIgnoreAddr(addr24)) {
    debug_error("train: ignore list full");
    return;
  }
  if (libretro_host_debugResume()) {
    machine_setRunning(&debugger.machine, 1);
  }
}

static void
trainer_setMarkerCB(e9ui_context_t *ctx, void *user)
{
  (void)ctx;
  (void)user;
  uint64_t frame = debugger.frameCounter;
  memory_track_ui_addFrameMarker(frame);
  debug_printf("Marker set at frame %llu\n", (unsigned long long)frame);
  trainer_markDirty();
}

static void
trainer_clearAllCB(e9ui_context_t *ctx, void *user)
{
  (void)ctx;
  (void)user;
  protect_clear();
  trainer_markDirty();
}

static void
trainer_resetMarkersCB(e9ui_context_t *ctx, void *user)
{
  (void)ctx;
  (void)user;
  memory_track_ui_clearMarkers();
  trainer_markDirty();
}

static void
trainer_toggleAllCB(e9ui_context_t *ctx, void *user)
{
  (void)ctx;
  (void)user;
  geo_debug_protect_t protects[GEO_PROTECT_COUNT];
  size_t count = 0;
  uint64_t enabledMask = 0;
  if (!libretro_host_debugReadProtects(protects, GEO_PROTECT_COUNT, &count)) {
    return;
  }
  if (!libretro_host_debugGetProtectEnabledMask(&enabledMask)) {
    return;
  }
  int anyEnabled = 0;
  for (size_t i = 0; i < count; ++i) {
    if (protects[i].sizeBits == 0) {
      continue;
    }
    if ((enabledMask >> i) & 1ull) {
      anyEnabled = 1;
      break;
    }
  }
  uint64_t nextMask = 0;
  if (!anyEnabled) {
    for (size_t i = 0; i < count; ++i) {
      if (protects[i].sizeBits == 0) {
        continue;
      }
      nextMask |= (1ull << i);
    }
  }
  libretro_host_debugSetProtectEnabledMask(nextMask);
  trainer_markDirty();
}

static e9ui_component_t *
trainer_makeList(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) return NULL;

    trainer_list_state_t *st = (trainer_list_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }

    c->name = "trainer_list";
    c->state = st;

    st->entries = e9ui_stack_makeVertical();
    if (st->entries) {
        e9ui_child_add(c, st->entries, 0);
    }

    trainer_registerListState(st);

    c->preferredHeight = trainer_listPreferredHeight;
    c->layout = trainer_listLayout;
    c->render = trainer_listRender;
    c->dtor = trainer_listDtor;

    return c;
}


e9ui_component_t *
trainer_makeComponent(void)
{
    e9ui_component_t *list = trainer_makeList();

    e9ui_component_t *toolbar = e9ui_flow_make();
    e9ui_flow_setPadding(toolbar, 0);
    e9ui_flow_setSpacing(toolbar, 6);
    e9ui_flow_setWrap(toolbar, 1);

    trainer_list_state_t *list_state = list ? (trainer_list_state_t*)list->state : NULL;

    e9ui_component_t *btn_add = e9ui_button_make("Marker", trainer_setMarkerCB, list_state);
    e9ui_button_setMini(btn_add, 1);
    e9ui_button_setIconAsset(btn_add, "assets/icons/profile.png");

    e9ui_flow_add(toolbar, btn_add);

    e9ui_component_t *btn_ignore = e9ui_button_make("Ignore", trainer_ignoreCB, list_state);
    e9ui_button_setMini(btn_ignore, 1);
    e9ui_button_setIconAsset(btn_ignore, "assets/icons/clear.png");
    e9ui_setTooltip(btn_ignore, "Train ignore + continue");
    e9ui_flow_add(toolbar, btn_ignore);
    if (list_state) {
        list_state->ignoreButton = btn_ignore;
        trainer_updateIgnoreButton(list_state);
    }

    e9ui_component_t *btn_mt = e9ui_button_make("Track", trainer_memoryTrackerCB, NULL);
    e9ui_button_setMini(btn_mt, 1);
    e9ui_button_setIconAsset(btn_mt, "assets/icons/ram.png");

    e9ui_flow_add(toolbar, btn_mt);   

    e9ui_component_t *btn_toggle = e9ui_button_make("Toggle", trainer_toggleAllCB, list_state);
    e9ui_button_setMini(btn_toggle, 1);
    e9ui_flow_add(toolbar, btn_toggle);

    e9ui_component_t *btn_clear = e9ui_button_make("Clear", trainer_clearAllCB, list_state);
    e9ui_button_setMini(btn_clear, 1);
    e9ui_button_setIconAsset(btn_clear, "assets/icons/trash.png");
    e9ui_flow_add(toolbar, btn_clear);
    
    e9ui_component_t *toolbar_box = e9ui_box_make(toolbar);
    e9ui_box_setPadding(toolbar_box, 6);
    e9ui_box_setBorder(toolbar_box, E9UI_BORDER_BOTTOM, (SDL_Color){70, 70, 70, 255}, 1);

    e9ui_component_t *marker_row = e9ui_flow_make();
    e9ui_flow_setWrap(marker_row, 1);
    e9ui_flow_setSpacing(marker_row, 8);
    e9ui_component_t *btn_reset = e9ui_button_make("Reset Markers", trainer_resetMarkersCB, list_state);
    e9ui_button_setMini(btn_reset, 1);
    e9ui_button_setIconAsset(btn_reset, "assets/icons/trash.png");
    e9ui_flow_add(marker_row, btn_reset);
    e9ui_component_t *marker_label = e9ui_text_make("Markers: 0");
    list_state->markerLabel = marker_label;
    if (marker_label) {
        e9ui_flow_add(marker_row, marker_label);
    }
    e9ui_component_t *marker_box = e9ui_box_make(marker_row);
    e9ui_box_setPadding(marker_box, 6);
    e9ui_box_setBorder(marker_box, E9UI_BORDER_BOTTOM, (SDL_Color){70, 70, 70, 255}, 1);

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(stack, toolbar_box);
    e9ui_stack_addFixed(stack, marker_box);
    if (list) {
        e9ui_stack_addFlex(stack, list);
    }

    return stack;
}
