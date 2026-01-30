/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include <sys/stat.h>
#include <unistd.h>
#include "tinyfiledialogs.h"

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#endif

typedef struct e9ui_fileselect_state {
    char *label;
    int labelWidth_px;
    int totalWidth_px;
    int allowEmpty;
    e9ui_component_t *textbox;
    e9ui_component_t *button;
    e9ui_component_t *newButton;
    char **extensions;
    int extensionCount;
    e9ui_fileselect_mode_t mode;
    e9ui_fileselect_change_cb_t onChange;
    void *onChangeUser;
    e9ui_fileselect_validate_cb_t validate;
    void *validateUser;
    e9ui_component_t *self;
} e9ui_fileselect_state_t;

static int
e9ui_fileselect_pathValid(const e9ui_fileselect_state_t *st)
{
    if (!st || !st->textbox) {
        return 0;
    }
    const char *path = e9ui_textbox_getText(st->textbox);
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    if (st->mode == E9UI_FILESELECT_FOLDER) {
        return S_ISDIR(sb.st_mode) ? 1 : 0;
    }
    return S_ISREG(sb.st_mode) ? 1 : 0;
}

static int
e9ui_fileselect_isValid(e9ui_context_t *ctx, const e9ui_fileselect_state_t *st)
{
    if (!st || !st->textbox) {
        return 0;
    }
    const char *text = e9ui_textbox_getText(st->textbox);
    if (st->validate) {
        return st->validate(ctx, st->self, text ? text : "", st->validateUser) ? 1 : 0;
    }
    return e9ui_fileselect_pathValid(st);
}

static void
e9ui_fileselect_drawStatusBorder(const e9ui_component_t *textbox, e9ui_context_t *ctx, int valid)
{
    if (!textbox || !ctx || !ctx->renderer) {
        return;
    }
    SDL_Color base = valid ? (SDL_Color){80, 200, 120, 180} : (SDL_Color){210, 80, 80, 180};
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    int blur = e9ui_scale_px(ctx, 3);
    if (blur < 1) {
        blur = 1;
    }
    for (int i = blur; i >= 1; --i) {
        int alpha = base.a / (i + 1);
        SDL_SetRenderDrawColor(ctx->renderer, base.r, base.g, base.b, (Uint8)alpha);
        SDL_Rect r = { textbox->bounds.x - i, textbox->bounds.y - i,
                       textbox->bounds.w + i * 2, textbox->bounds.h + i * 2 };
        SDL_RenderDrawRect(ctx->renderer, &r);
    }
}

static void
e9ui_fileselect_notifyChange(e9ui_context_t *ctx, e9ui_fileselect_state_t *st)
{
    if (!st || !st->onChange) {
        return;
    }
    const char *text = st->textbox ? e9ui_textbox_getText(st->textbox) : NULL;
    st->onChange(ctx, st->self, text ? text : "", st->onChangeUser);
}

static void
e9ui_fileselect_textChanged(e9ui_context_t *ctx, void *user)
{
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)user;
    if (!st) {
        return;
    }
    e9ui_fileselect_notifyChange(ctx, st);
}

static int
e9ui_fileselect_getInitialDir(const e9ui_fileselect_state_t *st, char *out, size_t cap)
{
    if (!st || !out || cap == 0) {
        return 0;
    }
    const char *path = st->textbox ? e9ui_textbox_getText(st->textbox) : NULL;
    if (!path || !*path) {
        return 0;
    }
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    if (st->mode == E9UI_FILESELECT_FOLDER) {
        if (S_ISDIR(sb.st_mode)) {
            strncpy(out, path, cap - 1);
            out[cap - 1] = '\0';
            return 1;
        }
        return 0;
    }
    if (!S_ISREG(sb.st_mode)) {
        return 0;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *sep = slash > back ? slash : back;
    if (!sep) {
        return 0;
    }
    size_t len = (size_t)(sep - path);
    if (len == 0) {
        out[0] = '/';
        out[1] = '\0';
        return 1;
    }
    if (len + 2 > cap) {
        len = cap - 2;
    }
    memcpy(out, path, len);
    out[len] = '/';
    out[len + 1] = '\0';
    return 1;
}

static void
e9ui_fileselect_openDialog(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)user;
    if (!st || !st->textbox) {
        return;
    }
    const char *title = st->label ? st->label : "Select File";
    char *result = NULL;
    char initial[PATH_MAX];
    const char *start = NULL;
    if (e9ui_fileselect_getInitialDir(st, initial, sizeof(initial))) {
        start = initial;
    } else if (getcwd(initial, sizeof(initial))) {
        start = initial;
    }
    if (st->mode == E9UI_FILESELECT_FOLDER) {
        result = tinyfd_selectFolderDialog(title, start);
    } else {
        result = tinyfd_openFileDialog(title, start,
                                       st->extensionCount,
                                       (const char * const *)st->extensions,
                                       NULL, 0);
    }
    if (result && *result) {
        e9ui_textbox_setText(st->textbox, result);
        e9ui_fileselect_notifyChange(ctx, st);
    }
}

static void
e9ui_fileselect_newFileDialog(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)user;
    if (!st || !st->textbox) {
        return;
    }
    if (st->mode != E9UI_FILESELECT_FILE) {
        return;
    }
    const char *title = st->label ? st->label : "New File";
    char *result = NULL;
    char initial[PATH_MAX];
    char dirWithSlash[PATH_MAX + 2];
    const char *start = NULL;

    if (e9ui_fileselect_getInitialDir(st, initial, sizeof(initial))) {
        start = initial;
    } else if (getcwd(initial, sizeof(initial))) {
        start = initial;
    }

    if (start && *start) {
        size_t len = strlen(start);
        if (len + 2 <= sizeof(dirWithSlash)) {
            if (start[len - 1] != '/') {
                snprintf(dirWithSlash, sizeof(dirWithSlash), "%s/", start);
                start = dirWithSlash;
            }
        }
    }

    result = tinyfd_saveFileDialog(title,
                                   start,
                                   st->extensionCount,
                                   (const char * const *)st->extensions,
                                   NULL);
    if (result && *result) {
        e9ui_textbox_setText(st->textbox, result);
        e9ui_fileselect_notifyChange(ctx, st);
    }
}

static int
e9ui_fileselect_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)self->state;
    if (!st) {
        return 0;
    }
    int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : 0;
    int gap = e9ui_scale_px(ctx, 8);
    int buttonW = 0;
    int buttonH = 0;
    if (st->button) {
        e9ui_button_measure(st->button, ctx, &buttonW, &buttonH);
    }
    int newButtonW = 0;
    int newButtonH = 0;
    if (st->newButton) {
        e9ui_button_measure(st->newButton, ctx, &newButtonW, &newButtonH);
    }
    int totalW = availW;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int gapCount = st->newButton ? 3 : 2;
    int textboxW = totalW - labelW - buttonW - newButtonW - gap * gapCount;
    if (textboxW < 0) {
        textboxW = 0;
    }
    int textboxH = 0;
    if (st->textbox && st->textbox->preferredHeight) {
        textboxH = st->textbox->preferredHeight(st->textbox, ctx, textboxW);
    }
    int h = textboxH > buttonH ? textboxH : buttonH;
    if (newButtonH > h) {
        h = newButtonH;
    }
    return h;
}

static void
e9ui_fileselect_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)self->state;
    if (!st || !st->textbox || !st->button) {
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
    int buttonW = 0;
    int buttonH = 0;
    e9ui_button_measure(st->button, ctx, &buttonW, &buttonH);
    int newButtonW = 0;
    int newButtonH = 0;
    if (st->newButton) {
        e9ui_button_measure(st->newButton, ctx, &newButtonW, &newButtonH);
    }
    int totalW = bounds.w;
    if (st->totalWidth_px > 0) {
        int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
        if (scaled < totalW) {
            totalW = scaled;
        }
    }
    int gapCount = st->newButton ? 3 : 2;
    int textboxW = totalW - labelW - buttonW - newButtonW - gap * gapCount;
    if (textboxW < 0) {
        textboxW = 0;
    }
    int textboxH = st->textbox->preferredHeight ? st->textbox->preferredHeight(st->textbox, ctx, textboxW) : 0;
    int rowH = textboxH > buttonH ? textboxH : buttonH;
    if (newButtonH > rowH) {
        rowH = newButtonH;
    }
    if (rowH < 0) {
        rowH = 0;
    }
    int rowX = bounds.x + (bounds.w - totalW) / 2;
    int rowY = bounds.y + (bounds.h - rowH) / 2;
    e9ui_rect_t textboxRect = { rowX + labelW + gap, rowY, textboxW, rowH };
    e9ui_rect_t buttonRect = { rowX + labelW + gap + textboxW + gap, rowY, buttonW, rowH };
    st->textbox->layout(st->textbox, ctx, textboxRect);
    st->button->layout(st->button, ctx, buttonRect);
    if (st->newButton && st->newButton->layout) {
        e9ui_rect_t newButtonRect = { buttonRect.x + buttonW + gap, rowY, newButtonW, rowH };
        st->newButton->layout(st->newButton, ctx, newButtonRect);
    }
}

static void
e9ui_fileselect_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->textbox) {
        const char *text = e9ui_textbox_getText(st->textbox);
        if (!st->allowEmpty || (text && *text)) {
            e9ui_fileselect_drawStatusBorder(st->textbox, ctx, e9ui_fileselect_isValid(ctx, st));
        }
    }
    if (st->label && *st->label) {
        TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
        if (font) {
            SDL_Color color = (SDL_Color){220, 220, 220, 255};
            int tw = 0;
            int th = 0;
            SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->label, color, &tw, &th);
            if (tex) {
                int gap = e9ui_scale_px(ctx, 8);
                int labelW = st->labelWidth_px > 0 ? e9ui_scale_px(ctx, st->labelWidth_px) : tw + gap;
                int totalW = self->bounds.w;
                if (st->totalWidth_px > 0) {
                    int scaled = e9ui_scale_px(ctx, st->totalWidth_px);
                    if (scaled < totalW) {
                        totalW = scaled;
                    }
                }
                int rowX = self->bounds.x + (self->bounds.w - totalW) / 2;
                int rowY = self->bounds.y + (self->bounds.h - th) / 2;
                int textX = rowX + labelW - tw;
                SDL_Rect dst = { textX, rowY, tw, th };
                SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
            }
        }
    }
    if (st->textbox && st->textbox->render) {
        st->textbox->render(st->textbox, ctx);
    }
    if (st->button && st->button->render) {
        st->button->render(st->button, ctx);
    }
    if (st->newButton && st->newButton->render) {
        st->newButton->render(st->newButton, ctx);
    }
}

static void
e9ui_fileselect_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)self->state;
    if (!st) {
        return;
    }
    if (st->label) {
        alloc_free(st->label);
        st->label = NULL;
    }
    if (st->extensions) {
        for (int i = 0; i < st->extensionCount; ++i) {
            if (st->extensions[i]) {
                alloc_free(st->extensions[i]);
            }
        }
        alloc_free(st->extensions);
        st->extensions = NULL;
        st->extensionCount = 0;
    }
}

e9ui_component_t *
e9ui_fileSelect_make(const char *label, int labelWidth_px, int totalWidth_px,
                     const char *buttonText,
                     const char **extensions, int extensionCount,
                     e9ui_fileselect_mode_t mode)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)alloc_calloc(1, sizeof(*st));
    st->labelWidth_px = labelWidth_px;
    st->totalWidth_px = totalWidth_px;
    if (label && *label) {
        st->label = alloc_strdup(label);
    }
    if (extensions && extensionCount > 0) {
        st->extensions = (char**)alloc_calloc((size_t)extensionCount, sizeof(char*));
        st->extensionCount = extensionCount;
        for (int i = 0; i < extensionCount; ++i) {
            if (extensions[i]) {
                st->extensions[i] = alloc_strdup(extensions[i]);
            }
        }
    }
    st->textbox = e9ui_textbox_make(512, NULL, e9ui_fileselect_textChanged, st);
    st->button = e9ui_button_make((buttonText && *buttonText) ? buttonText : "...",
                                  e9ui_fileselect_openDialog, st);
    st->mode = mode;
    if (st->textbox) {
        e9ui_textbox_setCompletionMode(st->textbox,
                                       (mode == E9UI_FILESELECT_FOLDER) ? e9ui_textbox_completion_folder
                                                                        : e9ui_textbox_completion_filename);
    }
    c->name = "e9ui_fileSelect";
    c->state = st;
    c->preferredHeight = e9ui_fileselect_preferredHeight;
    c->layout = e9ui_fileselect_layout;
    c->render = e9ui_fileselect_render;
    c->dtor = e9ui_fileselect_dtor;
    st->self = c;
    if (st->textbox) {
        e9ui_child_add(c, st->textbox, 0);
    }
    if (st->button) {
        e9ui_child_add(c, st->button, 0);
    }
    return c;
}

void
e9ui_fileSelect_setLabelWidth(e9ui_component_t *comp, int labelWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)comp->state;
    st->labelWidth_px = labelWidth_px;
}

void
e9ui_fileSelect_setTotalWidth(e9ui_component_t *comp, int totalWidth_px)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)comp->state;
    st->totalWidth_px = totalWidth_px;
}

void
e9ui_fileSelect_setAllowEmpty(e9ui_component_t *comp, int allowEmpty)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)comp->state;
    st->allowEmpty = allowEmpty ? 1 : 0;
}

void
e9ui_fileSelect_setText(e9ui_component_t *comp, const char *text)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)comp->state;
    if (!st || !st->textbox) {
        return;
    }
    e9ui_textbox_setText(st->textbox, text);
}

const char *
e9ui_fileSelect_getText(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    const e9ui_fileselect_state_t *st = (const e9ui_fileselect_state_t*)comp->state;
    if (!st || !st->textbox) {
        return NULL;
    }
    return e9ui_textbox_getText(st->textbox);
}

void
e9ui_fileSelect_setOnChange(e9ui_component_t *comp, e9ui_fileselect_change_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)comp->state;
    st->onChange = cb;
    st->onChangeUser = user;
}

void
e9ui_fileSelect_enableNewButton(e9ui_component_t *comp, const char *buttonText)
{
    if (!comp || !comp->state) {
        return;
    }
    if (!buttonText || !*buttonText) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)comp->state;
    if (!st || st->mode != E9UI_FILESELECT_FILE) {
        return;
    }
    if (st->newButton) {
        e9ui_button_setLabel(st->newButton, buttonText);
        return;
    }
    st->newButton = e9ui_button_make(buttonText, e9ui_fileselect_newFileDialog, st);
    if (st->newButton) {
        e9ui_child_add(comp, st->newButton, 0);
    }
}

void
e9ui_fileSelect_setValidate(e9ui_component_t *comp, e9ui_fileselect_validate_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    e9ui_fileselect_state_t *st = (e9ui_fileselect_state_t*)comp->state;
    st->validate = cb;
    st->validateUser = user;
}
