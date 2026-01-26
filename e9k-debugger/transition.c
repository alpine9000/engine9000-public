// Transition selection helpers and startup transition runner
#include "transition.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
    int idx = debugger.transitionCycleIndex % 5;
    if (idx == 1) {
        mode = e9k_transition_explode;
    } else if (idx == 2) {
        mode = e9k_transition_doom;
    } else if (idx == 3) {
        mode = e9k_transition_flip;
    } else if (idx == 4) {
        mode = e9k_transition_rbar;
    }
    debugger.transitionCycleIndex = (idx + 1) % 5;
    return mode;
}

void
transition_runIntro(void)
{
    e9ui_component_t *root = debugger.ui.fullscreen ? debugger.ui.fullscreen : debugger.ui.root;
    if (!root || !debugger.ui.ctx.renderer) {
        return;
    }
    if (debugger.ui.fullscreen) {
        return;
    }
    if (debugger.transitionMode == e9k_transition_none) {
        return;
    }

    int w = 0;
    int h = 0;
    SDL_GetRendererOutputSize(debugger.ui.ctx.renderer, &w, &h);
    if (root->layout) {
        e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
        root->layout(root, &debugger.ui.ctx, full);
    }

    e9k_transition_mode_t mode = debugger.transitionMode;
    if (mode == e9k_transition_random || mode == e9k_transition_cycle) {
        mode = (mode == e9k_transition_cycle) ? transition_pickCycle() : transition_pickRandom();
    }
    switch (mode) {
    case e9k_transition_slide:
        debugger.inTransition = 1;
        transition_slide_run(NULL, root, w, h);
        break;
    case e9k_transition_explode:
        debugger.inTransition = 1;
        transition_explode_run(NULL, root, w, h);
        break;
    case e9k_transition_doom:
        debugger.inTransition = 1;
        transition_doom_run(root, w, h);
        break;
    case e9k_transition_flip:
        break;
    case e9k_transition_rbar:
        debugger.inTransition = 1;
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
    e9k_transition_mode_t mode = debugger.transitionMode;
    if (mode != e9k_transition_random && mode != e9k_transition_cycle) {
        debugger.transitionFullscreenModeSet = 0;
        return mode;
    }
    if (entering) {
        mode = (mode == e9k_transition_cycle) ? transition_pickCycle() : transition_pickRandom();
        debugger.transitionFullscreenMode = mode;
        debugger.transitionFullscreenModeSet = 1;
    } else {
        if (debugger.transitionFullscreenModeSet) {
            mode = debugger.transitionFullscreenMode;
        } else {
            mode = (mode == e9k_transition_cycle) ? transition_pickCycle() : transition_pickRandom();
        }
        debugger.transitionFullscreenModeSet = 0;
    }
    return mode;
}
