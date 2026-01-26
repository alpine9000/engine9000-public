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
} analyseProfileSampleEntry;

int
analyseInit(void);

void
analyseShutdown(void);

int
analyseReset(void);

int
analyseHandlePacket(const char *line, size_t len);

int
analyseWriteFinalJson(const char *jsonPath);

int
analyseProfileSnapshot(analyseProfileSampleEntry **out, size_t *count);

void
analyseProfileSnapshotFree(analyseProfileSampleEntry *entries);

void
analysePopulateSampleLocations(analyseProfileSampleEntry *entries, size_t count);


