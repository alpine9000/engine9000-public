/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "resource.h"


void*
__alloc_alloc(size_t size, const char* func, const char* file, int line, const char* desc);
void*
__alloc_calloc(size_t count, size_t size, const char* func, const char* file, int line, const char* desc);
void*
__alloc_realloc(void* ptr, size_t size, const char* func, const char* file, int line, const char* desc);
void
__alloc_free(void* ptr, const char* func, const char* file, int line);

char *
__alloc_strdup(const char* s, const char* func, const char* file, int line);

#ifdef TRACK_RESOURCES
#define alloc_alloc(size) __alloc_alloc(size, __func__, __FILE__, __LINE__, "")
#define alloc_calloc(count, size) __alloc_calloc(count, size, __func__, __FILE__, __LINE__, "")
#define alloc_realloc(ptr, size) __alloc_realloc(ptr, size, __func__, __FILE__, __LINE__, "")
#define alloc_free(p) __alloc_free(p, __func__, __FILE__, __LINE__)
#define alloc_strdup(s) __alloc_strdup(s, __func__, __FILE__, __LINE__)
#else
#define alloc_alloc(size) malloc(size)
#define alloc_calloc(count, size) calloc(count, size)
#define alloc_realloc(ptr, size) realloc(ptr, size)
//#define alloc_free(p)  do { printf("alloc_free: %s %s:%d\n", __func__, __FILE__, __LINE__); free(p); } while(0)
#define alloc_free(p) free(p)
#define alloc_strdup(s) strdup(s)
#endif
