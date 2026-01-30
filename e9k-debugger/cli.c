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
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            cli_helpRequestedFlag = 1;
            return;
        }
        if (strcmp(argv[i], "--rom-folder") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.cliConfig.neogeo.romFolder, sizeof(debugger.cliConfig.neogeo.romFolder), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--rom-folder=", sizeof("--rom-folder=") - 1) == 0) {
            cli_copyPath(debugger.cliConfig.neogeo.romFolder, sizeof(debugger.cliConfig.neogeo.romFolder), argv[i] + sizeof("--rom-folder=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--elf") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.elfPath, sizeof(debugger.cliConfig.neogeo.libretro.elfPath), argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--libretro-core") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.corePath, sizeof(debugger.cliConfig.neogeo.libretro.corePath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--libretro-core=", sizeof("--libretro-core=") - 1) == 0) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.corePath, sizeof(debugger.cliConfig.neogeo.libretro.corePath), argv[i] + sizeof("--libretro-core=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--libretro-rom") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.romPath, sizeof(debugger.cliConfig.neogeo.libretro.romPath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--libretro-rom=", sizeof("--libretro-rom=") - 1) == 0) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.romPath, sizeof(debugger.cliConfig.neogeo.libretro.romPath), argv[i] + sizeof("--libretro-rom=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--libretro-system-dir") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.systemDir, sizeof(debugger.cliConfig.neogeo.libretro.systemDir), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--libretro-system-dir=", sizeof("--libretro-system-dir=") - 1) == 0) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.systemDir, sizeof(debugger.cliConfig.neogeo.libretro.systemDir), argv[i] + sizeof("--libretro-system-dir=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--libretro-save-dir") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.saveDir, sizeof(debugger.cliConfig.neogeo.libretro.saveDir), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--libretro-save-dir=", sizeof("--libretro-save-dir=") - 1) == 0) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.saveDir, sizeof(debugger.cliConfig.neogeo.libretro.saveDir), argv[i] + sizeof("--libretro-save-dir=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--source-dir") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.sourceDir, sizeof(debugger.cliConfig.neogeo.libretro.sourceDir), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--source-dir=", sizeof("--source-dir=") - 1) == 0) {
            cli_copyPath(debugger.cliConfig.neogeo.libretro.sourceDir, sizeof(debugger.cliConfig.neogeo.libretro.sourceDir), argv[i] + sizeof("--source-dir=") - 1);
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
        if (strcmp(argv[i], "--no-gl-composite") == 0) {
            e9ui->glCompositeEnabled = 0;
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
    printf("Options:\n");
    printf("  --help, -h                 Show this help and exit\n");
    printf("  --elf PATH                 ELF file path\n");
    printf("  --libretro-core PATH        Libretro core path\n");
    printf("  --libretro-rom PATH         Libretro ROM (.neo) path\n");
    printf("  --rom-folder PATH           ROM folder (generates .neo)\n");
    printf("  --libretro-system-dir PATH  Libretro system/BIOS directory\n");
    printf("  --libretro-save-dir PATH    Libretro saves directory\n");
    printf("  --source-dir PATH           Source directory\n");
    printf("  --audio-buffer-ms MS        Audio buffer in milliseconds\n");
    printf("  --window-size WxH           Initial window size override\n");
    printf("  --record PATH               Record input events to a file\n");
    printf("  --playback PATH             Replay input events from a file\n");
    printf("  --make-smoke PATH           Save frames and inputs to a folder\n");
    printf("  --smoke-test PATH           Replay inputs and compare frames\n");
    printf("  --smoke-open                Open montage on smoke-test failure\n");
    printf("  --headless                  Hide main window (useful for --smoke-test)\n");
    printf("  --warp                      Start in speed multiplier mode\n");
    printf("  --fullscreen                Start in UI fullscreen mode (ESC toggle)\n");
    printf("  --no-rolling-record         Disable rolling state recording\n");
    printf("  --no-gl-composite           Disable OpenGL composite path\n");
    printf("\n");
    printf("You can also use --option=VALUE forms for the PATH/MS options.\n");
}

void
cli_applyOverrides(void)
{
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
