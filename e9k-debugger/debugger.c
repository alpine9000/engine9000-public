/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#include "e9ui.h"
#include "libretro_host.h"
#include "debugger.h"
#include "profile.h"

#include "analyse.h"
#include "linebuf.h"
#include "sprite_debug.h"
#include "machine.h"
#include "source.h"
#include "syntax_highlight.h"
#include "dasm.h"
#include "addr2line.h"
#include "state_buffer.h"
#include "snapshot.h"
#include "rom_config.h"
#include "debugger_signal.h"
#include "transition.h"
#include "input_record.h"
#include "smoke_test.h"
#include "ui_test.h"
#include "shader_ui.h"
#include "custom_ui.h"
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
#include "debugger_platform.h"
#include "amiga_uae_options.h"
#include "neogeo_core_options.h"
#include "breakpoints.h"

e9ui_global_t _e9ui;
e9k_debugger_t debugger;
e9ui_global_t *e9ui = &_e9ui;

static int debugger_analyseInitFailed = 0;
static int debugger_loadTestTempConfig = 0;
static int debugger_testRestartCount = 0;

static int debugger_pathExistsFile(const char *path);
static void debugger_syncCoreBreakpointsForTextBaseChange(uint32_t oldBaseAddr, uint32_t newBaseAddr);

void
debugger_setLoadTestTempConfig(int enabled)
{
    debugger_loadTestTempConfig = enabled ? 1 : 0;
}

int
debugger_getLoadTestTempConfig(void)
{
    return debugger_loadTestTempConfig ? 1 : 0;
}

void
debugger_setTestRestartCount(int count)
{
    debugger_testRestartCount = count > 0 ? count : 0;
}

int
debugger_getTestRestartCount(void)
{
    return debugger_testRestartCount;
}

void
debugger_onSetDebugBaseFromCore(uint32_t section, uint32_t base)
{
    const char *name = "unknown";
    switch (section) {
    case 0u:
        debugger_setTextBaseAddress(base);
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
debugger_syncCoreBreakpointsForTextBaseChange(uint32_t oldBaseAddr, uint32_t newBaseAddr)
{
    const machine_breakpoint_t *bps = NULL;
    int count = 0;
    if (!machine_getBreakpoints(&debugger.machine, &bps, &count) || !bps || count <= 0) {
        debugger.machine.textBaseAddr = newBaseAddr;
        return;
    }

    for (int i = 0; i < count; ++i) {
        if (!bps[i].enabled) {
            continue;
        }
        uint32_t addr = (uint32_t)(bps[i].addr & 0x00ffffffu);
        libretro_host_debugRemoveBreakpoint(addr);
    }

    machine_rebaseTextBreakpoints(&debugger.machine, oldBaseAddr, newBaseAddr);
    debugger.machine.textBaseAddr = newBaseAddr;

    for (int i = 0; i < count; ++i) {
        machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
        if (!bp->enabled) {
            breakpoints_resolveLocation(bp);
            continue;
        }
        uint32_t addr = (uint32_t)(bp->addr & 0x00ffffffu);
        libretro_host_debugAddBreakpoint(addr);
        breakpoints_resolveLocation(bp);
    }
    breakpoints_markDirty();
}

void
debugger_setTextBaseAddress(uint32_t base)
{
    uint32_t newBaseAddr = base & 0x00ffffffu;
    uint32_t oldBaseAddr = debugger.machine.textBaseAddr & 0x00ffffffu;
    if (oldBaseAddr == newBaseAddr) {
        debugger.machine.textBaseAddr = newBaseAddr;
        return;
    }
    debugger_syncCoreBreakpointsForTextBaseChange(oldBaseAddr, newBaseAddr);
}

void
debugger_onAddBreakpointFromCore(uint32_t addr)
{
    machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
    if (!bp) {
        return;
    }
    breakpoints_resolveLocation(bp);
    breakpoints_markDirty();
    e9ui_showTransientMessage("BREAKPOINT ADDED");
}

static int
debugger_onResolveSourceLocationFromCore(uint32_t pc, uint64_t *out_location, void *user)
{
    (void)user;
    if (!out_location) {
        return 0;
    }
    *out_location = 0;

    const char *elf = debugger.libretro.exePath;
    if (!elf || !*elf || !debugger.elfValid) {
        return 0;
    }
    if (!addr2line_start(elf)) {
        return 0;
    }

    char path[PATH_MAX];
    int line = 0;
    uint32_t pc24 = pc & 0x00ffffffu;
    if (!addr2line_resolve((uint64_t)pc24, path, sizeof(path), &line) && pc24 >= 2u) {
        if (!addr2line_resolve((uint64_t)(pc24 - 2u), path, sizeof(path), &line)) {
            return 0;
        }
    }
    if (!path[0] || line <= 0) {
        return 0;
    }

    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        hash ^= (uint64_t)(*p);
        hash *= 1099511628211ull;
    }
    hash ^= (uint64_t)(uint32_t)line;
    hash *= 1099511628211ull;
    *out_location = hash;
    return 1;
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

void
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

static void
debugger_captureBootSaveDirs(void)
{
    // TODO
    target_iface_t* amiga = target_amiga();  
    debugger_copyPath(amiga->bootSaveDir, sizeof(amiga->bootSaveDir), debugger.config.amiga.libretro.saveDir);
    debugger_copyPath(amiga->bootSystemDir, sizeof(amiga->bootSystemDir), debugger.config.amiga.libretro.systemDir);
    target_iface_t* neogeo = target_neogeo();      
    debugger_copyPath(neogeo->bootSaveDir, sizeof(neogeo->bootSaveDir), debugger.config.neogeo.libretro.saveDir);
    debugger_copyPath(neogeo->bootSystemDir, sizeof(neogeo->bootSystemDir), debugger.config.neogeo.libretro.systemDir);
    target_iface_t* megadrive = target_megadrive();
    debugger_copyPath(megadrive->bootSaveDir, sizeof(megadrive->bootSaveDir), debugger.config.megadrive.libretro.saveDir);
    debugger_copyPath(megadrive->bootSystemDir, sizeof(megadrive->bootSystemDir), debugger.config.megadrive.libretro.systemDir);
}

void
debugger_libretroSelectConfig(void)
{
  memset(&debugger.libretro, 0, sizeof(debugger.libretro));
  
  target->libretroSelectConfig();

  const char *corePath = target && target->defaultCorePath ? target->defaultCorePath() : NULL;
  debugger.libretro.enabled = (corePath && corePath[0] && debugger.libretro.romPath[0]) ? 1 : 0;
}

int
debugger_toolchainBuildBinary(char *out, size_t cap, const char *tool)
{
    if (!out || cap == 0 || !tool || !*tool) {
        return 0;
    }
    int written = 0;
    const char *prefix = debugger.libretro.toolchainPrefix;
    if (!prefix || !*prefix) {
        written = snprintf(out, cap, "%s", tool);
    } else {
        size_t len = strlen(prefix);
        int hasDash = (len > 0 && prefix[len - 1] == '-') ? 1 : 0;
        if (hasDash) {
            written = snprintf(out, cap, "%s%s", prefix, tool);
        } else {
            written = snprintf(out, cap, "%s-%s", prefix, tool);
        }
    }
    if (written < 0 || (size_t)written >= cap) {
        out[cap - 1] = '\0';
        return 0;
    }
    return debugger_platform_finalizeToolBinary(out, cap);
}

void
debugger_refreshElfValid(void)
{
    debugger.elfValid = 0;
    const char *rawElf = NULL;
    const char* toolchainPrefix = NULL;
    char elfPath[PATH_MAX];
    elfPath[0] = '\0';   
    target->pickElfToolchainPaths(&rawElf, &toolchainPrefix);
    debugger_copyPath(elfPath, sizeof(elfPath), rawElf);
    if (elfPath[0] && debugger_pathExistsFile(elfPath) && toolchainPrefix && toolchainPrefix[0] != 0) {
      debugger.elfValid = 1;
    }
    ui_applySourcePaneElfMode();
}

void
debugger_applyCoreOptions(void)
{
    if (target == target_neogeo() && debugger.config.neogeo.systemType[0]) {
        libretro_host_setCoreOption("geolith_system_type", debugger.config.neogeo.systemType);
    } else {
        libretro_host_setCoreOption("geolith_system_type", NULL);
    }

    target->applyCoreOptions();
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
  rom_config_saveOnExit();
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
  syntax_highlight_shutdown();
  custom_ui_shutdown();
  shader_ui_shutdown();
  memory_track_ui_shutdown();
  target_releaseUiResources();
  e9ui_shutdown();
  resource_status();  
}

static void
debugger_ctor(void)
{
  memset(e9ui, 0, sizeof(*e9ui));
  memset(&debugger, 0, sizeof(debugger));
  srand((unsigned)time(NULL));
  debugger_setArgv0();
  target_setTargetIndex(TARGET_AMIGA);
  debugger.opts.redirectStdout = E9K_DEBUG_PRINTF_STDOUT_DEFAULT;
  debugger.opts.redirectStderr = E9K_DEBUG_ERROR_STDERR_DEFAULT;
  debugger.opts.redirectGdbStdout = E9K_DEBUG_GDB_STDOUT_DEFAULT;
  debugger.opts.enableTrace = E9K_DEBUG_TRACE_ENABLE_DEFAULT;
  debugger.opts.completionListRows = 30; // default completion popup rows
  debugger.coreOptionsShowHelp = 1;
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
  debugger.config.megadrive.libretro.audioEnabled = 1;
  debugger.config.megadrive.libretro.audioBufferMs = 50;
  debugger.frameStepPending = 0;
  debugger.vblankCaptureActive = 0;  
  debugger.uiFrameCounter = 0;
  debugger.uiRefreshHz = 0;
  debugger.config.crtEnabled = 1;
  snprintf(debugger.config.neogeo.libretro.toolchainPrefix, sizeof(debugger.config.neogeo.libretro.toolchainPrefix), "m68k-neogeo-elf");
  snprintf(debugger.config.amiga.libretro.toolchainPrefix, sizeof(debugger.config.amiga.libretro.toolchainPrefix), "m68k-amigaos-");
  snprintf(debugger.config.megadrive.libretro.toolchainPrefix, sizeof(debugger.config.megadrive.libretro.toolchainPrefix), "m68k-elf");
  debugger.recordPath[0] = '\0';
  debugger.playbackPath[0] = '\0';
  smoke_test_reset(&debugger);
  debugger.cliWindowOverride = 0;
  debugger.cliWindowW = 0;
  debugger.cliWindowH = 0;
  debugger.cliDisableRollingRecord = 0;
  debugger.cliStartFullscreen = 0;
  debugger.cliHeadless = 0;
  debugger.cliWarp = 0;
  debugger.cliAudioVolume = -1;
  debugger.cliResetCfg = 0;
  debugger.cliCoreSystemOverride = 0;
  debugger.cliTargetIndex = TARGET_AMIGA;
#if E9K_ENABLE_GL_COMPOSITE
  e9ui->glCompositeEnabled = 1;
#else
  e9ui->glCompositeEnabled = 0;
#endif
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
  target_ctor();
  debugger_ctor();
  signal_installHandlers();
 
  config_loadConfig();
  debugger_captureBootSaveDirs();
  cli_parseArgs(argc, argv);
  if (cli_helpRequested()) {
    cli_printUsage(argv && argv[0] ? argv[0] : NULL);
    return 0;
  }
  if (cli_hasError()) {
    cli_printUsage(argv && argv[0] ? argv[0] : NULL);
    return 1;
  }
  if (debugger.cliResetCfg) {
    const char *path = debugger_configPath();
    if (path && *path) {
      errno = 0;
      if (remove(path) != 0 && errno != ENOENT) {
        debug_error("reset-cfg: failed to delete '%s': %s", path, strerror(errno));
        return 1;
      }
      debug_printf("reset-cfg: deleted '%s'", path);
    }
    return 2;
  }
  if (debugger.cliCoreSystemOverride) {
    target_setTargetIndex(debugger.cliTargetIndex);
  }
  if (debugger.smokeTestMode != SMOKE_TEST_MODE_NONE || ui_test_getMode() != UI_TEST_MODE_NONE ||
      debugger.cliHeadless || debugger.cliDisableRollingRecord) {
    state_buffer_setRollingPaused(1);
  }
  if (debugger.cliWarp) {
    debugger.speedMultiplier = 10;
  }
  if (!smoke_test_bootstrap(&debugger)) {
    return 1;
  }
  if (!ui_test_bootstrap()) {
    return 1;
  }
  if (ui_test_getMode() != UI_TEST_MODE_NONE) {
    config_loadConfig();
    debugger_captureBootSaveDirs();
    if (debugger.cliCoreSystemOverride) {
      target_setTargetIndex(debugger.cliTargetIndex);
    }
  }
  if (debugger.recordPath[0]) {
    input_record_setRecordPath(debugger.recordPath);
  }
  if (debugger.playbackPath[0]) {
    input_record_setPlaybackPath(debugger.playbackPath);
  }
  if (!input_record_init()) {
    ui_test_shutdown();
    smoke_test_cleanup();
    return 1;
  }

  if (!e9ui_ctor(debugger_configPath(), debugger.cliWindowOverride, debugger.cliWindowW, debugger.cliWindowH, debugger.cliHeadless)) {
    input_record_shutdown();
    ui_test_shutdown();
    smoke_test_cleanup();
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
  rom_config_loadSettingsForSelectedRom();
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
    } else if (debugger.cliAudioVolume >= 0) {
      libretro_host_setAudioVolume(debugger.cliAudioVolume);
    }
  }

  if (debugger.libretro.enabled) {
    const char *corePath = target && target->defaultCorePath ? target->defaultCorePath() : NULL;
    if (!corePath || !*corePath) {
      debug_error("libretro: missing core path for target");
      debugger.libretro.enabled = 0;
    } else {
      debugger_applyCoreOptions();
      debugger.amigaDebug.debugDma = NULL;
      if (!libretro_host_start(corePath, debugger.libretro.romPath,
                               debugger.libretro.systemDir, debugger.libretro.saveDir)) {
        debug_error("libretro: failed to start core");
        debugger.libretro.enabled = 0;
      } else {
        if (!libretro_host_setDebugSourceLocationCallback(debugger_onResolveSourceLocationFromCore, NULL)) {
	  debug_error("source_location: core does not expose e9k_debug_set_source_location_resolver");
        }
        target->validateAPI();
        if (ui_test_getMode() == UI_TEST_MODE_NONE) {
	  snapshot_loadOnBoot();
        }
        rom_config_loadRuntimeStateOnBoot();
      }
    }
  }
  if (debugger.libretro.enabled) {
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
  int uiTestExit = ui_test_getExitCode();
  int smokeExit = smoke_test_getExitCode(&debugger);
  debugger_cleanup();
  input_record_shutdown();
  ui_test_shutdown();
  smoke_test_cleanup();
  if (uiTestExit >= 0) {
    return uiTestExit;
  }
  if (smokeExit >= 0) {
    return smokeExit;
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
  return target->audioEnabled();
}

void
debugger_setAudioEnabled(int enabled)
{
  target->audioEnable(enabled);
}

uint32_t
debugger_uiTicks(void)
{
  int fps = debugger.uiRefreshHz > 0 ? debugger.uiRefreshHz : 60;
  return (uint32_t)((debugger.uiFrameCounter * 1000ull) / (uint64_t)fps);
}
