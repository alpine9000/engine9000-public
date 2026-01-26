/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once


typedef struct LineBuf {
    char **lines;
    unsigned char *is_err;
    int   cap;
    int   n;
    int   start;
    
} LineBuf;

void
linebuf_init(LineBuf *b, int cap);

void
linebuf_dtor(LineBuf *b);

void
linebuf_push(LineBuf *b, const char *s);

void
linebuf_pushErr(LineBuf *b, const char *s);

void
linebuf_clear(LineBuf *b);

static inline int
linebuf_count(const LineBuf *b) { return b->n; }

static inline int
linebuf_capacity(const LineBuf *b) { return b->cap; }

static inline int
linebuf_phys_index(const LineBuf *b, int logical_index) {
    int idx = b->start + logical_index;
    if (idx >= b->cap) {
        idx -= b->cap;
    }
    return idx;
}


