#include "debugger.h"
#include "target.h"
#include <SDL.h>

target_iface_t* target_targets[3];
target_iface_t* target;
static size_t target_targetCount = 0;

static void
target_applyConfigDefaultsFor(target_iface_t *iface)
{
    if (iface && iface->setConfigDefaults) {
        iface->setConfigDefaults(&debugger.config);
    }
}

void
target_ctor(void)
{
    target_targets[TARGET_AMIGA] = target_amiga();
    target_targets[TARGET_NEOGEO] = target_neogeo();
    target_targetCount = 2;
#if E9K_ENABLE_MEGADRIVE
    target_targets[TARGET_MEGADRIVE] = target_megadrive();
    target_targetCount = 3;
#endif

    target_setConfigDefaults();
}

void
target_releaseUiResources(void)
{
    for (size_t i = 0; i < target_targetCount; i++) {
        target_iface_t *iface = target_targets[i];
        if (!iface) {
            continue;
        }
        if (iface->badge) {
            SDL_DestroyTexture(iface->badge);
            iface->badge = NULL;
        }
        iface->badgeRenderer = NULL;
        iface->badgeW = 0;
        iface->badgeH = 0;
    }
}

void
target_setConfigDefaults(void)
{
    target_iface_t *amiga = target_amiga();
    target_iface_t *neogeo = target_neogeo();
    target_iface_t *megadrive = target_megadrive();

    target_applyConfigDefaultsFor(amiga);
    target_applyConfigDefaultsFor(neogeo);
    target_applyConfigDefaultsFor(megadrive);
}

void
target_settingsClearAllOptions(void)
{
    for (size_t i = 0; i < target_targetCount; i++) {
        if (target_targets[i]->settingsClearOptions) {
            target_targets[i]->settingsClearOptions();
        }
    }
}

void
target_setTarget(target_iface_t* newTarget)
{
  target = newTarget;
  debugger.config.target = newTarget;
}

void
target_setTargetIndex(int index)
{
  if (index >= 0 && index < (int)target_targetCount) {
    target = target_targets[index];
    debugger.config.target = target;
    return;
  }

  debug_printf("target_setTargetIndex: invalid target index %d\n", index);
}

void
target_nextTarget(void)
{
  size_t i;
  for (i = 0; i < target_targetCount; i++) {
    if (target == target_targets[i]) {
      break;
    }
  }

  i++;
  i = i % target_targetCount;
  target = target_targets[i];
  debugger.config.target = target;
}
