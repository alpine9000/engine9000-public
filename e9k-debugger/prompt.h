/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include "ui_common.h"
#include "e9ui_component.h"
#include "e9ui_context.h"

typedef enum {
    PROMPT_EVENT_NONE = 0,
    PROMPT_EVENT_SUBMIT,
    PROMPT_EVENT_INTERRUPT,
    PROMPT_EVENT_COMPLETE_REQUEST
} PromptEventType;

typedef struct {
    PromptEventType type;
    char text[PROMPT_MAX];
} PromptEvent;

 

e9ui_component_t *
prompt_makeComponent(void);

void
prompt_applyCompletion(e9ui_context_t *ctx, int prefixLen, const char *insert);

void
prompt_showCompletions(e9ui_context_t *ctx, const char * const *cands, int count);

void
prompt_hideCompletions(e9ui_context_t *ctx);

void
prompt_focus(e9ui_context_t *ctx, e9ui_component_t *prompt);

int
prompt_isFocused(e9ui_context_t *ctx, e9ui_component_t *prompt);


