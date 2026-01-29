/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debugger.h"

int
debugger_platform_pathJoin(char *out, size_t cap, const char *dir, const char *name)
{
    if (!out || cap == 0 || !dir || !*dir || !name || !*name) {
        return 0;
    }
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int need_sep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    size_t total = dlen + (need_sep ? 1 : 0) + nlen;
    if (total + 1 > cap) {
        return 0;
    }
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (need_sep) {
        out[pos++] = '/';
    }
    memcpy(out + pos, name, nlen);
    out[pos + nlen] = '\0';
    return 1;
}

int
debugger_platform_scanFolder(const char *folder, int (*cb)(const char *path, void *user), void *user)
{
    if (!folder || !*folder || !cb) {
        return 0;
    }
    DIR *dir = opendir(folder);
    if (!dir) {
        return 0;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char full[PATH_MAX];
        if (!debugger_platform_pathJoin(full, sizeof(full), folder, ent->d_name)) {
            continue;
        }
        if (!cb(full, user)) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return 1;
}

char *
debugger_configPath(void)
{
    static char pathbuf[1024];
    const char *home = getenv("HOME");
    if (!home || !*home) {
        return NULL;
    }
    snprintf(pathbuf, sizeof(pathbuf), "%s/.e9k-debugger.cfg", home);
    return pathbuf;
}

void
debugger_platform_setDefaults(e9k_neogeo_config_t *config)
{
    if (!config) {
        return;
    }
    snprintf(config->libretro.corePath, sizeof(config->libretro.corePath), "./system/geolith_libretro.dylib");
    snprintf(config->libretro.systemDir, sizeof(config->libretro.systemDir), "./system");
    snprintf(config->libretro.saveDir, sizeof(config->libretro.saveDir), "./saves");
    snprintf(config->libretro.sourceDir, sizeof(config->libretro.sourceDir), ".");
    snprintf(config->libretro.toolchainPrefix, sizeof(config->libretro.toolchainPrefix), "m68k-neogeo-elf");
    config->libretro.audioBufferMs = 250;
    config->skipBiosLogo = 0;
    strncpy(config->systemType, "aes", sizeof(config->systemType) - 1);
    config->systemType[sizeof(config->systemType) - 1] = '\0';
    config->libretro.elfPath[0] = '\0';
}

void
debugger_platform_setDefaultsAmiga(e9k_amiga_config_t *config)
{
    if (!config) {
        return;
    }
    snprintf(config->libretro.corePath, sizeof(config->libretro.corePath), "./system/puae_libretro.dylib");
    snprintf(config->libretro.systemDir, sizeof(config->libretro.systemDir), "./system");
    snprintf(config->libretro.saveDir, sizeof(config->libretro.saveDir), "./saves");
    snprintf(config->libretro.sourceDir, sizeof(config->libretro.sourceDir), ".");
    snprintf(config->libretro.toolchainPrefix, sizeof(config->libretro.toolchainPrefix), "m68k-amigaos-");
    config->libretro.audioBufferMs = 250;
    config->libretro.elfPath[0] = '\0';
}
