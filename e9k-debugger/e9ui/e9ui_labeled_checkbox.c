/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include <string.h>

typedef struct e9ui_labeled_checkbox_state {
    char *label;
    int labelWidth_px;
    int totalWidth_px;
    e9ui_component_t *checkbox;
    char **infoLines;
    int infoLineCount;
    int infoLineCap;
    char **infoWrappedLines;
    int infoWrappedLineCount;
    int infoWrappedLineCap;
    int infoWrappedWidth;
    TTF_Font *infoWrappedFont;
    e9ui_labeled_checkbox_cb_t cb;
    void *user;
    e9ui_component_t *self;
} e9ui_labeled_checkbox_state_t;

static char *
e9ui_labeled_checkbox_strndup(const char *str, size_t len)
{
    if (!str) {
        return NULL;
    }
    char *dup = alloc_alloc(len + 1);
    if (!dup) {
        return NULL;
    }
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

static void
e9ui_labeled_checkbox_clearInfo(e9ui_labeled_checkbox_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->infoLines) {
        for (int i = 0; i < st->infoLineCount; ++i) {
            alloc_free(st->infoLines[i]);
        }
        alloc_free(st->infoLines);
        st->infoLines = NULL;
    }
    st->infoLineCount = 0;
    st->infoLineCap = 0;
}

static void
e9ui_labeled_checkbox_clearWrappedInfo(e9ui_labeled_checkbox_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->infoWrappedLines) {
        for (int i = 0; i < st->infoWrappedLineCount; ++i) {
            alloc_free(st->infoWrappedLines[i]);
        }
        alloc_free(st->infoWrappedLines);
        st->infoWrappedLines = NULL;
    }
    st->infoWrappedLineCount = 0;
    st->infoWrappedLineCap = 0;
    st->infoWrappedWidth = 0;
    st->infoWrappedFont = NULL;
}

static void
e9ui_labeled_checkbox_addInfoLine(e9ui_labeled_checkbox_state_t *st, const char *line, size_t len)
{
    if (!st) {
        return;
    }
    if (st->infoLineCount >= st->infoLineCap) {
        int nextCap = st->infoLineCap > 0 ? st->infoLineCap * 2 : 4;
        char **next = (char**)alloc_realloc(st->infoLines, (size_t)nextCap * sizeof(*next));
        if (!next) {
            return;
        }
        st->infoLines = next;
        st->infoLineCap = nextCap;
    }

    if (len > 0 && line[len - 1] == '\r') {
        len--;
    }

    char *dup = NULL;
    if (len == 0) {
        dup = alloc_strdup(" ");
    } else {
        dup = e9ui_labeled_checkbox_strndup(line, len);
    }
    if (!dup) {
        return;
    }
    st->infoLines[st->infoLineCount++] = dup;
}

static void
e9ui_labeled_checkbox_addWrappedLine(e9ui_labeled_checkbox_state_t *st, const char *line, size_t len)
{
    if (!st) {
        return;
    }
    if (st->infoWrappedLineCount >= st->infoWrappedLineCap) {
        int nextCap = st->infoWrappedLineCap > 0 ? st->infoWrappedLineCap * 2 : 8;
        char **next = (char**)alloc_realloc(st->infoWrappedLines, (size_t)nextCap * sizeof(*next));
        if (!next) {
            return;
        }
        st->infoWrappedLines = next;
        st->infoWrappedLineCap = nextCap;
    }

    if (len > 0 && line[len - 1] == '\r') {
        len--;
    }

    char *dup = NULL;
    if (len == 0) {
        dup = alloc_strdup(" ");
    } else {
        dup = e9ui_labeled_checkbox_strndup(line, len);
    }
    if (!dup) {
        return;
    }
    st->infoWrappedLines[st->infoWrappedLineCount++] = dup;
}

static void
e9ui_labeled_checkbox_wrapOneInfoLine(e9ui_labeled_checkbox_state_t *st, TTF_Font *font,
                                      const char *line, int wrapWidth)
{
    if (!st) {
        return;
    }
    if (!line) {
        e9ui_labeled_checkbox_addWrappedLine(st, " ", 1);
        return;
    }
    if (!font || wrapWidth <= 0) {
        e9ui_labeled_checkbox_addWrappedLine(st, line, strlen(line));
        return;
    }

    const char *p = line;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        e9ui_labeled_checkbox_addWrappedLine(st, " ", 1);
        return;
    }

    while (*p) {
        const char *bestEnd = NULL;
        const char *scan = p;
        while (1) {
            const char *wordEnd = scan;
            while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\t') {
                wordEnd++;
            }
            if (wordEnd == scan) {
                break;
            }

            const char *candidateEnd = wordEnd;
            const char *after = wordEnd;
            while (*after == ' ' || *after == '\t') {
                after++;
            }

            int w = 0;
            size_t len = (size_t)(candidateEnd - p);
            char *tmp = e9ui_labeled_checkbox_strndup(p, len);
            if (!tmp) {
                return;
            }
            TTF_SizeUTF8(font, tmp, &w, NULL);
            alloc_free(tmp);

            if (w <= wrapWidth || !bestEnd) {
                bestEnd = candidateEnd;
                scan = after;
                if (*after == '\0') {
                    break;
                }
                continue;
            }
            break;
        }

        if (!bestEnd) {
            bestEnd = p;
            while (*bestEnd && *bestEnd != ' ' && *bestEnd != '\t') {
                bestEnd++;
            }
            if (bestEnd == p) {
                bestEnd++;
            }
        }

        e9ui_labeled_checkbox_addWrappedLine(st, p, (size_t)(bestEnd - p));

        p = bestEnd;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }
}

static void
e9ui_labeled_checkbox_ensureWrappedInfo(e9ui_labeled_checkbox_state_t *st, e9ui_context_t *ctx, int wrapWidth)
{
    if (!st || st->infoLineCount <= 0) {
        return;
    }

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    if (st->infoWrappedLines && st->infoWrappedFont == font && st->infoWrappedWidth == wrapWidth) {
        return;
    }

    e9ui_labeled_checkbox_clearWrappedInfo(st);
    st->infoWrappedWidth = wrapWidth;
    st->infoWrappedFont = font;

    for (int i = 0; i < st->infoLineCount; ++i) {
        const char *line = st->infoLines[i] ? st->infoLines[i] : "";
        e9ui_labeled_checkbox_wrapOneInfoLine(st, font, line, wrapWidth);
    }
}

static void
labeled_checkbox_notify(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)user;
    if (!st || !st->cb) {
        return;
    }
    st->cb(self, ctx, selected, st->user);
}

static int
labeled_checkbox_targetHeight(e9ui_context_t *ctx)
{
    const e9k_theme_button_t *theme = &e9ui->theme.button;
    TTF_Font *useFont = theme->font ? theme->font : (ctx ? ctx->font : NULL);
    int lh = useFont ? TTF_FontHeight(useFont) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int padding = 0;
    if (theme->padding > 0) {
        padding = e9ui_scale_px(ctx, theme->padding);
    }
    int h = lh + 8 + padding * 2;
    return h > 0 ? h : 0;
}

static int
labeled_checkbox_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st || !st->checkbox) {
        return 0;
    }
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    int gap = e9ui_scale_px(ctx, 8);
    int totalW = availW;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int checkboxW = totalW - labelW - gap;
    if (checkboxW < 0) {
        checkboxW = 0;
    }
    int h = st->checkbox->preferredHeight ? st->checkbox->preferredHeight(st->checkbox, ctx, checkboxW) : 0;
    int targetH = labeled_checkbox_targetHeight(ctx);
    if (targetH > h) {
        h = targetH;
    }
    if (st->infoLineCount > 0) {
        TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
        int lh = font ? TTF_FontHeight(font) : 16;
        if (lh <= 0) {
            lh = 16;
        }
        e9ui_labeled_checkbox_ensureWrappedInfo(st, ctx, checkboxW);
        int lineCount = st->infoWrappedLineCount > 0 ? st->infoWrappedLineCount : st->infoLineCount;
        int padY = e9ui_scale_px(ctx, 4);
        h += padY + (lh * lineCount);
    }
    return h;
}

static void
labeled_checkbox_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st || !st->checkbox) {
        return;
    }
    int gap = e9ui_scale_px(ctx, 8);
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    if (labelW == 0 && st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        if (font) {
            int textW = 0;
            TTF_SizeText(font, st->label, &textW, NULL);
            labelW = textW + gap;
        }
    }
    int totalW = bounds.w;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int checkboxW = totalW - labelW - gap;
    if (checkboxW < 0) {
        checkboxW = 0;
    }
    int checkboxH = st->checkbox->preferredHeight ? st->checkbox->preferredHeight(st->checkbox, ctx, checkboxW) : 0;
    int rowH = checkboxH;
    int targetH = labeled_checkbox_targetHeight(ctx);
    if (targetH > rowH) {
        rowH = targetH;
    }
    int rowX = bounds.x + (bounds.w - totalW) / 2;
    int rowY = bounds.y;
    int checkboxY = rowY + (rowH - checkboxH) / 2;
    if (checkboxY < rowY) {
        checkboxY = rowY;
    }
    e9ui_rect_t checkboxRect = { rowX + labelW + gap, checkboxY, checkboxW, checkboxH };
    st->checkbox->layout(st->checkbox, ctx, checkboxRect);
}

static void
labeled_checkbox_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st) {
        return;
    }
    int gap = e9ui_scale_px(ctx, 8);
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    if (labelW == 0 && st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        if (font) {
            int textW = 0;
            TTF_SizeText(font, st->label, &textW, NULL);
            labelW = textW + gap;
        }
    }
    int totalW = self->bounds.w;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int rowX = self->bounds.x + (self->bounds.w - totalW) / 2;
    int checkboxW = totalW - labelW - gap;
    if (checkboxW < 0) {
        checkboxW = 0;
    }
    int checkboxH = 0;
    if (st->checkbox && st->checkbox->bounds.h > 0) {
        checkboxH = st->checkbox->bounds.h;
    } else if (st->checkbox && st->checkbox->preferredHeight) {
        checkboxH = st->checkbox->preferredHeight(st->checkbox, ctx, checkboxW);
    }
    int rowH = checkboxH;
    int targetH = labeled_checkbox_targetHeight(ctx);
    if (targetH > rowH) {
        rowH = targetH;
    }
    int rowY = self->bounds.y;
    int checkboxY = rowY + (rowH - checkboxH) / 2;
    if (checkboxY < rowY) {
        checkboxY = rowY;
    }
    e9ui_rect_t checkboxRect = { rowX + labelW + gap, checkboxY, checkboxW, checkboxH };

    if (st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        if (font) {
            SDL_Color color = (SDL_Color){220, 220, 220, 255};
            int tw = 0;
            int th = 0;
            SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->label, color, &tw, &th);
            if (tex) {
                int layoutLabelW = st->labelWidth_px > 0 ? labelW : tw + gap;
                int textX = rowX + layoutLabelW - tw;
                int textY = rowY + (rowH - th) / 2;
                if (textY < rowY) {
                    textY = rowY;
                }
                SDL_Rect dst = { textX, textY, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }
    if (st->checkbox && st->checkbox->render) {
        st->checkbox->render(st->checkbox, ctx);
    }

    if (st->infoLineCount > 0) {
        TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
        if (font) {
            e9ui_labeled_checkbox_ensureWrappedInfo(st, ctx, checkboxRect.w);
            SDL_Color color = (SDL_Color){140, 140, 140, 255};
            int lh = TTF_FontHeight(font);
            if (lh <= 0) {
                lh = 16;
            }
            int padY = e9ui_scale_px(ctx, 4);
            int baseY = rowY + rowH + padY;
            int lineCount = st->infoWrappedLineCount > 0 ? st->infoWrappedLineCount : st->infoLineCount;
            for (int i = 0; i < lineCount; ++i) {
                const char *line = (st->infoWrappedLineCount > 0) ? st->infoWrappedLines[i] : st->infoLines[i];
                if (!line) {
                    continue;
                }
                int tw = 0;
                int th = 0;
                SDL_Texture *tex = e9ui_text_cache_getUTF8(ctx->renderer, font, line, color, &tw, &th);
                if (!tex) {
                    continue;
                }
                int y = baseY + (lh * i) + (lh - th) / 2;
                if (y < baseY + lh * i) {
                    y = baseY + lh * i;
                }
                SDL_Rect dst = { checkboxRect.x, y, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }
}

static void
labeled_checkbox_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
    e9ui_labeled_checkbox_clearInfo(st);
    e9ui_labeled_checkbox_clearWrappedInfo(st);
}

e9ui_component_t *
e9ui_labeled_checkbox_make(const char *label, int labelWidth_px, int totalWidth_px,
                           int selected, e9ui_labeled_checkbox_cb_t cb, void *user)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)alloc_calloc(1, sizeof(*st));
    st->labelWidth_px = labelWidth_px;
    st->totalWidth_px = totalWidth_px;
    if (label && *label) {
        st->label = alloc_strdup(label);
    }
    st->checkbox = e9ui_checkbox_make("", selected, labeled_checkbox_notify, st);
    st->cb = cb;
    st->user = user;
    c->name = "e9ui_labeledCheckbox";
    c->state = st;
    c->preferredHeight = labeled_checkbox_preferredHeight;
    c->layout = labeled_checkbox_layout;
    c->render = labeled_checkbox_render;
    c->dtor = labeled_checkbox_dtor;
    st->self = c;
    if (st->checkbox) {
        e9ui_child_add(c, st->checkbox, 0);
    }
    return c;
}

void
e9ui_labeled_checkbox_setInfo(e9ui_component_t *comp, const char *info)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    e9ui_labeled_checkbox_clearInfo(st);
    e9ui_labeled_checkbox_clearWrappedInfo(st);
    if (!info || !*info) {
        return;
    }
    const char *start = info;
    const char *p = info;
    while (*p) {
        if (*p == '\n') {
            e9ui_labeled_checkbox_addInfoLine(st, start, (size_t)(p - start));
            p++;
            start = p;
            continue;
        }
        p++;
    }
    if (p != start) {
        e9ui_labeled_checkbox_addInfoLine(st, start, (size_t)(p - start));
    } else if (st->infoLineCount == 0) {
        e9ui_labeled_checkbox_addInfoLine(st, " ", 1);
    }
}

void
e9ui_labeled_checkbox_setLabelWidth(e9ui_component_t *comp, int labelWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    st->labelWidth_px = labelWidth_px;
}

void
e9ui_labeled_checkbox_setTotalWidth(e9ui_component_t *comp, int totalWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    st->totalWidth_px = totalWidth_px;
}

void
e9ui_labeled_checkbox_setSelected(e9ui_component_t *comp, int selected, e9ui_context_t *ctx)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    if (!st || !st->checkbox) {
        return;
    }
    e9ui_checkbox_setSelected(st->checkbox, selected, ctx);
}

int
e9ui_labeled_checkbox_isSelected(e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return 0;
    }
    e9ui_labeled_checkbox_state_t *st = (e9ui_labeled_checkbox_state_t*)comp->state;
    if (!st || !st->checkbox) {
        return 0;
    }
    return e9ui_checkbox_isSelected(st->checkbox);
}

e9ui_component_t *
e9ui_labeled_checkbox_getCheckbox(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_checkbox_state_t *st = (const e9ui_labeled_checkbox_state_t*)comp->state;
    return st ? st->checkbox : NULL;
}
