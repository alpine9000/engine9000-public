/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "resource.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef TRACK_RESOURCES

typedef struct {
    void* key;     /* tracked pointer */
    char* desc;    /* malloc'd description */
} resource_entry_t;

#define RESOURCE_MAP_CAPACITY (1024*1024*64)  /* must be power of two */
#define RESOURCE_DESC_MAX     255

static resource_entry_t resourceMap[RESOURCE_MAP_CAPACITY];

static inline size_t
ptrHash(void* ptr)
{
  uintptr_t v = (uintptr_t)ptr;
  /* mix bits a little to reduce clustering */
  v ^= v >> 33;
  v *= 0xff51afd7ed558ccdULL;
  v ^= v >> 33;
  return (size_t)v;
}

static size_t
resource_find(void* ptr)
{
  size_t mask = RESOURCE_MAP_CAPACITY - 1;
  size_t idx = ptrHash(ptr) & mask;
  
  for (;;) {
    if (resourceMap[idx].key == NULL) {
      return SIZE_MAX; /* not found */
    }
    if (resourceMap[idx].key == ptr) {
      return idx;
    }
    idx = (idx + 1) & mask;
  }
}

void
__resource_untrack(void* ptr, const char* func, const char* file, int line)
{
  if (!ptr) {
    return;
  }
  size_t idx = resource_find(ptr);
  
  if (idx == SIZE_MAX) {
    printf("_resourceUntrack: %s %s:%d %p not tracked ??\n", func, file, line, ptr);
    return;
  }
  
  free(resourceMap[idx].desc);
  resourceMap[idx].desc = NULL;
  resourceMap[idx].key = NULL;
  
  /*
   * IMPORTANT:
   * Reinsert subsequent cluster entries to avoid lookup breakage
   */
  size_t mask = RESOURCE_MAP_CAPACITY - 1;
  size_t next = (idx + 1) & mask;
  
  while (resourceMap[next].key != NULL) {
    void* rekey = resourceMap[next].key;
    char* redesc = resourceMap[next].desc;
    
    resourceMap[next].key = NULL;
    resourceMap[next].desc = NULL;
    
    size_t reidx = ptrHash(rekey) & mask;
    while (resourceMap[reidx].key != NULL) {
      reidx = (reidx + 1) & mask;
    }
    
    resourceMap[reidx].key = rekey;
    resourceMap[reidx].desc = redesc;
    
    next = (next + 1) & mask;
  }
}

void
resource_status(void)
{
  int count = 0;
  
  for (size_t i = 0; i < RESOURCE_MAP_CAPACITY; i++) {
    if (resourceMap[i].key != NULL) {
      count++;
      printf("leak: %s\n", resourceMap[i].desc);
    }
  }
  
  printf("_resourceStatus: %d leaks\n", count);
}

void
__resource_track(void* ptr, const char* func, const char* file, int line, const char* desc)
{
    if (!ptr) return;

    size_t mask = RESOURCE_MAP_CAPACITY - 1;
    size_t idx  = ptrHash(ptr) & mask;

    for (size_t n = 0; n < RESOURCE_MAP_CAPACITY; n++) {
        if (resourceMap[idx].key == NULL) {
            resourceMap[idx].key = ptr;
            resourceMap[idx].desc = malloc(RESOURCE_DESC_MAX);
            if (resourceMap[idx].desc) {
                snprintf(resourceMap[idx].desc, RESOURCE_DESC_MAX,
                         "%s %s:%d %s", func, file, line, desc);
            }
            return;
        }

        if (resourceMap[idx].key == ptr) {
            printf("__resourceTrack: already tracked %s (%s:%d)\n",
                   resourceMap[idx].desc, func, line);
            return;
        }

        idx = (idx + 1) & mask;
    }

    printf("__resourceTrack: resource map full (%s:%d)\n", func, line);
}

#endif

