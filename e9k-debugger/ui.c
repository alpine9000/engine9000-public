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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ui.h"
#include "aux_window.h"
#include "breakpoints.h"
#include "clipboard.h"
#include "config.h"
#include "console_cmd.h"
#include "debug.h"
#include "debugger.h"
#include "e9ui.h"
#include "file.h"
#include "emu.h"
#include "hotkeys.h"
#include "libretro_host.h"
#include "machine.h"
#include "memory.h"
#include "profile.h"
#include "profile_list.h"
#include "profile_checkpoints.h"
#include "print_eval.h"
#include "prompt.h"
#include "registers.h"
#include "settings.h"
#include "snapshot.h"
#include "rom_config.h"
#include "source_pane.h"
#include "stack.h"
#include "status_bar.h"
#include "system_badge.h"
#include "state_buffer.h"
#include "trainer.h"
#include "console.h"
#include "smoke_test.h"
#include "transition.h"
#include "input_record.h"
#include "ui_test.h"
#include "gl_composite.h"

static e9ui_component_t *ui_source_panes[2];
static e9ui_component_t *ui_compRegisters = NULL;
static e9ui_component_t *ui_btnDebugProcessor = NULL;
static e9ui_component_t *ui_btnContinue = NULL;
static e9ui_component_t *ui_btnPause = NULL;
static e9ui_component_t *ui_btnStep = NULL;
static e9ui_component_t *ui_btnNext = NULL;
static e9ui_component_t *ui_btnStepInst = NULL;
static e9ui_component_t *ui_btnWarp = NULL;
static e9ui_component_t *ui_btnFrameBack = NULL;
static e9ui_component_t *ui_btnFrameStep = NULL;
static e9ui_component_t *ui_btnFrameContinue = NULL;
static e9ui_component_t *ui_btnSaveState = NULL;
static e9ui_component_t *ui_btnRestoreState = NULL;
static e9ui_component_t *ui_btnAudio = NULL;
static e9ui_component_t *ui_btnRecord = NULL;
static e9ui_component_t *ui_btnSettings = NULL;
static e9ui_component_t *ui_btnReset = NULL;
static e9ui_component_t *ui_btnRestart = NULL;
static char ui_debugProcessorMessage[64];
static char ui_tipDebugProcessor[128];
static char ui_tipContinue[128];
static char ui_tipPause[128];
static char ui_tipStep[128];
static char ui_tipNext[128];
static char ui_tipStepInst[128];
static char ui_tipWarp[128];
static char ui_tipFrameBack[128];
static char ui_tipFrameStep[128];
static char ui_tipFrameContinue[128];
static char ui_tipSaveState[128];
static char ui_tipRestoreState[128];
static char ui_tipAudio[128];
static char ui_tipRecord[128];
static char ui_tipSettings[128];
static char ui_tipReset[128];
static char ui_tipRestart[128];
static source_pane_mode_t ui_sourcePanePrimaryAddressModes[2];
static int ui_sourcePanePrimaryAddressModeValid[2];
#ifdef _WIN32
static SDL_Rect ui_windowsDefaultUsableBounds = {0, 0, 0, 0};
static int ui_windowsApplyDefaultUsableBounds = 0;
#endif

static uint32_t
ui_hostGetTicks(e9ui_context_t *ctx)
{
    (void)ctx;
    return debugger_uiTicks();
}

static void
ui_hostSetRefreshHz(e9ui_context_t *ctx, int refreshHz)
{
    (void)ctx;
    debugger.uiRefreshHz = refreshHz;
}

static const char *
ui_hostGetWindowIconAssetPath(e9ui_context_t *ctx)
{
    (void)ctx;
    return debugger_platform_windowIconAssetPath();
}

static uint64_t
ui_hostGetNextUiFrameId(e9ui_context_t *ctx)
{
    (void)ctx;
    return debugger.uiFrameCounter + 1;
}

static void
ui_hostCommitUiFrameId(e9ui_context_t *ctx, uint64_t frameId)
{
    (void)ctx;
    debugger.uiFrameCounter = frameId;
}

static int
ui_hostCaptureUiFrame(e9ui_context_t *ctx, uint64_t frameId, SDL_Renderer *renderer)
{
    (void)ctx;
    if (ui_test_hasFailed()) {
        return 0;
    }
    return ui_test_captureWindowFrame(frameId, renderer);
}

static int
ui_hostTransformTextboxSelection(e9ui_context_t *ctx,
                                 const char *actionId,
                                 const char *input,
                                 char *out,
                                 size_t outCap)
{
    (void)ctx;
    if (!actionId || !input || !out || outCap == 0) {
        return 0;
    }
    if (strcmp(actionId, "eval") != 0) {
        return 0;
    }
    return print_eval_eval(input, out, outCap);
}

static int
ui_hostShouldPresentFrame(e9ui_context_t *ctx)
{
    (void)ctx;
    return 1;
}

static int
ui_hostPollInjectedUiEvent(e9ui_context_t *ctx, uint64_t frameId, SDL_Event *eventValue)
{
    (void)ctx;
    (void)frameId;
    ui_test_mode_t mode = ui_test_getMode();
    if (mode != UI_TEST_MODE_COMPARE && mode != UI_TEST_MODE_REMAKE) {
        return 0;
    }
    int hasEvent = input_record_pollUiEvent(eventValue);
    SDL_Event dummy;
    SDL_PollEvent(&dummy);
    return hasEvent ? 1 : -1;
}

static void
ui_hostRecordUiEvent(e9ui_context_t *ctx, uint64_t frameId, const SDL_Event *eventValue)
{
    (void)ctx;
    if (input_record_isUiEventRecording() && !input_record_isInjecting()) {
        input_record_recordUiEvent(frameId, eventValue);
    }
}

void
ui_prepareMainWindow(e9ui_context_t *ctx,
                     int cliOverrideWindowSize,
                     int startHidden,
                     int *wantX,
                     int *wantY,
                     int *wantW,
                     int *wantH,
                     Uint32 *winFlags)
{
    (void)ctx;
    (void)startHidden;
#ifdef _WIN32
    (void)wantX;
    (void)wantY;
    (void)wantW;
    (void)wantH;
    ui_windowsDefaultUsableBounds = (SDL_Rect){0, 0, 0, 0};
    ui_windowsApplyDefaultUsableBounds = 0;
#endif
#ifdef __APPLE__
    if (e9ui->glCompositeEnabled && !debugger_platform_glCompositeNeedsOpenGLHint()) {
        e9ui->glCompositeEnabled = 0;
        debug_error("gl-composite: disabled (virtualized macOS renderer path)");
    }
#endif
    int useDefaultWindowGeometry =
        (!cliOverrideWindowSize &&
         e9ui->layout.winX == E9UI_LAYOUT_WIN_X &&
         e9ui->layout.winY == E9UI_LAYOUT_WIN_Y &&
         e9ui->layout.winW == E9UI_LAYOUT_WIN_W &&
         e9ui->layout.winH == E9UI_LAYOUT_WIN_H) ? 1 : 0;
    if (useDefaultWindowGeometry && ui_shouldUseVsync(NULL)) {
        SDL_Rect usable = {0, 0, 0, 0};
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0 &&
            usable.w > 0 && usable.h > 0) {
#ifdef _WIN32
            ui_windowsDefaultUsableBounds = usable;
            ui_windowsApplyDefaultUsableBounds = 1;
#else
            *wantW = usable.w;
            *wantH = usable.h;
            *wantX = usable.x;
            *wantY = usable.y;
#endif
        }
    }
    if (e9ui->glCompositeEnabled && debugger_platform_glCompositeNeedsOpenGLHint()) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    }
    if (e9ui->glCompositeEnabled) {
        *winFlags |= SDL_WINDOW_OPENGL;
    }
}

int
ui_shouldUseVsync(e9ui_context_t *ctx)
{
    (void)ctx;
    ui_test_mode_t mode = ui_test_getMode();
    if (mode == UI_TEST_MODE_COMPARE || mode == UI_TEST_MODE_REMAKE) {
        return 0;
    }
    return 1;
}

void
ui_finalizeMainWindow(e9ui_context_t *ctx,
                      SDL_Window *window,
                      SDL_Renderer *renderer,
                      int wantW,
                      int wantH)
{
    (void)ctx;
#ifdef _WIN32
#if SDL_VERSION_ATLEAST(2,0,5)
    if (ui_windowsApplyDefaultUsableBounds) {
        int borderTop = 0;
        int borderLeft = 0;
        int borderBottom = 0;
        int borderRight = 0;
        if (SDL_GetWindowBordersSize(window, &borderTop, &borderLeft, &borderBottom, &borderRight) == 0) {
            int fitClientW = ui_windowsDefaultUsableBounds.w - borderLeft - borderRight;
            int fitClientH = ui_windowsDefaultUsableBounds.h - borderTop - borderBottom;
            if (fitClientW > 0 && fitClientH > 0) {
                SDL_SetWindowSize(window, fitClientW, fitClientH);
                SDL_SetWindowPosition(window,
                                      ui_windowsDefaultUsableBounds.x + borderLeft,
                                      ui_windowsDefaultUsableBounds.y + borderTop);
            }
        } else {
            debug_error("SDL_GetWindowBordersSize failed: %s", SDL_GetError());
        }
    }
#endif
#endif
    if (!ui_shouldUseVsync(NULL)) {
        SDL_SetWindowSize(window, wantW, wantH);
    }
    if (e9ui->glCompositeEnabled) {
        if (!gl_composite_init(window, renderer)) {
            debug_error("gl-composite: disabled (init failed)");
        }
    }
}

static void
ui_recordRefreshTooltip(void);

static void
ui_recordRefreshButton(void);

static void
ui_refreshFrameBackButton(void);

static void
ui_validateToolbarButtonRef(e9ui_component_t **slot)
{
    if (!slot || !*slot) {
        return;
    }
    if (!e9ui || !e9ui->toolbar) {
        *slot = NULL;
        return;
    }
    for (list_t *ptr = e9ui->toolbar->children; ptr; ptr = ptr->next) {
        e9ui_component_child_t *container = (e9ui_component_child_t *)ptr->data;
        if (container && container->component == *slot) {
            return;
        }
    }
    *slot = NULL;
}

static void
ui_setActionTooltip(e9ui_component_t *btn, const char *baseLabel, const char *actionId, char *buf, size_t bufCap)
{
    if (!btn || !buf || bufCap == 0 || !baseLabel) {
        return;
    }
    char binding[96];
    binding[0] = '\0';
    if (hotkeys_formatActionBindingDisplay(actionId, binding, sizeof(binding)) && binding[0]) {
        snprintf(buf, bufCap, "%s - %s", baseLabel, binding);
    } else {
        snprintf(buf, bufCap, "%s", baseLabel);
    }
    buf[bufCap - 1] = '\0';
    e9ui_setTooltip(btn, buf);
}

static void
ui_debugProcessorLabel(const e9k_debug_processor_info_t *processor, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!processor) {
        return;
    }
    if (strcmp(processor->name, "M68000") == 0) {
        snprintf(out, cap, "68K");
    } else if (processor->name[0]) {
        snprintf(out, cap, "%s", processor->name);
    } else {
        snprintf(out, cap, "CPU %u", (unsigned)processor->id);
    }
    out[cap - 1] = '\0';
}

static size_t
ui_readDebugProcessors(e9k_debug_processor_info_t *processors, size_t cap)
{
    size_t count = 0;

    if (!processors || cap == 0) {
        return 0;
    }
    memset(processors, 0, cap * sizeof(*processors));
    if (!libretro_host_debugReadProcessors(processors, cap, &count)) {
        return 0;
    }
    if (count > cap) {
        count = cap;
    }
    return count;
}

static int
ui_findDebugProcessorIndex(const e9k_debug_processor_info_t *processors, size_t count, uint32_t processorId)
{
    if (!processors || count == 0) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (processors[i].id == processorId) {
            return (int)i;
        }
    }
    return -1;
}

static int
ui_activeDebugProcessorIsPrimary(void)
{
    e9k_debug_processor_info_t processors[8];
    size_t count = ui_readDebugProcessors(processors, sizeof(processors) / sizeof(processors[0]));

    if (count == 0) {
        return debugger.activeDebugProcessorId == MACHINE_PROCESSOR_PRIMARY ? 1 : 0;
    }
    int selected = ui_findDebugProcessorIndex(processors, count, debugger.activeDebugProcessorId);
    if (selected < 0) {
        return 0;
    }
    return (processors[selected].flags & E9K_DEBUG_PROCESSOR_PRIMARY) ? 1 : 0;
}

static void
ui_syncSourcePanesForDebugProcessor(int activeIsPrimary)
{
    for (size_t i = 0; i < sizeof(ui_source_panes) / sizeof(ui_source_panes[0]); ++i) {
        e9ui_component_t *pane = ui_source_panes[i];
        if (!pane) {
            continue;
        }
        source_pane_mode_t mode = source_pane_getMode(pane);
        if (activeIsPrimary) {
            if (mode == source_pane_mode_z80s) {
                source_pane_setMode(pane, source_pane_mode_c);
            } else if (mode == source_pane_mode_z80) {
                source_pane_mode_t dest = source_pane_mode_a;
                if (ui_sourcePanePrimaryAddressModeValid[i]) {
                    dest = ui_sourcePanePrimaryAddressModes[i];
                }
                source_pane_setMode(pane, dest);
            }
            continue;
        }
        if (mode == source_pane_mode_c) {
            source_pane_setMode(pane, source_pane_mode_z80s);
        } else if (mode == source_pane_mode_a || mode == source_pane_mode_h) {
            ui_sourcePanePrimaryAddressModes[i] = mode;
            ui_sourcePanePrimaryAddressModeValid[i] = 1;
            source_pane_setMode(pane, source_pane_mode_z80);
        }
    }
}

static void
ui_refreshDebugProcessorButton(void)
{
    e9k_debug_processor_info_t processors[8];
    size_t count = ui_readDebugProcessors(processors, sizeof(processors) / sizeof(processors[0]));

    if (count <= 1) {
        if (ui_btnDebugProcessor) {
            e9ui_setHidden(ui_btnDebugProcessor, 1);
        }
        if (count == 1) {
            debugger.activeDebugProcessorId = processors[0].id;
        } else {
            debugger.activeDebugProcessorId = 0;
        }
        return;
    }

    int selected = ui_findDebugProcessorIndex(processors, count, debugger.activeDebugProcessorId);
    if (selected < 0) {
        selected = 0;
        for (size_t i = 0; i < count; ++i) {
            if (processors[i].flags & E9K_DEBUG_PROCESSOR_PRIMARY) {
                selected = (int)i;
                break;
            }
        }
        debugger.activeDebugProcessorId = processors[selected].id;
    }

    char label[32];
    ui_debugProcessorLabel(&processors[selected], label, sizeof(label));
    snprintf(ui_tipDebugProcessor,
             sizeof(ui_tipDebugProcessor),
             "Debugger CPU: %s",
             label);
    ui_tipDebugProcessor[sizeof(ui_tipDebugProcessor) - 1] = '\0';
    if (ui_btnDebugProcessor) {
        e9ui_button_setLabel(ui_btnDebugProcessor, label);
        e9ui_setHidden(ui_btnDebugProcessor, 0);
        e9ui_setTooltip(ui_btnDebugProcessor, ui_tipDebugProcessor);
    }
}

static void
ui_debugProcessorToggle(e9ui_context_t *ctx, void *user)
{
    e9k_debug_processor_info_t processors[8];
    size_t count = ui_readDebugProcessors(processors, sizeof(processors) / sizeof(processors[0]));

    (void)ctx;
    (void)user;
    if (count <= 1) {
        ui_refreshDebugProcessorButton();
        return;
    }

    int selected = ui_findDebugProcessorIndex(processors, count, debugger.activeDebugProcessorId);
    if (selected < 0) {
        selected = 0;
    } else {
        selected = (selected + 1) % (int)count;
    }
    debugger.activeDebugProcessorId = processors[selected].id;
    ui_syncSourcePanesForDebugProcessor((processors[selected].flags & E9K_DEBUG_PROCESSOR_PRIMARY) ? 1 : 0);
    ui_refreshDebugProcessorButton();
    char label[32];
    ui_debugProcessorLabel(&processors[selected], label, sizeof(label));
    debug_printf("debugger cpu: %s", label);
    snprintf(ui_debugProcessorMessage, sizeof(ui_debugProcessorMessage), "DEBUGGER CPU: %s", label);
    ui_debugProcessorMessage[sizeof(ui_debugProcessorMessage) - 1] = '\0';
    e9ui_showTransientMessage(ui_debugProcessorMessage);
}

static int
ui_stepActiveDebugProcessorInstruction(void)
{
    if (ui_activeDebugProcessorIsPrimary()) {
        return libretro_host_debugStepInstr() ? 1 : 0;
    }
    (void)libretro_host_debugSuppressProcessorBreakpointAtPc(debugger.activeDebugProcessorId);
    return libretro_host_debugStepProcessorInstr(debugger.activeDebugProcessorId) ? 1 : 0;
}

static void
ui_refreshAfterActiveDebugProcessorStep(void)
{
    machine_setRunning(&debugger.machine, 0);
    registers_refreshExtraRegsNow(ui_compRegisters);
    ui_refreshOnPause();
}

void
ui_refreshHotkeyTooltips(void)
{
    ui_validateToolbarButtonRef(&ui_btnDebugProcessor);
    ui_validateToolbarButtonRef(&ui_btnContinue);
    ui_validateToolbarButtonRef(&ui_btnPause);
    ui_validateToolbarButtonRef(&ui_btnStep);
    ui_validateToolbarButtonRef(&ui_btnNext);
    ui_validateToolbarButtonRef(&ui_btnStepInst);
    ui_validateToolbarButtonRef(&ui_btnWarp);
    ui_validateToolbarButtonRef(&ui_btnFrameBack);
    ui_validateToolbarButtonRef(&ui_btnFrameStep);
    ui_validateToolbarButtonRef(&ui_btnFrameContinue);
    ui_validateToolbarButtonRef(&ui_btnSaveState);
    ui_validateToolbarButtonRef(&ui_btnRestoreState);
    ui_validateToolbarButtonRef(&ui_btnAudio);
    ui_validateToolbarButtonRef(&ui_btnRecord);
    ui_validateToolbarButtonRef(&ui_btnSettings);
    ui_validateToolbarButtonRef(&ui_btnReset);
    ui_validateToolbarButtonRef(&ui_btnRestart);

    ui_refreshDebugProcessorButton();
    ui_setActionTooltip(ui_btnContinue, "Continue", "continue", ui_tipContinue, sizeof(ui_tipContinue));
    ui_setActionTooltip(ui_btnPause, "Pause", "pause", ui_tipPause, sizeof(ui_tipPause));
    ui_setActionTooltip(ui_btnStep, "Step", "step", ui_tipStep, sizeof(ui_tipStep));
    ui_setActionTooltip(ui_btnNext, "Next", "next", ui_tipNext, sizeof(ui_tipNext));
    ui_setActionTooltip(ui_btnStepInst, "Step Inst", "step_inst", ui_tipStepInst, sizeof(ui_tipStepInst));
    ui_setActionTooltip(ui_btnWarp, "Warp", "warp", ui_tipWarp, sizeof(ui_tipWarp));
    ui_setActionTooltip(ui_btnFrameBack, "Frame step back", "frame_back", ui_tipFrameBack, sizeof(ui_tipFrameBack));
    ui_refreshFrameBackButton();
    ui_setActionTooltip(ui_btnFrameStep, "Frame step", "frame_step", ui_tipFrameStep, sizeof(ui_tipFrameStep));
    ui_setActionTooltip(ui_btnFrameContinue, "Frame continue", "frame_continue", ui_tipFrameContinue, sizeof(ui_tipFrameContinue));
    ui_setActionTooltip(ui_btnSaveState, "Save state", "save_state", ui_tipSaveState, sizeof(ui_tipSaveState));
    ui_setActionTooltip(ui_btnRestoreState, "Restore state", "restore_state", ui_tipRestoreState, sizeof(ui_tipRestoreState));
    ui_setActionTooltip(ui_btnAudio, "Audio", "audio_toggle", ui_tipAudio, sizeof(ui_tipAudio));
    ui_recordRefreshTooltip();
    ui_setActionTooltip(ui_btnSettings, "Settings", "settings", ui_tipSettings, sizeof(ui_tipSettings));
    ui_setActionTooltip(ui_btnReset, "Reset core", "reset_core", ui_tipReset, sizeof(ui_tipReset));
    ui_setActionTooltip(ui_btnRestart, "Restart", "restart", ui_tipRestart, sizeof(ui_tipRestart));
    breakpoints_refreshHotkeyTooltips();
    profile_checkpoints_refreshHotkeyTooltips();
}

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

static const char *
ui_windowTitlePath(void)
{
    const e9k_libretro_config_t *configLibretro =
        debugger.config.target && debugger.config.target->selectLibretroConfig ?
        debugger.config.target->selectLibretroConfig(&debugger.config) : NULL;
    const char *configuredRomPath = configLibretro ? configLibretro->romPath : "";
    if (debugger.config.target == target_neogeo() &&
        debugger.config.neogeo.romFolder[0] &&
        debugger.libretro.romPath[0]) {
        return debugger.config.neogeo.romFolder;
    }
    const char *romExt = strrchr(configuredRomPath, '.');
    if (romExt && strcasecmp(romExt + 1, "zip") == 0 &&
        debugger.libretro.romPath[0]) {
        return configuredRomPath;
    }
    return debugger.libretro.romPath;
}

int
ui_runFullscreenTransition(e9ui_context_t *ctx,
                           int entering,
                           e9ui_component_t *from,
                           e9ui_component_t *to,
                           int width,
                           int height)
{
    (void)ctx;
    e9k_transition_mode_t mode = transition_pickFullscreenMode(entering ? 1 : 0);
    if (mode == e9k_transition_none) {
        return 0;
    }
    e9ui->transition.inTransition = 1;
    if (entering) {
        if (mode == e9k_transition_slide) {
            transition_slide_runTo(from, to, width, height);
        } else if (mode == e9k_transition_explode) {
            transition_explode_runTo(from, to, width, height);
        } else if (mode == e9k_transition_doom) {
            transition_doom_runTo(from, to, width, height);
        } else if (mode == e9k_transition_flip) {
            transition_flip_runTo(from, to, width, height);
        } else if (mode == e9k_transition_rbar) {
            transition_rbar_runTo(from, to, width, height);
        }
    } else {
        if (mode == e9k_transition_slide) {
            transition_slide_run(from, to, width, height);
        } else if (mode == e9k_transition_explode) {
            transition_explode_run(from, to, width, height);
        } else if (mode == e9k_transition_doom) {
            transition_doom_runTo(from, to, width, height);
        } else if (mode == e9k_transition_flip) {
            transition_flip_run(from, to, width, height);
        } else if (mode == e9k_transition_rbar) {
            transition_rbar_run(from, to, width, height);
        }
    }
    return 1;
}

void
ui_setMainWindowFocused(e9ui_context_t *ctx, int focused)
{
    if (!focused) {
        (void)emu_mouseCaptureRelease(ctx);
    }
    aux_window_setFocus(focused);
}

static void
ui_blockingWindowOpened(e9ui_context_t *ctx)
{
    (void)emu_mouseCaptureRelease(ctx);
}

static int
ui_handleHostEvent(e9ui_context_t *ctx, const SDL_Event *ev)
{
    if (ev->type == SDL_KEYDOWN && hotkeys_handleMouseCaptureKeydown(ctx, &ev->key)) {
        return 1;
    }
    return emu_handleCapturedMouseEvent(ctx, ev);
}

int
ui_handleGlobalKeydown(e9ui_context_t *ctx, const SDL_KeyboardEvent *kev)
{
    return hotkeys_handleKeydown(ctx, kev);
}

void
ui_shutdownHostUi(e9ui_context_t *ctx)
{
    (void)ctx;
    gl_composite_shutdown();
    hotkeys_shutdown();
}

void
ui_updateWindowTitle(void)
{
    if (!e9ui || !e9ui->ctx.window) {
        return;
    }

    const char *romPath = ui_windowTitlePath();
    const char *base = ui_basename(romPath);
    char title[PATH_MAX + 64];

    if (base && *base) {
        snprintf(title, sizeof(title), "ENGINE9000 DEBUGGER/PROFILER 68K - %s", base);
    } else {
        snprintf(title, sizeof(title), "ENGINE9000 DEBUGGER/PROFILER 68K");
    }
    title[sizeof(title) - 1] = '\0';
    SDL_SetWindowTitle(e9ui->ctx.window, title);
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
    registers_refreshExtraRegsNow(ui_compRegisters);
    for (size_t i = 0; i < sizeof(ui_source_panes)/sizeof(ui_source_panes[0]); ++i) {
        if (ui_source_panes[i]) {
            source_pane_markNeedsRefresh(ui_source_panes[i]);
        }
    }
}

void
ui_centerSourceOnAddress(uint32_t addr)
{
    e9ui_component_t *pane = NULL;

    for (size_t i = 0; i < sizeof(ui_source_panes) / sizeof(ui_source_panes[0]); ++i) {
        if (ui_source_panes[i] && source_pane_getMode(ui_source_panes[i]) != source_pane_mode_cpr) {
            pane = ui_source_panes[i];
            break;
        }
    }

    if (!pane) {
        pane = ui_source_panes[0] ? ui_source_panes[0] : ui_source_panes[1];
    }
    if (!pane) {
        return;
    }

    if (source_pane_getMode(pane) != source_pane_mode_a &&
        source_pane_getMode(pane) != source_pane_mode_h) {
        source_pane_setMode(pane, source_pane_mode_a);
    }
    source_pane_submitAddress(pane, &e9ui->ctx, addr);
    e9ui_setFocus(&e9ui->ctx, pane);
}

void
ui_openSourceLocation(const char *file, int line, uint32_t fallbackAddr)
{
    if (!file || !*file || line <= 0) {
        ui_centerSourceOnAddress(fallbackAddr);
        return;
    }

    e9ui_component_t *pane = NULL;
    for (size_t i = 0; i < sizeof(ui_source_panes) / sizeof(ui_source_panes[0]); ++i) {
        if (ui_source_panes[i] && source_pane_getMode(ui_source_panes[i]) == source_pane_mode_c) {
            pane = ui_source_panes[i];
            break;
        }
    }
    if (!pane) {
        for (size_t i = 0; i < sizeof(ui_source_panes) / sizeof(ui_source_panes[0]); ++i) {
            if (ui_source_panes[i] && source_pane_getMode(ui_source_panes[i]) != source_pane_mode_cpr) {
                pane = ui_source_panes[i];
                break;
            }
        }
    }
    if (!pane) {
        pane = ui_source_panes[0] ? ui_source_panes[0] : ui_source_panes[1];
    }
    if (!pane) {
        return;
    }

    source_pane_openSourceLocation(pane, &e9ui->ctx, file, line);
    e9ui_setFocus(&e9ui->ctx, pane);
}

void
ui_centerCprSourceOnAddress(uint32_t addr)
{
    e9ui_component_t *pane = NULL;

    for (size_t i = 0; i < sizeof(ui_source_panes) / sizeof(ui_source_panes[0]); ++i) {
        if (ui_source_panes[i] && source_pane_getMode(ui_source_panes[i]) == source_pane_mode_cpr) {
            pane = ui_source_panes[i];
            break;
        }
    }

    if (!pane) {
        pane = ui_source_panes[1] ? ui_source_panes[1] : ui_source_panes[0];
    }
    if (!pane) {
        return;
    }

    if (source_pane_getMode(pane) != source_pane_mode_cpr) {
        source_pane_setMode(pane, source_pane_mode_cpr);
    }
    source_pane_submitAddress(pane, &e9ui->ctx, addr);
    e9ui_setFocus(&e9ui->ctx, pane);
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
        if (debugger.symbolFileKind == DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP &&
            source_pane_getMode(pane) == source_pane_mode_c) {
            source_pane_setMode(pane, source_pane_mode_c);
        }
        if (!debugger.elfValid &&
            debugger.symbolFileKind != DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP &&
            source_pane_getMode(pane) == source_pane_mode_c) {
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
    if (state_buffer_isRollingPaused()) {
        return;
    }
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
    if (!ui_activeDebugProcessorIsPrimary()) {
        if (ui_stepActiveDebugProcessorInstruction()) {
            ui_refreshAfterActiveDebugProcessorStep();
            return;
        }
        debug_error("step: active processor does not expose debug instruction step");
        return;
    }
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
    if (!ui_activeDebugProcessorIsPrimary()) {
        debug_error("step next: active processor does not expose debug step next");
        return;
    }
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
    if (!ui_activeDebugProcessorIsPrimary()) {
        if (ui_stepActiveDebugProcessorInstruction()) {
            ui_refreshAfterActiveDebugProcessorStep();
            return;
        }
        debug_error("step instruction: active processor does not expose debug step");
        return;
    }
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
    if (!ui_activeDebugProcessorIsPrimary()) {
        debug_error("step out: active processor does not expose debug step out");
        return;
    }
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepOut()) {
        machine_setRunning(&debugger.machine, 1);
        return;
    }
    debug_error("step out: libretro core does not expose debug step out");
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

void
ui_refreshRecordButton(void)
{
    ui_recordRefreshButton();
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
    const char *icon = debugger_getAudioEnabled() ? "assets/icons/audio.png"
                                                    : "assets/icons/mute.png";
    e9ui_button_setIconAsset(e9ui->audioButton, icon);
}

static void
ui_recordRefreshTooltip(void)
{
    ui_validateToolbarButtonRef(&ui_btnRecord);
    const char *label = state_buffer_isRollingPaused() ? "Recording paused" : "Recording";
    ui_setActionTooltip(ui_btnRecord, label, "rolling_save_toggle", ui_tipRecord, sizeof(ui_tipRecord));
}

static void
ui_recordRefreshButton(void)
{
    ui_validateToolbarButtonRef(&ui_btnRecord);
    if (!ui_btnRecord) {
        return;
    }
    e9ui_button_setIconAsset(ui_btnRecord, "assets/icons/record.png");
    if (state_buffer_isRollingPaused()) {
        e9ui_button_clearTheme(ui_btnRecord);
    } else {
        e9ui_button_setTheme(ui_btnRecord, e9ui_theme_button_preset_red());
    }
    ui_recordRefreshTooltip();
    ui_refreshFrameBackButton();
}

static void
ui_refreshFrameBackButton(void)
{
    ui_validateToolbarButtonRef(&ui_btnFrameBack);
    if (!ui_btnFrameBack) {
        return;
    }
    e9ui_setDisabled(ui_btnFrameBack, state_buffer_isRollingPaused());
}

void
ui_toggleRollingSavePauseResume(void)
{
    int paused = state_buffer_isRollingPaused() ? 0 : 1;
    state_buffer_setRollingPaused(paused);
    ui_recordRefreshButton();
    e9ui_showTransientMessage(paused ? "ROLLING SAVE PAUSED" : "ROLLING SAVE RESUMED");
}

static void
ui_audioToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    debugger_setAudioEnabled(!debugger_getAudioEnabled());
    libretro_host_setAudioEnabled(debugger_getAudioEnabled());
    ui_audioRefreshButton();
    config_saveConfig();
}

static void
ui_recordToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;
    ui_toggleRollingSavePauseResume();
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
        // Persist immediately, not just on exit (matches user expectation for the Save button).
        rom_config_saveOnExit();
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

static void
ui_copyFramebufferWithDisplayAspectToClipboard(const uint8_t *data,
                                               int width,
                                               int height,
                                               size_t pitch,
                                               float displayAspect)
{
    if (!data || width <= 0 || height <= 0 || pitch == 0) {
        debug_error("clipboard: invalid framebuffer");
        return;
    }

    int targetWidth = width;
    int targetHeight = height;
    double rawAspect = (double)width / (double)height;
    if (displayAspect > 0.0001f) {
        double targetAspect = (double)displayAspect;
        if (fabs(rawAspect - targetAspect) > 0.01) {
            if (targetAspect >= rawAspect) {
                targetWidth = (int)(targetAspect * (double)height + 0.5);
            } else {
                targetHeight = (int)(((double)width / targetAspect) + 0.5);
            }
        }
    }

    if (targetWidth <= 0) {
        targetWidth = width;
    }
    if (targetHeight <= 0) {
        targetHeight = height;
    }

    if (targetWidth == width && targetHeight == height) {
        if (!clipboard_setImageXRGB8888(data, width, height, pitch)) {
            debug_error("clipboard: failed to set image");
        }
        return;
    }

    SDL_Surface *srcSurface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void *)data, width, height, 32, (int)pitch, SDL_PIXELFORMAT_XRGB8888);
    if (!srcSurface) {
        debug_error("clipboard: SDL_CreateRGBSurfaceWithFormatFrom failed: %s", SDL_GetError());
        return;
    }

    SDL_Surface *dstSurface = SDL_CreateRGBSurfaceWithFormat(0,
                                                             targetWidth,
                                                             targetHeight,
                                                             32,
                                                             SDL_PIXELFORMAT_XRGB8888);
    if (!dstSurface) {
        debug_error("clipboard: SDL_CreateRGBSurfaceWithFormat failed: %s", SDL_GetError());
        SDL_FreeSurface(srcSurface);
        return;
    }

    if (SDL_BlitScaled(srcSurface, NULL, dstSurface, NULL) != 0) {
        debug_error("clipboard: SDL_BlitScaled failed: %s", SDL_GetError());
        SDL_FreeSurface(dstSurface);
        SDL_FreeSurface(srcSurface);
        return;
    }

    if (!clipboard_setImageXRGB8888((const uint8_t *)dstSurface->pixels,
                                    dstSurface->w,
                                    dstSurface->h,
                                    (size_t)dstSurface->pitch)) {
        debug_error("clipboard: failed to set scaled image");
    }

    SDL_FreeSurface(dstSurface);
    SDL_FreeSurface(srcSurface);
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
    float displayAspect = libretro_host_getDisplayAspect();
    ui_copyFramebufferWithDisplayAspectToClipboard(copy, width, height, pitch, displayAspect);
    free(copy);
    e9ui_showTransientMessage("COPIED SCREEN TO CLIPBOARD");
}

void
ui_configureE9uiHost(void)
{
    e9ui->ctx.getTicks = ui_hostGetTicks;
    e9ui->ctx.setRefreshHz = ui_hostSetRefreshHz;
    e9ui->ctx.getWindowIconAssetPath = ui_hostGetWindowIconAssetPath;
    e9ui->ctx.getNextUiFrameId = ui_hostGetNextUiFrameId;
    e9ui->ctx.commitUiFrameId = ui_hostCommitUiFrameId;
    e9ui->ctx.captureUiFrame = ui_hostCaptureUiFrame;
    e9ui->ctx.transformTextboxSelection = ui_hostTransformTextboxSelection;
    e9ui->ctx.shouldPresentFrame = ui_hostShouldPresentFrame;
    e9ui->ctx.pollInjectedUiEvent = ui_hostPollInjectedUiEvent;
    e9ui->ctx.recordUiEvent = ui_hostRecordUiEvent;
    e9ui->ctx.runFullscreenTransition = ui_runFullscreenTransition;
    e9ui->ctx.setMainWindowFocused = ui_setMainWindowFocused;
    e9ui->ctx.blockingWindowOpened = ui_blockingWindowOpened;
    e9ui->ctx.handleHostEvent = ui_handleHostEvent;
    e9ui->ctx.handleGlobalKeydown = ui_handleGlobalKeydown;
    e9ui->ctx.shutdownHostUi = ui_shutdownHostUi;
    e9ui->ctx.prepareMainWindow = ui_prepareMainWindow;
    e9ui->ctx.shouldUseVsync = ui_shouldUseVsync;
    e9ui->ctx.finalizeMainWindow = ui_finalizeMainWindow;
    e9ui->ctx.registerHotkey = hotkeys_registerHotkey;
    e9ui->ctx.unregisterHotkey = hotkeys_unregisterHotkey;
    e9ui->ctx.dispatchHotkey = hotkeys_dispatchHotkey;
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
    ui_configureE9uiHost();

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
    e9ui_box_setTitlebar(comp_libretro_box, target->name, "assets/icons/game.png");

    e9ui_component_t *comp_gdb_geo = e9ui_split_makeComponent(comp_console_box, comp_libretro_box, e9ui_orient_horizontal, 0.60f, 6);
    e9ui_split_setId(comp_gdb_geo, "gdb_geo");
    e9ui_component_t *comp_split = e9ui_split_makeComponent(comp_sources_box, comp_gdb_geo, e9ui_orient_vertical, 0.66f, 6);

    e9ui_split_setId(comp_split, "src_console");
    e9ui_component_t *comp_prompt = prompt_makeComponent();
    e9ui_setDisableVariable(comp_prompt, machine_getRunningState(debugger.machine), 1);
    e9ui->prompt = comp_prompt;
    if (e9ui->ctx.registerHotkey) {
        hotkeys_registerActionHotkey(&e9ui->ctx, "prompt_focus", ui_promptFocusHotkey, comp_prompt);
    }

    // Build top row: [ left image 240x48 ] [ toolbar buttons ] [ right image 139x48 ]
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
    if (debugger.config.logosEnabled && logoTex) {
        comp_logo = e9ui_image_makeFromTexture(logoTex, logoW, logoH);
        e9ui_component_t *logo_box = e9ui_box_make(comp_logo);
        e9ui_box_setWidth(logo_box, e9ui_dim_fixed, 240);
        e9ui_box_setHeight(logo_box, e9ui_dim_fixed, 48);
        comp_logo = logo_box;
    }

    int systemW = 0;
    int systemH = 0;
    SDL_Texture *systemTex = system_badge_getTexture(e9ui->ctx.renderer, target, &systemW, &systemH);
    e9ui_component_t *comp_system = NULL;
    if (debugger.config.logosEnabled && systemTex) {
        comp_system = e9ui_image_makeFromTexture(systemTex, systemW, systemH);
        e9ui_component_t *system_box = e9ui_box_make(comp_system);
        e9ui_box_setWidth(system_box, e9ui_dim_fixed, 139);
        e9ui_box_setHeight(system_box, e9ui_dim_fixed, 48);
        comp_system = system_box;
    }

    e9ui_component_t *toolbar = e9ui_header_flow_make(comp_logo, comp_logo ? 240 : 0,
                                                      comp_system, comp_system ? 139 : 0, 48);
    e9ui->toolbar = toolbar;
    e9ui_header_flow_setPadding(toolbar, 10);
    e9ui_header_flow_setSpacing(toolbar, 8);
    e9ui_header_flow_setWrap(toolbar, 1);
    // Build buttons and bind hotkeys at creation
    e9ui_component_t *btn_debug_processor = e9ui_button_make("68K", ui_debugProcessorToggle, NULL);
    e9ui_button_setLargestLabel(btn_debug_processor, "Z80");
    ui_btnDebugProcessor = btn_debug_processor;
    ui_refreshDebugProcessorButton();
    e9ui_header_flow_add(toolbar, btn_debug_processor);
    hotkeys_registerActionHotkey(&e9ui->ctx, "debug_processor_toggle", ui_debugProcessorToggle, NULL);

    e9ui_component_t *btn_continue = e9ui_button_make("", ui_continue, NULL);
    e9ui_button_setIconAsset(btn_continue, "assets/icons/continue.png");
    hotkeys_registerButtonActionHotkey(btn_continue, &e9ui->ctx, "continue");
    ui_btnContinue = btn_continue;
    e9ui_setHiddenVariable(btn_continue, machine_getRunningState(debugger.machine), 1);
    e9ui_header_flow_add(toolbar, btn_continue);

    e9ui_component_t *btn_pause = e9ui_button_make("", ui_pause, NULL);
    e9ui_button_setIconAsset(btn_pause, "assets/icons/pause.png");
    hotkeys_registerButtonActionHotkey(btn_pause, &e9ui->ctx, "pause");
    ui_btnPause = btn_pause;
    e9ui_setHiddenVariable(btn_pause, machine_getRunningState(debugger.machine), 0);
    e9ui_header_flow_add(toolbar, btn_pause);

    e9ui_component_t *btn_step = e9ui_button_make("Step", ui_step, NULL);
    e9ui_button_setIconAsset(btn_step, "assets/icons/step.png");
    hotkeys_registerButtonActionHotkey(btn_step, &e9ui->ctx, "step");
    ui_btnStep = btn_step;
    e9ui_setDisableVariable(btn_step, machine_getRunningState(debugger.machine), 1);
    e9ui_setHiddenVariable(btn_step, &debugger.elfValid, 0);
    e9ui_header_flow_add(toolbar, btn_step);

    e9ui_component_t *btn_next = e9ui_button_make("Next", ui_next, NULL);
    e9ui_button_setIconAsset(btn_next, "assets/icons/next.png");
    hotkeys_registerButtonActionHotkey(btn_next, &e9ui->ctx, "next");
    ui_btnNext = btn_next;
    e9ui_setDisableVariable(btn_next, machine_getRunningState(debugger.machine), 1);
    e9ui_setHiddenVariable(btn_next, &debugger.elfValid, 0);
    e9ui_header_flow_add(toolbar, btn_next);

    // Instruction step (si) with global hotkey 'i'
    e9ui_component_t *btn_stepi = e9ui_button_make("Inst", ui_stepi, NULL);
    e9ui_button_setIconAsset(btn_stepi, "assets/icons/step.png");
    hotkeys_registerButtonActionHotkey(btn_stepi, &e9ui->ctx, "step_inst");
    ui_btnStepInst = btn_stepi;
    e9ui_setDisableVariable(btn_stepi, machine_getRunningState(debugger.machine), 1);
    e9ui_header_flow_add(toolbar, btn_stepi);

    e9ui_component_t *btn_finish = e9ui_button_make("Out", ui_finish, NULL);
    e9ui_button_setIconAsset(btn_finish, "assets/icons/step_out.png");
    e9ui_setTooltip(btn_finish, "Step Out");
    e9ui_setDisableVariable(btn_finish, machine_getRunningState(debugger.machine), 1);
    e9ui_setHiddenVariable(btn_finish, &debugger.elfValid, 0);
    e9ui_header_flow_add(toolbar, btn_finish);

    e9ui_component_t *sep_step_speed = e9ui_separator_make(9);
    e9ui_header_flow_add(toolbar, sep_step_speed);

    e9ui_component_t *btn_speed = e9ui_button_make("", ui_speedToggle, NULL);
    e9ui_button_setIconAsset(btn_speed, "assets/icons/speed_normal.png");
    hotkeys_registerButtonActionHotkey(btn_speed, &e9ui->ctx, "warp");
    e9ui->speedButton = btn_speed;
    ui_btnWarp = btn_speed;
    ui_speedButtonRefresh();

    e9ui_component_t *btn_frame_stepBack = e9ui_button_make("Back", ui_frameStepBack, NULL);
    e9ui_button_setIconAsset(btn_frame_stepBack, "assets/icons/back.png");
    hotkeys_registerButtonActionHotkey(btn_frame_stepBack, &e9ui->ctx, "frame_back");
    ui_btnFrameBack = btn_frame_stepBack;
    e9ui_header_flow_add(toolbar, btn_frame_stepBack);

    e9ui_component_t *btn_frame_step = e9ui_button_make("Frame", ui_frameStep, NULL);
    e9ui_button_setIconAsset(btn_frame_step, "assets/icons/step.png");
    hotkeys_registerButtonActionHotkey(btn_frame_step, &e9ui->ctx, "frame_step");
    ui_btnFrameStep = btn_frame_step;
    e9ui_header_flow_add(toolbar, btn_frame_step);

    e9ui_component_t *btn_frame_continue = e9ui_button_make("Continue", ui_frameContinue, NULL);
    hotkeys_registerButtonActionHotkey(btn_frame_continue, &e9ui->ctx, "frame_continue");
    ui_btnFrameContinue = btn_frame_continue;
    e9ui_setDisableVariable(btn_frame_continue, &debugger.frameStepMode, 0);
    e9ui_header_flow_add(toolbar, btn_frame_continue);

    e9ui_component_t *sep_frame_save = e9ui_separator_make(9);
    e9ui_header_flow_add(toolbar, sep_frame_save);

    e9ui_component_t *btn_save = e9ui_button_make("Save", ui_saveState, NULL);
    hotkeys_registerButtonActionHotkey(btn_save, &e9ui->ctx, "save_state");
    ui_btnSaveState = btn_save;
    e9ui_header_flow_add(toolbar, btn_save);

    e9ui_component_t *btn_restore = e9ui_button_make("Restore", ui_restoreState, NULL);
    hotkeys_registerButtonActionHotkey(btn_restore, &e9ui->ctx, "restore_state");
    ui_btnRestoreState = btn_restore;
    e9ui_setDisableVariable(btn_restore, &debugger.hasStateSnapshot, 0);
    e9ui_header_flow_add(toolbar, btn_restore);

    e9ui_component_t *sep_restore_reset = e9ui_separator_make(9);
    e9ui_header_flow_add(toolbar, sep_restore_reset);

    e9ui_component_t *btn_settings = e9ui_button_make("Settings", settings_uiOpen, NULL);
    hotkeys_registerButtonActionHotkey(btn_settings, &e9ui->ctx, "settings");
    e9ui->settingsButton = btn_settings;
    ui_btnSettings = btn_settings;
    e9ui_header_flow_add(toolbar, btn_settings);

    if (btn_speed) {
        e9ui_header_flow_add(toolbar, btn_speed);
    }

    e9ui_component_t *btn_audio = e9ui_button_make("", ui_audioToggle, NULL);
    hotkeys_registerButtonActionHotkey(btn_audio, &e9ui->ctx, "audio_toggle");
    e9ui->audioButton = btn_audio;
    ui_btnAudio = btn_audio;
    ui_audioRefreshButton();
    e9ui_header_flow_add(toolbar, btn_audio);

    e9ui_component_t *btn_record = e9ui_button_make("", ui_recordToggle, NULL);
    hotkeys_registerButtonActionHotkey(btn_record, &e9ui->ctx, "rolling_save_toggle");
    ui_btnRecord = btn_record;
    e9ui_header_flow_add(toolbar, btn_record);
    ui_recordRefreshButton();

    e9ui_component_t *btn_reset = e9ui_button_make("", ui_reset, NULL);
    hotkeys_registerButtonActionHotkey(btn_reset, &e9ui->ctx, "reset_core");
    e9ui_button_setIconAsset(btn_reset, "assets/icons/reset.png");
    e9ui_button_setTheme(btn_reset, e9ui_theme_button_preset_profile_active());
    e9ui->resetButton = btn_reset;
    ui_btnReset = btn_reset;
    e9ui_header_flow_add(toolbar, btn_reset);

    e9ui_component_t *btn_restart = e9ui_button_make("", ui_restart, NULL);
    hotkeys_registerButtonActionHotkey(btn_restart, &e9ui->ctx, "restart");
    e9ui_button_setIconAsset(btn_restart, "assets/icons/reset.png");
    e9ui_button_setTheme(btn_restart, e9ui_theme_button_preset_red());
    e9ui->restartButton = btn_restart;
    ui_btnRestart = btn_restart;
    e9ui_header_flow_add(toolbar, btn_restart);

    ui_refreshHotkeyTooltips();

    e9ui_component_t *comp_stack = e9ui_stack_makeVertical();
    // Add bottom border under the top logo/title row
    e9ui_component_t *top_row_box = e9ui_box_make(toolbar);
    e9ui_box_setBorder(top_row_box, E9UI_BORDER_BOTTOM, (SDL_Color){70,70,70,255}, 1);
    e9ui_stack_addFixed(comp_stack, top_row_box);
    // Insert registers panel and make it resizable vs source/console (left pane)
    e9ui_component_t *comp_registers = registers_makeComponent();
    ui_compRegisters = comp_registers;
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

    e9ui_component_t *btn_profile_reset = e9ui_button_make("Reset", profile_uiReset, NULL);
    e9ui_button_setMini(btn_profile_reset, 1);
    e9ui_button_setTheme(btn_profile_reset, e9ui_theme_button_preset_red());
    e9ui_flow_add(comp_profile_toolbar, btn_profile_reset);

    e9ui_component_t *btn_profile_metric = e9ui_button_make("Cycles", profile_uiMetricToggle, NULL);
    e9ui_button_setMini(btn_profile_metric, 1);
    e9ui_button_setLargestLabel(btn_profile_metric, "Samples");
    profile_metricButtonRegister(btn_profile_metric);
    e9ui_flow_add(comp_profile_toolbar, btn_profile_metric);

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
    if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE ||
        debugger.smokeTestMode == SMOKE_TEST_MODE_REMAKE) {
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
