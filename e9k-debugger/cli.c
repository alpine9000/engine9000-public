/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include "cli.h"
#include "debugger.h"
#include "debug.h"
#include "smoke_test.h"

static int cli_helpRequestedFlag = 0;
static int cli_errorFlag = 0;
static char cli_savedArgv0[PATH_MAX];
static int cli_resetCfgConsumed = 0;

static void
cli_copyPath(char *dest, size_t capacity, const char *src)
{
    if (!dest || capacity == 0) {
        return;
    }
    if (!src || !*src) {
        dest[0] = '\0';
        return;
    }
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            int written = snprintf(dest, capacity, "%s%s", home, src + 1);
            if (written < 0 || (size_t)written >= capacity) {
                dest[capacity - 1] = '\0';
            }
            return;
        }
    }
    strncpy(dest, src, capacity - 1);
    dest[capacity - 1] = '\0';
}

static void
cli_setError(const char *message)
{
    if (message && *message) {
        debug_error("%s", message);
    }
    cli_errorFlag = 1;
}

static debugger_system_type_t
cli_getTargetCoreSystem(int argc, char **argv)
{
    debugger_system_type_t system = debugger.config.coreSystem;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--amiga") == 0) {
            system = DEBUGGER_SYSTEM_AMIGA;
        } else if (strcmp(argv[i], "--neogeo") == 0) {
            system = DEBUGGER_SYSTEM_NEOGEO;
        }
    }
    return system;
}

static e9k_libretro_config_t *
cli_targetLibretroConfig(debugger_system_type_t system)
{
    if (system == DEBUGGER_SYSTEM_AMIGA) {
        return &debugger.cliConfig.amiga.libretro;
    }
    return &debugger.cliConfig.neogeo.libretro;
}

void
cli_setArgv0(const char *argv0)
{
    if (!argv0 || !*argv0) {
        cli_savedArgv0[0] = '\0';
        return;
    }
    strncpy(cli_savedArgv0, argv0, sizeof(cli_savedArgv0) - 1);
    cli_savedArgv0[sizeof(cli_savedArgv0) - 1] = '\0';
}

const char *
cli_getArgv0(void)
{
    return cli_savedArgv0;
}

void
cli_parseArgs(int argc, char **argv)
{
    debugger_system_type_t targetSystem = cli_getTargetCoreSystem(argc, argv);
    e9k_libretro_config_t *targetLibretro = cli_targetLibretroConfig(targetSystem);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            cli_helpRequestedFlag = 1;
            return;
        }
        if (strcmp(argv[i], "--reset-cfg") == 0) {
            if (!cli_resetCfgConsumed) {
                debugger.cliResetCfg = 1;
                cli_resetCfgConsumed = 1;
            }
            continue;
        }
        if (strcmp(argv[i], "--rom-folder") == 0) {
            if (targetSystem == DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("rom-folder: only supported for Neo Geo (use --neogeo)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("rom-folder: missing folder path");
                return;
            }
            cli_copyPath(debugger.cliConfig.neogeo.romFolder, sizeof(debugger.cliConfig.neogeo.romFolder), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--rom-folder=", sizeof("--rom-folder=") - 1) == 0) {
            if (targetSystem == DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("rom-folder: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_copyPath(debugger.cliConfig.neogeo.romFolder, sizeof(debugger.cliConfig.neogeo.romFolder), argv[i] + sizeof("--rom-folder=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--elf") == 0) {
            if (targetSystem == DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("elf: only supported for Neo Geo (use --neogeo)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("elf: missing file path");
                return;
            }
            cli_copyPath(debugger.cliConfig.neogeo.libretro.elfPath, sizeof(debugger.cliConfig.neogeo.libretro.elfPath), argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--hunk") == 0) {
            if (targetSystem != DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("hunk: only supported for Amiga (use --amiga)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("hunk: missing file path");
                return;
            }
            cli_copyPath(debugger.cliConfig.amiga.libretro.elfPath, sizeof(debugger.cliConfig.amiga.libretro.elfPath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--hunk=", sizeof("--hunk=") - 1) == 0) {
            if (targetSystem != DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("hunk: only supported for Amiga (use --amiga)");
                return;
            }
            if (argv[i][sizeof("--hunk=") - 1] == '\0') {
                cli_setError("hunk: missing file path");
                return;
            }
            cli_copyPath(debugger.cliConfig.amiga.libretro.elfPath, sizeof(debugger.cliConfig.amiga.libretro.elfPath), argv[i] + sizeof("--hunk=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            cli_copyPath(targetLibretro->corePath, sizeof(targetLibretro->corePath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--core=", sizeof("--core=") - 1) == 0) {
            cli_copyPath(targetLibretro->corePath, sizeof(targetLibretro->corePath), argv[i] + sizeof("--core=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--rom") == 0) {
            if (targetSystem == DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("rom: only supported for Neo Geo (use --neogeo)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("rom: missing rom path");
                return;
            }
            cli_copyPath(debugger.cliConfig.neogeo.libretro.romPath, sizeof(debugger.cliConfig.neogeo.libretro.romPath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--rom=", sizeof("--rom=") - 1) == 0) {
            if (targetSystem == DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("rom: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_copyPath(debugger.cliConfig.neogeo.libretro.romPath, sizeof(debugger.cliConfig.neogeo.libretro.romPath), argv[i] + sizeof("--rom=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--uae") == 0) {
            if (targetSystem != DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("uae: only supported for Amiga (use --amiga)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("uae: missing file path");
                return;
            }
            cli_copyPath(debugger.cliConfig.amiga.libretro.romPath, sizeof(debugger.cliConfig.amiga.libretro.romPath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--uae=", sizeof("--uae=") - 1) == 0) {
            if (targetSystem != DEBUGGER_SYSTEM_AMIGA) {
                cli_setError("uae: only supported for Amiga (use --amiga)");
                return;
            }
            if (argv[i][sizeof("--uae=") - 1] == '\0') {
                cli_setError("uae: missing file path");
                return;
            }
            cli_copyPath(debugger.cliConfig.amiga.libretro.romPath, sizeof(debugger.cliConfig.amiga.libretro.romPath), argv[i] + sizeof("--uae=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--system-dir") == 0 && i + 1 < argc) {
            cli_copyPath(targetLibretro->systemDir, sizeof(targetLibretro->systemDir), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--system-dir=", sizeof("--system-dir=") - 1) == 0) {
            cli_copyPath(targetLibretro->systemDir, sizeof(targetLibretro->systemDir), argv[i] + sizeof("--system-dir=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc) {
            cli_copyPath(targetLibretro->saveDir, sizeof(targetLibretro->saveDir), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--save-dir=", sizeof("--save-dir=") - 1) == 0) {
            cli_copyPath(targetLibretro->saveDir, sizeof(targetLibretro->saveDir), argv[i] + sizeof("--save-dir=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--source-dir") == 0 && i + 1 < argc) {
            cli_copyPath(targetLibretro->sourceDir, sizeof(targetLibretro->sourceDir), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--source-dir=", sizeof("--source-dir=") - 1) == 0) {
            cli_copyPath(targetLibretro->sourceDir, sizeof(targetLibretro->sourceDir), argv[i] + sizeof("--source-dir=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--audio-buffer-ms") == 0 && i + 1 < argc) {
            char *end = NULL;
            long ms = strtol(argv[++i], &end, 10);
            if (end && end != argv[i] && ms > 0 && ms <= INT_MAX) {
                debugger.cliConfig.neogeo.libretro.audioBufferMs = (int)ms;
            }
            continue;
        }
        if (strncmp(argv[i], "--audio-buffer-ms=", sizeof("--audio-buffer-ms=") - 1) == 0) {
            const char *val = argv[i] + sizeof("--audio-buffer-ms=") - 1;
            char *end = NULL;
            long ms = strtol(val, &end, 10);
            if (end && end != val && ms > 0 && ms <= INT_MAX) {
                debugger.cliConfig.neogeo.libretro.audioBufferMs = (int)ms;
            }
            continue;
        }
        if (strcmp(argv[i], "--window-size") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            const char *sep = strchr(arg, 'x');
            if (!sep) {
                sep = strchr(arg, 'X');
            }
            if (sep) {
                char *end = NULL;
                long w = strtol(arg, &end, 10);
                long h = strtol(sep + 1, NULL, 10);
                if (end && end == sep && w > 0 && h > 0 && w <= INT_MAX && h <= INT_MAX) {
                    debugger.cliWindowOverride = 1;
                    debugger.cliWindowW = (int)w;
                    debugger.cliWindowH = (int)h;
                }
            } else if (i + 1 < argc) {
                char *endW = NULL;
                char *endH = NULL;
                long w = strtol(arg, &endW, 10);
                long h = strtol(argv[i + 1], &endH, 10);
                if (endW && *endW == '\0' && endH && *endH == '\0' &&
                    w > 0 && h > 0 && w <= INT_MAX && h <= INT_MAX) {
                    debugger.cliWindowOverride = 1;
                    debugger.cliWindowW = (int)w;
                    debugger.cliWindowH = (int)h;
                    i++;
                }
            }
            continue;
        }
        if (strncmp(argv[i], "--window-size=", sizeof("--window-size=") - 1) == 0) {
            const char *arg = argv[i] + sizeof("--window-size=") - 1;
            const char *sep = strchr(arg, 'x');
            if (!sep) {
                sep = strchr(arg, 'X');
            }
            if (sep) {
                char *end = NULL;
                long w = strtol(arg, &end, 10);
                long h = strtol(sep + 1, NULL, 10);
                if (end && end == sep && w > 0 && h > 0 && w <= INT_MAX && h <= INT_MAX) {
                    debugger.cliWindowOverride = 1;
                    debugger.cliWindowW = (int)w;
                    debugger.cliWindowH = (int)h;
                }
            }
            continue;
        }
        if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.recordPath, sizeof(debugger.recordPath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--record=", sizeof("--record=") - 1) == 0) {
            cli_copyPath(debugger.recordPath, sizeof(debugger.recordPath), argv[i] + sizeof("--record=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--playback") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.playbackPath, sizeof(debugger.playbackPath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--playback=", sizeof("--playback=") - 1) == 0) {
            cli_copyPath(debugger.playbackPath, sizeof(debugger.playbackPath), argv[i] + sizeof("--playback=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--make-smoke") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.smokeTestPath, sizeof(debugger.smokeTestPath), argv[++i]);
            debugger.smokeTestMode = SMOKE_TEST_MODE_RECORD;
            continue;
        }
        if (strcmp(argv[i], "--make-smoke") == 0) {
            cli_setError("make-smoke: missing folder path");
            return;
        }
        if (strncmp(argv[i], "--make-smoke=", sizeof("--make-smoke=") - 1) == 0) {
            if (argv[i][sizeof("--make-smoke=") - 1] == '\0') {
                cli_setError("make-smoke: missing folder path");
                return;
            }
            cli_copyPath(debugger.smokeTestPath, sizeof(debugger.smokeTestPath),
                         argv[i] + sizeof("--make-smoke=") - 1);
            debugger.smokeTestMode = SMOKE_TEST_MODE_RECORD;
            continue;
        }
        if (strcmp(argv[i], "--smoke-test") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.smokeTestPath, sizeof(debugger.smokeTestPath), argv[++i]);
            debugger.smokeTestMode = SMOKE_TEST_MODE_COMPARE;
            continue;
        }
        if (strcmp(argv[i], "--smoke-test") == 0) {
            cli_setError("smoke-test: missing folder path");
            return;
        }
        if (strncmp(argv[i], "--smoke-test=", sizeof("--smoke-test=") - 1) == 0) {
            if (argv[i][sizeof("--smoke-test=") - 1] == '\0') {
                cli_setError("smoke-test: missing folder path");
                return;
            }
            cli_copyPath(debugger.smokeTestPath, sizeof(debugger.smokeTestPath),
                         argv[i] + sizeof("--smoke-test=") - 1);
            debugger.smokeTestMode = SMOKE_TEST_MODE_COMPARE;
            continue;
        }
        if (strcmp(argv[i], "--smoke-open") == 0) {
            debugger.smokeTestOpenOnFail = 1;
            continue;
        }
        if (strcmp(argv[i], "--amiga") == 0) {
            debugger.cliCoreSystemOverride = 1;
            debugger.cliCoreSystem = DEBUGGER_SYSTEM_AMIGA;
            continue;
        }
        if (strcmp(argv[i], "--neogeo") == 0) {
            debugger.cliCoreSystemOverride = 1;
            debugger.cliCoreSystem = DEBUGGER_SYSTEM_NEOGEO;
            continue;
        }
        if (strcmp(argv[i], "--headless") == 0) {
            debugger.cliHeadless = 1;
            continue;
        }
        if (strcmp(argv[i], "--warp") == 0) {
            debugger.cliWarp = 1;
            continue;
        }
        if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "--start-fullscreen") == 0) {
            debugger.cliStartFullscreen = 1;
            continue;
        }
        if (strcmp(argv[i], "--no-rolling-record") == 0) {
            debugger.cliDisableRollingRecord = 1;
            continue;
        }
        {
            char msg[256];
            if (argv[i] && argv[i][0] == '-') {
                snprintf(msg, sizeof(msg), "unknown option: %s", argv[i]);
            } else {
                snprintf(msg, sizeof(msg), "unexpected argument: %s", argv[i] ? argv[i] : "(null)");
            }
            msg[sizeof(msg) - 1] = '\0';
            cli_setError(msg);
            return;
        }
    }
}

int
cli_helpRequested(void)
{
    return cli_helpRequestedFlag;
}

int
cli_hasError(void)
{
    return cli_errorFlag;
}

void
cli_printUsage(const char *argv0)
{
    const char *prog = argv0 && *argv0 ? argv0 : "e9k-debugger";
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Global options:\n");
    printf("  --help, -h                   Show this help and exit\n");
    printf("  --reset-cfg                  Delete saved config file and restart\n");
    printf("  --core PATH                  Core path (applies to current system)\n");
    printf("  --system-dir PATH            System/BIOS directory (applies to current system)\n");
    printf("  --save-dir PATH              Saves directory (applies to current system)\n");
    printf("  --source-dir PATH            Source directory (applies to current system)\n");
    printf("  --audio-buffer-ms MS         Audio buffer in milliseconds\n");
    printf("  --window-size WxH            Initial window size override\n");
    printf("  --record PATH                Record input events to a file\n");
    printf("  --playback PATH              Replay input events from a file\n");
    printf("  --make-smoke PATH            Save frames and inputs to a folder\n");
    printf("  --smoke-test PATH            Replay inputs and compare frames\n");
    printf("  --smoke-open                 Open montage on smoke-test failure\n");
    printf("  --headless                   Hide main window (useful for --smoke-test)\n");
    printf("  --warp                       Start in speed multiplier mode\n");
    printf("  --fullscreen                 Start in UI fullscreen mode (ESC toggle)\n");
    printf("  --no-rolling-record          Disable rolling state recording\n");
    printf("\n");
    printf("Neo Geo options (use with --neogeo):\n");
    printf("  --neogeo                     Start in Neo Geo system mode\n");    
    printf("  --elf PATH                   ELF file path\n");
    printf("  --rom PATH                   Neo Geo ROM (.neo) path\n");
    printf("  --rom-folder PATH            ROM folder (generates a .neo)\n");
    printf("\n");
    printf("Amiga options (use with --amiga):\n");
    printf("  --amiga                      Start in Amiga system mode\n");    
    printf("  --hunk PATH                  Amiga debug binary (hunk) path\n");
    printf("  --uae PATH                   Amiga UAE config (.uae) path\n");
    printf("\n");
    printf("You can also use --option=VALUE forms for the PATH/MS options.\n");
}

void
cli_applyOverrides(void)
{
    if (debugger.cliConfig.amiga.libretro.corePath[0]) {
        cli_copyPath(debugger.config.amiga.libretro.corePath, sizeof(debugger.config.amiga.libretro.corePath), debugger.cliConfig.amiga.libretro.corePath);
    }
    if (debugger.cliConfig.amiga.libretro.romPath[0]) {
        cli_copyPath(debugger.config.amiga.libretro.romPath, sizeof(debugger.config.amiga.libretro.romPath), debugger.cliConfig.amiga.libretro.romPath);
    }
    if (debugger.cliConfig.amiga.libretro.elfPath[0]) {
        cli_copyPath(debugger.config.amiga.libretro.elfPath, sizeof(debugger.config.amiga.libretro.elfPath), debugger.cliConfig.amiga.libretro.elfPath);
    }
    if (debugger.cliConfig.amiga.libretro.systemDir[0]) {
        cli_copyPath(debugger.config.amiga.libretro.systemDir, sizeof(debugger.config.amiga.libretro.systemDir), debugger.cliConfig.amiga.libretro.systemDir);
    }
    if (debugger.cliConfig.amiga.libretro.saveDir[0]) {
        cli_copyPath(debugger.config.amiga.libretro.saveDir, sizeof(debugger.config.amiga.libretro.saveDir), debugger.cliConfig.amiga.libretro.saveDir);
    }
    if (debugger.cliConfig.amiga.libretro.sourceDir[0]) {
        cli_copyPath(debugger.config.amiga.libretro.sourceDir, sizeof(debugger.config.amiga.libretro.sourceDir), debugger.cliConfig.amiga.libretro.sourceDir);
    }

    if (debugger.cliConfig.neogeo.libretro.corePath[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.corePath, sizeof(debugger.config.neogeo.libretro.corePath), debugger.cliConfig.neogeo.libretro.corePath);
    }
    if (debugger.cliConfig.neogeo.libretro.romPath[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.romPath, sizeof(debugger.config.neogeo.libretro.romPath), debugger.cliConfig.neogeo.libretro.romPath);
    }
    if (debugger.cliConfig.neogeo.romFolder[0]) {
        cli_copyPath(debugger.config.neogeo.romFolder, sizeof(debugger.config.neogeo.romFolder), debugger.cliConfig.neogeo.romFolder);
        debugger.config.neogeo.libretro.romPath[0] = '\0';
    }
    if (debugger.cliConfig.neogeo.libretro.elfPath[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.elfPath, sizeof(debugger.config.neogeo.libretro.elfPath), debugger.cliConfig.neogeo.libretro.elfPath);
    }
    if (debugger.cliConfig.neogeo.libretro.systemDir[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.systemDir, sizeof(debugger.config.neogeo.libretro.systemDir), debugger.cliConfig.neogeo.libretro.systemDir);
    }
    if (debugger.cliConfig.neogeo.libretro.saveDir[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.saveDir, sizeof(debugger.config.neogeo.libretro.saveDir), debugger.cliConfig.neogeo.libretro.saveDir);
    }
    if (debugger.cliConfig.neogeo.libretro.sourceDir[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.sourceDir, sizeof(debugger.config.neogeo.libretro.sourceDir), debugger.cliConfig.neogeo.libretro.sourceDir);
    }
    if (debugger.cliConfig.neogeo.libretro.audioBufferMs > 0) {
        debugger.config.neogeo.libretro.audioBufferMs = debugger.cliConfig.neogeo.libretro.audioBufferMs;
    }
}
