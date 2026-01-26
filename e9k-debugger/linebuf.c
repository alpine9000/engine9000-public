/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdlib.h>
#include <string.h>
#include "alloc.h"
#include "linebuf.h"

static void
linebuf_pushOwned(LineBuf *b, char *s, unsigned char isErr)
{
  int idx = 0;
  if (!b || b->cap <= 0) {
    if (s) {
      alloc_free(s);
    }
    return;
  }
  if (!s) {
    s = alloc_strdup("");
  }

  if (b->n < b->cap) {
    idx = (b->start + b->n) % b->cap;
    b->n++;
  } else {
    // overwrite oldest
    idx = b->start;
    if (b->lines[idx]) {
      alloc_free(b->lines[idx]);
    }
    b->start = (b->start + 1) % b->cap;
  }

  b->lines[idx] = s;
  b->is_err[idx] = isErr;
}

static void
linebuf_pushMultiline(LineBuf *b, const char *s, unsigned char isErr)
{
  if (!s) {
    linebuf_pushOwned(b, alloc_strdup(""), isErr);
    return;
  }

  const char *lineStart = s;
  const char *p = s;
  while (*p) {
    if (*p == '\n') {
      size_t len = (size_t)(p - lineStart);

      // Drop a trailing empty line caused by a terminating newline.
      if (len == 0 && p[1] == '\0') {
        break;
      }

      if (len > 0 && lineStart[len - 1] == '\r') {
        len--;
      }

      char *line = (char *)alloc_alloc(len + 1);
      if (!line) {
        return;
      }
      if (len > 0) {
        memcpy(line, lineStart, len);
      }
      line[len] = '\0';
      linebuf_pushOwned(b, line, isErr);

      p++;
      lineStart = p;
      continue;
    }
    p++;
  }

  if (p != lineStart) {
    size_t len = (size_t)(p - lineStart);
    if (len > 0 && lineStart[len - 1] == '\r') {
      len--;
    }
    char *line = (char *)alloc_alloc(len + 1);
    if (!line) {
      return;
    }
    if (len > 0) {
      memcpy(line, lineStart, len);
    }
    line[len] = '\0';
    linebuf_pushOwned(b, line, isErr);
  }
}

void
linebuf_init(LineBuf *b, int cap)
{
  b->lines = (char**)alloc_calloc((size_t)cap, sizeof(char*));
  b->is_err = (unsigned char*)alloc_calloc((size_t)cap, sizeof(unsigned char));
  b->cap = cap; b->n = 0; b->start = 0;
}

void
linebuf_dtor(LineBuf *b)
{
  if (b) {
    if (b->lines) {
      for (int i = 0; i < b->cap; i++) {
        if (b->lines[i]) {
          alloc_free(b->lines[i]);
          b->lines[i] = NULL;
        }
      }
      alloc_free(b->lines);
      b->lines = 0;
    }
    if (b->is_err) {
      alloc_free(b->is_err);
    }
  }
}

void
linebuf_push(LineBuf *b, const char *s)
{
  linebuf_pushMultiline(b, s, 0);
}

void
linebuf_pushErr(LineBuf *b, const char *s)
{
  linebuf_pushMultiline(b, s, 1);
}

void
linebuf_clear(LineBuf *b)
{
  if (!b || !b->lines || !b->is_err || b->cap <= 0) {
    return;
  }
  for (int i = 0; i < b->cap; ++i) {
    if (b->lines[i]) {
      alloc_free(b->lines[i]);
      b->lines[i] = NULL;
    }
    b->is_err[i] = 0;
  }
  b->n = 0;
  b->start = 0;
}
