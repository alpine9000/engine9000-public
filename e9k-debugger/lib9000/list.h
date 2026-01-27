/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

typedef struct _list {
  void* data;
  struct _list* next;
} list_t;


void
list_append(list_t** listPtr, void* ptr);

void
list_remove(list_t** listPtr, void* item, int freeData);

void
list_free(list_t** listPtr, int freeData);

int
list_count(list_t* list);

list_t *
list_last(list_t *head);

void*
list_get(list_t* list, int index);
