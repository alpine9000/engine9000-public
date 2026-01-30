/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>
#include <windows.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "profile_view.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"

#define PROFILE_VIEWER_PYTHON_ENV "E9K_PROFILE_VIEWER_PYTHON"
#define PROFILE_VIEWER_SCRIPT_ENV "E9K_PROFILE_VIEWER_SCRIPT"
#define PROFILE_VIEWER_DEFAULT_PYTHON "python3"
#define PROFILE_VIEWER_DEFAULT_SCRIPT "tools/profileui/build_viewer.py"

static int profile_viewer_try_env_path(const char *env, char *out, size_t cap);
static int profile_viewer_is_executable(const char *path);
static int profile_viewer_make_temp_dir(char *out, size_t cap);

static int
profile_viewer_resolve_python(const char *env, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    if (profile_viewer_try_env_path(env, out, cap)) {
        if (!profile_viewer_is_executable(out)) {
            debug_error("profile: python env path %s not executable; falling back to PATH", out);
        } else {
            return 1;
        }
    }
    return file_findInPath(PROFILE_VIEWER_DEFAULT_PYTHON, out, cap);
}

static int
profile_viewer_resolve_script(const char *env, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    if (profile_viewer_try_env_path(env, out, cap)) {
        struct stat st;
        if (stat(out, &st) == 0 && S_ISREG(st.st_mode)) {
            return 1;
        }
        debug_error("profile: viewer script env path %s invalid; falling back to assets", out);
    }
    return file_getAssetPath(PROFILE_VIEWER_DEFAULT_SCRIPT, out, cap);
}

static int
profile_viewer_try_env_path(const char *env, char *out, size_t cap)
{
    if (!env || !*env || !out || cap == 0) {
        return 0;
    }
    size_t len = strlen(env);
    if (len >= cap) {
        return 0;
    }
    memcpy(out, env, len + 1);
    return 1;
}

static int
profile_viewer_is_executable(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int
profile_viewer_make_temp_dir(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    char base[MAX_PATH];
    DWORD base_len = GetTempPathA((DWORD)sizeof(base), base);
    if (base_len == 0 || base_len >= (DWORD)sizeof(base)) {
        return 0;
    }
    char temp_file[MAX_PATH];
    if (!GetTempFileNameA(base, "e9k", 0, temp_file)) {
        return 0;
    }
    DeleteFileA(temp_file);
    if (!CreateDirectoryA(temp_file, NULL)) {
        return 0;
    }
    size_t len = strlen(temp_file);
    if (len >= cap) len = cap - 1;
    memcpy(out, temp_file, len);
    out[len] = '\0';
    return 1;
}

static int
profile_viewer_build_cmd(char *out, size_t cap, const char *python, const char *script,
                         const char *json_path, const char *out_dir)
{
    if (!out || cap == 0) {
        return 0;
    }
    size_t used = 0;
    int n = snprintf(out, cap, "\"%s\" \"%s\" --input \"%s\" --out \"%s\"",
                     python, script, json_path, out_dir);
    if (n <= 0 || (size_t)n >= cap) {
        out[cap - 1] = '\0';
        return 0;
    }
    used = (size_t)n;
    if (debugger.libretro.toolchainPrefix[0]) {
        n = snprintf(out + used, cap - used, " --toolchain-prefix \"%s\"", debugger.libretro.toolchainPrefix);
        if (n <= 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            return 0;
        }
        used += (size_t)n;
    }
    if (debugger.libretro.elfPath[0]) {
        n = snprintf(out + used, cap - used, " --elf \"%s\"", debugger.libretro.elfPath);
        if (n <= 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            return 0;
        }
        used += (size_t)n;
    }
    if (debugger.libretro.sourceDir[0]) {
        n = snprintf(out + used, cap - used, " --src-base \"%s\"", debugger.libretro.sourceDir);
        if (n <= 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            return 0;
        }
        used += (size_t)n;
    }
    if (debugger.machine.textBaseAddr) {
        n = snprintf(out + used, cap - used, " --text-base 0x%08X", (unsigned)debugger.machine.textBaseAddr);
        if (n <= 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            return 0;
        }
        used += (size_t)n;
    }
    if (debugger.machine.dataBaseAddr) {
        n = snprintf(out + used, cap - used, " --data-base 0x%08X", (unsigned)debugger.machine.dataBaseAddr);
        if (n <= 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            return 0;
        }
        used += (size_t)n;
    }
    if (debugger.machine.bssBaseAddr) {
        n = snprintf(out + used, cap - used, " --bss-base 0x%08X", (unsigned)debugger.machine.bssBaseAddr);
        if (n <= 0 || (size_t)n >= cap - used) {
            out[cap - 1] = '\0';
            return 0;
        }
        used += (size_t)n;
    }
    return 1;
}

int
profile_viewer_run(const char *json_path)
{
    if (!json_path || !*json_path) {
        return 0;
    }
    char python_path[PATH_MAX];
    if (!profile_viewer_resolve_python(getenv(PROFILE_VIEWER_PYTHON_ENV), python_path, sizeof(python_path))) {
        debug_error("profile: unable to locate python interpreter (%s)", PROFILE_VIEWER_DEFAULT_PYTHON);
        return 0;
    }
    char script_path[PATH_MAX];
    if (!profile_viewer_resolve_script(getenv(PROFILE_VIEWER_SCRIPT_ENV), script_path, sizeof(script_path))) {
        debug_error("profile: unable to locate viewer script (%s)", PROFILE_VIEWER_DEFAULT_SCRIPT);
        return 0;
    }
    char out_dir[PATH_MAX];
    if (!profile_viewer_make_temp_dir(out_dir, sizeof(out_dir))) {
        debug_error("profile: unable to create viewer temp dir");
        return 0;
    }
    char cmd[PATH_MAX * 4];
    if (!profile_viewer_build_cmd(cmd, sizeof(cmd), python_path, script_path, json_path, out_dir)) {
        debug_error("profile: unable to build viewer command line");
        return 0;
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        debug_error("profile: unable to launch viewer process");
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exit_code != 0) {
        debug_error("profile: viewer process failed (exit=%lu)", (unsigned long)exit_code);
        return 0;
    }
    debug_printf("Profile viewer generated at %s\\index.html\n", out_dir);
    return 1;
}
