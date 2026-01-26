/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"
#include "debugger.h"

const char *
transition_modeName(e9k_transition_mode_t mode);

int
transition_parseMode(const char *value, e9k_transition_mode_t *out);

e9k_transition_mode_t
transition_pickRandom(void);

e9k_transition_mode_t
transition_pickCycle(void);

void
transition_runIntro(void);

e9k_transition_mode_t
transition_pickFullscreenMode(int entering);

void
transition_doom_run(e9ui_component_t *root, int w, int h);

void
transition_doom_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_slide_run(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_slide_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_explode_run(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_explode_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_rbar_run(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_rbar_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_flip_run(e9ui_component_t *from, e9ui_component_t *to, int w, int h);

void
transition_flip_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h);


