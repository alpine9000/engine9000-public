/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "profile_view.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"

#define PROFILE_VIEWER_PYTHON_ENV "E9K_PROFILE_VIEWER_PYTHON"
#define PROFILE_VIEWER_SCRIPT_ENV "E9K_PROFILE_VIEWER_SCRIPT"
#define PROFILE_VIEWER_DEFAULT_PYTHON "python3"
#define PROFILE_VIEWER_DEFAULT_SCRIPT "tools/profileui/build_viewer.py"
#define PROFILE_VIEWER_TEMP_TEMPLATE "/tmp/e9k-profile-viewer-XXXXXX"

static int profile_viewer_try_env_path(const char *env, char *out, size_t cap);
static int profile_viewer_is_executable(const char *path);

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
        if (!profile_viewer_is_executable(out)) {
            struct stat st;
            if (stat(out, &st) == 0 && S_ISREG(st.st_mode)) {
                return 1;
            }
            debug_error("profile: viewer script env path %s invalid; falling back to assets", out);
        } else {
            return 1;
        }
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
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    if (access(path, X_OK) != 0) {
        return 0;
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
    char temp_dir[PATH_MAX];
    strncpy(temp_dir, PROFILE_VIEWER_TEMP_TEMPLATE, sizeof(temp_dir));
    temp_dir[sizeof(temp_dir) - 1] = '\0';
    char *out_dir = mkdtemp(temp_dir);
    if (!out_dir) {
        debug_error("profile: unable to create viewer temp dir: %s", strerror(errno));
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        debug_error("profile: failed to spawn viewer process: %s", strerror(errno));
        return 0;
    }
    if (pid == 0) {
        char *viewer_args[32];
        char text_base_buf[32];
        char data_base_buf[32];
        char bss_base_buf[32];
        int i = 0;
        viewer_args[i++] = python_path;
        viewer_args[i++] = script_path;
        viewer_args[i++] = (char *)"--input";
        viewer_args[i++] = (char *)json_path;
        viewer_args[i++] = (char *)"--out";
        viewer_args[i++] = out_dir;
        if (debugger.libretro.toolchainPrefix[0]) {
            viewer_args[i++] = (char *)"--toolchain-prefix";
            viewer_args[i++] = debugger.libretro.toolchainPrefix;
        }
        if (debugger.libretro.elfPath[0]) {
            viewer_args[i++] = (char *)"--elf";
            viewer_args[i++] = debugger.libretro.elfPath;
        }
        if (debugger.libretro.sourceDir[0]) {
            viewer_args[i++] = (char *)"--src-base";
            viewer_args[i++] = debugger.libretro.sourceDir;
        }
        if (debugger.machine.textBaseAddr) {
            snprintf(text_base_buf, sizeof(text_base_buf), "0x%08X", debugger.machine.textBaseAddr);
            viewer_args[i++] = (char *)"--text-base";
            viewer_args[i++] = text_base_buf;
        }
        if (debugger.machine.dataBaseAddr) {
            snprintf(data_base_buf, sizeof(data_base_buf), "0x%08X", debugger.machine.dataBaseAddr);
            viewer_args[i++] = (char *)"--data-base";
            viewer_args[i++] = data_base_buf;
        }
        if (debugger.machine.bssBaseAddr) {
            snprintf(bss_base_buf, sizeof(bss_base_buf), "0x%08X", debugger.machine.bssBaseAddr);
            viewer_args[i++] = (char *)"--bss-base";
            viewer_args[i++] = bss_base_buf;
        }
        viewer_args[i++] = NULL;
        execv(python_path, viewer_args);
        debug_error("profile: exec viewer process failed: %s (python=%s, script=%s)", strerror(errno), python_path, script_path);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        debug_error("profile: viewer process wait failed: %s", strerror(errno));
        return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        debug_error("profile: viewer process failed (exit=%d)", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return 0;
    }
    debug_printf("Profile viewer generated at %s/index.html\n", out_dir);
    return 1;
}
