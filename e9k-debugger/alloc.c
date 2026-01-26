/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdlib.h>
#include "alloc.h"
#include "resource.h"
#include "string.h"

#ifdef TRACK_RESOURCES
void*
__alloc_alloc(size_t size, const char* func, const char* file, int line, const char* desc)
{
  void* ptr = malloc(size);
  __resource_track(ptr, func, file, line, desc);
  return ptr;
}

void*
__alloc_calloc(size_t count, size_t size, const char* func, const char* file, int line, const char* desc)
{
  void* ptr = calloc(count, size);
  __resource_track(ptr, func, file, line, desc);
  return ptr;
}

void*
__alloc_realloc(void* ptr, size_t size,
                const char* func, const char* file, int line, const char* desc)
{
  if (!ptr) {
    void* new_ptr = malloc(size);
    if (new_ptr)
      __resource_track(new_ptr, func, file, line, desc);
    return new_ptr;
  }
  
  if (size == 0) {
    __resource_untrack(ptr, func, file, line);
    free(ptr);
    return NULL;
  }
  
  void* new_ptr = realloc(ptr, size);
  
  if (!new_ptr) {
    return NULL;
  }
  
  if (new_ptr != ptr) {
    __resource_untrack(ptr, func, file, line);
    __resource_track(new_ptr, func, file, line, desc);
  }
  
  return new_ptr;
}

void
__alloc_free(void* ptr, const char* func, const char* file, int line)
{
  __resource_untrack(ptr, func, file, line);
  free(ptr);
}

char*
__alloc_strdup(const char* s, const char* func, const char* file, int line)
{
    if (!s) s = "";
    size_t n = strlen(s) + 1;
    char* p = (char*)__alloc_alloc(n, func, file, line, "");
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}
#endif
