/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stdarg.h>

#ifndef E9K_DEBUG_PRINTF_STDOUT_DEFAULT
#define E9K_DEBUG_PRINTF_STDOUT_DEFAULT 1
#endif

void
debug_printf(const char *fmt, ...);

#ifndef E9K_DEBUG_ERROR_STDERR_DEFAULT
#define E9K_DEBUG_ERROR_STDERR_DEFAULT 1
#endif

void
debug_error(const char *fmt, ...);

#ifndef E9K_DEBUG_GDB_STDOUT_DEFAULT
#define E9K_DEBUG_GDB_STDOUT_DEFAULT 0
#endif

#ifndef E9K_DEBUG_TRACE_ENABLE_DEFAULT
#define E9K_DEBUG_TRACE_ENABLE_DEFAULT 0
#endif

void
debug_trace(const char *fmt, ...);


