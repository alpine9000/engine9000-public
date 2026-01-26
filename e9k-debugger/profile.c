/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#include "e9ui.h"
#include "profile.h"
#include "analyse.h"
#include "debug.h"
#include "debugger.h"
#include "libretro_host.h"
#include "profile_list.h"
#include "profile_view.h"


static void
profile_analyseRefresh(void)
{
  e9ui_component_t *btn = debugger.ui.analyseButton;
  if (!btn) {
    return;
  }
  int hasData = (debugger.geo.streamPacketCount > 0) && !debugger.geo.profilerEnabled;
  e9ui_setHidden(btn, !hasData);
}

static void
profile_updateEnabled(int enabled)
{
    if (debugger.geo.profilerEnabled == enabled) {
        return;
    }
    debugger.geo.profilerEnabled = enabled;
    profile_buttonRefresh();
    profile_analyseRefresh();
}

static void
profile_streamStart(void)
{
    if (!analyseReset()) {
        debug_error("profile: aggregator reset failed");
    }
    debugger.geo.streamPacketCount = 0;
    profile_analyseRefresh();
}

void
profile_streamStop(void)
{
}

static void
profile_handleStreamLine(const char *line)
{
    if (!line || !*line) {
        return;
    }
    debugger.geo.streamPacketCount++;
    if (strstr(line, "\"enabled\":\"enabled\"")) {
        profile_updateEnabled(1);
    } else if (strstr(line, "\"enabled\":\"disabled\"")) {
        profile_updateEnabled(0);
    }
    analyseHandlePacket(line, strlen(line));
    profile_list_notifyUpdate();
}

void
profile_buttonRefresh(void)
{
    e9ui_component_t *btn = debugger.ui.profileButton;
    if (!btn) {
        return;
    }
    if (debugger.geo.profilerEnabled) {
        e9ui_button_setTheme(btn, e9ui_theme_button_preset_profile_active());
    } else {
        e9ui_button_clearTheme(btn);
    }
}

static int
profile_defaultJsonPath(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
#ifdef _WIN32
    char tmp[L_tmpnam];
    if (!tmpnam(tmp)) {
        return 0;
    }
    int written = snprintf(out, cap, "%s.json", tmp);
    return (written > 0 && (size_t)written < cap) ? 1 : 0;
#else
    char tmpl[] = "/tmp/e9k-profile-XXXXXX.json";
    int fd = mkstemps(tmpl, 5);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    strncpy(out, tmpl, cap - 1);
    out[cap - 1] = '\0';
    return 1;
#endif
}


void
profile_uiAnalyse(e9ui_context_t *ctx, void *user)
{
    (void)ctx; (void)user;
    char jsonPath[PATH_MAX];
    const char *envJson = getenv("E9K_PROFILE_JSON");
    if (envJson && *envJson) {
        strncpy(jsonPath, envJson, sizeof(jsonPath));
    } else {
        if (!profile_defaultJsonPath(jsonPath, sizeof(jsonPath))) {
            debug_error("profile: unable to create temporary json output path");
            return;
        }
    }
    jsonPath[sizeof(jsonPath) - 1] = '\0';
    unsigned int start_ticks = SDL_GetTicks();
    debug_printf("Profile analysis started (output=%s)\n", jsonPath);
    if (!analyseWriteFinalJson(jsonPath)) {
        unsigned int elapsed = SDL_GetTicks() - start_ticks;
        debug_error("profile: analysis failed after %u ms; see earlier logs", elapsed);
        return;
    }
    profile_viewer_run(jsonPath);
    unsigned int elapsed = SDL_GetTicks() - start_ticks;
    debug_printf("Profile analysis completed (%s) in %.3fs\n", jsonPath, elapsed / 1000.0f);
}

void
analyse_buttonRefresh(void)
{
    e9ui_component_t *btn = debugger.ui.analyseButton;
    if (!btn) {
        return;
    }
    e9ui_button_setTheme(btn, e9ui_theme_button_preset_red());
}

void
profile_uiToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx; (void)user;
    if (!debugger.libretro.enabled) {
        return;
    }
    if (debugger.geo.profilerEnabled) {
        if (!libretro_host_profilerStop()) {
            return;
        }
        profile_streamStop();
        profile_updateEnabled(0);
    } else {
        if (!libretro_host_profilerStart(1)) {
            return;
        }
        profile_streamStart();
        profile_updateEnabled(1);
    }
}

void
profile_drainStream(void)
{
    if (!debugger.libretro.enabled) {
        return;
    }
    enum { kBufSize = 262144 };
    static char buf[kBufSize];
    size_t len = 0;
    while (libretro_host_profilerStreamNext(buf, sizeof(buf), &len) && len > 0) {
        buf[len] = '\0';
        profile_handleStreamLine(buf);
        len = 0;
    }
}
