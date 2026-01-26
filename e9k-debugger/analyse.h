/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>

#define ANALYSE_LOCATION_TEXT_CAP 128

typedef struct {
    unsigned int pc;
    unsigned long long samples;
    char location[ANALYSE_LOCATION_TEXT_CAP];
} analyse_profile_sample_entry;

int
analyse_init(void);

void
analyse_shutdown(void);

int
analyse_reset(void);

int
analyse_handlePacket(const char *line, size_t len);

int
analyse_writeFinalJson(const char *jsonPath);

int
analyse_profileSnapshot(analyse_profile_sample_entry **out, size_t *count);

void
analyse_profileSnapshotFree(analyse_profile_sample_entry *entries);

void
analyse_populateSampleLocations(analyse_profile_sample_entry *entries, size_t count);


