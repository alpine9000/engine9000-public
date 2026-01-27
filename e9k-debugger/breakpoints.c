/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "breakpoints.h"
#include "e9ui.h"
#include "machine.h"
#include "libretro_host.h"
#include "debugger.h"
#include "addr2line.h"

typedef struct breakpoints_record {
    machine_breakpoint_t data;
    int                 present;
} breakpoints_record_t;

typedef struct breakpoints_entry_state {
    char               primary[768];
    char               condition[768];
    int                hasCondition;
    e9ui_component_t  *checkbox;   // child component (owned via e9ui_addChild)
    breakpoints_record_t *record;  // non-owning pointer into list state's array
    struct breakpoints_list_state *list; // non-owning pointer (owner is list component)
} breakpoints_entry_state_t;


struct breakpoints_list_state {
    e9ui_component_t *entries;              // child stack (owned via e9ui_addChild)
    breakpoints_record_t *records;          // owned by list state
    int                  record_count;
    int                  record_cap;
    const machine_breakpoint_t *last_bps;   // non-owning snapshot pointer
    int                        last_count;
};

static void breakpoints_listMarkDirty(breakpoints_list_state_t *st);
static void breakpoints_listRefreshAndMarkDirty(breakpoints_list_state_t *st);

static breakpoints_list_state_t *breakpoints_listState = NULL;


void
breakpoints_resolveLocation(machine_breakpoint_t *bp)
{
    if (!bp || (bp->file[0] && bp->line > 0)) {
        return;
    }
    const char *elf = debugger.config.elfPath;
    if (!elf || !*elf || !debugger.elfValid) {
        return;
    }
    if (!addr2line_start(elf)) {
        return;
    }
    char path[PATH_MAX];
    int line = 0;
    if (addr2line_resolve((uint64_t)bp->addr, path, sizeof(path), &line)) {
        strncpy(bp->file, path, sizeof(bp->file) - 1);
        bp->file[sizeof(bp->file) - 1] = '\0';
        bp->line = line;
    }
}

static const char *
breakpoints_stripCliSrcPrefix(const char *path)
{
    if (!path || !path[0]) return path;

    const char *prefix = debugger.config.sourceDir;
    if (!prefix || !prefix[0]) return path;

    const size_t prefix_len = strlen(prefix);
    if (!prefix_len) return path;

    if (strncmp(path, prefix, prefix_len) != 0) return path;

    const char *stripped = path + prefix_len;
    if (*stripped == '/' || *stripped == '\\') ++stripped;
    return *stripped ? stripped : path;
}

static void
breakpoints_formatLocation(const machine_breakpoint_t *bp, char *dst, size_t cap)
{
    if (!dst || cap == 0) return;
    dst[0] = '\0';

    if (!bp) {
        snprintf(dst, cap, "<unknown>");
        return;
    }

    const char *file = breakpoints_stripCliSrcPrefix(bp->file);
    if (file && file[0] && bp->line > 0) {
        snprintf(dst, cap, "%s:%d", file, bp->line);
        return;
    }
    if (bp->func[0]) {
        snprintf(dst, cap, "%s()", bp->func);
        return;
    }
    if (bp->addr_text[0]) {
        snprintf(dst, cap, "%s", bp->addr_text);
        return;
    }
    if (bp->addr != 0) {
        snprintf(dst, cap, "0x%llX", (unsigned long long)bp->addr);
        return;
    }
    snprintf(dst, cap, "<unknown>");
}

static void
breakpoints_formatState(const machine_breakpoint_t *bp, char *dst, size_t cap)
{
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!bp) return;

    size_t pos = 0;
    int written = snprintf(dst + pos, cap - pos, bp->enabled ? "enabled" : "disabled");
    if (written < 0) return;
    pos += (size_t)written;

    if (bp->disp[0] && strcmp(bp->disp, "keep") != 0 && pos + 2 < cap) {
        written = snprintf(dst + pos, cap - pos, "%s%s", pos ? ", " : "", bp->disp);
        if (written > 0) pos += (size_t)written;
    }
    if (bp->type[0] && strcmp(bp->type, "breakpoint") != 0 && pos + 2 < cap) {
        written = snprintf(dst + pos, cap - pos, "%s%s", pos ? ", " : "", bp->type);
        if (written > 0) pos += (size_t)written;
    }
}


void
breakpoints_registerListState(breakpoints_list_state_t *state)
{
    breakpoints_listState = state;
}

void
breakpoints_unregisterListState(breakpoints_list_state_t *state)
{
    if (breakpoints_listState == state) {
        breakpoints_listState = NULL;
    }
}

void
breakpoints_markDirty(void)
{
    if (breakpoints_listState) {
        breakpoints_listRefreshAndMarkDirty(breakpoints_listState);
    }
}

static void
breakpoints_entryCheckboxCB(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)user;
    if (!st || !st->record) return;

    uint32_t addr = 0;
    if (!machine_setBreakpointEnabled(&debugger.machine, st->record->data.number, selected ? 1 : 0, &addr)) {
        return;
    }
    if (selected) {
        libretro_host_debugAddBreakpoint(addr);
    } else {
        libretro_host_debugRemoveBreakpoint(addr);
    }
    if (st->list) {
        breakpoints_listRefreshAndMarkDirty(st->list);
    }
}

static int
breakpoints_entryPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)self->state;

    TTF_Font *font = e9ui->theme.text.source;
    if (!font && ctx) font = ctx->font;

    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) lh = 16;

    int lines = 1 + (st && st->hasCondition ? 1 : 0);

    int padY = e9ui_checkbox_getMargin(ctx);
    if (padY <= 0) padY = E9UI_THEME_CHECKBOX_MARGIN;

    return padY + lines * lh + padY;
}

static void
breakpoints_entryLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)self->state;
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
breakpoints_entryRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !self->state || !ctx || !ctx->renderer) return;

    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)self->state;

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
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
breakpoints_entryMake(breakpoints_record_t *rec, breakpoints_list_state_t *list)
{
    if (!rec) return NULL;

    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    breakpoints_entry_state_t *st = (breakpoints_entry_state_t*)alloc_calloc(1, sizeof(*st));
    if (!c || !st) {
        alloc_free(c);
        alloc_free(st);
        return NULL;
    }

    c->name = "breakpoints_entry";
    c->state = st;

    const machine_breakpoint_t *bp = &rec->data;

    st->hasCondition = bp->cond[0] != '\0';
    st->record = rec;
    st->list = list;

    int checkbox_selected = bp->enabled ? 1 : 0;
    st->checkbox = e9ui_checkbox_make(NULL, checkbox_selected, breakpoints_entryCheckboxCB, st);
    e9ui_checkbox_setLeftMargin(st->checkbox, 8);
    e9ui_setDisableVariable(st->checkbox, machine_getRunningState(debugger.machine), 1);    
    if (!st->checkbox) {
        alloc_free(st);
        alloc_free(c);
        return NULL;
    }

    // ownership: entry owns checkbox as child
    e9ui_child_add(c, st->checkbox, 0);

    char location[512];
    char state[128];
    breakpoints_formatLocation(bp, location, sizeof(location));
    breakpoints_formatState(bp, state, sizeof(state));

    if (state[0]) {
        snprintf(st->primary, sizeof(st->primary), "#%d %s (%s)", bp->number, location, state);
    } else {
        snprintf(st->primary, sizeof(st->primary), "#%d %s", bp->number, location);
    }
    if (st->hasCondition) {
        snprintf(st->condition, sizeof(st->condition), "  if %s", bp->cond);
    }

    c->preferredHeight = breakpoints_entryPreferredHeight;
    c->layout = breakpoints_entryLayout;
    c->render = breakpoints_entryRender;

    return c;
}

static int
breakpoints_emptyPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)availW;

    TTF_Font *font = e9ui->theme.text.source;
    if (!font && ctx) font = ctx->font;

    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) lh = 16;

    const int padY = 4;
    return padY + lh + padY;
}

static void
breakpoints_emptyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
breakpoints_emptyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) return;

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    if (!font) return;

    const SDL_Color meta = {180, 180, 210, 255};
    const int padX = 8;
    const int padY = 4;

    int tw = 0, th = 0;
    SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, font, "No breakpoints", meta, &tw, &th);
    if (t) {
        SDL_Rect tr = { self->bounds.x + padX, self->bounds.y + padY, tw, th };
        SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
    }
}

static e9ui_component_t *
breakpoints_emptyMake(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->name = "breakpoints_empty";
    c->preferredHeight = breakpoints_emptyPreferredHeight;
    c->layout = breakpoints_emptyLayout;
    c->render = breakpoints_emptyRender;
    return c;
}

static void
breakpoints_listMarkDirty(breakpoints_list_state_t *st)
{
    if (!st) return;
    st->last_bps = NULL;
    st->last_count = -1;
}

static void
breakpoints_listRefreshAndMarkDirty(breakpoints_list_state_t *st)
{
    machine_refresh();
    if (st) breakpoints_listMarkDirty(st);
}

static int
breakpoints_recordEnsureCapacity(breakpoints_list_state_t *st, int minCap)
{
    if (!st) return 0;
    if (st->record_cap >= minCap) return 1;

    int newCap = st->record_cap ? st->record_cap * 2 : 8;
    while (newCap < minCap) newCap *= 2;

    breakpoints_record_t *newArr =
        (breakpoints_record_t*)alloc_realloc(st->records, (size_t)newCap * sizeof(breakpoints_record_t));
    if (!newArr) return 0;

    st->records = newArr;
    st->record_cap = newCap;
    return 1;
}

static breakpoints_record_t *
breakpoints_recordFind(breakpoints_list_state_t *st, int number)
{
    if (!st || st->record_count <= 0) return NULL;

    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].data.number == number) {
            return &st->records[i];
        }
    }
    return NULL;
}

static breakpoints_record_t *
breakpoints_recordAdd(breakpoints_list_state_t *st, const machine_breakpoint_t *bp)
{
    if (!st || !bp) return NULL;

    if (st->record_count == st->record_cap) {
        if (!breakpoints_recordEnsureCapacity(st, st->record_count + 1)) {
            return NULL;
        }
    }

    breakpoints_record_t *rec = &st->records[st->record_count++];
    memcpy(&rec->data, bp, sizeof(rec->data));
    rec->present = 0;
    return rec;
}

static int
breakpoints_updateRecords(breakpoints_list_state_t *st, const machine_breakpoint_t *bps, int count)
{
    if (!st) return 0;

    int changed = 0;
    int hadActive = 0;

    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].present) hadActive = 1;
        st->records[i].present = 0;
    }

    if (!bps || count <= 0) {
        if (hadActive) changed = 1;
        return changed;
    }

    for (int i = 0; i < count; ++i) {
        const machine_breakpoint_t *bp = &bps[i];
        breakpoints_record_t *rec = breakpoints_recordFind(st, bp->number);

        if (!rec) {
            rec = breakpoints_recordAdd(st, bp);
            if (!rec) continue;
            changed = 1;
        } else {
            if (memcmp(&rec->data, bp, sizeof(rec->data)) != 0) {
                memcpy(&rec->data, bp, sizeof(rec->data));
                changed = 1;
            }
        }
        rec->present = 1;
    }

    return changed;
}

static void
breakpoints_rebuildEntries(breakpoints_list_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->entries) return;

    e9ui_stack_removeAll(st->entries, ctx);

    int presentCount = 0;
    for (int i = 0; i < st->record_count; ++i) {
        if (st->records[i].present) presentCount++;
    }

    if (presentCount == 0) {
        e9ui_component_t *empty = breakpoints_emptyMake();
        if (empty) {
            e9ui_stack_addFlex(st->entries, empty);
        }
        return;
    }

    for (int i = 0; i < st->record_count; ++i) {
        breakpoints_record_t *rec = &st->records[i];
        if (!rec->present) continue;

        e9ui_component_t *entry = breakpoints_entryMake(rec, st);
        if (entry) {
            e9ui_stack_addFixed(st->entries, entry);
        }
    }
}

static void
breakpoints_listRebuild(breakpoints_list_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->entries) return;

    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);

    int changed = 0;
    if (bps != st->last_bps || count != st->last_count) {
        st->last_bps = bps;
        st->last_count = count;
        changed = 1;
    }
    if (breakpoints_updateRecords(st, bps, count)) {
        changed = 1;
    }
    if (!changed) return;

    breakpoints_rebuildEntries(st, ctx);
}

static int
breakpoints_listPreferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0;
}

static void
breakpoints_listLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)self->state;
    if (!st || !st->entries) return;

    breakpoints_listRebuild(st, ctx);

    if (st->entries->layout) {
        st->entries->layout(st->entries, ctx, bounds);
    }
}

static void
breakpoints_listRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!ctx || !ctx->renderer) return;

    SDL_Rect r = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &r);

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)self->state;
    if (st && st->entries && st->entries->render) {
        st->entries->render(st->entries, ctx);
    }
}

static void
breakpoints_listDtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)self->state;
    if (!st) return;

    breakpoints_unregisterListState(st);

    if (st->records) {
        alloc_free(st->records);
        st->records = NULL;
    }
    st->record_count = 0;
    st->record_cap = 0;
}

static void
breakpoints_addCurrentCB(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    unsigned long pc = 0;
    if (!machine_findReg(&debugger.machine, "PC", &pc)) {
        return;
    }
    uint32_t addr = (uint32_t)pc & 0x00ffffffu;
    machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, addr);
    if (bp) {
        if (!bp->enabled) {
            bp->enabled = 1;
            libretro_host_debugAddBreakpoint(addr);
        }
        breakpoints_resolveLocation(bp);
    } else {
        bp = machine_addBreakpoint(&debugger.machine, addr, 1);
        if (bp) {
            libretro_host_debugAddBreakpoint(addr);
            breakpoints_resolveLocation(bp);
        }
    }
    breakpoints_markDirty();
}

static void
breakpoints_clearAllCB(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);
    if (bps && count > 0) {
        for (int i = 0; i < count; ++i) {
            uint32_t addr = (uint32_t)(bps[i].addr & 0x00ffffffu);
            libretro_host_debugRemoveBreakpoint(addr);
        }
    }
    machine_clearBreakpoints(&debugger.machine);
    breakpoints_markDirty();
}

static void
breakpoints_toggleAllCB(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    machine_getBreakpoints(&debugger.machine, &bps, &count);
    if (!bps || count <= 0) {
        return;
    }
    int anyEnabled = 0;
    for (int i = 0; i < count; ++i) {
        if (bps[i].enabled) {
            anyEnabled = 1;
            break;
        }
    }
    int targetEnabled = anyEnabled ? 0 : 1;
    for (int i = 0; i < count; ++i) {
        uint32_t addr = (uint32_t)(bps[i].addr & 0x00ffffffu);
        machine_setBreakpointEnabled(&debugger.machine, bps[i].number, targetEnabled, NULL);
        if (targetEnabled) {
            libretro_host_debugAddBreakpoint(addr);
        } else {
            libretro_host_debugRemoveBreakpoint(addr);
        }
    }
    breakpoints_markDirty();
}

static e9ui_component_t *
breakpoints_makeList(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) return NULL;

    breakpoints_list_state_t *st = (breakpoints_list_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }

    c->name = "breakpoints_list";
    c->state = st;

    st->entries = e9ui_stack_makeVertical();
    if (st->entries) {
        e9ui_child_add(c, st->entries, 0);
    }

    breakpoints_registerListState(st);

    c->preferredHeight = breakpoints_listPreferredHeight;
    c->layout = breakpoints_listLayout;
    c->render = breakpoints_listRender;
    c->dtor = breakpoints_listDtor;

    return c;
}


e9ui_component_t *
breakpoints_makeComponent(void)
{
    e9ui_component_t *list = breakpoints_makeList();

    e9ui_component_t *toolbar = e9ui_flow_make();
    e9ui_flow_setPadding(toolbar, 0);
    e9ui_flow_setSpacing(toolbar, 6);
    e9ui_flow_setWrap(toolbar, 1);

    breakpoints_list_state_t *list_state = list ? (breakpoints_list_state_t*)list->state : NULL;

    e9ui_component_t *btn_add = e9ui_button_make("Add Current", breakpoints_addCurrentCB, list_state);
    e9ui_setDisableVariable(btn_add, machine_getRunningState(debugger.machine), 1);        
    e9ui_button_setMini(btn_add, 1);
    e9ui_button_setIconAsset(btn_add, "assets/icons/break.png");
    e9ui_setTooltip(btn_add, "Add Current - b");
    e9ui_button_registerHotkey(btn_add, &e9ui->ctx, SDLK_b, 0, 0);

    e9ui_flow_add(toolbar, btn_add);

    e9ui_component_t *btn_toggle = e9ui_button_make("Toggle", breakpoints_toggleAllCB, list_state);
    e9ui_setDisableVariable(btn_toggle, machine_getRunningState(debugger.machine), 1);
    e9ui_button_setMini(btn_toggle, 1);
    e9ui_flow_add(toolbar, btn_toggle);

    e9ui_component_t *btn_clear = e9ui_button_make("Clear", breakpoints_clearAllCB, list_state);
    e9ui_setDisableVariable(btn_clear, machine_getRunningState(debugger.machine), 1);
    e9ui_button_setMini(btn_clear, 1);
    e9ui_button_setIconAsset(btn_clear, "assets/icons/trash.png");
    e9ui_flow_add(toolbar, btn_clear);

    e9ui_component_t *toolbar_box = e9ui_box_make(toolbar);
    e9ui_box_setPadding(toolbar_box, 6);
    e9ui_box_setBorder(toolbar_box, E9UI_BORDER_BOTTOM, (SDL_Color){70, 70, 70, 255}, 1);

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(stack, toolbar_box);
    if (list) {
        e9ui_stack_addFlex(stack, list);
    }

    return stack;
}
