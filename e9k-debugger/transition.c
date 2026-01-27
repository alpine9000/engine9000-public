/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "transition.h"

const char *
transition_modeName(e9k_transition_mode_t mode)
{
    switch (mode) {
    case e9k_transition_slide:
        return "slide";
    case e9k_transition_explode:
        return "explode";
    case e9k_transition_doom:
        return "doom";
    case e9k_transition_flip:
        return "flip";
    case e9k_transition_rbar:
        return "rbar";
    case e9k_transition_random:
        return "random";
    case e9k_transition_cycle:
        return "cycle";
    case e9k_transition_none:
    default:
        return "none";
    }
}

int
transition_parseMode(const char *value, e9k_transition_mode_t *out)
{
    if (!value || !*value || !out) {
        return 0;
    }
    if (strcasecmp(value, "slide") == 0) {
        *out = e9k_transition_slide;
        return 1;
    }
    if (strcasecmp(value, "explode") == 0) {
        *out = e9k_transition_explode;
        return 1;
    }
    if (strcasecmp(value, "doom") == 0) {
        *out = e9k_transition_doom;
        return 1;
    }
    if (strcasecmp(value, "flip") == 0) {
        *out = e9k_transition_flip;
        return 1;
    }
    if (strcasecmp(value, "rbar") == 0) {
        *out = e9k_transition_rbar;
        return 1;
    }
    if (strcasecmp(value, "random") == 0) {
        *out = e9k_transition_random;
        return 1;
    }
    if (strcasecmp(value, "cycle") == 0) {
        *out = e9k_transition_cycle;
        return 1;
    }
    if (strcasecmp(value, "none") == 0) {
        *out = e9k_transition_none;
        return 1;
    }
    return 0;
}

e9k_transition_mode_t
transition_pickRandom(void)
{
    switch (rand() % 5) {
    case 0:
        return e9k_transition_slide;
    case 1:
        return e9k_transition_explode;
    case 2:
        return e9k_transition_doom;
    case 3:
        return e9k_transition_flip;
    default:
        return e9k_transition_rbar;
    }
}

e9k_transition_mode_t
transition_pickCycle(void)
{
    e9k_transition_mode_t mode = e9k_transition_slide;
    int idx = e9ui->transition.cycleIndex % 5;
    if (idx == 1) {
        mode = e9k_transition_explode;
    } else if (idx == 2) {
        mode = e9k_transition_doom;
    } else if (idx == 3) {
        mode = e9k_transition_flip;
    } else if (idx == 4) {
        mode = e9k_transition_rbar;
    }
    e9ui->transition.cycleIndex = (idx + 1) % 5;
    return mode;
}

void
transition_runIntro(void)
{
    e9ui_component_t *root = e9ui->fullscreen ? e9ui->fullscreen : e9ui->root;
    if (!root || !e9ui->ctx.renderer) {
        return;
    }
    if (e9ui->fullscreen) {
        return;
    }
    if (e9ui->transition.mode == e9k_transition_none) {
        return;
    }

    int w = 0;
    int h = 0;
    SDL_GetRendererOutputSize(e9ui->ctx.renderer, &w, &h);
    if (root->layout) {
        e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
        root->layout(root, &e9ui->ctx, full);
    }

    e9k_transition_mode_t mode = e9ui->transition.mode;
    if (mode == e9k_transition_random || mode == e9k_transition_cycle) {
        mode = (mode == e9k_transition_cycle) ? transition_pickCycle() : transition_pickRandom();
    }
    switch (mode) {
    case e9k_transition_slide:
        e9ui->transition.inTransition = 1;
        transition_slide_run(NULL, root, w, h);
        break;
    case e9k_transition_explode:
        e9ui->transition.inTransition = 1;
        transition_explode_run(NULL, root, w, h);
        break;
    case e9k_transition_doom:
        e9ui->transition.inTransition = 1;
        transition_doom_run(root, w, h);
        break;
    case e9k_transition_flip:
        break;
    case e9k_transition_rbar:
        e9ui->transition.inTransition = 1;
        transition_rbar_run(NULL, root, w, h);
        break;
    case e9k_transition_none:
    default:
        break;
    }
}

e9k_transition_mode_t
transition_pickFullscreenMode(int entering)
{
    e9k_transition_mode_t mode = e9ui->transition.mode;
    if (mode != e9k_transition_random && mode != e9k_transition_cycle) {
        e9ui->transition.fullscreenModeSet = 0;
        return mode;
    }
    if (entering) {
        mode = (mode == e9k_transition_cycle) ? transition_pickCycle() : transition_pickRandom();
        e9ui->transition.fullscreenMode = mode;
        e9ui->transition.fullscreenModeSet = 1;
    } else {
        if (e9ui->transition.fullscreenModeSet) {
            mode = e9ui->transition.fullscreenMode;
        } else {
            mode = (mode == e9k_transition_cycle) ? transition_pickCycle() : transition_pickRandom();
        }
        e9ui->transition.fullscreenModeSet = 0;
    }
    return mode;
}
