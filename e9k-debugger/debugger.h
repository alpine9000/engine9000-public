/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <SDL_ttf.h>
#include "e9ui.h"
#include "debug.h"
#include "linebuf.h"
#include "machine.h"
#include "geo_debug_sprite.h"

#ifdef _WIN32
#include "w64_debugger_platform.h"
#endif

typedef struct e9k_debug_options {
    int redirectStdout;
    int redirectStderr; 
    int redirectGdbStdout; 
    int enableTrace; 
    int debugLayout; 
    int completionListRows;
} e9k_debug_options_t;



typedef struct e9k_hotkey_entry {
    int id;
    int key;   // SDL_Keycode
    int mask;  // SDL_Keymod mask
    int value; // SDL_Keymod value
    void (*cb)(e9ui_context_t *ctx, void *user);
    void *user;
    int  active;
} e9k_hotkey_entry_t;

typedef struct e9k_hotkey_registry {
    e9k_hotkey_entry_t *entries;
    int count;
    int cap;
    int next_id;
} e9k_hotkey_registry_t;

typedef struct e9k_layout_config {
    float splitSrcConsole;
    float splitUpper;
    float splitRight;
    float splitLr;
    int   winX;
    int   winY;
    int   winW;
    int   winH;
} e9k_layout_config_t;

typedef struct e9k_path_config {
    char corePath[PATH_MAX];
    char romPath[PATH_MAX];
    char romFolder[PATH_MAX];
    char elfPath[PATH_MAX];
    char biosDir[PATH_MAX];
    char savesDir[PATH_MAX];
    char sourceDir[PATH_MAX];
    char systemType[16];
    int  audioBufferMs;
    int  audioEnabled;
    int  skipBiosLogo;
    int  crtEnabled;
} e9k_path_config_t;

typedef enum e9k_transition_mode {
    e9k_transition_none = 0,
    e9k_transition_slide,
    e9k_transition_explode,
    e9k_transition_doom,
    e9k_transition_flip,
    e9k_transition_rbar,
    e9k_transition_random,
    e9k_transition_cycle
} e9k_transition_mode_t;

typedef enum {
  DEBUGGER_RUNMODE_CAPTURE,
  DEBUGGER_RUNMODE_RESTORE,
} debugger_run_mode_t;


typedef struct e9k_debugger {
    LineBuf console;
    int     consoleScrollLines;
    char    argv0[PATH_MAX];
    e9k_path_config_t config;
    e9k_path_config_t cliConfig;
    e9k_path_config_t settingsEdit;
    struct {
        int mouseX;
        int mouseY;
        e9ui_component_t *root;
        e9ui_component_child_t* rootComponent;
        e9ui_context_t    ctx;
        e9ui_component_t *toolbar; 
        e9ui_component_t *profileButton; 
        e9ui_component_t *analyseButton;
        e9ui_component_t *speedButton; 
        e9ui_component_t *restartButton;
        e9ui_component_t *resetButton;
        e9ui_component_t *audioButton; 
        e9ui_component_t *settingsButton;
        e9ui_component_t *settingsModal; 
        e9ui_component_t *settingsSaveButton;
        e9ui_component_t *helpModal; 
        e9ui_component_t *prompt; 
        e9ui_component_t *pendingRemove; 
        e9ui_component_t *sourceBox; 
        e9ui_component_t *fullscreen;
        char sourceTitle[PATH_MAX];
        e9k_hotkey_registry_t hotkeys;
    } ui;
    struct {
        int connected;
        int sock;
        int port;
        int profilerEnabled;
        unsigned long long streamPacketCount;
    } geo;
    machine_t machine;
    int seeking;
    int hasStateSnapshot;
    int speedMultiplier;
    int frameStepMode;
    int frameStepPending;
    int inTransition;
    int suppressBpActive;
    uint32_t suppressBpAddr;
    uint64_t frameCounter;
    uint64_t frameTimeCounter;
    double frameTimeAccum;
    int vblankCaptureActive;
    int spriteShadowReady;
    geo_debug_sprite_state_t spriteShadow;
    uint16_t *spriteShadowVram;
    size_t spriteShadowWords;
    char recordPath[PATH_MAX];
    char playbackPath[PATH_MAX];
    char smokeTestPath[PATH_MAX];
    int smokeTestMode;
    int smokeTestCompleted;
    int smokeTestFailed;
    int smokeTestExitCode;
    int smokeTestOpenOnFail;
    int cliWindowOverride;
    int cliWindowW;
    int cliWindowH;
    int glCompositeEnabled;
    int glCompositeCapture;
    int settingsOk;
    int elfValid;
    int restartRequested;
    int transitionFullscreenModeSet;
    e9k_transition_mode_t transitionMode;
    e9k_transition_mode_t transitionFullscreenMode;
    int transitionCycleIndex;
    e9k_debug_options_t opts;
    struct {
        e9k_theme_button_t button;
        e9k_theme_button_t miniButton;
        e9k_theme_text_t   text;
        e9k_theme_titlebar_t titlebar;
        e9k_theme_checkbox_t checkbox;
        e9k_theme_disabled_t disabled;
    } theme;
    struct {
        char corePath[PATH_MAX];
        char romPath[PATH_MAX];
        char systemDir[PATH_MAX];
        char saveDir[PATH_MAX];
        int enabled;
    } libretro;
    int loopEnabled;
    uint64_t loopFrom;
    uint64_t loopTo;
    e9k_layout_config_t layout;
} e9k_debugger_t;

extern e9k_debugger_t debugger;

char *
debugger_configPath(void);

void
debugger_toggleSpeed(void);

void
debugger_clearFrameStep(void);

int
debugger_main(int argc, char **argv);

int
debugger_platform_pathJoin(char *out, size_t cap, const char *dir, const char *name);

void
debugger_suppressBreakpointAtPC(void);

void
debugger_cancelSettingsModal(void);

void
debugger_setSeeking(int seeking);

int
debugger_isSeeking(void);

void debugger_libretroSetupPaths(void);

void
debugger_refreshElfValid(void);

void
debugger_applyCoreOptions(void);
