/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

//#define TRACK_RESOURCES

#ifdef TRACK_RESOURCES
#define resource_track(v) __resource_track(v, __func__, __FILE__, __LINE__, "")
#define resource_untrack(p) __resource_untrack(p, __func__, __FILE__, __LINE__)
void
__resource_track(void* ptr, const char* func, const char* file, int line, const char* desc);
void
__resource_untrack(void* ptr, const char* func, const char* file, int line);
void
resource_status(void);
#else
#define resource_track(v)
#define resource_untrack(p)
#define resource_status() printf("Resource tracking disabled\n")
#define __resource_track(ptr, func, line, desc)
#define __resource_untrack(ptr, func, line)

#endif

