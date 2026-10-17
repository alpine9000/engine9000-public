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

#include "libretro.h"
#include "e9k-geo.h"
#include "e9k-mega.h"
#include "e9k-lib.h"


#define LIBRETRO_HOST_MAX_PORTS 4

bool
libretro_host_init(SDL_Renderer *renderer);

bool
libretro_host_start(const char *core_path, const char *rom_path,
                    const char *system_dir, const char *save_dir);

void
libretro_host_shutdown(void);

void
_libretro_host_runOnce(void);

bool
libretro_host_isRunning(void);

bool
libretro_host_resetCore(void);

uint64_t
libretro_host_getFrameCount(void);

const char *
libretro_host_getRomPath(void);

const char *
libretro_host_getCorePath(void);

const char *
libretro_host_getSaveDir(void);

const char *
libretro_host_getSystemDir(void);

SDL_Texture *
libretro_host_getTexture(SDL_Renderer *renderer);

bool
libretro_host_getFrame(const uint8_t **out_data, int *outWidth, int *outHeight, size_t *out_pitch);

float
libretro_host_getDisplayAspect(void);

double
libretro_host_getTimingFps(void);

void
libretro_host_setJoypadState(unsigned port, unsigned id, int pressed);

void
libretro_host_clearJoypadState(void);

void
libretro_host_addMouseMotion(unsigned port, int dx, int dy);

void
libretro_host_setMouseButton(unsigned port, unsigned id, int pressed);

void
libretro_host_clearMouseState(void);

unsigned
libretro_host_getMousePort(void);

void
libretro_host_sendKeyEvent(unsigned keycode, uint32_t character,
                           uint16_t modifiers, int pressed);

void
libretro_host_setControllerPortDevice(unsigned port, unsigned device);

const void *
libretro_host_getMemory(unsigned id, size_t *size);

size_t
libretro_host_getMemoryMapDescriptors(const struct retro_memory_descriptor **outDescriptors);

bool
libretro_host_readRegs(uint32_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugReadProcessors(e9k_debug_processor_info_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugReadProcessorRegs(uint32_t processorId, e9k_debug_processor_reg_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugReadProcessorMemory(uint32_t processorId, uint32_t addr, void *out, size_t cap);

bool
libretro_host_debugWriteProcessorMemory(uint32_t processorId, uint32_t addr, uint32_t value, size_t size);

bool
libretro_host_debugDisassembleProcessorQuick(uint32_t processorId, uint32_t pc, char *out, size_t cap, size_t *out_len);

bool
libretro_host_debugSuppressProcessorBreakpointAtPc(uint32_t processorId);

bool
libretro_host_debugStepProcessorInstr(uint32_t processorId);

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
libretro_host_debugStepOut(void);

bool
libretro_host_debugAddBreakpoint(uint32_t addr);

bool
libretro_host_debugRemoveBreakpoint(uint32_t addr);

bool
libretro_host_debugAddProcessorBreakpoint(uint32_t processorId, uint32_t addr);

bool
libretro_host_debugRemoveProcessorBreakpoint(uint32_t processorId, uint32_t addr);

bool
libretro_host_debugAddTempBreakpoint(uint32_t addr);

bool
libretro_host_debugRemoveTempBreakpoint(uint32_t addr);

bool
libretro_host_debugDisassembleQuick(uint32_t pc, char *out, size_t cap, size_t *out_len);

size_t
libretro_host_debugReadKnownPcs(uint32_t start_addr, uint32_t end_addr, uint32_t *out, size_t cap);

size_t
libretro_host_debugTextRead(char *out, size_t cap);

bool
libretro_host_debugResetWatchpoints(void);

bool
libretro_host_debugAddWatchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand, uint32_t access_source_operand, uint32_t *out_index);

bool
libretro_host_debugRemoveWatchpoint(uint32_t index);

bool
libretro_host_debugReadWatchpoints(e9k_debug_watchpoint_t *out, size_t cap, size_t *out_count);

bool
libretro_host_debugGetWatchpointEnabledMask(uint64_t *out_mask);

bool
libretro_host_debugSetWatchpointEnabledMask(uint64_t mask);

bool
libretro_host_debugConsumeWatchbreak(e9k_debug_watchbreak_t *out);

bool
libretro_host_debugResetProtects(void);

bool
libretro_host_debugAddProtect(uint32_t addr, uint32_t size_bits, uint32_t mode, uint32_t value, uint32_t *out_index);

bool
libretro_host_debugRemoveProtect(uint32_t index);

bool
libretro_host_debugReadProtects(e9k_debug_protect_t *out, size_t cap, size_t *out_count);

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

size_t
libretro_host_debugReadCheckpoints(e9k_debug_checkpoint_t *out, size_t cap);

bool
libretro_host_debugResetCheckpoints(void);

size_t
libretro_host_debugReadCounters(e9k_debug_counter_t *out, size_t cap);

bool
libretro_host_debugResetCounters(void);

uint64_t
libretro_host_debugReadCycleCount(void);

bool
libretro_host_debugSetCheckpointEnabled(int enabled);

bool
libretro_host_debugGetCheckpointEnabled(int *outEnabled);

bool
libretro_host_profilerStart(int stream);

bool
libretro_host_profilerStop(void);

bool
libretro_host_profilerIsEnabled(int *outEnabled);

bool
libretro_host_profilerStreamNext(char *out, size_t cap, size_t *out_len);

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
libretro_host_saveState(size_t *out_size, size_t *out_diff);

bool
libretro_host_restoreState(size_t *out_size);

bool
libretro_host_setVblankCallback(void (*cb)(void *), void *user);

bool
libretro_host_setDebugSourceLocationCallback(int (*cb)(uint32_t pc, uint64_t *out_location, void *user), void *user);

bool
libretro_host_debugSetDebugOption(e9k_debug_option_t option, uint32_t argument, void *user);

void
libretro_host_setCoreOption(const char *key, const char *value);

const char *
libretro_host_getCoreOptionOverrideValue(const char *key);

bool
libretro_host_hasCoreOptionsV2(void);

const struct retro_core_option_v2_category *
libretro_host_getCoreOptionCategories(size_t *out_count);

const struct retro_core_option_v2_definition *
libretro_host_getCoreOptionDefinitions(size_t *out_count);

const char *
libretro_host_getCoreOptionValue(const char *key);

const char *
libretro_host_getCoreOptionDefaultValue(const char *key);

int
libretro_host_isCoreOptionVisible(const char *key);

void
libretro_host_refreshCoreOptionVisibility(void);

bool
libretro_host_setAudioEnabled(int enabled);

void
libretro_host_setAudioVolume(int volumePercent);

/* Amiga */
void
libretro_host_amiga_setEstimateFpsEnabled(int enabled);

int
libretro_host_amiga_getEstimateFpsEnabled(void);

double
libretro_host_amiga_getEstimatedVideoFps(void);

unsigned
libretro_host_amiga_getEstimatedVideoDistinctColors(void);

bool
libretro_host_amiga_getEstimatedVideoVisibleArea(unsigned *outWidth, unsigned *outHeight);

bool
libretro_host_amiga_getVideoLineCount(int *outLineCount);

bool
libretro_host_amiga_getRasterLineCount(int *outLineCount);

bool
libretro_host_amiga_videoLineToCoreLine(int videoLine, int *outCoreLine);

bool
libretro_host_amiga_coreLineToVideoLine(int coreLine, int *outVideoLine);

const e9k_debug_ami_video_line_state_t *
libretro_host_amiga_getVideoLineStates(void);

bool
libretro_host_amiga_getDmaAddr(int **outAddr);

bool
libretro_host_amiga_getCopperAddr(int **outAddr);

bool
libretro_host_amiga_setDebugBaseCallback(void (*cb)(uint32_t section, uint32_t base));

bool
libretro_host_amiga_setDebugBaseStackCallback(void (*cb)(uint32_t section, uint32_t base, uint32_t size));

bool
libretro_host_amiga_setDebugBreakpointCallback(void (*cb)(uint32_t addr));

bool
libretro_host_amiga_setDebugExitCallback(void (*cb)(void));

bool
libretro_host_amiga_setDebugSmokeStartCallback(void (*cb)(void));

bool
libretro_host_amiga_setDebugProfileStartCallback(void (*cb)(void));

bool
libretro_host_amiga_setDebugArgs(const uint32_t *args, size_t count);

bool
libretro_host_amiga_setDeterministic(int enabled);

const e9k_debug_ami_dma_debug_frame_view_t *
libretro_host_amiga_getDmaDebugFrameView(uint32_t frameSelect);

const e9k_debug_ami_copper_debug_frame_view_t *
libretro_host_amiga_getCopperDebugFrameView(uint32_t frameSelect);

const e9k_debug_ami_custom_reg_state_t *
libretro_host_amiga_getCustomRegs(void);

bool
libretro_host_amiga_setCustomLogFrameCallback(e9k_debug_ami_custom_log_frame_callback_t cb, void *user);

bool
libretro_host_amiga_setBlitterDebug(int enabled);

bool
libretro_host_amiga_getBlitterDebug(int *outEnabled);

size_t
libretro_host_amiga_readBlitterVisSpans(e9k_debug_ami_blitter_vis_span_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);

size_t
libretro_host_amiga_readBlitterVisPoints(e9k_debug_ami_blitter_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);

bool
libretro_host_amiga_readBlitterVisStats(e9k_debug_ami_blitter_vis_stats_t *out);

size_t
libretro_host_amiga_readBlitterVisWordTags(uint32_t addr, uint32_t *out, size_t cap);

bool
libretro_host_amiga_setSpriteVis(int enabled);

bool
libretro_host_amiga_getSpriteVis(int *outEnabled);

bool
libretro_host_amiga_setStatusbar(int enabled);

bool
libretro_host_amiga_getStatusbar(int *outEnabled);

size_t
libretro_host_amiga_readSpriteVisPoints(e9k_debug_ami_sprite_vis_point_t *out, size_t cap, uint32_t *outWidth, uint32_t *outHeight);

bool
libretro_host_amiga_setFloppyPath(int drive, const char *path);

/* NeoGeo */
bool
libretro_host_neogeo_getSpriteState(e9k_debug_sprite_state_t *out);

bool
libretro_host_neogeo_getPaletteState(e9k_debug_palette_state_t *out);

bool
libretro_host_neogeo_getP1Rom(e9k_debug_rom_region_t *out);

bool
libretro_host_neogeo_getCRom(e9k_debug_rom_region_t *out);

bool
libretro_host_neogeo_getFixRom(e9k_debug_rom_region_t *out);

size_t
libretro_host_neogeo_getRoms(e9k_debug_rom_entry_t *out, size_t cap);

bool
libretro_host_neogeo_getAudioFrame(e9k_debug_audio_frame_t *out);

bool
libretro_host_neogeo_setSpriteGrayscaleSelection(const e9k_debug_sprite_grayscale_selection_t *selection);

bool
libretro_host_neogeo_setPaletteGrayscaleMask(const e9k_debug_palette_grayscale_mask_t *mask);

bool
libretro_host_neogeo_getPaletteGrayscaleMask(e9k_debug_palette_grayscale_mask_t *outMask);

bool
libretro_host_neogeo_setFixLayerMode(e9k_debug_geo_fix_layer_mode_t mode);

bool
libretro_host_neogeo_setAudioVisEnabled(int enabled);

bool
libretro_host_neogeo_setAudioMuteMask(uint32_t mask);

bool
libretro_host_neogeo_setRegisterLogFrameCallback(e9k_debug_geo_register_log_frame_callback_t cb, void *user);

void
libretro_host_neogeo_bindApis(void);

void
libretro_host_neogeo_unbindApis(void);

/* Megadrive */
bool
libretro_host_megadrive_getSpriteState(e9k_debug_mega_sprite_state_t *out);

size_t
libretro_host_megadrive_getRoms(e9k_debug_rom_entry_t *out, size_t cap);

bool
libretro_host_megadrive_setPaletteGreyscaleMask(uint32_t mask);

bool
libretro_host_megadrive_getPaletteGreyscaleMask(uint32_t *outMask);

bool
libretro_host_megadrive_getAudioFrame(e9k_debug_mega_audio_frame_t *out);

bool
libretro_host_megadrive_getVdpBandwidthFrame(e9k_debug_mega_vdp_bandwidth_frame_t *out);

bool
libretro_host_megadrive_getRasterLineCount(int *outLineCount);

bool
libretro_host_megadrive_setAudioVisEnabled(int enabled);

bool
libretro_host_megadrive_setAudioMuteMask(uint32_t mask);

void
libretro_host_megadrive_bindApis(void);

void
libretro_host_megadrive_unbindApis(void);
