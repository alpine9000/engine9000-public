/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"

typedef enum {
    E9UI_FILESELECT_FILE = 0,
    E9UI_FILESELECT_FOLDER = 1,
} e9ui_fileselect_mode_t;

typedef void (*e9ui_fileselect_change_cb_t)(e9ui_context_t *ctx, e9ui_component_t *comp,
                                            const char *text, void *user);

e9ui_component_t *
e9ui_fileSelect_make(const char *label, int labelWidth_px, int totalWidth_px,
                     const char *buttonText,
                     const char **extensions, int extensionCount,
                     e9ui_fileselect_mode_t mode);

void
e9ui_fileSelect_setLabelWidth(e9ui_component_t *comp, int labelWidth_px);

void
e9ui_fileSelect_setTotalWidth(e9ui_component_t *comp, int totalWidth_px);

void
e9ui_fileSelect_setAllowEmpty(e9ui_component_t *comp, int allowEmpty);

void
e9ui_fileSelect_setText(e9ui_component_t *comp, const char *text);

const char *
e9ui_fileSelect_getText(const e9ui_component_t *comp);

void
e9ui_fileSelect_setOnChange(e9ui_component_t *comp, e9ui_fileselect_change_cb_t cb, void *user);


