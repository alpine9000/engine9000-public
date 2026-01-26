/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL_image.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <ctype.h>

#include "e9ui.h"
#include "libretro_host.h"
#include "resource.h"
#include "debugger.h"
#include "profile.h"
#include "debug.h"
#include "analyse.h"
#include "linebuf.h"
#include "sprite_debug.h"
#include "machine.h"
#include "source.h"
#include "dasm.h"
#include "addr2line.h"
#include "state_buffer.h"
#include "snapshot.h"
#include "debugger_signal.h"
#include "transition.h"
#include "input_record.h"
#include "smoke_test.h"
#include "shader_ui.h"
#include "memory_track_ui.h"
#include "crt.h"
#include "settings.h"
#include "cli.h"
#include "runtime.h"
#include "config.h"
#include "romset.h"
#include "ui.h"

e9k_debugger_t debugger;

static int debugger_analyseInitFailed = 0;

static int debugger_pathExistsFile(const char *path);

static void
debugger_copyPath(char *dest, size_t cap, const char *src);

static void
debugger_setArgv0(void)
{
    const char *argv0 = cli_getArgv0();
    if (!argv0 || !*argv0) {
        debugger.argv0[0] = '\0';
        return;
    }
    strncpy(debugger.argv0, argv0, sizeof(debugger.argv0) - 1);
    debugger.argv0[sizeof(debugger.argv0) - 1] = '\0';
}

void
debugger_suppressBreakpointAtPC(void)
{
    if (debugger.suppressBpActive) {
        return;
    }
    unsigned long pc = 0;
    if (!machine_findReg(&debugger.machine, "PC", &pc)) {
        return;
    }
    uint32_t addr = (uint32_t)(pc & 0x00ffffffu);
    machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, addr);
    if (!bp || !bp->enabled) {
        return;
    }
    debugger.suppressBpActive = 1;
    debugger.suppressBpAddr = addr;
    libretro_host_debugRemoveBreakpoint(addr);
}

void
debugger_clearFrameStep(void)
{
    debugger.frameStepMode = 0;
    debugger.frameStepPending = 0;
}

void
debugger_toggleSpeed(void)
{
    debugger.speedMultiplier = (debugger.speedMultiplier == 10) ? 1 : 10;
    ui_refreshSpeedButton();
}

void
debugger_cancelSettingsModal(void)
{
    settings_cancelModal();
}

static void
debugger_copyPath(char *dest, size_t cap, const char *src)
{
    if (!dest || cap == 0) {
        return;
    }
    if (!src || !*src) {
        dest[0] = '\0';
        return;
    }
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            int written = snprintf(dest, cap, "%s%s", home, src + 1);
            if (written < 0 || (size_t)written >= cap) {
                dest[cap - 1] = '\0';
            }
            return;
        }
    }
    strncpy(dest, src, cap - 1);
    dest[cap - 1] = '\0';
}

static int
debugger_pathExistsFile(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    return S_ISREG(sb.st_mode) ? 1 : 0;
}

void
debugger_libretroSetupPaths(void)
{
    debugger_copyPath(debugger.libretro.corePath, sizeof(debugger.libretro.corePath), debugger.config.corePath);
    debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), debugger.config.romPath);
    debugger_copyPath(debugger.libretro.systemDir, sizeof(debugger.libretro.systemDir), debugger.config.biosDir);
    debugger_copyPath(debugger.libretro.saveDir, sizeof(debugger.libretro.saveDir), debugger.config.savesDir);
    if (debugger.config.romFolder[0]) {
        char neo_path[PATH_MAX];
        if (romset_buildNeoFromFolder(debugger.config.romFolder, neo_path, sizeof(neo_path))) {
            debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), neo_path);
        } else {
            debugger.libretro.romPath[0] = '\0';
        }
    }
    debugger.libretro.enabled = (debugger.libretro.corePath[0] && debugger.libretro.romPath[0]) ? 1 : 0;
}

void
debugger_refreshElfValid(void)
{
    debugger.elfValid = 0;
    if (debugger.config.elfPath[0] && debugger_pathExistsFile(debugger.config.elfPath)) {
        debugger.elfValid = 1;
    }
    ui_applySourcePaneElfMode();
}

void
debugger_applyCoreOptions(void)
{
    if (debugger.config.systemType[0]) {
        libretro_host_setCoreOption("geolith_system_type", debugger.config.systemType);
    } else {
        libretro_host_setCoreOption("geolith_system_type", NULL);
    }
}

void
debugger_setSeeking(int seeking)
{
    debugger.seeking = seeking ? 1 : 0;
}

int
debugger_isSeeking(void)
{
    return debugger.seeking ? 1 : 0;
}

static void
debugger_cleanup(void)
{
  config_saveConfig();
  snapshot_saveOnExit();
  if (sprite_debug_is_open()) {
    sprite_debug_toggle();
  }
  libretro_host_shutdown();
  free(debugger.spriteShadowVram);
  debugger.spriteShadowVram = NULL;
  debugger.spriteShadowWords = 0;
  addr2line_stop();
  profile_streamStop();
  state_buffer_shutdown();
  machine_shutdown(&debugger.machine);
  linebuf_dtor(&debugger.console);
  analyseShutdown();
  dasm_shutdown();
  source_shutdown();
  shader_ui_shutdown();
  memory_track_ui_shutdown();
  e9ui_shutdown();
  resource_status();  
}

static void
debugger_ctor(void)
{
  memset(&debugger, 0, sizeof(debugger));
  srand((unsigned)time(NULL));
  debugger_setArgv0();
  debugger.opts.redirectStdout = E9K_DEBUG_PRINTF_STDOUT_DEFAULT;
  debugger.opts.redirectStderr = E9K_DEBUG_ERROR_STDERR_DEFAULT;
  debugger.opts.redirectGdbStdout = E9K_DEBUG_GDB_STDOUT_DEFAULT;
  debugger.opts.enableTrace = E9K_DEBUG_TRACE_ENABLE_DEFAULT;
  debugger.opts.completionListRows = 30; // default completion popup rows
  linebuf_init(&debugger.console, 2000);
  linebuf_push(&debugger.console, "--== PRESS F1 FOR HELP ==--");
  if (!analyseInit()) {
    debugger_analyseInitFailed = 1;
  }
  debugger.geo.connected = 0;
  debugger.geo.sock = -1;
  debugger.geo.port = 9000;
  debugger.geo.streamPacketCount = 0;
  debugger.hasStateSnapshot = 0;
  debugger.speedMultiplier = 1;
  debugger.frameStepMode = 0;
  debugger.config.audioEnabled = 1;
  debugger.frameStepPending = 0;
  debugger.vblankCaptureActive = 0;
  debugger.config.audioBufferMs = 50;
  debugger.config.skipBiosLogo = 0;
  debugger.config.crtEnabled = 1;
  debugger.recordPath[0] = '\0';
  debugger.playbackPath[0] = '\0';
  debugger.smokeTestPath[0] = '\0';
  debugger.smokeTestMode = SMOKE_TEST_MODE_NONE;
  debugger.smokeTestCompleted = 0;
  debugger.smokeTestFailed = 0;
  debugger.smokeTestExitCode = -1;
  debugger.smokeTestOpenOnFail = 0;
  debugger.cliWindowOverride = 0;
  debugger.cliWindowW = 0;
  debugger.cliWindowH = 0;
  debugger.glCompositeEnabled = 1;
  debugger.transitionMode = e9k_transition_random;
  debugger.transitionFullscreenMode = e9k_transition_none;
  debugger.transitionFullscreenModeSet = 0;
  debugger.transitionCycleIndex = 0;
  machine_init(&debugger.machine);
  size_t buf_bytes = 64 * 1024 * 1024;
  const char *env_buf = getenv("E9K_STATE_BUFFER_BYTES");
  if (env_buf && *env_buf) {
    char *end = NULL;
    unsigned long long v = strtoull(env_buf, &end, 10);
    if (end && end != env_buf) {
      buf_bytes = (size_t)v;
    }
  }
  state_buffer_init(buf_bytes);
}

int
debugger_main(int argc, char **argv)
{
  debugger_ctor();
  signal_installHandlers();
 
  config_loadConfig();
  cli_parseArgs(argc, argv);
  if (cli_helpRequested()) {
    cli_printUsage(argv && argv[0] ? argv[0] : NULL);
    return 0;
  }
  if (debugger.smokeTestMode != SMOKE_TEST_MODE_NONE) {
    debugger.speedMultiplier = 10;
    if (debugger.smokeTestMode == SMOKE_TEST_MODE_RECORD) {
      if (debugger.playbackPath[0]) {
        debug_error("make-smoke: cannot use --playback with --make-smoke");
        return 1;
      }
    } else if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE) {
      if (debugger.recordPath[0] || debugger.playbackPath[0]) {
        debug_error("smoke-test: cannot combine with --record or --playback");
        return 1;
      }
    }
    smoke_test_setFolder(debugger.smokeTestPath);
    smoke_test_setMode((smoke_test_mode_t)debugger.smokeTestMode);
    smoke_test_setOpenOnFail(debugger.smokeTestOpenOnFail);
    if (!smoke_test_init()) {
      return 1;
    }
    char path[PATH_MAX];
    if (smoke_test_getRecordPath(path, sizeof(path))) {
      if (debugger.smokeTestMode == SMOKE_TEST_MODE_RECORD) {
        debugger_copyPath(debugger.recordPath, sizeof(debugger.recordPath), path);
      } else if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE) {
        debugger_copyPath(debugger.playbackPath, sizeof(debugger.playbackPath), path);
      }
    }
  }
  if (debugger.recordPath[0]) {
    input_record_setRecordPath(debugger.recordPath);
  }
  if (debugger.playbackPath[0]) {
    input_record_setPlaybackPath(debugger.playbackPath);
  }
  if (!input_record_init()) {
    smoke_test_shutdown();
    return 1;
  }

  if (!e9ui_ctor()) {
    input_record_shutdown();
    smoke_test_shutdown();
    {
      int sig = signal_getExitCode();
      return sig ? (128 + sig) : 1;
    }
  }
  crt_setEnabled(debugger.config.crtEnabled ? 1 : 0);

  ui_build();
  cli_applyOverrides();
  debugger_libretroSetupPaths();
  debugger_refreshElfValid();
  if (debugger.elfValid && debugger_analyseInitFailed) {
    debug_error("profile: aggregator init failed");
    debugger_analyseInitFailed = 0;
  }
  debugger.settingsOk = settings_configIsOk();
  if (!debugger.settingsOk) {
    config_saveConfig();
  }
  settings_applyToolbarMode();
  settings_updateButton(debugger.settingsOk);

  if (debugger.libretro.enabled) {
    if (!libretro_host_init(debugger.ui.ctx.renderer)) {
      debug_error("libretro: failed to init host renderer");
      debugger.libretro.enabled = 0;
    }
  }

  if (debugger.libretro.enabled) {
    debugger_applyCoreOptions();
    if (!libretro_host_start(debugger.libretro.corePath, debugger.libretro.romPath,
                             debugger.libretro.systemDir, debugger.libretro.saveDir)) {
      debug_error("libretro: failed to start core");
      debugger.libretro.enabled = 0;
    } else {
      snapshot_loadOnBoot();
    }
  }
  if (debugger.config.romPath[0] || debugger.config.romFolder[0]) {
    if (!dasm_preloadText()) {
      debug_error("dasm: preload failed");
    }
  }
  if (debugger.libretro.enabled) {
    int prof_enabled = 0;
    if (libretro_host_profilerIsEnabled(&prof_enabled)) {
      debugger.geo.profilerEnabled = prof_enabled ? 1 : 0;
      profile_buttonRefresh();
      profile_buttonRefresh();
    }
    debugger.vblankCaptureActive = libretro_host_setVblankCallback(runtime_onVblank, NULL) ? 1 : 0;
    int paused = 0;
    if (libretro_host_debugIsPaused(&paused)) {
      machine_setRunning(&debugger.machine, paused ? 0 : 1);
    } else {
      machine_setRunning(&debugger.machine, 1);
    }
  }
  transition_runIntro();
  runtime_runLoop();
  debugger_cleanup();
  input_record_shutdown();
  smoke_test_shutdown();
  if (debugger.smokeTestExitCode >= 0) {
    return debugger.smokeTestExitCode;
  }
  {
    int sig = signal_getExitCode();
    if (sig) {
      return 128 + sig;
    }
  }
  if (debugger.restartRequested) {
    return 2;
  }
  return 0;
}
