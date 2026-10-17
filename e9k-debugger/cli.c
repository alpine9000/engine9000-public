/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "cli.h"
#include "debugger.h"
#include "debug.h"
#include "smoke_test.h"
#include "strutil.h"
#include "ui_test.h"

static int cli_helpRequestedFlag = 0;
static int cli_errorFlag = 0;
static char cli_savedArgv0[PATH_MAX];
static int cli_resetCfgConsumed = 0;
static int cli_clearRestartArgs = 0;
static ui_test_mode_t cli_restartTestMode;
static char cli_restartTestFolder[PATH_MAX];
static int cli_restartHeadless;
static int cli_restartOpenOnFail;

static void
cli_registerUiTest(const char *folder, ui_test_mode_t mode)
{
    cli_restartTestMode = mode;
    strutil_strlcpy(cli_restartTestFolder, sizeof(cli_restartTestFolder), folder);
    ui_test_registerRequestedMode(folder, mode);
}

int
cli_buildRestartArgs(char **restartArgv)
{
    int count = 0;
    restartArgv[count++] = cli_savedArgv0;
    if (cli_restartTestMode != UI_TEST_MODE_NONE) {
        switch (cli_restartTestMode) {
        case UI_TEST_MODE_RECORD:
            restartArgv[count++] = "--make-test";
            break;
        case UI_TEST_MODE_REMAKE:
            restartArgv[count++] = "--remake-test";
            break;
        case UI_TEST_MODE_COMPARE:
            restartArgv[count++] = "--test";
            break;
        default:
            break;
        }
        restartArgv[count++] = cli_restartTestFolder;
        if (cli_restartHeadless) {
            restartArgv[count++] = "--headless";
        }
        if (cli_restartOpenOnFail) {
            restartArgv[count++] = "--test-auto-open-fail";
        }
    }
    restartArgv[count] = NULL;
    return count;
}


static void
cli_copyPath(char *dest, size_t capacity, const char *src)
{
    char expanded[PATH_MAX];
    char cwd[PATH_MAX];

    if (!dest || capacity == 0) {
        return;
    }
    if (!src || !*src) {
        dest[0] = '\0';
        return;
    }

    expanded[0] = '\0';
    if (!debugger_platform_expandHomePath(expanded, sizeof(expanded), src)) {
        strutil_strlcpy(dest, capacity, src);
        debugger_platform_normalizePathSeparators(dest);
        return;
    }
    if (debugger_platform_pathIsAbsolute(expanded)) {
        strutil_strlcpy(dest, capacity, expanded);
        debugger_platform_normalizePathSeparators(dest);
        return;
    }

    cwd[0] = '\0';
    if (debugger_platform_getCurrentDir(cwd, sizeof(cwd)) &&
        debugger_platform_pathJoin(dest, capacity, cwd, expanded)) {
        return;
    }

    strutil_strlcpy(dest, capacity, expanded);
    debugger_platform_normalizePathSeparators(dest);
}

static void
cli_setError(const char *message)
{
    if (message && *message) {
        debug_error("%s", message);
    }
    cli_errorFlag = 1;
}

static int
cli_parseUint32(const char *value, uint32_t *out)
{
    if (!value || !*value || !out) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 0);
    if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX) {
        return 0;
    }
    *out = (uint32_t)parsed;
    return 1;
}

static int
cli_addDebugArg(const char *value)
{
    if (debugger.debugArgCount >= 10) {
        cli_setError("debug-arg: maximum is 10 values");
        return 0;
    }
    uint32_t parsed = 0;
    if (!cli_parseUint32(value, &parsed)) {
        cli_setError("debug-arg: value must be a 32-bit integer");
        return 0;
    }
    debugger.debugArgs[debugger.debugArgCount++] = parsed;
    return 1;
}

static void
cli_setNeogeoSystemType(const char *value)
{
    strutil_strlcpy(debugger.cliConfig.neogeo.systemType,
                    sizeof(debugger.cliConfig.neogeo.systemType),
                    value);
}

static void
cli_setNeogeoCdSystemType(const char *value)
{
    strutil_strlcpy(debugger.cliConfig.neogeo.cdSystemType,
                    sizeof(debugger.cliConfig.neogeo.cdSystemType),
                    value);
}

static int
cli_uiTestSessionConfigExists(const char *folder, char *outCfgPath, size_t outCap)
{
    if (!folder || !*folder || !outCfgPath || outCap == 0) {
        return 0;
    }
    if (!debugger_platform_pathJoin(outCfgPath, outCap, folder, ".e9k-debugger.cfg")) {
        outCfgPath[0] = '\0';
        return 0;
    }
    struct stat st;
    if (stat(outCfgPath, &st) != 0) {
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    return 1;
}

static const target_iface_t*
cli_getTargetCoreSystem(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--amiga") == 0) {
            return target_amiga();
#if E9K_ENABLE_NEOGEO
        } else if (strcmp(argv[i], "--neogeo") == 0) {
            return target_neogeo();
#endif
#if E9K_ENABLE_MEGADRIVE
        } else if (strcmp(argv[i], "--megadrive") == 0) {
            return target_megadrive();
#endif
        }
    }
    return target_firstEnabled();
}


void
cli_setArgv0(const char *argv0)
{
    if (!argv0 || !*argv0) {
        cli_savedArgv0[0] = '\0';
        return;
    }
    strutil_strlcpy(cli_savedArgv0, sizeof(cli_savedArgv0), argv0);
}

const char *
cli_getArgv0(void)
{
    return cli_savedArgv0;
}

void
cli_resetConfigOverrides(void)
{
    memset(&debugger.cliConfig, 0, sizeof(debugger.cliConfig));
    debugger.cliCoreSystemOverride = 0;
    cli_clearRestartArgs = 1;
}

int
cli_shouldClearRestartArgs(void)
{
    return cli_clearRestartArgs ? 1 : 0;
}

void
cli_parseArgs(int argc, char **argv)
{
    cli_restartTestMode = UI_TEST_MODE_NONE;
    cli_restartHeadless = 0;
    cli_restartOpenOnFail = 0;
#if !E9K_ENABLE_AMIGA
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--amiga") == 0) {
            cli_setError("amiga: disabled in this build");
            return;
        }
    }
#endif
#if !E9K_ENABLE_NEOGEO
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--neogeo") == 0) {
            cli_setError("neogeo: disabled in this build");
            return;
        }
    }
#endif
#if !E9K_ENABLE_MEGADRIVE
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--megadrive") == 0) {
            cli_setError("megadrive: disabled in this build");
            return;
        }
    }
#endif
    const target_iface_t* targetSystem = cli_getTargetCoreSystem(argc, argv);
    e9k_libretro_config_t *targetLibretro = targetSystem ? targetSystem->getLibretroCliConfig() : NULL;
    if (!targetLibretro) {
        cli_setError("internal: missing target libretro config");
        return;
    }

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
            if (targetSystem != target_neogeo()) {
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
            if (targetSystem != target_neogeo()) {
                cli_setError("rom-folder: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_copyPath(debugger.cliConfig.neogeo.romFolder, sizeof(debugger.cliConfig.neogeo.romFolder), argv[i] + sizeof("--rom-folder=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--aes") == 0) {
            if (targetSystem != target_neogeo()) {
                cli_setError("aes: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_setNeogeoSystemType("aes");
            continue;
        }
        if (strcmp(argv[i], "--mvs") == 0) {
            if (targetSystem != target_neogeo()) {
                cli_setError("mvs: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_setNeogeoSystemType("mvs");
            continue;
        }
        if (strcmp(argv[i], "--uni") == 0) {
            if (targetSystem != target_neogeo()) {
                cli_setError("uni: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_setNeogeoSystemType("uni");
            continue;
        }
        if (strcmp(argv[i], "--cdz") == 0) {
            if (targetSystem != target_neogeo()) {
                cli_setError("cdz: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_setNeogeoCdSystemType("cdz");
            continue;
        }
        if (strcmp(argv[i], "--cd-front") == 0) {
            if (targetSystem != target_neogeo()) {
                cli_setError("cd-front: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_setNeogeoCdSystemType("cd_front");
            continue;
        }
        if (strcmp(argv[i], "--cd-top") == 0) {
            if (targetSystem != target_neogeo()) {
                cli_setError("cd-top: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_setNeogeoCdSystemType("cd_top");
            continue;
        }
        if (strcmp(argv[i], "--cdz-uni") == 0) {
            if (targetSystem != target_neogeo()) {
                cli_setError("cdz-uni: only supported for Neo Geo (use --neogeo)");
                return;
            }
            cli_setNeogeoCdSystemType("cdz_unibios");
            continue;
        }
        if (strcmp(argv[i], "--elf") == 0) {
            if (targetSystem == target_amiga()) {
                cli_setError("elf: only supported for Neo Geo or Mega Drive (use --neogeo or --megadrive)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("elf: missing file path");
                return;
            }
            cli_copyPath(targetLibretro->exePath, sizeof(targetLibretro->exePath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--elf=", sizeof("--elf=") - 1) == 0) {
            if (targetSystem == target_amiga()) {
                cli_setError("elf: only supported for Neo Geo or Mega Drive (use --neogeo or --megadrive)");
                return;
            }
            if (argv[i][sizeof("--elf=") - 1] == '\0') {
                cli_setError("elf: missing file path");
                return;
            }
            cli_copyPath(targetLibretro->exePath, sizeof(targetLibretro->exePath), argv[i] + sizeof("--elf=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--symbols") == 0) {
            if (i + 1 >= argc) {
                cli_setError("symbols: missing file path");
                return;
            }
            cli_copyPath(targetLibretro->exePath, sizeof(targetLibretro->exePath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--symbols=", sizeof("--symbols=") - 1) == 0) {
            if (argv[i][sizeof("--symbols=") - 1] == '\0') {
                cli_setError("symbols: missing file path");
                return;
            }
            cli_copyPath(targetLibretro->exePath, sizeof(targetLibretro->exePath), argv[i] + sizeof("--symbols=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--hunk") == 0) {
	     if (targetSystem != target_amiga()) {
                cli_setError("hunk: only supported for Amiga (use --amiga)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("hunk: missing file path");
                return;
            }
            cli_copyPath(debugger.cliConfig.amiga.libretro.exePath, sizeof(debugger.cliConfig.amiga.libretro.exePath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--hunk=", sizeof("--hunk=") - 1) == 0) {
            if (targetSystem != target_amiga()) {
                cli_setError("hunk: only supported for Amiga (use --amiga)");
                return;
            }
            if (argv[i][sizeof("--hunk=") - 1] == '\0') {
                cli_setError("hunk: missing file path");
                return;
            }
            cli_copyPath(debugger.cliConfig.amiga.libretro.exePath, sizeof(debugger.cliConfig.amiga.libretro.exePath), argv[i] + sizeof("--hunk=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--rom") == 0) {
            if (targetSystem == target_amiga()) {
                cli_setError("rom: only supported for Neo Geo and Mega Drive (use --neogeo or --megadrive)");
                return;
            }
            if (i + 1 >= argc) {
                cli_setError("rom: missing rom path");
                return;
            }
            cli_copyPath(targetLibretro->romPath, sizeof(targetLibretro->romPath), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--rom=", sizeof("--rom=") - 1) == 0) {
            if (targetSystem == target_amiga()) {
                cli_setError("rom: only supported for Neo Geo and Mega Drive (use --neogeo or --megadrive)");
                return;
            }
            cli_copyPath(targetLibretro->romPath, sizeof(targetLibretro->romPath), argv[i] + sizeof("--rom=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--uae") == 0) {
            if (targetSystem != target_amiga()) {
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
            if (targetSystem != target_amiga()) {
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
        if (strcmp(argv[i], "--toolchain-prefix") == 0) {
            if (i + 1 >= argc) {
                cli_setError("toolchain-prefix: missing value");
                return;
            }
            strutil_strlcpy(targetLibretro->toolchainPrefix, sizeof(targetLibretro->toolchainPrefix), argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--toolchain-prefix=", sizeof("--toolchain-prefix=") - 1) == 0) {
            if (argv[i][sizeof("--toolchain-prefix=") - 1] == '\0') {
                cli_setError("toolchain-prefix: missing value");
                return;
            }
            strutil_strlcpy(targetLibretro->toolchainPrefix,
                            sizeof(targetLibretro->toolchainPrefix),
                            argv[i] + sizeof("--toolchain-prefix=") - 1);
            continue;
        }
        if (strcmp(argv[i], "--audio-buffer-ms") == 0 && i + 1 < argc) {
            char *end = NULL;
            long ms = strtol(argv[++i], &end, 10);
            if (end && end != argv[i] && ms > 0 && ms <= INT_MAX) {
                targetLibretro->audioBufferMs = (int)ms;
            }
            continue;
        }
        if (strncmp(argv[i], "--audio-buffer-ms=", sizeof("--audio-buffer-ms=") - 1) == 0) {
            const char *val = argv[i] + sizeof("--audio-buffer-ms=") - 1;
            char *end = NULL;
            long ms = strtol(val, &end, 10);
            if (end && end != val && ms > 0 && ms <= INT_MAX) {
                targetLibretro->audioBufferMs = (int)ms;
            }
            continue;
        }
        if (strcmp(argv[i], "--volume") == 0) {
            if (i + 1 >= argc) {
                cli_setError("volume: missing value");
                return;
            }
            char *end = NULL;
            long volume = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || volume < 0 || volume > 100) {
                cli_setError("volume: value must be in range 0..100");
                return;
            }
            debugger.cliAudioVolume = (int)volume;
            continue;
        }
        if (strncmp(argv[i], "--volume=", sizeof("--volume=") - 1) == 0) {
            const char *value = argv[i] + sizeof("--volume=") - 1;
            if (!value[0]) {
                cli_setError("volume: missing value");
                return;
            }
            char *end = NULL;
            long volume = strtol(value, &end, 10);
            if (!end || *end != '\0' || volume < 0 || volume > 100) {
                cli_setError("volume: value must be in range 0..100");
                return;
            }
            debugger.cliAudioVolume = (int)volume;
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
        if (strcmp(argv[i], "--remake-smoke") == 0 && i + 1 < argc) {
            cli_copyPath(debugger.smokeTestPath, sizeof(debugger.smokeTestPath), argv[++i]);
            debugger.smokeTestMode = SMOKE_TEST_MODE_REMAKE;
            continue;
        }
        if (strcmp(argv[i], "--remake-smoke") == 0) {
            cli_setError("remake-smoke: missing folder path");
            return;
        }
        if (strncmp(argv[i], "--remake-smoke=", sizeof("--remake-smoke=") - 1) == 0) {
            if (argv[i][sizeof("--remake-smoke=") - 1] == '\0') {
                cli_setError("remake-smoke: missing folder path");
                return;
            }
            cli_copyPath(debugger.smokeTestPath, sizeof(debugger.smokeTestPath),
                         argv[i] + sizeof("--remake-smoke=") - 1);
            debugger.smokeTestMode = SMOKE_TEST_MODE_REMAKE;
            continue;
        }
        if (strcmp(argv[i], "--smoke-start-on-write") == 0) {
            debugger.smokeTestStartOnWrite = 1;
            continue;
        }
        if (strcmp(argv[i], "--profile-start-on-write") == 0) {
            debugger.profileStartOnWrite = 1;
            continue;
        }
        if (strcmp(argv[i], "--debug-arg") == 0) {
            if (i + 1 >= argc) {
                cli_setError("debug-arg: missing value");
                return;
            }
            if (!cli_addDebugArg(argv[++i])) {
                return;
            }
            continue;
        }
        if (strncmp(argv[i], "--debug-arg=", sizeof("--debug-arg=") - 1) == 0) {
            const char *value = argv[i] + sizeof("--debug-arg=") - 1;
            if (!value[0]) {
                cli_setError("debug-arg: missing value");
                return;
            }
            if (!cli_addDebugArg(value)) {
                return;
            }
            continue;
        }
        if (strcmp(argv[i], "--smoke-threshold") == 0) {
            if (i + 1 >= argc) {
                cli_setError("smoke-threshold: missing value");
                return;
            }
            char *end = NULL;
            long threshold = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || threshold <= 0 || threshold > INT_MAX) {
                cli_setError("smoke-threshold: value must be greater than 0");
                return;
            }
            debugger.smokeTestThreshold = (int)threshold;
            continue;
        }
        if (strncmp(argv[i], "--smoke-threshold=", sizeof("--smoke-threshold=") - 1) == 0) {
            const char *value = argv[i] + sizeof("--smoke-threshold=") - 1;
            if (!value[0]) {
                cli_setError("smoke-threshold: missing value");
                return;
            }
            char *end = NULL;
            long threshold = strtol(value, &end, 10);
            if (!end || *end != '\0' || threshold <= 0 || threshold > INT_MAX) {
                cli_setError("smoke-threshold: value must be greater than 0");
                return;
            }
            debugger.smokeTestThreshold = (int)threshold;
            continue;
        }
        if (strcmp(argv[i], "--make-test") == 0 && i + 1 < argc) {
            char folder[PATH_MAX];
            folder[0] = '\0';
            cli_copyPath(folder, sizeof(folder), argv[++i]);
            cli_registerUiTest(folder, UI_TEST_MODE_RECORD);
            continue;
        }
        if (strcmp(argv[i], "--make-test") == 0) {
            cli_setError("make-test: missing folder path");
            return;
        }
        if (strncmp(argv[i], "--make-test=", sizeof("--make-test=") - 1) == 0) {
            if (argv[i][sizeof("--make-test=") - 1] == '\0') {
                cli_setError("make-test: missing folder path");
                return;
            }
            char folder[PATH_MAX];
            folder[0] = '\0';
            cli_copyPath(folder, sizeof(folder), argv[i] + sizeof("--make-test=") - 1);
            cli_registerUiTest(folder, UI_TEST_MODE_RECORD);
            continue;
        }
        if (strcmp(argv[i], "--remake-test") == 0 && i + 1 < argc) {
            char folder[PATH_MAX];
            folder[0] = '\0';
            cli_copyPath(folder, sizeof(folder), argv[++i]);
            char cfgPath[PATH_MAX];
            cfgPath[0] = '\0';
            if (!cli_uiTestSessionConfigExists(folder, cfgPath, sizeof(cfgPath))) {
                char msg[PATH_MAX + 128];
                snprintf(msg, sizeof(msg), "remake-test: missing %s (create with --make-test first)", cfgPath[0] ? cfgPath : ".e9k-debugger.cfg");
                msg[sizeof(msg) - 1] = '\0';
                cli_setError(msg);
                return;
            }
            cli_registerUiTest(folder, UI_TEST_MODE_REMAKE);
            continue;
        }
        if (strcmp(argv[i], "--remake-test") == 0) {
            cli_setError("remake-test: missing folder path");
            return;
        }
        if (strncmp(argv[i], "--remake-test=", sizeof("--remake-test=") - 1) == 0) {
            if (argv[i][sizeof("--remake-test=") - 1] == '\0') {
                cli_setError("remake-test: missing folder path");
                return;
            }
            char folder[PATH_MAX];
            folder[0] = '\0';
            cli_copyPath(folder, sizeof(folder), argv[i] + sizeof("--remake-test=") - 1);
            char cfgPath[PATH_MAX];
            cfgPath[0] = '\0';
            if (!cli_uiTestSessionConfigExists(folder, cfgPath, sizeof(cfgPath))) {
                char msg[PATH_MAX + 128];
                snprintf(msg, sizeof(msg), "remake-test: missing %s (create with --make-test first)", cfgPath[0] ? cfgPath : ".e9k-debugger.cfg");
                msg[sizeof(msg) - 1] = '\0';
                cli_setError(msg);
                return;
            }
            cli_registerUiTest(folder, UI_TEST_MODE_REMAKE);
            continue;
        }
        if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            char folder[PATH_MAX];
            folder[0] = '\0';
            cli_copyPath(folder, sizeof(folder), argv[++i]);
            char cfgPath[PATH_MAX];
            cfgPath[0] = '\0';
            if (!cli_uiTestSessionConfigExists(folder, cfgPath, sizeof(cfgPath))) {
                char msg[PATH_MAX + 128];
                snprintf(msg, sizeof(msg), "test: missing %s (create with --make-test first)", cfgPath[0] ? cfgPath : ".e9k-debugger.cfg");
                msg[sizeof(msg) - 1] = '\0';
                cli_setError(msg);
                return;
            }
            cli_registerUiTest(folder, UI_TEST_MODE_COMPARE);
            continue;
        }
        if (strcmp(argv[i], "--test") == 0) {
            cli_setError("test: missing folder path");
            return;
        }
        if (strncmp(argv[i], "--test=", sizeof("--test=") - 1) == 0) {
            if (argv[i][sizeof("--test=") - 1] == '\0') {
                cli_setError("test: missing folder path");
                return;
            }
            char folder[PATH_MAX];
            folder[0] = '\0';
            cli_copyPath(folder, sizeof(folder), argv[i] + sizeof("--test=") - 1);
            char cfgPath[PATH_MAX];
            cfgPath[0] = '\0';
            if (!cli_uiTestSessionConfigExists(folder, cfgPath, sizeof(cfgPath))) {
                char msg[PATH_MAX + 128];
                snprintf(msg, sizeof(msg), "test: missing %s (create with --make-test first)", cfgPath[0] ? cfgPath : ".e9k-debugger.cfg");
                msg[sizeof(msg) - 1] = '\0';
                cli_setError(msg);
                return;
            }
            cli_registerUiTest(folder, UI_TEST_MODE_COMPARE);
            continue;
        }
        if (strcmp(argv[i], "--smoke-open") == 0) {
            debugger.smokeTestOpenOnFail = 1;
            continue;
        }
        if (strcmp(argv[i], "--test-auto-open-fail") == 0) {
            cli_restartOpenOnFail = 1;
            ui_test_setOpenOnFail(1);
            continue;
        }
        if (strcmp(argv[i], "--amiga") == 0) {
#if E9K_ENABLE_AMIGA
            debugger.cliCoreSystemOverride = 1;
            debugger.cliTargetIndex = TARGET_AMIGA;
            continue;
#else
            cli_setError("amiga: disabled in this build");
            return;
#endif
        }
        if (strcmp(argv[i], "--neogeo") == 0) {
#if E9K_ENABLE_NEOGEO
            debugger.cliCoreSystemOverride = 1;
            debugger.cliTargetIndex = TARGET_NEOGEO;
            continue;
#else
            cli_setError("neogeo: disabled in this build");
            return;
#endif
        }
        if (strcmp(argv[i], "--megadrive") == 0) {
#if E9K_ENABLE_MEGADRIVE
            debugger.cliCoreSystemOverride = 1;
            debugger.cliTargetIndex = TARGET_MEGADRIVE;
            continue;
#else
            cli_setError("megadrive: disabled in this build");
            return;
#endif
        }
        if (strcmp(argv[i], "--headless") == 0) {
            cli_restartHeadless = 1;
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
        if (strcmp(argv[i], "--no-opengl") == 0) {
            debugger.cliDisableGlComposite = 1;
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
    printf("  --system-dir PATH            System/BIOS directory (applies to current system)\n");
    printf("  --save-dir PATH              Saves directory (applies to current system)\n");
    printf("  --source-dir PATH            Source directory (applies to current system)\n");
    printf("  --toolchain-prefix PREFIX    Toolchain command prefix (applies to current system)\n");
    printf("  --audio-buffer-ms MS         Audio buffer in milliseconds\n");
    printf("  --volume VOLUME              Audio volume (0..100)\n");
    printf("  --window-size WxH            Initial window size override\n");
    printf("  --record PATH                Record input events to a file\n");
    printf("  --playback PATH              Replay input events from a file\n");
    printf("  --make-smoke PATH            Save frames and inputs to a folder\n");
    printf("  --remake-smoke PATH          Replay inputs and regenerate smoke frames\n");
    printf("  --smoke-test PATH            Replay inputs and compare frames\n");
    printf("  --smoke-start-on-write       Start smoke capture after Amiga debug write\n");
    printf("  --profile-start-on-write     Start profiling after Amiga debug write\n");
    printf("  --smoke-threshold N          Fail after N smoke screen mismatches\n");
    printf("  --debug-arg VALUE            Set Amiga debug arg register (repeat up to 10)\n");
    printf("  --smoke-open                 Open montage on smoke-test failure\n");
    printf("  --make-test PATH             Record inputs + UI test frame captures\n");
    printf("  --remake-test PATH           Replay events and regenerate UI test frames\n");
    printf("  --test PATH                  Replay inputs and compare UI test frames\n");
    printf("  --test-auto-open-fail        Open montage on UI test failure\n");
    printf("  --headless                   Hide main window (useful for --smoke-test/--test)\n");
    printf("  --warp                       Start in speed multiplier mode\n");
    printf("  --fullscreen                 Start in UI fullscreen mode (ESC toggle)\n");
    printf("  --no-opengl                  Disable OpenGL composite renderer\n");
    printf("  --no-rolling-record          Disable rolling state recording\n");
    printf("\n");
#if E9K_ENABLE_NEOGEO
    printf("Neo Geo options (use with --neogeo):\n");
    printf("  --neogeo                     Start in Neo Geo system mode\n");    
    printf("  --elf PATH                   ELF file path\n");
    printf("  --symbols PATH               Text symbol map path\n");
    printf("  --rom PATH                   Neo Geo ROM (.neo) path\n");
    printf("                               Also accepts Neo Geo CD .cue/.chd media\n");
    printf("  --rom-folder PATH            ROM folder (generates a .neo)\n");
    printf("  --aes                        Use Neo Geo AES system type\n");
    printf("  --mvs                        Use Neo Geo MVS system type\n");
    printf("  --uni                        Use Neo Geo Universe BIOS system type\n");
    printf("  --cdz                        Use Neo Geo CDZ system type for CD media\n");
    printf("  --cd-front                   Use Neo Geo CD front loader system type\n");
    printf("  --cd-top                     Use Neo Geo CD top loader system type\n");
    printf("  --cdz-uni                    Use Neo Geo CDZ Universe BIOS system type\n");
    printf("\n");
#endif
#if E9K_ENABLE_MEGADRIVE
    printf("Mega Drive options (use with --megadrive):\n");
    printf("  --megadrive                  Start in Mega Drive system mode\n");
    printf("  --elf PATH                   ELF file path\n");
    printf("  --symbols PATH               Text symbol map path\n");
    printf("  --rom PATH                   Mega Drive ROM path\n");
    printf("\n");
#endif
#if E9K_ENABLE_AMIGA
    printf("Amiga options (use with --amiga):\n");
    printf("  --amiga                      Start in Amiga system mode\n");
    printf("  --hunk PATH                  Amiga debug binary (hunk) path\n");
    printf("  --symbols PATH               Text symbol map path\n");
    printf("  --uae PATH                   Amiga UAE config (.uae) path\n");
    printf("\n");
#endif
    printf("You can also use --option=VALUE forms for options that take values.\n");
}

void
cli_applyOverrides(void)
{
    if (debugger.cliConfig.amiga.libretro.romPath[0]) {
        cli_copyPath(debugger.config.amiga.libretro.romPath, sizeof(debugger.config.amiga.libretro.romPath), debugger.cliConfig.amiga.libretro.romPath);
    }
    if (debugger.cliConfig.amiga.libretro.exePath[0]) {
        cli_copyPath(debugger.config.amiga.libretro.exePath, sizeof(debugger.config.amiga.libretro.exePath), debugger.cliConfig.amiga.libretro.exePath);
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
    if (debugger.cliConfig.amiga.libretro.toolchainPrefix[0]) {
        strutil_strlcpy(debugger.config.amiga.libretro.toolchainPrefix, sizeof(debugger.config.amiga.libretro.toolchainPrefix), debugger.cliConfig.amiga.libretro.toolchainPrefix);
    }

    if (debugger.cliConfig.neogeo.libretro.romPath[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.romPath, sizeof(debugger.config.neogeo.libretro.romPath), debugger.cliConfig.neogeo.libretro.romPath);
        debugger.config.neogeo.romFolder[0] = '\0';
    }
    if (debugger.cliConfig.neogeo.romFolder[0]) {
        cli_copyPath(debugger.config.neogeo.romFolder, sizeof(debugger.config.neogeo.romFolder), debugger.cliConfig.neogeo.romFolder);
        debugger.config.neogeo.libretro.romPath[0] = '\0';
    }
    if (debugger.cliConfig.neogeo.libretro.exePath[0]) {
        cli_copyPath(debugger.config.neogeo.libretro.exePath, sizeof(debugger.config.neogeo.libretro.exePath), debugger.cliConfig.neogeo.libretro.exePath);
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
    if (debugger.cliConfig.neogeo.libretro.toolchainPrefix[0]) {
        strutil_strlcpy(debugger.config.neogeo.libretro.toolchainPrefix, sizeof(debugger.config.neogeo.libretro.toolchainPrefix), debugger.cliConfig.neogeo.libretro.toolchainPrefix);
    }
    if (debugger.cliConfig.neogeo.systemType[0]) {
        strutil_strlcpy(debugger.config.neogeo.systemType,
                        sizeof(debugger.config.neogeo.systemType),
                        debugger.cliConfig.neogeo.systemType);
    }
    if (debugger.cliConfig.neogeo.cdSystemType[0]) {
        strutil_strlcpy(debugger.config.neogeo.cdSystemType,
                        sizeof(debugger.config.neogeo.cdSystemType),
                        debugger.cliConfig.neogeo.cdSystemType);
    }
    if (debugger.cliConfig.neogeo.libretro.audioBufferMs > 0) {
        debugger.config.neogeo.libretro.audioBufferMs = debugger.cliConfig.neogeo.libretro.audioBufferMs;
    }

    if (debugger.cliConfig.megadrive.libretro.romPath[0]) {
        cli_copyPath(debugger.config.megadrive.libretro.romPath, sizeof(debugger.config.megadrive.libretro.romPath), debugger.cliConfig.megadrive.libretro.romPath);
    }
    if (debugger.cliConfig.megadrive.libretro.exePath[0]) {
        cli_copyPath(debugger.config.megadrive.libretro.exePath, sizeof(debugger.config.megadrive.libretro.exePath), debugger.cliConfig.megadrive.libretro.exePath);
    }
    if (debugger.cliConfig.megadrive.libretro.systemDir[0]) {
        cli_copyPath(debugger.config.megadrive.libretro.systemDir, sizeof(debugger.config.megadrive.libretro.systemDir), debugger.cliConfig.megadrive.libretro.systemDir);
    }
    if (debugger.cliConfig.megadrive.libretro.saveDir[0]) {
        cli_copyPath(debugger.config.megadrive.libretro.saveDir, sizeof(debugger.config.megadrive.libretro.saveDir), debugger.cliConfig.megadrive.libretro.saveDir);
    }
    if (debugger.cliConfig.megadrive.libretro.sourceDir[0]) {
        cli_copyPath(debugger.config.megadrive.libretro.sourceDir, sizeof(debugger.config.megadrive.libretro.sourceDir), debugger.cliConfig.megadrive.libretro.sourceDir);
    }
    if (debugger.cliConfig.megadrive.libretro.toolchainPrefix[0]) {
        strutil_strlcpy(debugger.config.megadrive.libretro.toolchainPrefix, sizeof(debugger.config.megadrive.libretro.toolchainPrefix), debugger.cliConfig.megadrive.libretro.toolchainPrefix);
    }
    if (debugger.cliConfig.megadrive.libretro.audioBufferMs > 0) {
        debugger.config.megadrive.libretro.audioBufferMs = debugger.cliConfig.megadrive.libretro.audioBufferMs;
    }
}
