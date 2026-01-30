/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <sys/stat.h>
#include <time.h>

#include "e9ui.h"
#include "libretro_host.h"
#include "debugger.h"
#include "profile.h"

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
#include "emu_geo.h"
#include "emu_ami.h"
#include "amiga_uae_options.h"
#include "neogeo_core_options.h"

e9ui_global_t _e9ui;
e9k_debugger_t debugger;
e9ui_global_t *e9ui = &_e9ui;

static int debugger_analyseInitFailed = 0;

static int debugger_pathExistsFile(const char *path);

static void
debugger_copyPath(char *dest, size_t cap, const char *src);

static void
debugger_onSetDebugBaseFromCore(uint32_t section, uint32_t base)
{
    const char *name = "unknown";
    switch (section) {
    case 0u:
        debugger.machine.textBaseAddr = base;
        name = "text";
        break;
    case 1u:
        debugger.machine.dataBaseAddr = base;
        name = "data";
        break;
    case 2u:
        debugger.machine.bssBaseAddr = base;
        name = "bss";
        break;
    default:
        debugger.machine.textBaseAddr = base;
        name = "text";
        break;
    }
    debug_printf("base: set %s to 0x%08X (from core)\n", name, (unsigned)base);
}

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
debugger_setCoreSystem(debugger_system_type_t type)
{
    switch (type) {
    case DEBUGGER_SYSTEM_AMIGA:
        debugger.config.coreSystem = type;
        debugger.dasm = &dasm_ami_iface;
	debugger.emu = &emu_ami_iface;
        break;
    case DEBUGGER_SYSTEM_NEOGEO:
    case DEBUGGER_SYSTEM_MEGADRIVE:
        debugger.config.coreSystem = type;
        debugger.dasm = &dasm_geo_iface;
	debugger.emu = &emu_geo_iface;	
        break;
    default:
        debugger.config.coreSystem = DEBUGGER_SYSTEM_NEOGEO;
        debugger.dasm = &dasm_geo_iface;
	debugger.emu = &emu_geo_iface;	
        break;
    }
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
debugger_libretroSelectConfig(void)
{
  memset(&debugger.libretro, 0, sizeof(debugger.libretro));
  
  if (debugger.config.coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
    debugger.libretro.audioBufferMs = debugger.config.neogeo.libretro.audioBufferMs;
    debugger.libretro.audioEnabled = debugger.config.neogeo.libretro.audioEnabled;
    debugger_copyPath(debugger.libretro.sourceDir, sizeof(debugger.libretro.sourceDir), debugger.config.neogeo.libretro.sourceDir);
    debugger_copyPath(debugger.libretro.elfPath, sizeof(debugger.libretro.elfPath), debugger.config.neogeo.libretro.elfPath);              
    debugger_copyPath(debugger.libretro.toolchainPrefix, sizeof(debugger.libretro.toolchainPrefix), debugger.config.neogeo.libretro.toolchainPrefix);
    debugger_copyPath(debugger.libretro.corePath, sizeof(debugger.libretro.corePath), debugger.config.neogeo.libretro.corePath);
    debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), debugger.config.neogeo.libretro.romPath);
    debugger_copyPath(debugger.libretro.systemDir, sizeof(debugger.libretro.systemDir), debugger.config.neogeo.libretro.systemDir);
    debugger_copyPath(debugger.libretro.saveDir, sizeof(debugger.libretro.saveDir), debugger.config.neogeo.libretro.saveDir);
    if (debugger.config.neogeo.romFolder[0]) {
      char neo_path[PATH_MAX];
      if (romset_buildNeoFromFolder(debugger.config.neogeo.romFolder, neo_path, sizeof(neo_path))) {
	debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), neo_path);
      } else {
	debugger.libretro.romPath[0] = '\0';
      }
    }
  } else if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
    debugger.libretro.audioBufferMs = debugger.config.amiga.libretro.audioBufferMs;
    debugger.libretro.audioEnabled = debugger.config.amiga.libretro.audioEnabled;
    debugger_copyPath(debugger.libretro.sourceDir, sizeof(debugger.libretro.sourceDir), debugger.config.amiga.libretro.sourceDir);
    debugger_copyPath(debugger.libretro.elfPath, sizeof(debugger.libretro.elfPath), debugger.config.amiga.libretro.elfPath);              
    debugger_copyPath(debugger.libretro.toolchainPrefix, sizeof(debugger.libretro.toolchainPrefix), debugger.config.amiga.libretro.toolchainPrefix);
    debugger_copyPath(debugger.libretro.corePath, sizeof(debugger.libretro.corePath), debugger.config.amiga.libretro.corePath);
    debugger_copyPath(debugger.libretro.romPath, sizeof(debugger.libretro.romPath), debugger.config.amiga.libretro.romPath);
    debugger_copyPath(debugger.libretro.systemDir, sizeof(debugger.libretro.systemDir), debugger.config.amiga.libretro.systemDir);
    debugger_copyPath(debugger.libretro.saveDir, sizeof(debugger.libretro.saveDir), debugger.config.amiga.libretro.saveDir);
  }

  debugger.libretro.enabled = (debugger.libretro.corePath[0] && debugger.libretro.romPath[0]) ? 1 : 0;  
}

int
debugger_toolchainBuildBinary(char *out, size_t cap, const char *tool)
{
    if (!out || cap == 0 || !tool || !*tool) {
        return 0;
    }
    const char *prefix = debugger.libretro.toolchainPrefix;
    if (!prefix || !*prefix) {
        int written = snprintf(out, cap, "%s", tool);
        if (written < 0 || (size_t)written >= cap) {
            out[cap - 1] = '\0';
            return 0;
        }
        return 1;
    }
    size_t len = strlen(prefix);
    int hasDash = (len > 0 && prefix[len - 1] == '-') ? 1 : 0;
    int written = 0;
    if (hasDash) {
        written = snprintf(out, cap, "%s%s", prefix, tool);
    } else {
        written = snprintf(out, cap, "%s-%s", prefix, tool);
    }
    if (written < 0 || (size_t)written >= cap) {
        out[cap - 1] = '\0';
        return 0;
    }
    return 1;
}

void
debugger_refreshElfValid(void)
{
    debugger.elfValid = 0;
    const char *rawElf = NULL;
    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        rawElf = debugger.config.amiga.libretro.elfPath;
    } else {
        rawElf = debugger.config.neogeo.libretro.elfPath;
    }
    char elfPath[PATH_MAX];
    elfPath[0] = '\0';
    debugger_copyPath(elfPath, sizeof(elfPath), rawElf);
    if (elfPath[0] && debugger_pathExistsFile(elfPath)) {
        debugger.elfValid = 1;
    }
    ui_applySourcePaneElfMode();
}

void
debugger_applyCoreOptions(void)
{
    if (debugger.config.neogeo.systemType[0]) {
        libretro_host_setCoreOption("geolith_system_type", debugger.config.neogeo.systemType);
    } else {
        libretro_host_setCoreOption("geolith_system_type", NULL);
    }

    if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
        const char *uaePath = debugger.libretro.romPath[0] ? debugger.libretro.romPath : debugger.config.amiga.libretro.romPath;
        if (uaePath && *uaePath) {
            amiga_uaeApplyPuaeOptionsToHost(uaePath);
        }
    } else if (debugger.config.coreSystem == DEBUGGER_SYSTEM_NEOGEO) {
        const char *romPath = debugger.libretro.romPath[0] ? debugger.libretro.romPath : debugger.config.neogeo.libretro.romPath;
        const char *saveDir = debugger.libretro.saveDir[0] ? debugger.libretro.saveDir : debugger.config.neogeo.libretro.saveDir;
        if (romPath && *romPath && saveDir && *saveDir) {
            neogeo_coreOptionsApplyFileToHost(saveDir, romPath);
        }
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
  analyse_shutdown();
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
  debugger_setCoreSystem(DEBUGGER_SYSTEM_AMIGA);
  debugger.opts.redirectStdout = E9K_DEBUG_PRINTF_STDOUT_DEFAULT;
  debugger.opts.redirectStderr = E9K_DEBUG_ERROR_STDERR_DEFAULT;
  debugger.opts.redirectGdbStdout = E9K_DEBUG_GDB_STDOUT_DEFAULT;
  debugger.opts.enableTrace = E9K_DEBUG_TRACE_ENABLE_DEFAULT;
  debugger.opts.completionListRows = 30; // default completion popup rows
  linebuf_init(&debugger.console, 2000);
  linebuf_push(&debugger.console, "--== PRESS F1 FOR HELP ==--");
  if (!analyse_init()) {
    debugger_analyseInitFailed = 1;
  }
  debugger.geo.connected = 0;
  debugger.geo.sock = -1;
  debugger.geo.port = 9000;
  debugger.geo.streamPacketCount = 0;
  debugger.hasStateSnapshot = 0;
  debugger.speedMultiplier = 1;
  debugger.frameStepMode = 0;
  debugger.config.neogeo.libretro.audioEnabled = 1;
  debugger.config.neogeo.libretro.audioBufferMs = 50;
  debugger.config.neogeo.skipBiosLogo = 0;
  debugger.frameStepPending = 0;
  debugger.vblankCaptureActive = 0;  
  debugger.config.crtEnabled = 1;
  snprintf(debugger.config.neogeo.libretro.toolchainPrefix, sizeof(debugger.config.neogeo.libretro.toolchainPrefix), "m68k-neogeo-elf");
  snprintf(debugger.config.amiga.libretro.toolchainPrefix, sizeof(debugger.config.amiga.libretro.toolchainPrefix), "m68k-amigaos-");
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
  debugger.cliDisableRollingRecord = 0;
  debugger.cliStartFullscreen = 0;
  debugger.cliHeadless = 0;
  debugger.cliWarp = 0;
  e9ui->glCompositeEnabled = 1;
  e9ui->transition.mode = e9k_transition_random;
  e9ui->transition.fullscreenMode = e9k_transition_none;
  e9ui->transition.fullscreenModeSet = 0;
  e9ui->transition.cycleIndex = 0;
  e9ui->layout.memTrackWinX = -1;
  e9ui->layout.memTrackWinY = -1;
  e9ui->layout.memTrackWinW = 0;
  e9ui->layout.memTrackWinH = 0;
  machine_init(&debugger.machine);
  size_t buf_bytes = 512 * 1024 * 1024;
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
  if (cli_hasError()) {
    cli_printUsage(argv && argv[0] ? argv[0] : NULL);
    return 1;
  }
  if (debugger.smokeTestMode != SMOKE_TEST_MODE_NONE || debugger.cliHeadless || debugger.cliDisableRollingRecord) {
    state_buffer_setRollingPaused(1);
  }
  if (debugger.cliWarp) {
    debugger.speedMultiplier = 10;
  }
  if (debugger.smokeTestMode != SMOKE_TEST_MODE_NONE) {
    if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE) {
      debugger.speedMultiplier = 10;
    }
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

  if (!e9ui_ctor(debugger_configPath(), debugger.cliWindowOverride, debugger.cliWindowW, debugger.cliWindowH, debugger.cliHeadless)) {
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
  if (debugger.cliStartFullscreen && !debugger.cliHeadless) {
    e9ui_component_t *target = e9ui_findById(e9ui->root, "libretro_box");
    if (!target) {
      target = e9ui_findById(e9ui->root, "geo_view");
    }
    if (target && e9ui->fullscreen != target) {
      e9ui_setFullscreenComponent(target);
    }
  }  
  debugger_libretroSelectConfig();
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
    if (!libretro_host_init(e9ui->ctx.renderer)) {
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
      if (!libretro_host_setDebugBaseCallback(debugger_onSetDebugBaseFromCore)) {
        if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
          debug_error("debug_base: core does not expose geo_set_debug_base_callback");
        }
      }
      snapshot_loadOnBoot();
    }
  }
  if (debugger.config.neogeo.libretro.romPath[0] || debugger.config.neogeo.romFolder[0]) {
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

int
debugger_getAudioEnabled(void)
{
  if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
    return debugger.config.amiga.libretro.audioEnabled;
  } else {
    return debugger.config.neogeo.libretro.audioEnabled;
  }
}

void
debugger_setAudioEnabled(int enabled)
{
  if (debugger.config.coreSystem == DEBUGGER_SYSTEM_AMIGA) {
    debugger.config.amiga.libretro.audioEnabled = enabled;
  } else {
    debugger.config.neogeo.libretro.audioEnabled = enabled;
  }
}
