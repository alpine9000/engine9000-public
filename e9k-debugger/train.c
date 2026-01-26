/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "train.h"

#include <string.h>

#include "alloc.h"
#include "list.h"

static list_t *s_train_ignoreAddrs = NULL;

static uint32_t s_train_lastWatchAddr = 0;
static int s_train_haveLastWatchAddr = 0;
static uint32_t s_train_watchIndex = UINT32_MAX;

void
train_clearIgnoreList(void)
{
    list_free(&s_train_ignoreAddrs, 1);
}

int
train_addIgnoreAddr(uint32_t addr24)
{
    addr24 &= 0x00ffffffu;
    for (list_t *it = s_train_ignoreAddrs; it; it = it->next) {
        uint32_t *p = (uint32_t *)it->data;
        if (p && (*p & 0x00ffffffu) == addr24) {
            return 1;
        }
    }

    uint32_t *ptr = (uint32_t *)alloc_alloc(sizeof(uint32_t));
    if (!ptr) {
        return 0;
    }
    *ptr = addr24;
    list_append(&s_train_ignoreAddrs, ptr);
    return 1;
}

int
train_isIgnoredAddr(uint32_t addr24)
{
    addr24 &= 0x00ffffffu;
    for (list_t *it = s_train_ignoreAddrs; it; it = it->next) {
        uint32_t *p = (uint32_t *)it->data;
        if (p && (*p & 0x00ffffffu) == addr24) {
            return 1;
        }
    }
    return 0;
}

void
train_setLastWatchbreak(const geo_debug_watchbreak_t *wb)
{
    if (!wb) {
        s_train_haveLastWatchAddr = 0;
        s_train_lastWatchAddr = 0;
        return;
    }
    if (s_train_watchIndex == UINT32_MAX || wb->index != s_train_watchIndex) {
        s_train_haveLastWatchAddr = 0;
        s_train_lastWatchAddr = 0;
        return;
    }
    s_train_haveLastWatchAddr = 1;
    s_train_lastWatchAddr = wb->access_addr & 0x00ffffffu;
}

void
train_setWatchIndex(uint32_t index)
{
    s_train_watchIndex = index;
    s_train_haveLastWatchAddr = 0;
    s_train_lastWatchAddr = 0;
}

int
train_isActive(void)
{
    return s_train_watchIndex != UINT32_MAX;
}

int
train_hasLastWatchbreak(void)
{
    return s_train_haveLastWatchAddr ? 1 : 0;
}

int
train_getLastWatchbreakAddr(uint32_t *out_addr24)
{
    if (out_addr24) {
        *out_addr24 = 0;
    }
    if (!s_train_haveLastWatchAddr) {
        return 0;
    }
    if (out_addr24) {
        *out_addr24 = s_train_lastWatchAddr;
    }
    return 1;
}
