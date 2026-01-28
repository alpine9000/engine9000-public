/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "dasm.h"
#include "debugger.h"

static const dasm_iface_t *
dasm_active(void)
{
    if (debugger.dasm) {
        return debugger.dasm;
    }
    return &dasm_geo_iface;
}

void
dasm_init(void)
{
    const dasm_iface_t *iface = dasm_active();
    if (iface->init) {
        iface->init();
    }
}

void
dasm_shutdown(void)
{
    const dasm_iface_t *iface = dasm_active();
    if (iface->shutdown) {
        iface->shutdown();
    }
}

int
dasm_preloadText(void)
{
    const dasm_iface_t *iface = dasm_active();
    if (!iface->preloadText) {
        return 0;
    }
    return iface->preloadText();
}

uint32_t
dasm_getFlags(void)
{
    const dasm_iface_t *iface = dasm_active();
    return iface->flags;
}

int
dasm_getTotal(void)
{
    const dasm_iface_t *iface = dasm_active();
    if (!iface->getTotal) {
        return 0;
    }
    return iface->getTotal();
}

int
dasm_getAddrHexWidth(void)
{
    const dasm_iface_t *iface = dasm_active();
    if (!iface->getAddrHexWidth) {
        return 0;
    }
    return iface->getAddrHexWidth();
}

int
dasm_findIndexForAddr(uint64_t addr, int *out_index)
{
    const dasm_iface_t *iface = dasm_active();
    if (!iface->findIndexForAddr) {
        return 0;
    }
    return iface->findIndexForAddr(addr, out_index);
}

int
dasm_getRangeByIndex(int start_index, int end_index,
                     const char ***out_lines,
                     const uint64_t **out_addrs,
                     int *out_first_index,
                     int *out_count)
{
    const dasm_iface_t *iface = dasm_active();
    if (!iface->getRangeByIndex) {
        return 0;
    }
    return iface->getRangeByIndex(start_index, end_index, out_lines, out_addrs, out_first_index, out_count);
}

