/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>

#include "e9ui.h"
#include "debugger.h"
#include "source.h"
#include "source_pane.h"
#include "dasm.h"
#include "addr2line.h"
#include "machine.h"
#include "breakpoints.h"
#include "libretro_host.h"
#include "debug.h"

typedef struct view_toggle_ctx {
  e9ui_component_t *pane;
  e9ui_component_t *button;
} view_toggle_t;

typedef struct source_pane_state {
    source_pane_mode_t viewMode; // C vs ASM
    int               scrollLine; // 1-based first line for C view
    int               scrollLineValid;
    int               scrollIndex; // 0-based first instruction for ASM view
    int               scrollIndexValid;
    uint64_t          lastPcAddr;
    uint64_t          lastResolvedPc;
    uint64_t          overrideAddr;
    int               overrideActive;
    char              curSrcPath[PATH_MAX];
    int               curSrcLine;
    char*             toggleBtnMeta;
    char*             lockBtnMeta;  
    int               gutterPending;
    int               gutterLine;
    uint32_t          gutterAddr;
    int               gutterDownX;
    int               gutterDownY;
    source_pane_mode_t gutterMode;
    int               bucketSource;
    int               bucketAddr;
} source_pane_state_t;

typedef struct source_pane_line_metrics {
    int maxLines;
    int lineHeight;
    int innerHeight;
} source_pane_line_metrics_t;

static void
source_pane_updateSourceLocation(source_pane_state_t *st);

static const char *
source_pane_basename(const char *path)
{
    if (!path || !path[0]) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static int
source_pane_parseHex(const char *s, uint32_t *out)
{
    if (!s || !*s || !out) {
        return 0;
    }
    char buf[32];
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        len--;
    }
    if (len > 0 && s[len - 1] == ':') {
        len--;
    }
    if (len == 0 || len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    const char *p = buf;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (!*p) {
        return 0;
    }
    for (const char *q = p; *q; ++q) {
        if (!isxdigit((unsigned char)*q)) {
            return 0;
        }
    }
    errno = 0;
    unsigned long v = strtoul(buf, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *out = (uint32_t)(v & 0x00ffffffu);
    return 1;
}

static int
source_pane_fileMatches(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    if (strcmp(a, b) == 0) {
        return 1;
    }
    const char *ba = source_pane_basename(a);
    const char *bb = source_pane_basename(b);
    if (ba && bb && strcmp(ba, bb) == 0) {
        return 1;
    }
    const char *src = debugger.config.sourceDir;
    if (src && *src) {
        size_t src_len = strlen(src);
        if (strncmp(a, src, src_len) == 0) {
            const char *rest = a + src_len;
            if (*rest == '/' || *rest == '\\') {
                rest++;
            }
            if (strcmp(rest, b) == 0) {
                return 1;
            }
        }
        if (strncmp(b, src, src_len) == 0) {
            const char *rest = b + src_len;
            if (*rest == '/' || *rest == '\\') {
                rest++;
            }
            if (strcmp(rest, a) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int
source_pane_resolveFileLine(const char *elf, const char *file, int line_no, uint32_t *out_addr)
{
    if (!elf || !*elf || !debugger.elfValid || !file || !*file || line_no <= 0 || !out_addr) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "m68k-neogeo-elf-objdump -l -d '%s'", elf);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }
    char line[1024];
    int want_addr = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        if (line[0] == '\0') {
            want_addr = 0;
            continue;
        }
        if (line[0] != ' ') {
            const char *colon = strrchr(line, ':');
            if (!colon || !colon[1]) {
                want_addr = 0;
                continue;
            }
            int got_line = atoi(colon + 1);
            if (got_line != line_no) {
                want_addr = 0;
                continue;
            }
            char file_buf[PATH_MAX];
            size_t len = (size_t)(colon - line);
            if (len >= sizeof(file_buf)) {
                len = sizeof(file_buf) - 1;
            }
            memcpy(file_buf, line, len);
            file_buf[len] = '\0';
            want_addr = source_pane_fileMatches(file_buf, file);
            continue;
        }
        if (want_addr) {
            char addr_buf[32];
            const char *p = line;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            size_t i = 0;
            while (*p && !isspace((unsigned char)*p) && i + 1 < sizeof(addr_buf)) {
                addr_buf[i++] = *p++;
            }
            addr_buf[i] = '\0';
            uint32_t addr = 0;
            if (source_pane_parseHex(addr_buf, &addr)) {
                *out_addr = addr;
                pclose(fp);
                return 1;
            }
        }
    }
    pclose(fp);
    return 0;
}

static machine_breakpoint_t *
source_pane_findBreakpointForLine(const char *path, int line,
                                  const machine_breakpoint_t *bps, int count)
{
    if (!path || line <= 0) {
        return NULL;
    }
    for (int i = 0; i < count; ++i) {
        machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
        if (bp->line == line && source_pane_fileMatches(bp->file, path)) {
            return bp;
        }
    }
    return NULL;
}


static source_pane_line_metrics_t
source_pane_computeLineMetrics(e9ui_component_t *self, TTF_Font *font, int padPx)
{
    source_pane_line_metrics_t out = {0};
    if (!self || !font) {
        out.lineHeight = 16;
        out.maxLines = 1;
        return out;
    }
    out.lineHeight = TTF_FontHeight(font);
    if (out.lineHeight <= 0) {
        out.lineHeight = 16;
    }
    out.innerHeight = self->bounds.h - padPx * 2;
    if (out.innerHeight <= 0) {
        out.maxLines = 0;
        return out;
    }
    out.maxLines = out.innerHeight / out.lineHeight;
    if (out.maxLines <= 0) {
        out.maxLines = 1;
    }
    return out;
}

static TTF_Font *
source_pane_resolveFont(const e9ui_context_t *ctx)
{
    if (e9ui->theme.text.source) {
        return e9ui->theme.text.source;
    }
    if (ctx) {
        return ctx->font;
    }
    return NULL;
}

static int
source_pane_pointInBounds(const e9ui_component_t *comp, int x, int y)
{
    if (!comp) {
        return 0;
    }
    return x >= comp->bounds.x && x < comp->bounds.x + comp->bounds.w &&
           y >= comp->bounds.y && y < comp->bounds.y + comp->bounds.h;
}

static void
source_pane_adjustScroll(source_pane_state_t *st, source_pane_mode_t mode, int delta)
{
    if (!st || delta == 0) {
        return;
    }
    if (mode == source_pane_mode_c) {
        int dest = st->scrollLine + delta;
        if (dest < 1) {
            dest = 1;
        }
        st->scrollLine = dest;
        st->scrollLineValid = 1;
        st->gutterPending = 0;
        return;
    }
    int dest = st->scrollIndex + delta;
    if (dest < 0) {
        dest = 0;
    }
    st->scrollIndex = dest;
    st->scrollIndexValid = 1;
    st->gutterPending = 0;
}

static void
source_pane_scrollToStart(source_pane_state_t *st, source_pane_mode_t mode)
{
    if (!st) {
        return;
    }
    if (mode == source_pane_mode_c) {
        st->scrollLine = 1;
        st->scrollLineValid = 1;
        st->gutterPending = 0;
        return;
    }
    st->scrollIndex = 0;
    st->scrollIndexValid = 1;
    st->gutterPending = 0;
}

static void
source_pane_scrollToEnd(source_pane_state_t *st, source_pane_mode_t mode, int maxLines)
{
    if (!st) {
        return;
    }
    if (maxLines <= 0) {
        maxLines = 1;
    }
    if (mode == source_pane_mode_c) {
        source_pane_updateSourceLocation(st);
        int total = 0;
        if (st->curSrcPath[0]) {
            total = source_getTotalLines(st->curSrcPath);
        }
        if (total <= 0) {
            st->scrollLine = 1;
        } else {
            int dest = total - maxLines + 1;
            if (dest < 1) {
                dest = 1;
            }
            st->scrollLine = dest;
        }
        st->scrollLineValid = 1;
        st->gutterPending = 0;
        return;
    }
    int total = dasm_getTotal();
    int dest = total - maxLines;
    if (dest < 0) {
        dest = 0;
    }
    st->scrollIndex = dest;
    st->scrollIndexValid = 1;
    st->gutterPending = 0;
}

static void
source_pane_followCurrent(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    st->scrollLineValid = 0;
    st->scrollIndexValid = 0;
    st->overrideActive = 0;
    st->gutterPending = 0;
}

static void
source_pane_trackPosition(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->overrideActive) {
        return;
    }
    unsigned long curAddr = 0;
    (void)machine_findReg(&debugger.machine, "PC", &curAddr);
    curAddr &= 0x00ffffffu;
    if (curAddr != st->lastPcAddr) {
        st->scrollLineValid = 0;
        st->scrollIndexValid = 0;
    }
    st->lastPcAddr = curAddr;
}

static void
source_pane_updateSourceLocation(source_pane_state_t *st)
{
    if (!st) {
        return;
    }
    if (!st->overrideActive && machine_getRunning(debugger.machine)) {
        return;
    }
    unsigned long pc = 0;
    if (st->overrideActive) {
        pc = (unsigned long)st->overrideAddr;
    } else {
        (void)machine_findReg(&debugger.machine, "PC", &pc);
    }
    pc &= 0x00ffffffu;
    if (st->lastResolvedPc == (uint64_t)pc && st->curSrcLine > 0 && st->curSrcPath[0]) {
        return;
    }
    st->lastResolvedPc = (uint64_t)pc;
    st->curSrcLine = 0;
    st->curSrcPath[0] = '\0';

    const char *elf = debugger.config.elfPath;
    if (!elf || !*elf || !debugger.elfValid) {
        return;
    }
    if (!addr2line_start(elf)) {
        return;
    }
    int line = 0;
    char path[PATH_MAX];
    if (addr2line_resolve((uint64_t)pc, path, sizeof(path), &line)) {
        strncpy(st->curSrcPath, path, sizeof(st->curSrcPath) - 1);
        st->curSrcPath[sizeof(st->curSrcPath) - 1] = '\0';
        st->curSrcLine = line;
    }
}

static int
source_pane_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self; (void)ctx; (void)availW;
    return 0; // flexible
}

static void
source_pane_layoutComp(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
source_pane_persistSave(e9ui_component_t *self, e9ui_context_t *ctx, FILE *f)
{
  (void)ctx;
  if (!self || !self->persist_id) {
    return;
  }
  source_pane_state_t *st = (source_pane_state_t*)self->state;
  fprintf(f, "comp.%s.mode=%d\n", self->persist_id, st ? st->viewMode : 0);
}

static void
source_pane_persistLoad(e9ui_component_t *self, e9ui_context_t *ctx, const char *key, const char *value)
{
  (void)ctx;
  if (!self || !key || !value) {
    return;
  }
  source_pane_state_t *st = (source_pane_state_t*)self->state;
  if (strcmp(key, "mode") == 0) {
    int m = (int)strtol(value, NULL, 10);
    // Historical values: 0=Source,1=Mixed,2=ASM. Map any non-zero to ASM.
    st->viewMode = (m == 0) ? source_pane_mode_c : source_pane_mode_a;
  }
}

static void
source_pane_renderAsm(e9ui_component_t *self, e9ui_context_t *ctx)
{
    TTF_Font *useFont = source_pane_resolveFont(ctx);
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    const int padPx = 10;
    SDL_SetRenderDrawColor(ctx->renderer, 20, 20, 24, 255);
    SDL_RenderFillRect(ctx->renderer, &area);
    if (!useFont) {
        return;
    }

    source_pane_state_t *st = (source_pane_state_t*)self->state;
    source_pane_trackPosition(st);

    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        return;
    }
    int maxLines = metrics.maxLines;
    if (maxLines <= 0) {
        maxLines = 1;
    }

    int total = dasm_getTotal();
    if (total <= 0) {
        SDL_Color icol = (SDL_Color){200,160,160,255};
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, useFont, "No disassembly available", icol, &tw, &th);
        if (t) {
            SDL_Rect r = {area.x + padPx, area.y + padPx, tw, th};
            SDL_RenderCopy(ctx->renderer, t, NULL, &r);
        }
        return;
    }

    int curIndex = 0;
    unsigned long curAddr = 0;
    (void)machine_findReg(&debugger.machine, "PC", &curAddr);
    if (!dasm_findIndexForAddr(curAddr, &curIndex)) {
        curIndex = 0;
    }
    int startIndex = curIndex - (maxLines / 2);
    if (st && st->scrollIndexValid) {
        startIndex = st->scrollIndex;
    }
    if (startIndex < 0) {
        startIndex = 0;
    }
    if (startIndex >= total) {
        startIndex = total - 1;
    }
    int endIndex = startIndex + maxLines - 1;
    if (endIndex >= total) {
        endIndex = total - 1;
    }

    const char **lines = NULL;
    const uint64_t *addrs = NULL;
    int first = 0;
    int count = 0;
    if (!dasm_getRangeByIndex(startIndex, endIndex, &lines, &addrs, &first, &count)) {
        return;
    }

    if (count < maxLines && total > 0) {
        int missing = maxLines - count;
        int altStart = first - missing;
        if (altStart < 0) {
            altStart = 0;
        }
        int altEnd = altStart + maxLines - 1;
        if (altEnd >= total) {
            altEnd = total - 1;
        }
        dasm_getRangeByIndex(altStart, altEnd, &lines, &addrs, &first, &count);
    }
    if (st) {
        st->scrollIndex = first;
    }

    int hexw = dasm_getAddrHexWidth();
    if (hexw < 6) {
        hexw = 6;
    }
    if (hexw > 16) {
        hexw = 16;
    }
    char sample[32];
    for (int i = 0; i < hexw; ++i) {
        sample[i] = 'F';
    }
    sample[hexw] = '\0';
    int gutterW = 0;
    int th = 0;
    TTF_SizeText(useFont, sample, &gutterW, &th);
    int gutterPad = e9ui_scale_px(ctx, 16);
    SDL_SetRenderDrawColor(ctx->renderer, 26, 26, 30, 255);
    SDL_Rect gutter = { area.x, area.y, padPx + gutterW + gutterPad, area.h };
    SDL_RenderFillRect(ctx->renderer, &gutter);

    SDL_Color txt = (SDL_Color){220,220,220,255};
    SDL_Color lno = (SDL_Color){160,160,200,255};
    SDL_Color lno_bp_on = (SDL_Color){120,200,120,255};
    SDL_Color lno_bp_off = (SDL_Color){200,140,60,255};
    int textX = area.x + padPx + gutterW + gutterPad;
    int hitW = area.x + area.w - textX - padPx;
    if (hitW < 0) {
        hitW = 0;
    }
    int y = area.y + padPx;
    for (int i = 0; i < count; ++i) {
        uint64_t a = addrs[i];
        const char *ins = lines[i] ? lines[i] : "";
        if (a == curAddr) {
            SDL_SetRenderDrawColor(ctx->renderer, 40, 72, 138, 255);
            SDL_Rect hl = { area.x + 2, y - 2, area.w - 4, metrics.lineHeight + 4 };
            SDL_RenderFillRect(ctx->renderer, &hl);
        }
        char abuf[32];
        snprintf(abuf, sizeof(abuf), "%0*llX", hexw, (unsigned long long)a);
        int nw = 0;
        int nh = 0;
        TTF_SizeText(useFont, abuf, &nw, &nh);
        int lnx = area.x + padPx + (gutterW - nw);
        SDL_Color use_col = lno;
        machine_breakpoint_t *bp = machine_findBreakpointByAddr(&debugger.machine, (uint32_t)a);
        if (bp) {
            use_col = bp->enabled ? lno_bp_on : lno_bp_off;
        }
        void *addrBucket = st ? (void*)&st->bucketAddr : (void*)self;
        void *sourceBucket = st ? (void*)&st->bucketSource : (void*)self;
        e9ui_text_select_drawText(ctx, self, useFont, abuf, use_col, lnx, y,
                                  metrics.lineHeight, 0, addrBucket, 1, 1);
        e9ui_text_select_drawText(ctx, self, useFont, ins, txt, textX, y,
                                  metrics.lineHeight, hitW, sourceBucket, 0, 1);
        y += metrics.lineHeight;
        if (y > area.y + area.h - padPx) {
            break;
        }
    }
}

static void
source_pane_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    TTF_Font *useFont = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    source_pane_state_t *st = (source_pane_state_t*)self->state;
    if (st) {
        source_pane_trackPosition(st);
    }
    int padPx = 10;
    SDL_SetRenderDrawColor(ctx->renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(ctx->renderer, &area);

    if (!useFont) {
        goto done;
    }
    if (st && st->viewMode == source_pane_mode_a) {
        source_pane_renderAsm(self, ctx);
        goto done;
    }
    if (st) {
        source_pane_updateSourceLocation(st);
    }
    const char *path = st ? st->curSrcPath : NULL;
    int curLine = st ? st->curSrcLine : 0;
    if (!path || !*path || curLine <= 0) {
        SDL_Color icol = (SDL_Color){200,160,160,255};
        const char *msg = "No source data available";
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, useFont, msg, icol, &tw, &th);
        if (t) {
            SDL_Rect r = {area.x + padPx, area.y + padPx, tw, th};
            SDL_RenderCopy(ctx->renderer, t, NULL, &r);
        }
        goto done;
    }
    source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, useFont, padPx);
    if (metrics.innerHeight <= 0) {
        goto done;
    }
    int maxLines = metrics.maxLines;
    if (maxLines <= 0) {
        maxLines = 1;
    }
    int start = curLine - (maxLines / 2);
    if (start < 1) {
        start = 1;
    }
    if (st && st->scrollLineValid) {
        start = st->scrollLine;
        if (start < 1) {
            start = 1;
        }
    }
    int end = start + maxLines - 1;

    const char **lines = NULL;
    int count = 0;
    int first = 0;
    int total = 0;
    if (!source_getRange(path, start, end, &lines, &count, &first, &total)) {
        SDL_Color icol = (SDL_Color){200,160,160,255};
        const char *msg = "Failed to load source";
        int tw = 0, th = 0;
        SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, useFont, msg, icol, &tw, &th);
        if (t) {
            SDL_Rect r = {area.x + padPx, area.y + padPx, tw, th};
            SDL_RenderCopy(ctx->renderer, t, NULL, &r);
        }
        goto done;
    }
    // Adjust start if near end of file for better centering
    if (count < maxLines && total > 0) {
        int missing = maxLines - count;
        int altStart = first - missing;
        if (altStart < 1) {
            altStart = 1;
        }
        int altEnd = altStart + maxLines - 1;
        if (altEnd > total) {
            altEnd = total;
        }
        source_getRange(path, altStart, altEnd, &lines, &count, &first, &total);
    }

    if (st) {
        st->scrollLine = first;
    }

    // Compute gutter width based on total line count
    int digits = 1;
    int tmp_total = (total > 0) ? total : (first + count - 1);
    for (int v = tmp_total; v >= 10; v /= 10) {
        digits++;
    }
    if (digits < 3) {
        digits = 3;
    }
    char zeros[16];
    if (digits >= (int)sizeof(zeros)) {
        digits = (int)sizeof(zeros) - 1;
    }
    for (int i=0; i<digits; ++i) {
        zeros[i] = '8';
    }
    zeros[digits] = '\0';
    int gutterW = 0, th = 0;
    TTF_SizeText(useFont, zeros, &gutterW, &th);
    int gutterPad = e9ui_scale_px(ctx, 16);
    // Draw gutter background
    SDL_SetRenderDrawColor(ctx->renderer, 26, 26, 30, 255);
    SDL_Rect gutter = { area.x, area.y, padPx + gutterW + gutterPad, area.h };
    SDL_RenderFillRect(ctx->renderer, &gutter);

    int y = area.y + padPx;
    int lineHeight = metrics.lineHeight;
    SDL_Color txt = (SDL_Color){220,220,220,255};
    SDL_Color lno = (SDL_Color){160,160,180,255};
    SDL_Color lno_bp_on = (SDL_Color){120,200,120,255};
    SDL_Color lno_bp_off = (SDL_Color){200,140,60,255};
    int textX = area.x + padPx + gutterW + gutterPad;
    int hitW = area.x + area.w - textX - padPx;
    if (hitW < 0) {
        hitW = 0;
    }
    const machine_breakpoint_t *bps = NULL;
    int bp_count = 0;
    if (machine_getBreakpoints(&debugger.machine, &bps, &bp_count)) {
        for (int i = 0; i < bp_count; ++i) {
            machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
            if (bp->line <= 0 || !bp->file[0]) {
                breakpoints_resolveLocation(bp);
            }
        }
    } else {
        bps = NULL;
        bp_count = 0;
    }
    for (int i=0; i<count; ++i) {
        const char *ln = lines[i] ? lines[i] : "";
        int lineNo = first + i;
        // Highlight current line (blue shade)
        if (lineNo == curLine) {
            SDL_SetRenderDrawColor(ctx->renderer, 40, 72, 138, 255);
            SDL_Rect hl = { area.x + 2, y - 2, area.w - 4, lineHeight + 4 };
            SDL_RenderFillRect(ctx->renderer, &hl);
        }
        // Line number (right-aligned in gutter)
        char numbuf[16];
        snprintf(numbuf, sizeof(numbuf), "%d", lineNo);
        int nw = 0, nh = 0;
        if (useFont) {
            TTF_SizeText(useFont, numbuf, &nw, &nh);
        }
        int lnx = area.x + padPx + (gutterW - nw);
        if (useFont) {
            int nsw = 0, nsh = 0;
            SDL_Color use_col = lno;
            machine_breakpoint_t *bp = source_pane_findBreakpointForLine(path, lineNo, bps, bp_count);
            if (bp) {
                use_col = bp->enabled ? lno_bp_on : lno_bp_off;
            }
            SDL_Texture *nt = e9ui_text_cache_getText(ctx->renderer, useFont, numbuf, use_col, &nsw, &nsh);
            if (nt) {
                SDL_Rect nr = { lnx, y, nsw, nsh };
                SDL_RenderCopy(ctx->renderer, nt, NULL, &nr);
            }
        }
        void *sourceBucket = st ? (void*)&st->bucketSource : (void*)self;
        e9ui_text_select_drawText(ctx, self, useFont, ln, txt, textX, y,
                                  lineHeight, hitW, sourceBucket, 0, 1);
        y += lineHeight;
        if (y > area.y + area.h - padPx) {
            break;
        }
    }

 done:
    {
      e9ui_component_t* overlay = e9ui_child_find(self, st->toggleBtnMeta);
      if (overlay) {
	source_pane_mode_t mode = source_pane_getMode(self);
	const char *label = (mode == source_pane_mode_c) ? "C" : "ASM";
	e9ui_button_setLabel(overlay, label);
	e9ui_button_measure(overlay, ctx, &overlay->bounds.w,  &overlay->bounds.h);
	overlay->bounds.x = self->bounds.w + self->bounds.x - overlay->bounds.w - e9ui_scale_px(ctx, 8);
	overlay->bounds.y = self->bounds.y + e9ui_scale_px(ctx, 8);      
	overlay->render(overlay, ctx);
      }
    }
}

static int
source_pane_handleEventComp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ev) {
        return 0;
    }
    source_pane_state_t *st = (source_pane_state_t*)self->state;
    source_pane_mode_t mode = st ? st->viewMode : source_pane_mode_c;
    if (ev->type == SDL_MOUSEMOTION) {
        if (!st || !st->gutterPending) {
            return 0;
        }
        int slop = ctx ? e9ui_scale_px(ctx, 4) : 4;
        int dx = ev->motion.x - st->gutterDownX;
        int dy = ev->motion.y - st->gutterDownY;
        if (dx * dx + dy * dy >= slop * slop) {
            st->gutterPending = 0;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        if (!st || !st->gutterPending) {
            return 0;
        }
        st->gutterPending = 0;
        int slop = ctx ? e9ui_scale_px(ctx, 4) : 4;
        int dx = ev->button.x - st->gutterDownX;
        int dy = ev->button.y - st->gutterDownY;
        if (dx * dx + dy * dy >= slop * slop) {
            return 0;
        }
        if (st->gutterMode == source_pane_mode_c) {
            const char *path = st->curSrcPath;
            int lineNo = st->gutterLine;
            if (!path || !path[0] || lineNo <= 0) {
                return 0;
            }
            const machine_breakpoint_t *bps = NULL;
            int bp_count = 0;
            if (machine_getBreakpoints(&debugger.machine, &bps, &bp_count)) {
                for (int i = 0; i < bp_count; ++i) {
                    machine_breakpoint_t *bp = (machine_breakpoint_t*)&bps[i];
                    if (bp->line <= 0 || !bp->file[0]) {
                        breakpoints_resolveLocation(bp);
                    }
                }
            } else {
                bps = NULL;
                bp_count = 0;
            }
            machine_breakpoint_t *existing = source_pane_findBreakpointForLine(path, lineNo, bps, bp_count);
            if (existing) {
                uint32_t addr = (uint32_t)existing->addr;
                if (machine_removeBreakpointByAddr(&debugger.machine, addr)) {
                    libretro_host_debugRemoveBreakpoint(addr);
                    breakpoints_markDirty();
                }
                return 1;
            }
            uint32_t addr = 0;
            if (!source_pane_resolveFileLine(debugger.config.elfPath, path, lineNo, &addr)) {
                return 0;
            }
            machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
            if (bp) {
                strncpy(bp->file, path, sizeof(bp->file) - 1);
                bp->file[sizeof(bp->file) - 1] = '\0';
                bp->line = lineNo;
                libretro_host_debugAddBreakpoint(addr);
                breakpoints_markDirty();
                return 1;
            }
            return 0;
        }
        if (st->gutterMode == source_pane_mode_a) {
            uint32_t addr = st->gutterAddr;
            machine_breakpoint_t *existing = machine_findBreakpointByAddr(&debugger.machine, addr);
            if (existing) {
                if (machine_removeBreakpointByAddr(&debugger.machine, addr)) {
                    libretro_host_debugRemoveBreakpoint(addr);
                    breakpoints_markDirty();
                }
                return 1;
            }
            machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
            if (bp) {
                breakpoints_resolveLocation(bp);
                libretro_host_debugAddBreakpoint(addr);
                breakpoints_markDirty();
                return 1;
            }
            return 0;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = e9ui->mouseX;
        int my = e9ui->mouseY;
        if (source_pane_pointInBounds(self, mx, my)) {
            int wheelY = ev->wheel.y;
            if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wheelY = -wheelY;
            }
            if (wheelY != 0) {
                const int linesPerTick = 1;
                int delta = wheelY * linesPerTick;
                source_pane_adjustScroll(st, mode, delta);
            }
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        if (!source_pane_pointInBounds(self, mx, my)) {
            return 0;
        }
        TTF_Font *useFont = source_pane_resolveFont(ctx);
        if (!useFont) {
            return 0;
        }
        const int padPx = 10;
        if (mode == source_pane_mode_c) {
            source_pane_updateSourceLocation(st);
            const char *path = st ? st->curSrcPath : NULL;
            int curLine = st ? st->curSrcLine : 0;
            if (!path || !path[0] || curLine <= 0) {
                return 0;
            }
            source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, useFont, padPx);
            if (metrics.innerHeight <= 0) {
                return 0;
            }
            int maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
            int start = curLine - (maxLines / 2);
            if (start < 1) {
                start = 1;
            }
            if (st && st->scrollLineValid) {
                start = st->scrollLine;
                if (start < 1) {
                    start = 1;
                }
            }
            int end = start + maxLines - 1;
            const char **lines = NULL;
            int count = 0;
            int first = 0;
            int total = 0;
            if (!source_getRange(path, start, end, &lines, &count, &first, &total)) {
                return 0;
            }
            if (count < maxLines && total > 0) {
                int missing = maxLines - count;
                int altStart = first - missing;
                if (altStart < 1) {
                    altStart = 1;
                }
                int altEnd = altStart + maxLines - 1;
                if (altEnd > total) {
                    altEnd = total;
                }
                source_getRange(path, altStart, altEnd, &lines, &count, &first, &total);
            }

            int digits = 1;
            int tmp_total = (total > 0) ? total : (first + count - 1);
            for (int v = tmp_total; v >= 10; v /= 10) {
                digits++;
            }
            if (digits < 3) {
                digits = 3;
            }
            char zeros[16];
            if (digits >= (int)sizeof(zeros)) {
                digits = (int)sizeof(zeros) - 1;
            }
            for (int i = 0; i < digits; ++i) {
                zeros[i] = '8';
            }
            zeros[digits] = '\0';
            int gutterW = 0;
            int th = 0;
            TTF_SizeText(useFont, zeros, &gutterW, &th);
            int gutterPad = e9ui_scale_px(ctx, 16);
            int gutterRight = self->bounds.x + padPx + gutterW + gutterPad;
            if (mx >= gutterRight) {
                return 0;
            }
            int row = (my - (self->bounds.y + padPx)) / metrics.lineHeight;
            if (row < 0 || row >= count) {
                return 0;
            }
            int lineNo = first + row;
            st->gutterPending = 1;
            st->gutterMode = source_pane_mode_c;
            st->gutterLine = lineNo;
            st->gutterDownX = mx;
            st->gutterDownY = my;
            return 1;
        }
        if (mode == source_pane_mode_a) {
            source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, useFont, padPx);
            if (metrics.innerHeight <= 0) {
                return 0;
            }
            int maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
            int total = dasm_getTotal();
            if (total <= 0) {
                return 0;
            }
            int curIndex = 0;
            unsigned long curAddr = 0;
            (void)machine_findReg(&debugger.machine, "PC", &curAddr);
            if (!dasm_findIndexForAddr(curAddr, &curIndex)) {
                curIndex = 0;
            }
            int startIndex = curIndex - (maxLines / 2);
            if (st && st->scrollIndexValid) {
                startIndex = st->scrollIndex;
            }
            if (startIndex < 0) {
                startIndex = 0;
            }
            if (startIndex >= total) {
                startIndex = total - 1;
            }
            int endIndex = startIndex + maxLines - 1;
            if (endIndex >= total) {
                endIndex = total - 1;
            }
            const char **lines = NULL;
            const uint64_t *addrs = NULL;
            int first = 0;
            int count = 0;
            if (!dasm_getRangeByIndex(startIndex, endIndex, &lines, &addrs, &first, &count)) {
                return 0;
            }
            if (count < maxLines && total > 0) {
                int missing = maxLines - count;
                int altStart = first - missing;
                if (altStart < 0) {
                    altStart = 0;
                }
                int altEnd = altStart + maxLines - 1;
                if (altEnd >= total) {
                    altEnd = total - 1;
                }
                dasm_getRangeByIndex(altStart, altEnd, &lines, &addrs, &first, &count);
            }
            int hexw = dasm_getAddrHexWidth();
            if (hexw < 6) {
                hexw = 6;
            }
            if (hexw > 16) {
                hexw = 16;
            }
            char sample[32];
            for (int i = 0; i < hexw; ++i) {
                sample[i] = 'F';
            }
            sample[hexw] = '\0';
            int gutterW = 0;
            int th = 0;
            TTF_SizeText(useFont, sample, &gutterW, &th);
            int gutterPad = e9ui_scale_px(ctx, 16);
            int gutterRight = self->bounds.x + padPx + gutterW + gutterPad;
            if (mx >= gutterRight) {
                return 0;
            }
            int row = (my - (self->bounds.y + padPx)) / metrics.lineHeight;
            if (row < 0 || row >= count) {
                return 0;
            }
            st->gutterPending = 1;
            st->gutterMode = source_pane_mode_a;
            st->gutterAddr = (uint32_t)(addrs[row] & 0x00ffffffu);
            st->gutterDownX = mx;
            st->gutterDownY = my;
            return 1;
        }
    }
    if (ev->type == SDL_KEYDOWN && ctx && e9ui_getFocus(ctx) == self) {
        const int padPx = 10;
        TTF_Font *useFont = source_pane_resolveFont(ctx);
        int maxLines = 1;
        if (useFont) {
            source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(self, useFont, padPx);
            maxLines = metrics.maxLines;
        }
        if (maxLines <= 0) {
            maxLines = 1;
        }
        SDL_Keycode kc = ev->key.keysym.sym;
        switch (kc) {
        case SDLK_PAGEUP:
            source_pane_adjustScroll(st, mode, -maxLines);
            return 1;
        case SDLK_PAGEDOWN:
            source_pane_adjustScroll(st, mode, maxLines);
            return 1;
        case SDLK_UP:
            source_pane_adjustScroll(st, mode, -1);
            return 1;
        case SDLK_DOWN:
            source_pane_adjustScroll(st, mode, 1);
            return 1;
        case SDLK_HOME:
            source_pane_scrollToStart(st, mode);
            return 1;
        case SDLK_END:
            source_pane_scrollToEnd(st, mode, maxLines);
            return 1;
        case SDLK_f:
            source_pane_followCurrent(st);
            return 1;
        default:
            break;
        }
    }
    return 0;
}

static void
source_toggleMode(e9ui_context_t *ctx, void *user)
{
  view_toggle_t* state = user;
  e9ui_component_t* pane = state->pane;
  e9ui_component_t* button = state->button;
  source_pane_mode_t mode = source_pane_getMode(pane);
  mode = (mode == source_pane_mode_c) ? source_pane_mode_a : source_pane_mode_c;
  source_pane_setMode(pane, mode);
  const char *label = (mode == source_pane_mode_c) ? "C" : "ASM";
  if (button) {
    e9ui_button_setLabel(button, label);
  }

  (void)ctx;
} 

e9ui_component_t *
source_pane_make(void)
{
  e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
  c->name = "source_pane";
  source_pane_state_t *st = (source_pane_state_t*)alloc_calloc(1, sizeof(source_pane_state_t));
  st->viewMode = source_pane_mode_c;
  st->scrollLine = 1;
  st->scrollLineValid = 0;
  st->scrollIndex = 0;
  st->scrollIndexValid = 0;
  c->state = st;
  c->focusable = 0;
  c->preferredHeight = source_pane_preferredHeight;
  c->layout = source_pane_layoutComp;
  c->render = source_pane_render;
  c->handleEvent = source_pane_handleEventComp;
  c->persistSave = source_pane_persistSave;
  c->persistLoad = source_pane_persistLoad;


  view_toggle_t* btn_state = malloc(sizeof(*btn_state));  
  e9ui_component_t *btn_mode = e9ui_button_make("C", source_toggleMode, btn_state);  
  btn_state->button = btn_mode;
  btn_state->pane = c;
  e9ui_button_setLargestLabel(btn_mode, "ASM");
  st->toggleBtnMeta = alloc_strdup("toggle");
  e9ui_child_add(c, btn_mode, st->toggleBtnMeta);
  
  return c;
}

void source_pane_setMode(e9ui_component_t *comp, source_pane_mode_t mode)
{
    if (!comp || !comp->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (mode != source_pane_mode_c && mode != source_pane_mode_a) {
        mode = source_pane_mode_c;
    }
    st->viewMode = mode;
    st->gutterPending = 0;
}

source_pane_mode_t source_pane_getMode(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return source_pane_mode_c;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    return st->viewMode;
}

void
source_pane_setToggleVisible(e9ui_component_t *comp, int visible)
{
    if (!comp || !comp->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (!st->toggleBtnMeta) {
        return;
    }
    e9ui_component_t *overlay = e9ui_child_find(comp, st->toggleBtnMeta);
    if (!overlay) {
        return;
    }
    e9ui_setHidden(overlay, visible ? 0 : 1);
}

void source_pane_markNeedsRefresh(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    st->scrollLineValid = 0;
    st->scrollIndexValid = 0;
    st->scrollLine = 1;
    st->scrollIndex = 0;
    st->gutterPending = 0;
}

void
source_pane_centerOnAddress(e9ui_component_t *comp, e9ui_context_t *ctx, uint32_t addr)
{
    if (!comp) {
        return;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (!st) {
        return;
    }
    st->overrideActive = 1;
    st->overrideAddr = (uint64_t)(addr & 0x00ffffffu);
    st->lastResolvedPc = 0;

    TTF_Font *useFont = source_pane_resolveFont(ctx);
    int maxLines = 1;
    if (useFont) {
        source_pane_line_metrics_t metrics = source_pane_computeLineMetrics(comp, useFont, 10);
        maxLines = metrics.maxLines > 0 ? metrics.maxLines : 1;
    }

    st->curSrcLine = 0;
    st->curSrcPath[0] = '\0';
    source_pane_updateSourceLocation(st);
    if (st->curSrcLine > 0) {
        int start = st->curSrcLine - (maxLines / 2);
        if (start < 1) {
            start = 1;
        }
        st->scrollLine = start;
        st->scrollLineValid = 1;
    }

    int idx = 0;
    if (dasm_findIndexForAddr((uint64_t)addr, &idx)) {
        int start = idx - (maxLines / 2);
        if (start < 0) {
            start = 0;
        }
        st->scrollIndex = start;
        st->scrollIndexValid = 1;
    }
    st->gutterPending = 0;
}

int
source_pane_getCurrentFile(e9ui_component_t *comp, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!comp) {
        return 0;
    }
    source_pane_state_t *st = (source_pane_state_t*)comp->state;
    if (!st || st->viewMode != source_pane_mode_c) {
        return 0;
    }
    if (!st->overrideActive && machine_getRunning(debugger.machine)) {
        if (!st->curSrcPath[0]) {
            return 0;
        }
    } else {
        source_pane_updateSourceLocation(st);
    }
    if (!st->curSrcPath[0]) {
        return 0;
    }
    strncpy(out, st->curSrcPath, cap - 1);
    out[cap - 1] = '\0';
    return 1;
}
