/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

#include <string.h>

typedef struct e9ui_labeled_select_state {
    char *label;
    int labelWidth_px;
    int totalWidth_px;
    e9ui_select_option_t *options;
    int optionCount;
    int selectedIndex;
    e9ui_component_t *textbox;
    char **infoLines;
    int infoLineCount;
    int infoLineCap;
    char **infoWrappedLines;
    int infoWrappedLineCount;
    int infoWrappedLineCap;
    int infoWrappedWidth;
    TTF_Font *infoWrappedFont;
    e9ui_labeled_select_change_cb_t onChange;
    void *onChangeUser;
    e9ui_component_t *self;
} e9ui_labeled_select_state_t;

static char *
e9ui_labeled_select_strndup(const char *str, size_t len)
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
e9ui_labeled_select_clearInfo(e9ui_labeled_select_state_t *st)
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
e9ui_labeled_select_clearWrappedInfo(e9ui_labeled_select_state_t *st)
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
e9ui_labeled_select_addInfoLine(e9ui_labeled_select_state_t *st, const char *line, size_t len)
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
        dup = e9ui_labeled_select_strndup(line, len);
    }
    if (!dup) {
        return;
    }
    st->infoLines[st->infoLineCount++] = dup;
}

static void
e9ui_labeled_select_addWrappedLine(e9ui_labeled_select_state_t *st, const char *line, size_t len)
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
        dup = e9ui_labeled_select_strndup(line, len);
    }
    if (!dup) {
        return;
    }
    st->infoWrappedLines[st->infoWrappedLineCount++] = dup;
}

static int
e9ui_labeled_select_utf8ByteOffsetForChars(const char *text, int charCount)
{
    if (!text || charCount <= 0) {
        return 0;
    }
    int offset = 0;
    int remaining = charCount;
    while (text[offset] && remaining > 0) {
        unsigned char c = (unsigned char)text[offset];
        int step = 1;
        if ((c & 0x80) == 0x00) {
            step = 1;
        } else if ((c & 0xE0) == 0xC0) {
            step = 2;
        } else if ((c & 0xF0) == 0xE0) {
            step = 3;
        } else if ((c & 0xF8) == 0xF0) {
            step = 4;
        } else {
            step = 1;
        }
        for (int i = 0; i < step; ++i) {
            if (!text[offset]) {
                break;
            }
            offset++;
        }
        remaining--;
    }
    return offset;
}

#if !(defined(SDL_TTF_VERSION_ATLEAST) && SDL_TTF_VERSION_ATLEAST(2, 0, 18))
static int
e9ui_labeled_select_utf8CharCount(const char *text)
{
    if (!text || !*text) {
        return 0;
    }
    int count = 0;
    int offset = 0;
    while (text[offset]) {
        unsigned char c = (unsigned char)text[offset];
        int step = 1;
        if ((c & 0x80) == 0x00) {
            step = 1;
        } else if ((c & 0xE0) == 0xC0) {
            step = 2;
        } else if ((c & 0xF0) == 0xE0) {
            step = 3;
        } else if ((c & 0xF8) == 0xF0) {
            step = 4;
        } else {
            step = 1;
        }
        for (int i = 0; i < step; ++i) {
            if (!text[offset]) {
                break;
            }
            offset++;
        }
        count++;
    }
    return count;
}
#endif

static int
e9ui_labeled_select_measureUtf8(TTF_Font *font, const char *text, int measureWidth, int *extent, int *count)
{
    if (extent) {
        *extent = 0;
    }
    if (count) {
        *count = 0;
    }
    if (!font || !text) {
        return -1;
    }

#if defined(SDL_TTF_VERSION_ATLEAST) && SDL_TTF_VERSION_ATLEAST(2, 0, 18)
    return TTF_MeasureUTF8(font, text, measureWidth, extent, count);
#else
    if (!*text) {
        return 0;
    }
    if (measureWidth <= 0) {
        return 0;
    }

    int fullW = 0;
    if (TTF_SizeUTF8(font, text, &fullW, NULL) == 0 && fullW <= measureWidth) {
        if (extent) {
            *extent = fullW;
        }
        if (count) {
            *count = e9ui_labeled_select_utf8CharCount(text);
        }
        return 0;
    }

    int totalChars = e9ui_labeled_select_utf8CharCount(text);
    int lo = 1;
    int hi = totalChars;
    int bestChars = 0;
    int bestExtent = 0;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int byteCount = e9ui_labeled_select_utf8ByteOffsetForChars(text, mid);
        if (byteCount <= 0) {
            hi = mid - 1;
            continue;
        }

        char *scratch = alloc_alloc((size_t)byteCount + 1);
        if (!scratch) {
            return -1;
        }
        memcpy(scratch, text, (size_t)byteCount);
        scratch[byteCount] = '\0';

        int w = 0;
        int ok = (TTF_SizeUTF8(font, scratch, &w, NULL) == 0) ? 1 : 0;
        alloc_free(scratch);
        if (!ok) {
            return -1;
        }

        if (w <= measureWidth) {
            bestChars = mid;
            bestExtent = w;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (extent) {
        *extent = bestExtent;
    }
    if (count) {
        *count = bestChars;
    }
    return 0;
#endif
}

static void
e9ui_labeled_select_wrapOneInfoLine(e9ui_labeled_select_state_t *st, TTF_Font *font, const char *line, int wrapWidth)
{
    if (!st) {
        return;
    }
    if (!line || !*line) {
        e9ui_labeled_select_addWrappedLine(st, "", 0);
        return;
    }
    if (wrapWidth <= 0 || !font) {
        e9ui_labeled_select_addWrappedLine(st, line, strlen(line));
        return;
    }

    int emittedAny = 0;
    const char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            if (!emittedAny) {
                e9ui_labeled_select_addWrappedLine(st, "", 0);
            }
            break;
        }

        int extent = 0;
        int charCount = 0;
        if (e9ui_labeled_select_measureUtf8(font, p, wrapWidth, &extent, &charCount) != 0 || charCount <= 0) {
            charCount = 1;
        }

        int byteCount = e9ui_labeled_select_utf8ByteOffsetForChars(p, charCount);
        if (byteCount <= 0) {
            byteCount = 1;
        }

        if (!p[byteCount]) {
            e9ui_labeled_select_addWrappedLine(st, p, strlen(p));
            emittedAny = 1;
            break;
        }

        int breakAt = -1;
        for (int i = 0; i < byteCount; ++i) {
            if (p[i] == ' ' || p[i] == '\t') {
                breakAt = i;
            }
        }
        if (breakAt > 0) {
            e9ui_labeled_select_addWrappedLine(st, p, (size_t)breakAt);
            emittedAny = 1;
            p += breakAt;
        } else {
            e9ui_labeled_select_addWrappedLine(st, p, (size_t)byteCount);
            emittedAny = 1;
            p += byteCount;
        }
    }
}

static void
e9ui_labeled_select_ensureWrappedInfo(e9ui_labeled_select_state_t *st, e9ui_context_t *ctx, int wrapWidth)
{
    if (!st || st->infoLineCount <= 0) {
        return;
    }

    TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : (ctx ? ctx->font : NULL);
    if (st->infoWrappedLines && st->infoWrappedFont == font && st->infoWrappedWidth == wrapWidth) {
        return;
    }

    e9ui_labeled_select_clearWrappedInfo(st);
    st->infoWrappedWidth = wrapWidth;
    st->infoWrappedFont = font;

    for (int i = 0; i < st->infoLineCount; ++i) {
        const char *line = st->infoLines[i] ? st->infoLines[i] : "";
        e9ui_labeled_select_wrapOneInfoLine(st, font, line, wrapWidth);
    }
}

static const e9ui_select_option_t *
e9ui_labeled_select_currentOption(const e9ui_labeled_select_state_t *st)
{
    if (!st || !st->options || st->optionCount <= 0) {
        return NULL;
    }
    int index = st->selectedIndex;
    if (index < 0) {
        index = 0;
    }
    if (index >= st->optionCount) {
        index = st->optionCount - 1;
    }
    return &st->options[index];
}

static const char *
e9ui_labeled_select_currentValue(const e9ui_labeled_select_state_t *st)
{
    const e9ui_select_option_t *opt = e9ui_labeled_select_currentOption(st);
    return opt ? opt->value : NULL;
}

static int
e9ui_labeled_select_findIndex(const e9ui_labeled_select_state_t *st, const char *value)
{
    if (!st || !st->options || st->optionCount <= 0 || !value) {
        return -1;
    }
    for (int i = 0; i < st->optionCount; ++i) {
        const char *v = st->options[i].value;
        if (v && strcmp(v, value) == 0) {
            return i;
        }
    }
    return -1;
}

static int
e9ui_labeled_select_targetHeight(e9ui_context_t *ctx)
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

static void
e9ui_labeled_select_syncTextboxLabel(e9ui_labeled_select_state_t *st)
{
    if (!st || !st->textbox) {
        return;
    }
    const char *value = e9ui_labeled_select_currentValue(st);
    if (value) {
        e9ui_textbox_setSelectedValue(st->textbox, value);
    } else {
        e9ui_textbox_setText(st->textbox, "");
    }
}

static void
e9ui_labeled_select_notifyChange(e9ui_context_t *ctx, e9ui_labeled_select_state_t *st)
{
    if (!st || !st->onChange) {
        return;
    }
    const char *value = e9ui_labeled_select_currentValue(st);
    st->onChange(ctx, st->self, value ? value : "", st->onChangeUser);
}

static void
e9ui_labeled_select_optionSelected(e9ui_context_t *ctx, e9ui_component_t *comp, const char *value, void *user)
{
    (void)comp;
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)user;
    if (!st || !st->options || st->optionCount <= 0 || !value) {
        return;
    }
    int idx = e9ui_labeled_select_findIndex(st, value);
    if (idx < 0) {
        return;
    }
    st->selectedIndex = idx;
    e9ui_labeled_select_notifyChange(ctx, st);
}

static int
e9ui_labeled_select_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
    if (!st) {
        return 0;
    }
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    int gap = e9ui_scale_px(ctx, 8);
    if (labelW == 0 && st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        if (font) {
            int textW = 0;
            TTF_SizeText(font, st->label, &textW, NULL);
            labelW = textW + gap;
        }
    }
    int totalW = availW;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int buttonW = totalW - labelW - gap;
    if (buttonW < 0) {
        buttonW = 0;
    }
    int buttonH = 0;
    if (st->textbox && st->textbox->preferredHeight) {
        buttonH = st->textbox->preferredHeight(st->textbox, ctx, buttonW);
    }
    int targetH = e9ui_labeled_select_targetHeight(ctx);
    if (targetH > buttonH) {
        buttonH = targetH;
    }
    int extraH = 0;
    if (st->infoLineCount > 0) {
        TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
        int lh = font ? TTF_FontHeight(font) : 16;
        if (lh <= 0) {
            lh = 16;
        }
        e9ui_labeled_select_ensureWrappedInfo(st, ctx, buttonW);
        int lineCount = st->infoWrappedLineCount > 0 ? st->infoWrappedLineCount : st->infoLineCount;
        int padY = e9ui_scale_px(ctx, 4);
        extraH = padY + (lh * lineCount);
    }
    return buttonH + extraH;
}

static void
e9ui_labeled_select_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
    if (!st || !st->textbox) {
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
    int buttonW = totalW - labelW - gap;
    if (buttonW < 0) {
        buttonW = 0;
    }
    int buttonH = st->textbox->preferredHeight ? st->textbox->preferredHeight(st->textbox, ctx, buttonW) : 0;
    int targetH = e9ui_labeled_select_targetHeight(ctx);
    if (targetH > buttonH) {
        buttonH = targetH;
    }
    int rowX = bounds.x + (bounds.w - totalW) / 2;
    int rowY = bounds.y;
    e9ui_rect_t buttonRect = { rowX + labelW + gap, rowY, buttonW, buttonH };
    st->textbox->layout(st->textbox, ctx, buttonRect);
}

static void
e9ui_labeled_select_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
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
    int buttonW = totalW - labelW - gap;
    if (buttonW < 0) {
        buttonW = 0;
    }
    int buttonH = 0;
    if (st->textbox && st->textbox->bounds.h > 0) {
        buttonH = st->textbox->bounds.h;
    } else if (st->textbox && st->textbox->preferredHeight) {
        buttonH = st->textbox->preferredHeight(st->textbox, ctx, buttonW);
    }
    int targetH = e9ui_labeled_select_targetHeight(ctx);
    if (targetH > buttonH) {
        buttonH = targetH;
    }
    e9ui_rect_t buttonRect = { rowX + labelW + gap, self->bounds.y, buttonW, buttonH };

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
                int rowY = self->bounds.y + (buttonRect.h - th) / 2;
                if (rowY < self->bounds.y) {
                    rowY = self->bounds.y;
                }
                SDL_Rect dst = { textX, rowY, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }
    if (st->textbox && st->textbox->render) {
        st->textbox->render(st->textbox, ctx);
    }

    if (st->infoLineCount > 0) {
        TTF_Font *font = e9ui->theme.text.source ? e9ui->theme.text.source : ctx->font;
        if (font) {
            e9ui_labeled_select_ensureWrappedInfo(st, ctx, buttonRect.w);
            SDL_Color color = (SDL_Color){140, 140, 140, 255};
            int lh = TTF_FontHeight(font);
            if (lh <= 0) {
                lh = 16;
            }
            int padY = e9ui_scale_px(ctx, 4);
            int baseY = buttonRect.y + buttonRect.h + padY;
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
                SDL_Rect dst = { buttonRect.x, y, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }
}

static void
e9ui_labeled_select_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->options) {
        alloc_free(st->options);
        st->options = NULL;
        st->optionCount = 0;
    }
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
    e9ui_labeled_select_clearInfo(st);
    e9ui_labeled_select_clearWrappedInfo(st);
}

e9ui_component_t *
e9ui_labeled_select_make(const char *label, int labelWidth_px, int totalWidth_px,
                         const e9ui_select_option_t *options, int optionCount,
                         const char *initialValue,
                         e9ui_labeled_select_change_cb_t cb, void *user)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)alloc_calloc(1, sizeof(*st));
    if (!c || !st) {
        alloc_free(c);
        alloc_free(st);
        return NULL;
    }
    st->labelWidth_px = labelWidth_px;
    st->totalWidth_px = totalWidth_px;
    if (label && *label) {
        st->label = alloc_strdup(label);
    }
    if (options && optionCount > 0) {
        st->options = (e9ui_select_option_t*)alloc_calloc((size_t)optionCount, sizeof(*st->options));
        if (!st->options) {
            alloc_free(c);
            alloc_free(st);
            return NULL;
        }
        memcpy(st->options, options, (size_t)optionCount * sizeof(*st->options));
        st->optionCount = optionCount;
    } else {
        st->options = NULL;
        st->optionCount = 0;
    }
    st->selectedIndex = 0;
    if (initialValue && *initialValue) {
        int found = e9ui_labeled_select_findIndex(st, initialValue);
        if (found >= 0) {
            st->selectedIndex = found;
        }
    }

    st->textbox = e9ui_textbox_make(512, NULL, NULL, NULL);
    if (st->textbox) {
        e9ui_textbox_setReadOnly(st->textbox, 1);
        e9ui_textbox_setOptions(st->textbox, NULL, 0);
        if (options && optionCount > 0) {
            e9ui_textbox_option_t *tbOpts =
                (e9ui_textbox_option_t*)alloc_calloc((size_t)optionCount, sizeof(*tbOpts));
            if (tbOpts) {
                for (int i = 0; i < optionCount; ++i) {
                    tbOpts[i].value = options[i].value;
                    tbOpts[i].label = options[i].label;
                }
                e9ui_textbox_setOptions(st->textbox, tbOpts, optionCount);
                alloc_free(tbOpts);
            }
        }
        e9ui_textbox_setOnOptionSelected(st->textbox, e9ui_labeled_select_optionSelected, st);
        if (optionCount <= 1) {
            st->textbox->disabled = 1;
        }
    }
    e9ui_labeled_select_syncTextboxLabel(st);
    st->onChange = cb;
    st->onChangeUser = user;

    c->name = "e9ui_labeledSelect";
    c->state = st;
    c->preferredHeight = e9ui_labeled_select_preferredHeight;
    c->layout = e9ui_labeled_select_layout;
    c->render = e9ui_labeled_select_render;
    c->dtor = e9ui_labeled_select_dtor;
    st->self = c;
    if (st->textbox) {
        e9ui_child_add(c, st->textbox, 0);
    }
    return c;
}

void
e9ui_labeled_select_setLabelWidth(e9ui_component_t *comp, int labelWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    st->labelWidth_px = labelWidth_px;
}

void
e9ui_labeled_select_setTotalWidth(e9ui_component_t *comp, int totalWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    st->totalWidth_px = totalWidth_px;
}

void
e9ui_labeled_select_setValue(e9ui_component_t *comp, const char *value)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    int found = e9ui_labeled_select_findIndex(st, value);
    if (found < 0) {
        return;
    }
    st->selectedIndex = found;
    e9ui_labeled_select_syncTextboxLabel(st);
}

const char *
e9ui_labeled_select_getValue(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_select_state_t *st = (const e9ui_labeled_select_state_t*)comp->state;
    return e9ui_labeled_select_currentValue(st);
}

void
e9ui_labeled_select_setOnChange(e9ui_component_t *comp, e9ui_labeled_select_change_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    st->onChange = cb;
    st->onChangeUser = user;
}

e9ui_component_t *
e9ui_labeled_select_getButton(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_labeled_select_state_t *st = (const e9ui_labeled_select_state_t*)comp->state;
    return st ? st->textbox : NULL;
}

void
e9ui_labeled_select_setInfo(e9ui_component_t *comp, const char *info)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_labeled_select_state_t *st = (e9ui_labeled_select_state_t*)comp->state;
    e9ui_labeled_select_clearInfo(st);
    e9ui_labeled_select_clearWrappedInfo(st);
    if (!info || !*info) {
        return;
    }

    const char *p = info;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        e9ui_labeled_select_addInfoLine(st, p, len);
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
}
