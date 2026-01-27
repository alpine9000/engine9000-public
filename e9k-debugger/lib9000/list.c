/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <wchar.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "alloc.h"
#include "list.h"

void*
list_get(list_t* list, int index)
{
  int i = 0;
  while (list && i != index) {
    list = list->next;
    i++;
  }

  if (i == index) {
    return list->data;
  }

  return 0;
}

int
list_count(list_t* list)
{
  int count = 0;
  while (list != 0) {
    count++;
    list = list->next;
  }
  return count;
}

list_t *
list_last(list_t *head)
{
  if (!head) return 0;
  
  while (head->next) {
    head = head->next;
  }
  
  return head;
}

void
list_free(list_t** listPtr, int freeData)
{
  list_t* list = *listPtr;

  while (list != 0) {
    list_t* ptr = list;
    list = list->next;
    if (freeData) {
      alloc_free(ptr->data);
    }
    alloc_free(ptr);
  }

  *listPtr = 0;
}

void
list_append(list_t** listPtr, void* ptr)
{
  list_t* list = *listPtr;
  if (list == 0) {
    list = alloc_alloc(sizeof(list_t));
    list->data = ptr;
    list->next = 0;
    *listPtr = list;
  } else {
    while (list->next != 0) {
      list = list->next;
    }
    list->next = alloc_alloc(sizeof(list_t));
    list->next->data = ptr;
    list->next->next = 0;
  }
}

void
list_remove(list_t** listPtr, void* item, int freeData)
{
  list_t* list = *listPtr;

  if (!list) {
    return;
  }

  if (list->data == item) {
    list_t* prev = list;
    list = list->next;
    if (freeData) {
      alloc_free(prev->data);
    }
    alloc_free(prev);
  } else {
    list_t* ptr = list;
    list_t* prev = ptr;
    while (ptr && ptr->data != item) {
      prev = ptr;
      ptr = ptr->next;
    }
    if (ptr && ptr->data == item) {
      prev->next = ptr->next;
      if (freeData) {
	alloc_free(ptr->data);
      }      
      alloc_free(ptr);
    }
  }

  *listPtr = list;
}
