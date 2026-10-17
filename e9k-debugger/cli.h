/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#define CLI_RESTART_ARGV_CAP 6

int
cli_buildRestartArgs(char **restartArgv);

void
cli_setArgv0(const char *argv0);

const char *
cli_getArgv0(void);

void
cli_parseArgs(int argc, char **argv);

void
cli_resetConfigOverrides(void);

int
cli_shouldClearRestartArgs(void);

int
cli_helpRequested(void);

int
cli_hasError(void);

void
cli_printUsage(const char *argv0);

void
cli_applyOverrides(void);
