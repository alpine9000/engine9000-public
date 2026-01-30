/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "libretro.h"

typedef struct core_config_options_v2 {
    struct retro_core_option_v2_definition *defs;
    size_t defCount;
    struct retro_core_option_v2_category *cats;
    size_t catCount;
} core_config_options_v2_t;

bool
core_config_probeCoreOptionsV2(const char *corePath,
                               const char *systemDir,
                               const char *saveDir,
                               core_config_options_v2_t *out);

void
core_config_freeCoreOptionsV2(core_config_options_v2_t *opts);
