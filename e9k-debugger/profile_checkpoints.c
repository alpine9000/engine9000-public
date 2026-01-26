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

#include "profile_checkpoints.h"
#include "profile_list.h"
#include "libretro_host.h"
#include "debugger.h"
#include "geo_checkpoint.h"
#include "e9ui.h"

typedef struct profile_checkpoints_state {
    geo_debug_checkpoint_t entries[GEO_CHECKPOINT_COUNT];
    size_t entryCount;
    size_t visibleCount;
    int enabled;
    e9ui_component_t *profileButton;
} profile_checkpoints_state_t;

static e9ui_component_t *profile_checkpoints_list_makeComponent(void);
static void profile_checkpoints_refresh(profile_checkpoints_state_t *st);
static int  profile_checkpoints_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW);
static void profile_checkpoints_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds);
static void profile_checkpoints_render(e9ui_component_t *self, e9ui_context_t *ctx);
static void profile_checkpoints_onToggle(e9ui_context_t *ctx, void *user);
static void profile_checkpoints_onReset(e9ui_context_t *ctx, void *user);
static void profile_checkpoints_onDump(e9ui_context_t *ctx, void *user);

static void
profile_checkpoints_refresh(profile_checkpoints_state_t *st)
{
    if (!st) {
        return;
    }

    libretro_host_debugGetCheckpointEnabled(&st->enabled);
    if (st->profileButton) {
        if (st->enabled) {
            e9ui_button_setTheme(st->profileButton, e9ui_theme_button_preset_profile_active());
        } else {
            e9ui_button_clearTheme(st->profileButton);
        }
    }
    size_t bytes = libretro_host_debugReadCheckpoints(st->entries, sizeof(st->entries));
    st->entryCount = bytes / sizeof(st->entries[0]);
    if (st->entryCount > GEO_CHECKPOINT_COUNT) {
        st->entryCount = GEO_CHECKPOINT_COUNT;
    }
    st->visibleCount = 0;
    for (size_t i = 0; i < st->entryCount; ++i) {
        if (st->entries[i].count > 0) {
            st->visibleCount++;
        }
    }
}

static int
profile_checkpoints_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    if (!self || !ctx || !self->state) {
        return 0;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)self->state;
    profile_checkpoints_refresh(st);

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    int lineHeight = font ? TTF_FontHeight(font) : 16;
    if (lineHeight <= 0) {
        lineHeight = 16;
    }

    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    size_t lines = st->visibleCount > 0 ? st->visibleCount : 1;
    return padY * 2 + (int)(lines * (size_t)lineHeight);
}

static void
profile_checkpoints_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    if (!self) {
        return;
    }
    self->bounds = bounds;
}

static void
profile_checkpoints_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer || !self->state) {
        return;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)self->state;
    profile_checkpoints_refresh(st);

    SDL_Rect bg = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    SDL_SetRenderDrawColor(ctx->renderer, 16, 16, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &bg);

    TTF_Font *font = debugger.theme.text.source ? debugger.theme.text.source : ctx->font;
    if (!font) {
        return;
    }

    int padX = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_X);
    int padY = e9ui_scale_px(ctx, PROFILE_LIST_PADDING_Y);
    int lineHeight = TTF_FontHeight(font);
    if (lineHeight <= 0) {
        lineHeight = 16;
    }

    SDL_Color textColor = { 200, 200, 200, 255 };
    int maxY = self->bounds.y + self->bounds.h - padY;
    int lineIndex = 0;

    if (st->visibleCount == 0) {
        const char *empty = "No checkpoints";
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getUTF8(ctx->renderer, font, empty, textColor, &tw, &th);
        if (tex) {
            SDL_Rect rect = { self->bounds.x + padX, self->bounds.y + padY, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &rect);
        }
        return;
    }

    for (size_t i = 0; i < st->entryCount; ++i) {
        if (st->entries[i].count == 0) {
            continue;
        }

        int y = self->bounds.y + padY + lineIndex * lineHeight;
        if (y + lineHeight > maxY) {
            break;
        }

        char line[160];
        snprintf(line, sizeof(line),
                 "%02zu avg:%llu min:%llu max:%llu",
                 i,
                 (unsigned long long)st->entries[i].average,
                 (unsigned long long)st->entries[i].minimum,
                 (unsigned long long)st->entries[i].maximum);

        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getUTF8(ctx->renderer, font, line, textColor, &tw, &th);
        if (tex) {
            SDL_Rect rect = { self->bounds.x + padX, y, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &rect);
        }

        lineIndex++;
    }
}

e9ui_component_t *
profile_checkpoints_makeComponent(void)
{
    e9ui_component_t *list = profile_checkpoints_list_makeComponent();
    if (!list) {
        return NULL;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)list->state;

    e9ui_component_t *toolbar = e9ui_flow_make();
    e9ui_flow_setWrap(toolbar, 0);
    e9ui_flow_setSpacing(toolbar, 6);
    e9ui_flow_setPadding(toolbar, 6);

    e9ui_component_t *btn_profile = e9ui_button_make("Profile", profile_checkpoints_onToggle, st);
    e9ui_button_setMini(btn_profile, 1);
    e9ui_setTooltip(btn_profile, "Profile - ,");
    st->profileButton = btn_profile;
    if (st->enabled) {
        e9ui_button_setTheme(btn_profile, e9ui_theme_button_preset_profile_active());
    } else {
        e9ui_button_clearTheme(btn_profile);
    }
    e9ui_flow_add(toolbar, btn_profile);

    e9ui_component_t *btn_reset = e9ui_button_make("Reset", profile_checkpoints_onReset, st);
    e9ui_button_setMini(btn_reset, 1);
    e9ui_button_setTheme(btn_reset, e9ui_theme_button_preset_red());
    e9ui_setTooltip(btn_reset, "Reset - .");
    e9ui_flow_add(toolbar, btn_reset);

    e9ui_component_t *btn_dump = e9ui_button_make("Dump", profile_checkpoints_onDump, st);
    e9ui_button_setMini(btn_dump, 1);
    e9ui_setTooltip(btn_dump, "Dump - /");
    e9ui_flow_add(toolbar, btn_dump);

    e9ui_component_t *stack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(stack, toolbar);
    e9ui_stack_addFlex(stack, list);
    stack->name = "profile_checkpoints";

    return stack;
}

static e9ui_component_t *
profile_checkpoints_list_makeComponent(void)
{
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) {
        return NULL;
    }

    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }

    comp->name = "profile_checkpoints";
    comp->state = st;
    comp->preferredHeight = profile_checkpoints_preferredHeight;
    comp->layout = profile_checkpoints_layout;
    comp->render = profile_checkpoints_render;

    return comp;
}

static void
profile_checkpoints_onToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)user;
    if (!st) {
        return;
    }
    int enabled = st->enabled ? 0 : 1;
    if (libretro_host_debugSetCheckpointEnabled(enabled)) {
        st->enabled = enabled;
    }
}

static void
profile_checkpoints_onReset(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)user;
    if (!st) {
        return;
    }
    if (libretro_host_debugResetCheckpoints()) {
        profile_checkpoints_refresh(st);
    }
}

static void
profile_checkpoints_onDump(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    profile_checkpoints_state_t *st = (profile_checkpoints_state_t*)user;
    if (!st) {
        return;
    }
    profile_checkpoints_refresh(st);
    printf("Profiler checkpoints (avg/min/max):\n");
    for (size_t i = 0; i < st->entryCount; ++i) {
        if (st->entries[i].count == 0) {
            continue;
        }
        printf("%02zu avg:%llu min:%llu max:%llu\n",
               i,
               (unsigned long long)st->entries[i].average,
               (unsigned long long)st->entries[i].minimum,
               (unsigned long long)st->entries[i].maximum);
    }
    fflush(stdout);
}
