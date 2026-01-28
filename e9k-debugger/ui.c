/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "breakpoints.h"
#include "clipboard.h"
#include "config.h"
#include "console_cmd.h"
#include "debug.h"
#include "debugger.h"
#include "e9ui.h"
#include "file.h"
#include "emu.h"
#include "libretro_host.h"
#include "machine.h"
#include "memory.h"
#include "profile.h"
#include "profile_list.h"
#include "profile_checkpoints.h"
#include "prompt.h"
#include "registers.h"
#include "settings.h"
#include "snapshot.h"
#include "source_pane.h"
#include "stack.h"
#include "status_bar.h"
#include "state_buffer.h"
#include "trainer.h"
#include "console.h"
#include "smoke_test.h"

static e9ui_component_t *ui_source_panes[2];

static const char *
ui_basename(const char *path)
{
    if (!path || !path[0]) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static void
ui_promptFocusHotkey(e9ui_context_t *ctx, void *user)
{
    e9ui_component_t *prompt = (e9ui_component_t*)user;
    if (!ctx || !prompt) {
        return;
    }
    if (*machine_getRunningState(debugger.machine)) {
        return;
    }
    prompt_focus(ctx, prompt);
}

void
ui_updateSourceTitle(void)
{
    if (!e9ui->sourceBox) {
        return;
    }
    char path[PATH_MAX];
    int has = 0;
    for (size_t i = 0; i < sizeof(ui_source_panes)/sizeof(ui_source_panes[0]); ++i) {
        if (ui_source_panes[i] && source_pane_getCurrentFile(ui_source_panes[i], path, sizeof(path))) {
            has = 1;
            break;
        }
    }
    char title[PATH_MAX + 16];
    if (has) {
        const char *base = ui_basename(path);
        if (base && *base) {
            snprintf(title, sizeof(title), "SOURCE - %s", base);
        } else {
            snprintf(title, sizeof(title), "SOURCE");
        }
    } else {
        snprintf(title, sizeof(title), "SOURCE");
    }
    if (strcmp(title, e9ui->sourceTitle) != 0) {
        strncpy(e9ui->sourceTitle, title, sizeof(e9ui->sourceTitle) - 1);
        e9ui->sourceTitle[sizeof(e9ui->sourceTitle) - 1] = '\0';
        e9ui_box_setTitlebar(e9ui->sourceBox, e9ui->sourceTitle, "assets/icons/source.png");
    }
}

void
ui_refreshOnPause(void)
{
    machine_refresh();
    memory_refreshOnBreak();
    for (size_t i = 0; i < sizeof(ui_source_panes)/sizeof(ui_source_panes[0]); ++i) {
        if (ui_source_panes[i]) {
            source_pane_markNeedsRefresh(ui_source_panes[i]);
        }
    }
}

void
ui_centerSourceOnAddress(uint32_t addr)
{
    for (size_t i = 0; i < sizeof(ui_source_panes)/sizeof(ui_source_panes[0]); ++i) {
        if (ui_source_panes[i]) {
            source_pane_centerOnAddress(ui_source_panes[i], &e9ui->ctx, addr);
        }
    }
}

void
ui_applySourcePaneElfMode(void)
{
    int showToggle = 1;
    for (size_t i = 0; i < sizeof(ui_source_panes)/sizeof(ui_source_panes[0]); ++i) {
        e9ui_component_t *pane = ui_source_panes[i];
        if (!pane) {
            continue;
        }
        if (!debugger.elfValid && source_pane_getMode(pane) == source_pane_mode_c) {
            source_pane_setMode(pane, source_pane_mode_a);
        }
        source_pane_setToggleVisible(pane, showToggle);
    }
}

static void
ui_pause(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (libretro_host_debugPause()) {
        machine_setRunning(&debugger.machine, 0);
        debugger_clearFrameStep();
        return;
    }
}

static void
ui_continue(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (libretro_host_debugResume()) {
        machine_setRunning(&debugger.machine, 1);
        return;
    }
}

static void
ui_frameStep(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger.frameStepMode = 1;
    debugger.frameStepPending = 1;
}

static void
ui_frameStepBack(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger.frameStepMode = 1;
    debugger.frameStepPending = -1;
}

static void
ui_frameContinue(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger.frameStepMode = 0;
    debugger.frameStepPending = 0;
}

static void
ui_step(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepLine()) {
        machine_setRunning(&debugger.machine, 1);
        return;
    }
    debug_error("step line: libretro core does not expose debug step line");
}

static void
ui_next(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepNext()) {
        machine_setRunning(&debugger.machine, 1);
        return;
    }
    debug_error("step next: libretro core does not expose debug step next");
}

static void
ui_stepi(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepInstr()) {
        machine_setRunning(&debugger.machine, 1);
        return;
    }
    debug_error("step instruction: libretro core does not expose debug step");
}

static void
ui_finish(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    console_cmd_sendLine("finish");
}

static void
ui_speedToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger_toggleSpeed();
}

static void
ui_speedButtonRefresh(void)
{
    if (!e9ui->speedButton) {
        return;
    }
    const char* asset = debugger.speedMultiplier == 1 ? "assets/icons/speed_normal.png" : "assets/icons/speed_fast.png";
    e9ui_button_setIconAsset(e9ui->speedButton, asset);
    if (debugger.speedMultiplier == 10) {
        e9ui_button_setTheme(e9ui->speedButton, e9ui_theme_button_preset_red());
    } else {
        e9ui_button_setTheme(e9ui->speedButton, e9ui_theme_button_preset_green());
    }
}

void
ui_refreshSpeedButton(void)
{
    ui_speedButtonRefresh();
}

static void
ui_reset(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    if (!libretro_host_resetCore()) {
        debug_error("Reset: libretro core does not expose reset");
    }
}

static void
ui_restart(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger.restartRequested = 1;
}

static void
ui_audioRefreshButton(void)
{
    if (!e9ui->audioButton) {
        return;
    }
    const char *icon = debugger.config.audioEnabled ? "assets/icons/audio.png"
                                                    : "assets/icons/mute.png";
    e9ui_button_setIconAsset(e9ui->audioButton, icon);
}

static void
ui_audioToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger.config.audioEnabled = debugger.config.audioEnabled ? 0 : 1;
    libretro_host_setAudioEnabled(debugger.config.audioEnabled);
    ui_audioRefreshButton();
    config_saveConfig();
}

static void
ui_saveState(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    size_t size = 0;
    size_t diff = 0;
    if (libretro_host_saveState(&size, &diff)) {
        // Saving a state should work even if the rolling state buffer is disabled.
        debugger.hasStateSnapshot = 1;
        debug_printf("ui: saveState ok size=%zu diff=%zu maxBytes=%zu count=%zu",
                     size, diff, state_buffer_getMaxBytes(), state_buffer_getCount());
        fprintf(stderr, "ui: saveState ok size=%zu diff=%zu maxBytes=%zu count=%zu\n",
                size, diff, state_buffer_getMaxBytes(), state_buffer_getCount());
        fflush(stderr);
        if (state_buffer_getMaxBytes() > 0 && state_buffer_getCount() > 0) {
            (void)state_buffer_snapshot();
        } else {
            const uint8_t *stateData = NULL;
            size_t stateSize = 0;
            uint64_t frameNo = state_buffer_getCurrentFrameNo();
            if (libretro_host_getStateData(&stateData, &stateSize)) {
                int ok = state_buffer_setSaveKeyframe(stateData, stateSize, frameNo);
                debug_printf("ui: saveState seeded snapshot ok=%d frameNo=%llu stateSize=%zu",
                             ok, (unsigned long long)frameNo, stateSize);
                fprintf(stderr, "ui: saveState seeded snapshot ok=%d frameNo=%llu stateSize=%zu\n",
                        ok, (unsigned long long)frameNo, stateSize);
                fflush(stderr);
            } else {
                debug_printf("ui: saveState missing host stateData");
                fprintf(stderr, "ui: saveState missing host stateData\n");
                fflush(stderr);
            }
        }
        // Persist immediately, not just on exit (matches user expectation for the Save button).
        snapshot_saveOnExit();
        e9ui_showTransientMessage("STATE SAVED");
    } else {
        debug_error("Save state failed");
    }
}

static void
ui_restoreState(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    size_t size = 0;
    if (libretro_host_restoreState(&size)) {
        int ok = state_buffer_restoreSnapshot();
        debugger.hasStateSnapshot = 1;
        if (ok) {
            debugger.frameCounter = state_buffer_getCurrentFrameNo();
        }
        e9ui_showTransientMessage("STATE RESTORED");
    } else {
        debug_error("Restore state failed");
    }
}

void
ui_copyFramebufferToClipboard(void)
{
    const uint8_t *data = NULL;
    int width = 0;
    int height = 0;
    size_t pitch = 0;
    if (!libretro_host_getFrame(&data, &width, &height, &pitch)) {
        debug_error("clipboard: framebuffer unavailable");
        return;
    }
    size_t needed = (size_t)height * pitch;
    uint8_t *copy = (uint8_t *)malloc(needed);
    if (!copy) {
        debug_error("clipboard: out of memory");
        return;
    }
    memcpy(copy, data, needed);
    int ok = clipboard_setImageXRGB8888(copy, width, height, pitch);
    free(copy);
    if (!ok) {
        debug_error("clipboard: failed to set image");
        return;
    }
    e9ui_showTransientMessage("COPIED SCREEN TO CLIPBOARD");
}

void
ui_build(void)
{
    // Build top row: [ image 240x48 ] [ toolbar grows ]
    // Two source panes side-by-side; overlay toggle on each
    e9ui_component_t *comp_source_left  = source_pane_make();
    comp_source_left->persist_id = "src_left";
    e9ui_component_t *comp_source_right = source_pane_make();
    comp_source_right->persist_id = "src_right";
    // Wire up context callbacks
    e9ui->ctx.sendLine = console_cmd_sendLine;
    e9ui->ctx.sendInterrupt = console_cmd_sendInterrupt;
    e9ui->ctx.applyCompletion = prompt_applyCompletion;
    e9ui->ctx.showCompletions = prompt_showCompletions;
    e9ui->ctx.hideCompletions = prompt_hideCompletions;

    e9ui_component_t *comp_console = console_makeComponent();
    e9ui_component_t *comp_console_box = e9ui_box_make(comp_console);
    comp_console_box->persist_id = "gdb_box";
    e9ui_box_setTitlebar(comp_console_box, "CONSOLE", "assets/icons/debug.png");

    ui_source_panes[0] = comp_source_left;
    ui_source_panes[1] = comp_source_right;
    e9ui_component_t *comp_sources_hs = e9ui_split_makeComponent(comp_source_left, comp_source_right, e9ui_orient_horizontal, 0.50f, 6);
    e9ui_component_t *comp_sources_box = e9ui_box_make(comp_sources_hs);
    comp_sources_box->persist_id = "source_box";
    e9ui_box_setTitlebar(comp_sources_box, "SOURCE", "assets/icons/source.png");
    e9ui->sourceBox = comp_sources_box;
    strncpy(e9ui->sourceTitle, "SOURCE", sizeof(e9ui->sourceTitle) - 1);
    e9ui->sourceTitle[sizeof(e9ui->sourceTitle) - 1] = '\0';
    e9ui_component_t *comp_libretro_view = emu_makeComponent();
    comp_libretro_view->persist_id = "geo_view";
    e9ui_component_t *comp_libretro_box = e9ui_box_make(comp_libretro_view);
    comp_libretro_box->persist_id = "libretro_box";
    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
      e9ui_box_setTitlebar(comp_libretro_box, "AMIGA", "assets/icons/game.png");
    } else {
      e9ui_box_setTitlebar(comp_libretro_box, "NEO GEO", "assets/icons/game.png");
    }
    e9ui_component_t *comp_gdb_geo = e9ui_split_makeComponent(comp_console_box, comp_libretro_box, e9ui_orient_horizontal, 0.60f, 6);
    e9ui_split_setId(comp_gdb_geo, "gdb_geo");
    e9ui_component_t *comp_split = e9ui_split_makeComponent(comp_sources_box, comp_gdb_geo, e9ui_orient_vertical, 0.66f, 6);

    e9ui_split_setId(comp_split, "src_console");
    e9ui_component_t *comp_prompt = prompt_makeComponent();
    e9ui_setDisableVariable(comp_prompt, machine_getRunningState(debugger.machine), 1);
    e9ui->prompt = comp_prompt;
    if (e9ui->ctx.registerHotkey) {
        e9ui->ctx.registerHotkey(&e9ui->ctx, SDLK_TAB, 0, 0, ui_promptFocusHotkey, comp_prompt);
    }

    // Build top row: [ image 240x48 ] [ toolbar grows ]
    SDL_Texture *logoTex = NULL; int logoW = 0, logoH = 0;
    {
        char exedir[PATH_MAX];
        if (file_getExeDir(exedir, sizeof(exedir))) {
            char p[PATH_MAX]; size_t n = strlen(exedir);
            if (n < sizeof(p)) {
                memcpy(p, exedir, n);
                if (n > 0 && p[n-1] != '/') {
                    p[n++] = '/';
                }
                const char *rel = "assets/enable.png";
                size_t rl = strlen(rel);
                if (n + rl < sizeof(p)) {
                    memcpy(p + n, rel, rl + 1);
                    SDL_Surface *s = IMG_Load(p);
                    if (s) {
                        logoTex = SDL_CreateTextureFromSurface(e9ui->ctx.renderer, s);
                        logoW = s->w;
                        logoH = s->h;
                        SDL_FreeSurface(s);
                    } else {
                        debug_error("e9k: IMG_Load failed for %s: %s", p, IMG_GetError());
                    }
                }
            }
        }
    }
    e9ui_component_t *comp_logo = NULL;
    if (logoTex) {
        comp_logo = e9ui_image_makeFromTexture(logoTex, logoW, logoH);
        // Inner box constrains image to 240x48
        e9ui_component_t *logo_box = e9ui_box_make(comp_logo);
        e9ui_box_setWidth(logo_box, e9ui_dim_fixed, 240);
        e9ui_box_setHeight(logo_box, e9ui_dim_fixed, 48);
        // Outer box provides 10px margin on all sides (no fixed size here to avoid double-counting padding)
        e9ui_component_t *logo_margin = e9ui_box_make(logo_box);
        e9ui_box_setPadding(logo_margin, 10);
        comp_logo = logo_margin;
    }
    // Build toolbar via flow of buttons
    e9ui_component_t *flow = e9ui_flow_make();
    e9ui->toolbar = flow;
    // Keep toolbar height tight to button height (no extra vertical padding)
    e9ui_flow_setPadding(flow, 0);
    e9ui_flow_setSpacing(flow, 8);
    e9ui_flow_setWrap(flow, 1);
    // Build buttons and bind hotkeys at creation
    e9ui_component_t *btn_continue = e9ui_button_make("Continue", ui_continue, NULL);
    e9ui_button_setIconAsset(btn_continue, "assets/icons/continue.png");
    e9ui_setTooltip(btn_continue, "Continue - c");
    e9ui_button_registerHotkey(btn_continue, &e9ui->ctx, SDLK_c, (SDL_Keymod)(KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI), 0);
    e9ui_setHiddenVariable(btn_continue, machine_getRunningState(debugger.machine), 1);
    e9ui_flow_add(flow, btn_continue);

    e9ui_component_t *btn_pause = e9ui_button_make("Pause", ui_pause, NULL);
    e9ui_button_setIconAsset(btn_pause, "assets/icons/pause.png");
    e9ui_setTooltip(btn_pause, "Pause - p");
    e9ui_button_setLargestLabel(btn_pause, "Continue");
    e9ui_button_registerHotkey(btn_pause, &e9ui->ctx, SDLK_p, 0, 0);
    e9ui_setHiddenVariable(btn_pause, machine_getRunningState(debugger.machine), 0);
    e9ui_flow_add(flow, btn_pause);

    e9ui_component_t *btn_step = e9ui_button_make("Step", ui_step, NULL);
    e9ui_button_setIconAsset(btn_step, "assets/icons/step.png");
    e9ui_setTooltip(btn_step, "Step - s");
    e9ui_button_registerHotkey(btn_step, &e9ui->ctx, SDLK_s, 0, 0);
    e9ui_setDisableVariable(btn_step, machine_getRunningState(debugger.machine), 1);
    e9ui_setHiddenVariable(btn_step, &debugger.elfValid, 0);
    e9ui_flow_add(flow, btn_step);

    e9ui_component_t *btn_next = e9ui_button_make("Next", ui_next, NULL);
    e9ui_button_setIconAsset(btn_next, "assets/icons/next.png");
    e9ui_setTooltip(btn_next, "Next - n");
    e9ui_button_registerHotkey(btn_next, &e9ui->ctx, SDLK_n, 0, 0);
    e9ui_setDisableVariable(btn_next, machine_getRunningState(debugger.machine), 1);
    e9ui_setHiddenVariable(btn_next, &debugger.elfValid, 0);
    e9ui_flow_add(flow, btn_next);

    // Instruction step (si) with global hotkey 'i'
    e9ui_component_t *btn_stepi = e9ui_button_make("Inst", ui_stepi, NULL);
    e9ui_button_setIconAsset(btn_stepi, "assets/icons/step.png");
    e9ui_setTooltip(btn_stepi, "Step Inst - i");
    e9ui_button_registerHotkey(btn_stepi, &e9ui->ctx, SDLK_i, 0, 0);
    e9ui_setDisableVariable(btn_stepi, machine_getRunningState(debugger.machine), 1);
    e9ui_flow_add(flow, btn_stepi);

    e9ui_component_t *btn_finish = e9ui_button_make("Out", ui_finish, NULL);
    e9ui_button_setIconAsset(btn_finish, "assets/icons/step_out.png");
    e9ui_setTooltip(btn_finish, "Step Out");
    e9ui_setDisableVariable(btn_finish, machine_getRunningState(debugger.machine), 1);
    e9ui_setHiddenVariable(btn_finish, &debugger.elfValid, 0);
    e9ui_flow_add(flow, btn_finish);

    e9ui_component_t *sep_step_speed = e9ui_separator_make(9);
    e9ui_flow_add(flow, sep_step_speed);

    e9ui_component_t *btn_speed = e9ui_button_make("", ui_speedToggle, NULL);
    e9ui_button_setIconAsset(btn_finish, "assets/icons/speed_normal.png");
    e9ui_setTooltip(btn_speed, "Speed toggle - F5");
    e9ui_button_registerHotkey(btn_speed, &e9ui->ctx, SDLK_F5, 0, 0);
    e9ui->speedButton = btn_speed;
    ui_speedButtonRefresh();

    e9ui_component_t *btn_frame_stepBack = e9ui_button_make("Back", ui_frameStepBack, NULL);
    e9ui_button_setIconAsset(btn_frame_stepBack, "assets/icons/back.png");
    e9ui_setTooltip(btn_frame_stepBack, "Frame step back - b");
    e9ui_button_registerHotkey(btn_frame_stepBack, &e9ui->ctx, SDLK_b, 0, 0);
    e9ui_flow_add(flow, btn_frame_stepBack);

    e9ui_component_t *btn_frame_step = e9ui_button_make("Frame", ui_frameStep, NULL);
    e9ui_button_setIconAsset(btn_frame_step, "assets/icons/step.png");
    e9ui_setTooltip(btn_frame_step, "Frame step - f");
    e9ui_button_registerHotkey(btn_frame_step, &e9ui->ctx, SDLK_f, 0, 0);
    e9ui_flow_add(flow, btn_frame_step);

    e9ui_component_t *btn_frame_continue = e9ui_button_make("Continue", ui_frameContinue, NULL);
    e9ui_setTooltip(btn_frame_continue, "Frame continue - g");
    e9ui_button_registerHotkey(btn_frame_continue, &e9ui->ctx, SDLK_g, 0, 0);
    e9ui_setDisableVariable(btn_frame_continue, &debugger.frameStepMode, 0);
    e9ui_flow_add(flow, btn_frame_continue);

    e9ui_component_t *sep_frame_save = e9ui_separator_make(9);
    e9ui_flow_add(flow, sep_frame_save);

    e9ui_component_t *btn_save = e9ui_button_make("Save", ui_saveState, NULL);
    e9ui_setTooltip(btn_save, "Save state - F7");
    e9ui_button_registerHotkey(btn_save, &e9ui->ctx, SDLK_F7, 0, 0);
    e9ui_flow_add(flow, btn_save);

    e9ui_component_t *btn_restore = e9ui_button_make("Restore", ui_restoreState, NULL);
    e9ui_setTooltip(btn_restore, "Restore state - F8");
    e9ui_button_registerHotkey(btn_restore, &e9ui->ctx, SDLK_F8, 0, 0);
    e9ui_setDisableVariable(btn_restore, &debugger.hasStateSnapshot, 0);
    e9ui_flow_add(flow, btn_restore);

    e9ui_component_t *sep_restore_reset = e9ui_separator_make(9);
    e9ui_flow_add(flow, sep_restore_reset);

    e9ui_component_t *toolbar_box = e9ui_box_make(flow);
    // No extra padding; fix height to flow's preferred height and center vertically
    e9ui_box_setPadding(toolbar_box, 0);
    e9ui_box_setVAlign(toolbar_box, e9ui_valign_center);
    e9ui_component_t *top_row = e9ui_hstack_make();
    if (comp_logo) {
        int logoSlotW = e9ui_scale_px(&e9ui->ctx, 240 + 20);
        e9ui_hstack_addFixed(top_row, comp_logo, logoSlotW);
    }

    e9ui_component_t *btn_settings = e9ui_button_make("Settings", settings_uiOpen, NULL);
    e9ui_setTooltip(btn_settings, "Settings");
    e9ui->settingsButton = btn_settings;
    e9ui_flow_add(flow, btn_settings);

    if (btn_speed) {
        e9ui_flow_add(flow, btn_speed);
    }

    e9ui_component_t *btn_audio = e9ui_button_make("", ui_audioToggle, NULL);
    e9ui_setTooltip(btn_audio, "Audio - F6");
    e9ui_button_registerHotkey(btn_audio, &e9ui->ctx, SDLK_F6, 0, 0);
    e9ui->audioButton = btn_audio;
    ui_audioRefreshButton();
    e9ui_flow_add(flow, btn_audio);

    e9ui_component_t *btn_reset = e9ui_button_make("", ui_reset, NULL);
    e9ui_setTooltip(btn_reset, "Reset core");
    e9ui_button_setIconAsset(btn_reset, "assets/icons/reset.png");
    e9ui_button_setTheme(btn_reset, e9ui_theme_button_preset_profile_active());
    e9ui->resetButton = btn_reset;
    e9ui_flow_add(flow, btn_reset);

    e9ui_component_t *btn_restart = e9ui_button_make("", ui_restart, NULL);
    e9ui_setTooltip(btn_restart, "Restart");
    e9ui_button_setIconAsset(btn_restart, "assets/icons/reset.png");
    e9ui_button_setTheme(btn_restart, e9ui_theme_button_preset_red());
    e9ui->restartButton = btn_restart;
    e9ui_flow_add(flow, btn_restart);

    if (comp_logo && flow->preferredHeight) {
        int logo_h = comp_logo->preferredHeight(comp_logo, &e9ui->ctx, 10000);
        int flow_h = flow->preferredHeight(flow, &e9ui->ctx, 10000);
        int margin = 0;
        if (logo_h > flow_h) {
            margin = (logo_h - flow_h) / 2;
        }
        e9ui_flow_setBaseMargin(flow, margin);
    }

    e9ui_hstack_addFlex(top_row, toolbar_box);

    e9ui_component_t *comp_stack = e9ui_stack_makeVertical();
    // Add bottom border under the top logo/title row
    e9ui_component_t *top_row_box = e9ui_box_make(top_row);
    e9ui_box_setBorder(top_row_box, E9UI_BORDER_BOTTOM, (SDL_Color){70,70,70,255}, 1);
    e9ui_stack_addFixed(comp_stack, top_row_box);
    // Insert registers panel and make it resizable vs source/console (left pane)
    e9ui_component_t *comp_registers = registers_makeComponent();
    e9ui_component_t *comp_registers_box = e9ui_box_make(comp_registers);
    comp_registers_box->persist_id = "registers_box";
    e9ui_box_setTitlebar(comp_registers_box, "Registers", "assets/icons/registers.png");
    e9ui_component_t *comp_upper_split = e9ui_split_makeComponent(comp_registers_box, comp_split, e9ui_orient_vertical, 0.20f, 6);
    e9ui_split_setId(comp_upper_split, "upper");

    // Build right-hand column: stack (top), memory (middle), breakpoints (bottom), vertically resizable
    e9ui_component_t *comp_stack_panel = stack_makeComponent();
    e9ui_component_t *comp_stack_box = e9ui_box_make(comp_stack_panel);
    comp_stack_box->persist_id = "stack_box";
    e9ui_box_setTitlebar(comp_stack_box, "Stack", "assets/icons/backtrace.png");
    e9ui_component_t *comp_memory_panel = memory_makeComponent();
    e9ui_component_t *comp_memory_box = e9ui_box_make(comp_memory_panel);
    comp_memory_box->persist_id = "memory_box";
    e9ui_box_setTitlebar(comp_memory_box, "Memory", "assets/icons/ram.png");

    e9ui_component_t *comp_breakpoints_panel = breakpoints_makeComponent();
    e9ui_component_t *comp_breakpoints_box = e9ui_box_make(comp_breakpoints_panel);
    comp_breakpoints_box->persist_id = "breakpoints_box";
    e9ui_box_setTitlebar(comp_breakpoints_box, "Breakpoints", "assets/icons/break.png");

    e9ui_component_t *comp_trainer_panel = trainer_makeComponent();
    e9ui_component_t *comp_trainer_box = e9ui_box_make(comp_trainer_panel);
    comp_trainer_box->persist_id = "trainer_box";
    e9ui_box_setTitlebar(comp_trainer_box, "Trainer", "assets/icons/trainer.png");

    e9ui_component_t *comp_profile_checkpoints = profile_checkpoints_makeComponent();
    e9ui_component_t *comp_profile_checkpoints_box = e9ui_box_make(comp_profile_checkpoints);
    comp_profile_checkpoints_box->persist_id = "profile_checkpoints_box";
    e9ui_box_setTitlebar(comp_profile_checkpoints_box, "Profiler Checkpoints", "assets/icons/profile.png");

    e9ui_component_t *comp_profile_toolbar = e9ui_flow_make();
    e9ui_flow_setWrap(comp_profile_toolbar, 0);
    e9ui_flow_setSpacing(comp_profile_toolbar, 6);
    e9ui_flow_setPadding(comp_profile_toolbar, 6);

    e9ui_component_t *btn_profile = e9ui_button_make("Profile", profile_uiToggle, NULL);
    e9ui_button_setMini(btn_profile, 1);
    e9ui->profileButton = btn_profile;
    profile_buttonRefresh();
    e9ui_setHiddenVariable(btn_profile, &debugger.elfValid, 0);
    e9ui_flow_add(comp_profile_toolbar, btn_profile);

    e9ui_component_t *btn_analyse = e9ui_button_make("Analyse", profile_uiAnalyse, NULL);
    e9ui_button_setMini(btn_analyse, 1);
    e9ui->analyseButton = btn_analyse;
    analyse_buttonRefresh();
    e9ui_setHidden(btn_analyse, 1);
    e9ui_button_setGlowPulse(btn_analyse, 1);
    e9ui_flow_add(comp_profile_toolbar, btn_analyse);

    e9ui_component_t *comp_profile_list = profile_list_makeComponent();
    e9ui_component_t *comp_profile_stack = e9ui_stack_makeVertical();
    e9ui_stack_addFixed(comp_profile_stack, comp_profile_toolbar);
    e9ui_stack_addFlex(comp_profile_stack, comp_profile_list);

    e9ui_component_t *comp_profile_box = e9ui_box_make(comp_profile_stack);
    comp_profile_box->persist_id = "profile_box";
    e9ui_box_setTitlebar(comp_profile_box, "Profiler Hotspots", "assets/icons/hotspots.png");

    e9ui_component_t *comp_right_stack = e9ui_split_stack_make();
    e9ui_split_stack_setId(comp_right_stack, "right");
    e9ui_split_stack_addPanel(comp_right_stack, comp_stack_box, "stack_box", 0.50f);
    e9ui_split_stack_addPanel(comp_right_stack, comp_memory_box, "memory_box", 0.30f);
    e9ui_split_stack_addPanel(comp_right_stack, comp_breakpoints_box, "breakpoints_box", 0.10f);
    e9ui_split_stack_addPanel(comp_right_stack, comp_profile_checkpoints_box, "profile_checkpoints_box", 0.05f);
    e9ui_split_stack_addPanel(comp_right_stack, comp_profile_box, "profile_box", 0.05f);
    e9ui_split_stack_addPanel(comp_right_stack, comp_trainer_box, "trainer_box", 0.05f);

    // Left column: source/console with prompt below
    e9ui_component_t *comp_left_col = e9ui_stack_makeVertical();
    e9ui_stack_addFlex(comp_left_col, comp_upper_split);
    // Add top border above the prompt area
    e9ui_component_t *prompt_box = e9ui_box_make(comp_prompt);
    e9ui_box_setBorder(prompt_box, E9UI_BORDER_TOP, (SDL_Color){70,70,70,255}, 1);
    e9ui_stack_addFixed(comp_left_col, prompt_box);
    // Left-right split between left column and new right column
    e9ui_component_t *comp_lr = e9ui_split_makeComponent(comp_left_col, comp_right_stack, e9ui_orient_horizontal, 0.70f, 6);
    e9ui_split_setId(comp_lr, "left_right");

    e9ui_stack_addFlex(comp_stack, comp_lr);
    e9ui_component_t *comp_status_bar = status_bar_make();
    e9ui_stack_addFixed(comp_stack, comp_status_bar);
    e9ui->root = comp_stack;
    // After tree is built and IDs are assigned, load persisted component state
    if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE) {
        e9ui_component_t *geoBox = e9ui_findById(e9ui->root, "libretro_box");
        if (geoBox) {
            e9ui->fullscreen = geoBox;
        }
    } else {
      e9ui_loadLayoutComponents(debugger_configPath());
    }
    
    // Apply loaded ratios immediately to avoid a frame of default layout
    if (e9ui->root && e9ui->root->layout) {
        int w,h; SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);
        e9ui_rect_t full = (e9ui_rect_t){0,0,w,h};
        e9ui->root->layout(e9ui->root, &e9ui->ctx, full);
    }
}
