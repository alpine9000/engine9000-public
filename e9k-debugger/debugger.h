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
#include <SDL_image.h>
#include "e9ui.h"
#include "linebuf.h"
#include "machine.h"
#include "target.h"

#define countof(x) (sizeof(x) / sizeof(x[0]))

struct state_wrap_info;

typedef struct e9k_debug_options {
    int redirectStdout;
    int redirectStderr; 
    int redirectGdbStdout; 
    int enableTrace; 
    int completionListRows;
} e9k_debug_options_t;

typedef enum {
  DEBUGGER_RUNMODE_CAPTURE,
  DEBUGGER_RUNMODE_RESTORE,
} debugger_run_mode_t;

typedef enum debugger_symbol_file_kind {
    DEBUGGER_SYMBOL_FILE_KIND_NONE = 0,
    DEBUGGER_SYMBOL_FILE_KIND_BINARY = 1,
    DEBUGGER_SYMBOL_FILE_KIND_TEXT_MAP = 2
} debugger_symbol_file_kind_t;

typedef struct e9k_libretro_config {
  char romPath[PATH_MAX];
  char systemDir[PATH_MAX];
  char saveDir[PATH_MAX];
  char sourceDir[PATH_MAX];
  char exePath[PATH_MAX];
  char toolchainPrefix[PATH_MAX];
  int enabled;
  int audioBufferMs;
  int audioEnabled;  
} e9k_libretro_config_t;

typedef struct e9k_path_config {
    e9k_libretro_config_t libretro;
    char romFolder[PATH_MAX];
    char systemType[16];
    char cdSystemType[16];
    int  skipBiosLogo;
} e9k_neogeo_config_t;

typedef struct e9k_amiga_config {
    e9k_libretro_config_t libretro;  
} e9k_amiga_config_t;

typedef struct e9k_megadrive_config {
    e9k_libretro_config_t libretro;
    char romFolder[PATH_MAX];
} e9k_megadrive_config_t;

typedef struct amiga_debug {
    int *debugDma;
    int *debugCopper;
} amiga_debug_t;

typedef struct e9k_system_config {
    //debugger_system_type_t coreSystem;
    struct target_iface* target;
    e9k_neogeo_config_t neogeo;
    e9k_amiga_config_t amiga;
    e9k_megadrive_config_t megadrive;
    int logsEnabled;
    int recordEnabled;
    int logosEnabled;
    int crtEnabled;
} e9k_system_config_t;

typedef struct e9k_debugger {
    LineBuf console;
    int     consoleScrollLines;
    char    argv0[PATH_MAX];
    char    preferredControllerGuid[64];
    e9k_system_config_t config;
    e9k_system_config_t cliConfig;
    e9k_system_config_t settingsEdit;
    e9k_system_config_t* currentConfig;
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
    uint32_t activeDebugProcessorId;
    int frameStepMode;
    int frameStepPending;
    int suppressBpActive;
    uint32_t suppressBpAddr;
    int suppressVblankFrameCounter;
    uint64_t frameCounter;
    uint64_t frameTimeCounter;
    double frameTimeAccum;
    int vblankCaptureActive;
    uint64_t uiFrameCounter;
    int uiRefreshHz;
    char recordPath[PATH_MAX];
    char playbackPath[PATH_MAX];
    char smokeTestPath[PATH_MAX];
    int smokeTestMode;
    int smokeTestCompleted;
    int smokeTestFailed;
    int smokeTestExitCode;
    int smokeTestOpenOnFail;
    int smokeTestStartOnWrite;
    int smokeTestThreshold;
    int profileStartOnWrite;
    uint32_t debugArgs[10];
    int debugArgCount;
    int cliWindowOverride;
    int cliWindowW;
    int cliWindowH;
    int cliDisableRollingRecord;
    int cliStartFullscreen;
    int cliDisableGlComposite;
    int cliHeadless;
    int cliWarp;
    int cliAudioVolume;
    int cliResetCfg;
    int cliCoreSystemOverride;
    int cliTargetIndex;
    int settingsOk;
    int elfValid;
    int symbolValid;
    debugger_symbol_file_kind_t symbolFileKind;
    int exitRequested;
    int restartRequested;
    e9k_debug_options_t opts;
    int coreOptionsShowHelp;
    e9k_libretro_config_t libretro;
    amiga_debug_t amigaDebug;
    int loopEnabled;
    uint64_t loopFrom;
    uint64_t loopTo;
} e9k_debugger_t;

extern e9ui_global_t _e9ui;
extern e9k_debugger_t debugger;

char *
debugger_configPath(void);

char *
debugger_defaultConfigPath(void);

char *
debugger_configTempPath(void);

void
debugger_setLoadTestTempConfig(int enabled);

int
debugger_getLoadTestTempConfig(void);

void
debugger_setTestRestartCount(int count);

int
debugger_getTestRestartCount(void);

void
debugger_toggleSpeed(void);

void
debugger_clearFrameStep(void);

int
debugger_main(int argc, char **argv);

int
debugger_onCoreReset(void);

int
debugger_platform_pathJoin(char *out, size_t cap, const char *dir, const char *name);

int
debugger_platform_pathIsAbsolute(const char *path);

int
debugger_platform_expandHomePath(char *out, size_t cap, const char *path);

int
debugger_platform_scanFolder(const char *folder, int (*cb)(const char *path, void *user), void *user);

int
debugger_platform_caseInsensitivePaths(void);

char
debugger_platform_preferredPathSeparator(void);

void
debugger_platform_normalizePathSeparators(char *path);

int
debugger_platform_getExeDir(char *out, size_t cap);

int
debugger_platform_isExecutableFile(const char *path);

int
debugger_platform_getHomeDir(char *out, size_t cap);

int
debugger_platform_getCurrentDir(char *out, size_t cap);

char
debugger_platform_pathListSeparator(void);

int
debugger_platform_makeDir(const char *path);

int
debugger_platform_removeDir(const char *path);

int
debugger_platform_makeTempFilePath(char *out, size_t cap, const char *prefix, const char *suffix);

int
debugger_platform_replaceFile(const char *srcPath, const char *dstPath);

int
debugger_platform_glCompositeNeedsOpenGLHint(void);

const char *
debugger_platform_windowIconAssetPath(void);

const char *
debugger_platform_selectFolderDialog(const char *title, const char *defaultPath);

const char *
debugger_platform_openFileDialog(const char *title,
                                 const char *defaultPathAndFile,
                                 int numOfFilterPatterns,
                                 const char * const *filterPatterns,
                                 const char *singleFilterDescription,
                                 int allowMultipleSelects);

const char *
debugger_platform_saveFileDialog(const char *title,
                                 const char *defaultPathAndFile,
                                 int numOfFilterPatterns,
                                 const char * const *filterPatterns,
                                 const char *singleFilterDescription);

void *
debugger_platform_loadSharedLibrary(const char *path);

void
debugger_platform_closeSharedLibrary(void *handle);

void *
debugger_platform_loadSharedSymbol(void *handle, const char *name);

ssize_t
debugger_platform_getline(char **lineptr, size_t *n, FILE *stream);

int
debugger_platform_formatToolCommand(char *out,
                                    size_t cap,
                                    const char *toolPath,
                                    const char *toolArgs,
                                    const char *targetPath,
                                    int suppressStderr);

int
debugger_platform_finalizeToolBinary(char *toolPath, size_t cap);

int
debugger_platform_uncompressBuffer(uint8_t *dest, size_t *inOutDestSize, const uint8_t *source, size_t sourceSize);

void
debugger_suppressBreakpointAtPC(void);

void
debugger_cancelSettingsModal(void);

void
debugger_setSeeking(int seeking);

int
debugger_isSeeking(void);

void debugger_libretroSelectConfig(void);

void
debugger_refreshElfValid(void);

void
debugger_applyCoreOptions(void);

int
debugger_toolchainBuildBinary(char *out, size_t cap, const char *tool);

int
debugger_toolchainUsesHunkAddr2line(void);

int
debugger_getAudioEnabled(void);

void
debugger_setAudioEnabled(int enabled);

uint32_t
debugger_uiTicks(void);

 void
debugger_copyPath(char *dest, size_t cap, const char *src);

void
debugger_onSetDebugBaseFromCore(uint32_t section, uint32_t base);

void
debugger_onPushDebugBaseFromCore(uint32_t section, uint32_t base, uint32_t size);

void
debugger_setTextBaseAddress(uint32_t base);

void
debugger_setDataBaseAddress(uint32_t base);

void
debugger_setBssBaseAddress(uint32_t base);

void
debugger_applyStateWrapBases(const struct state_wrap_info *info);

void
debugger_onAddBreakpointFromCore(uint32_t addr);
