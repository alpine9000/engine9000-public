/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

bool
amiga_uaeLoadUaeOptions(const char *uaePath);

void
amiga_uaeClearPuaeOptions(void);

int
amiga_uaeUaeOptionsDirty(void);

const char *
amiga_uaeGetPuaeOptionValue(const char *key);

void
amiga_uaeSetPuaeOptionValue(const char *key, const char *value);

const char *
amiga_uaeGetFloppyPath(int drive);

void
amiga_uaeSetFloppyPath(int drive, const char *path);

bool
amiga_uaeWriteUaeOptionsToFile(const char *uaePath);

bool
amiga_uaeApplyPuaeOptionsToHost(const char *uaePath);
