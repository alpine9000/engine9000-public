/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "geo_debug_sprite.h"
#include "geo_debug_rom.h"
#include "geo_checkpoint.h"
#include "geo_watchpoint.h"
#include "geo_protect.h"

bool
libretro_host_init(SDL_Renderer *renderer);

bool
libretro_host_start(const char *core_path, const char *rom_path,
                    const char *system_dir, const char *save_dir);

void
libretro_host_shutdown(void);

void
_libretro_host_runOnce(void);

SDL_Texture *
libretro_host_getTexture(SDL_Renderer *renderer);

bool
libretro_host_getFrame(const uint8_t **out_data, int *out_width, int *out_height, size_t *out_pitch);

float
libretro_host_getDisplayAspect(void);

double
libretro_host_getTimingFps(void);

void
libretro_host_setJoypadState(unsigned port, unsigned id, int pressed);

void
libretro_host_clearJoypadState(void);

void
libretro_host_sendKeyEvent(unsigned keycode, uint32_t character,
                           uint16_t modifiers, int pressed);

bool
libretro_host_isRunning(void);

const void *
libretro_host_getMemory(unsigned id, size_t *size);

bool
libretro_host_readRegs(uint32_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugPause(void);

bool
libretro_host_debugResume(void);

bool
libretro_host_debugIsPaused(int *out_paused);

bool
libretro_host_debugStepInstr(void);

bool
libretro_host_debugStepLine(void);

bool
libretro_host_debugStepNext(void);

bool
libretro_host_debugAddBreakpoint(uint32_t addr);

bool
libretro_host_debugRemoveBreakpoint(uint32_t addr);

bool
libretro_host_debugAddTempBreakpoint(uint32_t addr);

bool
libretro_host_debugRemoveTempBreakpoint(uint32_t addr);

bool
libretro_host_debugResetWatchpoints(void);

bool
libretro_host_debugAddWatchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand, uint32_t *out_index);

bool
libretro_host_debugRemoveWatchpoint(uint32_t index);

bool
libretro_host_debugReadWatchpoints(geo_debug_watchpoint_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugGetWatchpointEnabledMask(uint64_t *out_mask);

bool
libretro_host_debugSetWatchpointEnabledMask(uint64_t mask);

bool
libretro_host_debugConsumeWatchbreak(geo_debug_watchbreak_t *out);

bool
libretro_host_debugResetProtects(void);

bool
libretro_host_debugAddProtect(uint32_t addr, uint32_t size_bits, uint32_t mode, uint32_t value, uint32_t *out_index);

bool
libretro_host_debugRemoveProtect(uint32_t index);

bool
libretro_host_debugReadProtects(geo_debug_protect_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugGetProtectEnabledMask(uint64_t *out_mask);

bool
libretro_host_debugSetProtectEnabledMask(uint64_t mask);

bool
libretro_host_debugReadCallstack(uint32_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugReadMemory(uint32_t addr, void *out, size_t cap);

bool
libretro_host_debugWriteMemory(uint32_t addr, uint32_t value, size_t size);

bool
libretro_host_debugGetSpriteState(geo_debug_sprite_state_t *out);

bool
libretro_host_debugGetP1Rom(geo_debug_rom_region_t *out);

size_t
libretro_host_debugReadCheckpoints(geo_debug_checkpoint_t *out, size_t cap);

bool
libretro_host_debugResetCheckpoints(void);

uint64_t
libretro_host_debugReadCycleCount(void);

bool
libretro_host_debugSetCheckpointEnabled(int enabled);

bool
libretro_host_debugGetCheckpointEnabled(int *out_enabled);

bool
libretro_host_debugDisassembleQuick(uint32_t pc, char *out, size_t cap, size_t *out_len);

bool
libretro_host_profilerStart(int stream);

bool
libretro_host_profilerStop(void);

bool
libretro_host_profilerIsEnabled(int *out_enabled);

bool
libretro_host_profilerStreamNext(char *out, size_t cap, size_t *out_len);

size_t
libretro_host_debugTextRead(char *out, size_t cap);

bool
libretro_host_getSerializeSize(size_t *out_size);

bool
libretro_host_serializeTo(void *out, size_t size);

bool
libretro_host_unserializeFrom(const void *data, size_t size);

bool
libretro_host_setStateData(const void *data, size_t size);

bool
libretro_host_getStateData(const uint8_t **out_data, size_t *out_size);

bool
libretro_host_resetCore(void);

uint64_t
libretro_host_getFrameCount(void);

const char *
libretro_host_getRomPath(void);

bool
libretro_host_setVblankCallback(void (*cb)(void *), void *user);

bool
libretro_host_setDebugBaseCallback(void (*cb)(uint32_t section, uint32_t base));

void
libretro_host_setCoreOption(const char *key, const char *value);

bool
libretro_host_setAudioEnabled(int enabled);

bool
libretro_host_saveState(size_t *out_size, size_t *out_diff);

bool
libretro_host_restoreState(size_t *out_size);
