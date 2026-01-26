/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <errno.h>
#include <limits.h>
#include "libretro.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "debug.h"
#ifdef _WIN32
#include <direct.h>
#endif
#include <stdbool.h>

#include "libretro_host.h"
#include "debugger.h"
#include "input_record.h"
#include "alloc.h"


typedef void (*retro_set_environment_fn_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_fn_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_fn_t)(retro_audio_sample_t);
typedef void (*retro_set_audio_sample_batch_fn_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_fn_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_fn_t)(retro_input_state_t);
typedef void (*retro_init_fn_t)(void);
typedef void (*retro_deinit_fn_t)(void);
typedef bool (*retro_load_game_fn_t)(const struct retro_game_info *);
typedef void (*retro_unload_game_fn_t)(void);
typedef void (*retro_reset_fn_t)(void);
typedef void (*retro_run_fn_t)(void);
typedef void (*retro_get_system_av_info_fn_t)(struct retro_system_av_info *);
typedef void *(*retro_get_memory_data_fn_t)(unsigned id);
typedef size_t (*retro_get_memory_size_fn_t)(unsigned id);
typedef size_t (*retro_serialize_size_fn_t)(void);
typedef bool (*retro_serialize_fn_t)(void *data, size_t size);
typedef bool (*retro_unserialize_fn_t)(const void *data, size_t size);
typedef size_t (*geo_debug_read_regs_fn_t)(uint32_t *out, size_t cap);
typedef void (*geo_debug_pause_fn_t)(void);
typedef void (*geo_debug_resume_fn_t)(void);
typedef int (*geo_debug_is_paused_fn_t)(void);
typedef void (*geo_debug_step_instr_fn_t)(void);
typedef void (*geo_debug_step_line_fn_t)(void);
typedef void (*geo_debug_step_next_fn_t)(void);
typedef void (*geo_debug_add_breakpoint_fn_t)(uint32_t addr);
typedef void (*geo_debug_remove_breakpoint_fn_t)(uint32_t addr);
typedef void (*geo_debug_add_temp_breakpoint_fn_t)(uint32_t addr);
typedef void (*geo_debug_remove_temp_breakpoint_fn_t)(uint32_t addr);
typedef void (*geo_debug_reset_watchpoints_fn_t)(void);
typedef int (*geo_debug_add_watchpoint_fn_t)(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand);
typedef void (*geo_debug_remove_watchpoint_fn_t)(uint32_t index);
typedef size_t (*geo_debug_read_watchpoints_fn_t)(geo_debug_watchpoint_t *out, size_t cap);
typedef uint64_t (*geo_debug_get_watchpoint_enabled_mask_fn_t)(void);
typedef void (*geo_debug_set_watchpoint_enabled_mask_fn_t)(uint64_t mask);
typedef int (*geo_debug_consume_watchbreak_fn_t)(geo_debug_watchbreak_t *out);
typedef void (*geo_debug_reset_protects_fn_t)(void);
typedef int (*geo_debug_add_protect_fn_t)(uint32_t addr, uint32_t size_bits, uint32_t mode, uint32_t value);
typedef void (*geo_debug_remove_protect_fn_t)(uint32_t index);
typedef size_t (*geo_debug_read_protects_fn_t)(geo_debug_protect_t *out, size_t cap);
typedef uint64_t (*geo_debug_get_protect_enabled_mask_fn_t)(void);
typedef void (*geo_debug_set_protect_enabled_mask_fn_t)(uint64_t mask);
typedef size_t (*geo_debug_read_callstack_fn_t)(uint32_t *out, size_t cap);
typedef size_t (*geo_debug_read_memory_fn_t)(uint32_t addr, uint8_t *out, size_t cap);
typedef int (*geo_debug_write_memory_fn_t)(uint32_t addr, uint32_t value, size_t size);
typedef void (*geo_debug_profiler_start_fn_t)(int stream);
typedef void (*geo_debug_profiler_stop_fn_t)(void);
typedef int (*geo_debug_profiler_is_enabled_fn_t)(void);
typedef size_t (*geo_debug_profiler_stream_next_fn_t)(char *out, size_t cap);
typedef size_t (*geo_debug_text_read_fn_t)(char *out, size_t cap);
typedef size_t (*geo_debug_get_sprite_state_fn_t)(geo_debug_sprite_state_t *out, size_t cap);
typedef size_t (*geo_debug_get_p1_rom_fn_t)(geo_debug_rom_region_t *out, size_t cap);
typedef size_t (*geo_debug_disassemble_quick_fn_t)(uint32_t pc, char *out, size_t cap);
typedef size_t (*geo_debug_read_checkpoints_fn_t)(geo_debug_checkpoint_t *out, size_t cap);
typedef void (*geo_debug_reset_checkpoints_fn_t)(void);
typedef void (*geo_debug_set_checkpoint_enabled_fn_t)(int enabled);
typedef int (*geo_debug_get_checkpoint_enabled_fn_t)(void);
typedef uint64_t (*geo_debug_read_cycle_count_fn_t)(void);
typedef void (*geo_set_vblank_callback_fn_t)(void (*cb)(void *), void *user);

#define LIBRETRO_HOST_MAX_PORTS 4

typedef struct libretro_option {
    const char *key;
    const char *default_value;
    char *value;
} libretro_option_t;

typedef struct libretro_option_override {
    char *key;
    char *value;
} libretro_option_override_t;

typedef struct  {
    void *lib;
    SDL_Texture *texture;
    enum retro_pixel_format pixelFormat;
    int textureWidth;
    int textureHeight;
    uint64_t textureSeq;
    bool running;
    bool gameLoaded;
    char corePath[PATH_MAX];
    char romPath[PATH_MAX];
    char systemDir[PATH_MAX];
    char saveDir[PATH_MAX];
    void *romData;
    size_t romSize;
    struct retro_system_av_info avInfo;
    SDL_AudioDeviceID audioDev;
    int audioSampleRate;
    size_t audioMaxQueue;
    int audioEnabled;
    uint8_t *frameData;
    size_t frameCapacity;
    size_t framePitch;
    int frameWidth;
    int frameHeight;
    uint64_t frameSeq;
    retro_set_environment_fn_t setEnvironment;
    retro_set_video_refresh_fn_t setVideoRefresh;
    retro_set_audio_sample_fn_t setAudioSample;
    retro_set_audio_sample_batch_fn_t setAudioSampleBatch;
    retro_set_input_poll_fn_t setInputPoll;
    retro_set_input_state_fn_t setInputState;
    retro_init_fn_t init;
    retro_deinit_fn_t deinit;
    retro_load_game_fn_t loadGame;
    retro_unload_game_fn_t unloadGame;
    retro_run_fn_t run;
    retro_reset_fn_t reset;
    retro_get_system_av_info_fn_t getSystemAvInfo;
    retro_get_memory_data_fn_t getMemoryData;
    retro_get_memory_size_fn_t getMemorySize;
    retro_serialize_size_fn_t serializeSize;
    retro_serialize_fn_t serialize;
    retro_unserialize_fn_t unserialize;
    geo_debug_read_regs_fn_t debugReadRegs;
    geo_debug_pause_fn_t debugPause;
    geo_debug_resume_fn_t debugResume;
    geo_debug_is_paused_fn_t debugIsPaused;
    geo_debug_step_instr_fn_t debugStepInstr;
    geo_debug_step_line_fn_t debugStepLine;
    geo_debug_step_next_fn_t debugStepNext;
    geo_debug_add_breakpoint_fn_t debugAddBreakpoint;
    geo_debug_remove_breakpoint_fn_t debugRemoveBreakpoint;
    geo_debug_add_temp_breakpoint_fn_t debugAddTempBreakpoint;
    geo_debug_remove_temp_breakpoint_fn_t debugRemoveTempBreakpoint;
    geo_debug_reset_watchpoints_fn_t debugResetWatchpoints;
    geo_debug_add_watchpoint_fn_t debugAddWatchpoint;
    geo_debug_remove_watchpoint_fn_t debugRemoveWatchpoint;
    geo_debug_read_watchpoints_fn_t debugReadWatchpoints;
    geo_debug_get_watchpoint_enabled_mask_fn_t debugGetWatchpointEnabledMask;
    geo_debug_set_watchpoint_enabled_mask_fn_t debugSetWatchpointEnabledMask;
    geo_debug_consume_watchbreak_fn_t debugConsumeWatchbreak;
    geo_debug_reset_protects_fn_t debugResetProtects;
    geo_debug_add_protect_fn_t debugAddProtect;
    geo_debug_remove_protect_fn_t debugRemoveProtect;
    geo_debug_read_protects_fn_t debugReadProtects;
    geo_debug_get_protect_enabled_mask_fn_t debugGetProtectEnabledMask;
    geo_debug_set_protect_enabled_mask_fn_t debugSetProtectEnabledMask;
    geo_debug_read_callstack_fn_t debugReadCallstack;
    geo_debug_read_memory_fn_t debugReadMemory;
    geo_debug_write_memory_fn_t debugWriteMemory;
    geo_debug_profiler_start_fn_t debugProfilerStart;
    geo_debug_profiler_stop_fn_t debugProfilerStop;
    geo_debug_profiler_is_enabled_fn_t debugProfilerIsEnabled;
    geo_debug_profiler_stream_next_fn_t debugProfilerStreamNext;
    geo_debug_text_read_fn_t debugTextRead;
    geo_debug_get_sprite_state_fn_t debugGetSpriteState;
    geo_debug_get_p1_rom_fn_t debugGetP1Rom;
    geo_debug_disassemble_quick_fn_t debugDisassembleQuick;
    geo_debug_read_checkpoints_fn_t debugReadCheckpoints;
    geo_debug_reset_checkpoints_fn_t debugResetCheckpoints;
    geo_debug_set_checkpoint_enabled_fn_t debugSetCheckpointEnabled;
    geo_debug_get_checkpoint_enabled_fn_t debugGetCheckpointEnabled;
    geo_debug_read_cycle_count_fn_t debugReadCycleCount;
    geo_set_vblank_callback_fn_t setVblankCallback;
    uint8_t *stateData;
    size_t stateSize;
    uint32_t inputMask[LIBRETRO_HOST_MAX_PORTS];
    uint32_t autoInputMask[LIBRETRO_HOST_MAX_PORTS];
    int autoPressDelayFrames;
    int autoPressHoldFrames;
    uint8_t keyboardState[RETROK_LAST];
    retro_keyboard_event_t keyboardCb;
    libretro_option_t *options;
    size_t optionCount;
    unsigned videoFrameCount;
} libretro_host_t;

static libretro_host_t libretro_host;
static libretro_option_override_t *libretro_optionOverrides = NULL;
static size_t libretro_optionOverrideCount = 0;

static void
libretro_host_setOptionValue(const char *key, const char *value);

static void
libretro_host_applyOptionOverrides(void);

static void
libretro_host_clearOptionOverrides(void)
{
    if (!libretro_optionOverrides) {
        libretro_optionOverrideCount = 0;
        return;
    }
    for (size_t i = 0; i < libretro_optionOverrideCount; ++i) {
        free(libretro_optionOverrides[i].key);
        free(libretro_optionOverrides[i].value);
    }
    free(libretro_optionOverrides);
    libretro_optionOverrides = NULL;
    libretro_optionOverrideCount = 0;
}

static void
libretro_host_runFrame(void)
{
    if (libretro_host.running && libretro_host.run) {
        libretro_host.run();
    }
}

static void
libretro_host_clearOptions(void)
{
    if (!libretro_host.options) {
        libretro_host.optionCount = 0;
        return;
    }
    for (size_t i = 0; i < libretro_host.optionCount; ++i) {
        free(libretro_host.options[i].value);
        libretro_host.options[i].value = NULL;
    }
    free(libretro_host.options);
    libretro_host.options = NULL;
    libretro_host.optionCount = 0;
}

static libretro_option_t *
libretro_host_findOption(const char *key)
{
    if (!key) {
        return NULL;
    }
    for (size_t i = 0; i < libretro_host.optionCount; ++i) {
        if (libretro_host.options[i].key && strcmp(libretro_host.options[i].key, key) == 0) {
            return &libretro_host.options[i];
        }
    }
    return NULL;
}

void
libretro_host_setCoreOption(const char *key, const char *value)
{
    if (!key || !*key) {
        return;
    }
    for (size_t i = 0; i < libretro_optionOverrideCount; ++i) {
        if (strcmp(libretro_optionOverrides[i].key, key) == 0) {
            if (!value || !*value) {
                free(libretro_optionOverrides[i].key);
                free(libretro_optionOverrides[i].value);
                for (size_t j = i + 1; j < libretro_optionOverrideCount; ++j) {
                    libretro_optionOverrides[j - 1] = libretro_optionOverrides[j];
                }
                libretro_optionOverrideCount--;
                if (libretro_optionOverrideCount == 0) {
                    free(libretro_optionOverrides);
                    libretro_optionOverrides = NULL;
                }
                return;
            }
            free(libretro_optionOverrides[i].value);
            libretro_optionOverrides[i].value = strdup(value);
            return;
        }
    }
    if (!value || !*value) {
        return;
    }
    libretro_option_override_t *next = realloc(libretro_optionOverrides,
                                               sizeof(*libretro_optionOverrides) * (libretro_optionOverrideCount + 1));
    if (!next) {
        return;
    }
    libretro_optionOverrides = next;
    libretro_optionOverrides[libretro_optionOverrideCount].key = strdup(key);
    libretro_optionOverrides[libretro_optionOverrideCount].value = strdup(value);
    libretro_optionOverrideCount++;
}

static bool
libretro_host_setOptions(const struct retro_core_option_definition *defs)
{
    if (!defs) {
        return false;
    }
    size_t count = 0;
    while (defs[count].key) {
        ++count;
    }
    libretro_host_clearOptions();
    if (!count) {
        return true;
    }
    libretro_host.options = (libretro_option_t *)calloc(count, sizeof(*libretro_host.options));
    if (!libretro_host.options) {
        return false;
    }
    libretro_host.optionCount = count;
    for (size_t i = 0; i < count; ++i) {
        libretro_host.options[i].key = defs[i].key;
        libretro_host.options[i].default_value = defs[i].default_value;
        libretro_host.options[i].value = NULL;
    }
    return true;
}

static void
libretro_host_setOptionValue(const char *key, const char *value)
{
    if (!key) {
        return;
    }
    libretro_option_t *opt = libretro_host_findOption(key);
    if (!opt) {
        return;
    }
    free(opt->value);
    opt->value = NULL;
    if (value) {
        opt->value = strdup(value);
    }
}

static void
libretro_host_applyOptionOverrides(void)
{
    for (size_t i = 0; i < libretro_optionOverrideCount; ++i) {
        if (!libretro_optionOverrides[i].key || !libretro_optionOverrides[i].value) {
            continue;
        }
        libretro_host_setOptionValue(libretro_optionOverrides[i].key, libretro_optionOverrides[i].value);
    }
}

static bool
libretro_host_mkdir_p(const char *path)
{
    if (!path || !*path) {
        return false;
    }
    char buffer[PATH_MAX];
    strncpy(buffer, path, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    size_t len = strlen(buffer);
    for (size_t i = 1; i < len; ++i) {
        if (buffer[i] == '/') {
            char prev = buffer[i];
            buffer[i] = '\0';
            #ifdef _WIN32
            if (_mkdir(buffer) != 0 && errno != EEXIST) {
            #else
            if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
            #endif
                return false;
            }
            buffer[i] = prev;
        }
    }
    #ifdef _WIN32
    if (_mkdir(buffer) != 0 && errno != EEXIST) {
    #else
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
    #endif
        return false;
    }
    return true;
}

static void
libretro_host_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level == RETRO_LOG_DEBUG || level == RETRO_LOG_INFO) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "libretro: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void
libretro_host_destroyTexture(void)
{
    if (libretro_host.texture) {
        SDL_DestroyTexture(libretro_host.texture);
        libretro_host.texture = NULL;
    }
    libretro_host.textureWidth = 0;
    libretro_host.textureHeight = 0;
    libretro_host.textureSeq = 0;
}

static void
libretro_host_clearFrame(void)
{
    if (libretro_host.frameData) {
        free(libretro_host.frameData);
        libretro_host.frameData = NULL;
    }
    libretro_host.frameCapacity = 0;
    libretro_host.framePitch = 0;
    libretro_host.frameWidth = 0;
    libretro_host.frameHeight = 0;
    libretro_host.frameSeq = 0;
}

static void
libretro_host_closeAudio(void)
{
    if (libretro_host.audioDev) {
        SDL_ClearQueuedAudio(libretro_host.audioDev);
        SDL_CloseAudioDevice(libretro_host.audioDev);
        libretro_host.audioDev = 0;
    }
    libretro_host.audioSampleRate = 0;
    libretro_host.audioMaxQueue = 0;
}

static void
libretro_host_openAudio(void)
{
    if (!libretro_host.audioEnabled) {
        libretro_host_closeAudio();
        return;
    }
    libretro_host_closeAudio();
    int rate = (int)libretro_host.avInfo.timing.sample_rate;
    if (rate <= 0) {
        rate = 44100;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    SDL_AudioSpec got;
    SDL_zero(got);
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!dev) {
        fprintf(stderr, "libretro: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    libretro_host.audioDev = dev;
    libretro_host.audioSampleRate = got.freq;
    int ms = debugger.config.audioBufferMs;
    if (ms <= 0) {
        ms = 50;
    }
    size_t bytes_per_sec = (size_t)got.freq * (size_t)got.channels * sizeof(int16_t);
    libretro_host.audioMaxQueue = (bytes_per_sec * (size_t)ms) / 1000;
    SDL_PauseAudioDevice(dev, 0);
}

static void
libretro_host_videoCallback(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || !width || !height) {
        return;
    }
    unsigned frameIndex = ++libretro_host.videoFrameCount;
    (void)frameIndex;
    size_t bytesPerPixel = 0;
    if (width > 0) {
        bytesPerPixel = pitch / (size_t)width;
    }
    enum retro_pixel_format convertFormat = libretro_host.pixelFormat;
    if (convertFormat == RETRO_PIXEL_FORMAT_XRGB8888 && bytesPerPixel == 2) {
        convertFormat = RETRO_PIXEL_FORMAT_0RGB1555;
    }
    if (convertFormat == RETRO_PIXEL_FORMAT_RGB565 ||
        convertFormat == RETRO_PIXEL_FORMAT_0RGB1555) {
        size_t needed = (size_t)width * (size_t)height * 4;
        if (needed > libretro_host.frameCapacity) {
            uint8_t *next = (uint8_t *)realloc(libretro_host.frameData, needed);
            if (!next) {
                return;
            }
            libretro_host.frameData = next;
            libretro_host.frameCapacity = needed;
        }
        uint32_t *dst = (uint32_t *)libretro_host.frameData;
        if (convertFormat == RETRO_PIXEL_FORMAT_RGB565) {
            for (unsigned y = 0; y < height; ++y) {
                const uint16_t *srcRow = (const uint16_t *)((const uint8_t *)data + (size_t)y * pitch);
                uint32_t *dstRow = dst + (size_t)y * (size_t)width;
                for (unsigned x = 0; x < width; ++x) {
                    uint16_t p = srcRow[x];
                    uint8_t r = (uint8_t)(((p >> 11) & 0x1F) << 3);
                    uint8_t g = (uint8_t)(((p >> 5) & 0x3F) << 2);
                    uint8_t b = (uint8_t)((p & 0x1F) << 3);
                    dstRow[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                }
            }
        } else {
            for (unsigned y = 0; y < height; ++y) {
                const uint16_t *srcRow = (const uint16_t *)((const uint8_t *)data + (size_t)y * pitch);
                uint32_t *dstRow = dst + (size_t)y * (size_t)width;
                for (unsigned x = 0; x < width; ++x) {
                    uint16_t p = srcRow[x];
                    uint8_t r = (uint8_t)(((p >> 10) & 0x1F) << 3);
                    uint8_t g = (uint8_t)(((p >> 5) & 0x1F) << 3);
                    uint8_t b = (uint8_t)((p & 0x1F) << 3);
                    dstRow[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                }
            }
        }
        libretro_host.framePitch = (size_t)width * 4;
        libretro_host.frameWidth = (int)width;
        libretro_host.frameHeight = (int)height;
        libretro_host.frameSeq++;
        return;
    }

    size_t needed = (size_t)height * pitch;
    if (needed > libretro_host.frameCapacity) {
        uint8_t *next = (uint8_t *)realloc(libretro_host.frameData, needed);
        if (!next) {
            return;
        }
        libretro_host.frameData = next;
        libretro_host.frameCapacity = needed;
    }
    memcpy(libretro_host.frameData, data, needed);
    libretro_host.framePitch = pitch;
    libretro_host.frameWidth = (int)width;
    libretro_host.frameHeight = (int)height;
    libretro_host.frameSeq++;
}

static void
libretro_host_audioSample(int16_t left, int16_t right)
{
    if (!libretro_host.audioDev) {
        return;
    }
    int16_t sample[2] = { left, right };
    if (libretro_host.audioMaxQueue > 0) {
        size_t queued = SDL_GetQueuedAudioSize(libretro_host.audioDev);
        if (queued >= libretro_host.audioMaxQueue) {
            SDL_ClearQueuedAudio(libretro_host.audioDev);
            return;
        }
    }
    SDL_QueueAudio(libretro_host.audioDev, sample, sizeof(sample));
}

static size_t
libretro_host_audioSampleBatch(const int16_t *data, size_t frames)
{
    if (!libretro_host.audioDev || !data || frames == 0) {
        return frames;
    }
    size_t bytes = frames * 2 * sizeof(int16_t);
    if (libretro_host.audioMaxQueue > 0) {
        size_t queued = SDL_GetQueuedAudioSize(libretro_host.audioDev);
        if (queued >= libretro_host.audioMaxQueue) {
            SDL_ClearQueuedAudio(libretro_host.audioDev);
            return frames;
        }
    }
    SDL_QueueAudio(libretro_host.audioDev, data, bytes);
    return frames;
}

static void
libretro_host_inputPoll(void)
{
    if (libretro_host.autoPressDelayFrames > 0) {
        libretro_host.autoPressDelayFrames--;
        return;
    }
    if (libretro_host.autoPressHoldFrames > 0) {
        uint32_t bit = 1u << RETRO_DEVICE_ID_JOYPAD_START;
        libretro_host.autoInputMask[0] |= bit;
        libretro_host.autoPressHoldFrames--;
        if (libretro_host.autoPressHoldFrames == 0) {
            libretro_host.autoInputMask[0] &= ~bit;
        }
    }
}

static int16_t
libretro_host_inputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (device == RETRO_DEVICE_KEYBOARD) {
        if (id >= RETROK_LAST) {
            return 0;
        }
        uint8_t down = libretro_host.keyboardState[id];
        return down ? 1 : 0;
    }
    if (device != RETRO_DEVICE_JOYPAD || index != 0) {
        return 0;
    }
    if (port >= LIBRETRO_HOST_MAX_PORTS || id >= 32) {
        return 0;
    }
    uint32_t mask = libretro_host.inputMask[port] | libretro_host.autoInputMask[port];
    return (mask & (1u << id)) ? 1 : 0;
}

static void *
libretro_host_loadSymbol(const char *name)
{
    if (!libretro_host.lib) {
        return NULL;
    }
#ifdef _WIN32
    FARPROC symbol = GetProcAddress((HMODULE)libretro_host.lib, name);
    if (!symbol) {
        DWORD err = GetLastError();
        fprintf(stderr, "libretro: missing symbol %s: winerr=%lu\n", name, (unsigned long)err);
    }
    return (void*)symbol;
#else
    dlerror();
    void *symbol = dlsym(libretro_host.lib, name);
    if (!symbol) {
        const char *error = dlerror();
        fprintf(stderr, "libretro: missing symbol %s: %s\n", name, error ? error : "unknown");
    }
    return symbol;
#endif
}

static bool
libretro_host_environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_ROTATION:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        if (!data) {
            return false;
        }
        {
            const struct retro_message *msg = data;
            if (msg->msg) {
                fprintf(stderr, "libretro message: %s\n", msg->msg);
            }
        }
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (!data || !*libretro_host.systemDir) {
            return false;
        }
        *(const char **)data = libretro_host.systemDir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!data || !*libretro_host.saveDir) {
            return false;
        }
        *(const char **)data = libretro_host.saveDir;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        if (!data) {
            return false;
        }
        {
            struct retro_variable *var = data;
            libretro_option_t *opt = libretro_host_findOption(var->key);
            if (!opt) {
                return false;
            }
            if (opt->value) {
                var->value = opt->value;
            } else {
                var->value = opt->default_value;
            }
            return var->value != NULL;
        }
    case RETRO_ENVIRONMENT_SET_VARIABLE:
        if (!data) {
            return false;
        }
        {
            const struct retro_variable *var = data;
            libretro_host_setOptionValue(var->key, var->value);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        if (!data) {
            return false;
        }
        {
            const struct retro_variable *vars = data;
            for (size_t i = 0; vars[i].key; ++i) {
                libretro_host_setOptionValue(vars[i].key, vars[i].value);
            }
        }
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        if (!data) {
            return false;
        }
        if (*(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888) {
            libretro_host.pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
            return true;
        }
        return false;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!data) {
            return false;
        }
        {
            struct retro_log_callback *log = data;
            log->log = libretro_host_log;
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        libretro_host_setOptions((const struct retro_core_option_definition *)data);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        if (!data) {
            return false;
        }
        {
            const struct retro_core_options_intl *intl = data;
            const struct retro_core_option_definition *defs = intl->local ? intl->local : intl->us;
            libretro_host_setOptions(defs);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        if (!data) {
            return false;
        }
        {
            const struct retro_keyboard_callback *cb = data;
            libretro_host.keyboardCb = cb ? cb->callback : NULL;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (!data) {
            return false;
        }
        *(unsigned *)data = 1;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (!data) {
            return false;
        }
        *(bool *)data = false;
        return true;
    default:
        return false;
    }
}

static bool
libretro_host_loadRomData(const char *path, void **data, size_t *size)
{
    if (!path || !*path || !data || !size) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "libretro: failed to open rom %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "libretro: failed to seek rom %s: %s\n", path, strerror(errno));
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0) {
        fprintf(stderr, "libretro: failed to tell rom %s: %s\n", path, strerror(errno));
        fclose(file);
        return false;
    }
    if (length == 0) {
        fprintf(stderr, "libretro: rom %s is empty\n", path);
        fclose(file);
        return false;
    }
    if ((size_t)length > SIZE_MAX) {
        fprintf(stderr, "libretro: rom %s is too large\n", path);
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "libretro: failed to rewind rom %s: %s\n", path, strerror(errno));
        fclose(file);
        return false;
    }
    size_t romSize = (size_t)length;
    void *buffer = malloc(romSize);
    if (!buffer) {
        fprintf(stderr, "libretro: out of memory loading rom %s\n", path);
        fclose(file);
        return false;
    }
    size_t read = fread(buffer, 1, romSize, file);
    fclose(file);
    if (read != romSize) {
        fprintf(stderr, "libretro: failed to read rom %s (read %zu of %zu)\n", path, read, romSize);
        free(buffer);
        return false;
    }
    *data = buffer;
    *size = romSize;
    return true;
}

static void
libretro_host_clearState(void)
{
    libretro_host_clearFrame();
    if (libretro_host.stateData) {
        alloc_free(libretro_host.stateData);
        libretro_host.stateData = NULL;
        libretro_host.stateSize = 0;
    }
    libretro_host_clearOptions();
    libretro_host_clearOptionOverrides();
    memset(&libretro_host, 0, sizeof(libretro_host));
}

bool
libretro_host_init(SDL_Renderer *renderer)
{
    (void)renderer;
    libretro_host_clearState();
    libretro_host.pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
    return true;
}

bool
libretro_host_start(const char *corePath, const char *romPath,
                    const char *systemDir, const char *saveDir)
{
    if (!corePath || !*corePath || !romPath || !*romPath || !systemDir || !*systemDir) {
        fprintf(stderr, "libretro: core, rom, or system directory missing\n");
        return false;
    }
    strncpy(libretro_host.corePath, corePath, PATH_MAX - 1);
    libretro_host.corePath[PATH_MAX - 1] = '\0';
    strncpy(libretro_host.romPath, romPath, PATH_MAX - 1);
    libretro_host.romPath[PATH_MAX - 1] = '\0';
    strncpy(libretro_host.systemDir, systemDir, PATH_MAX - 1);
    libretro_host.systemDir[PATH_MAX - 1] = '\0';
    if (saveDir && *saveDir) {
        strncpy(libretro_host.saveDir, saveDir, PATH_MAX - 1);
        libretro_host.saveDir[PATH_MAX - 1] = '\0';
    } else {
        strncpy(libretro_host.saveDir, libretro_host.systemDir, PATH_MAX - 1);
        libretro_host.saveDir[PATH_MAX - 1] = '\0';
    }
    if (!libretro_host_loadRomData(libretro_host.romPath, &libretro_host.romData, &libretro_host.romSize)) {
        libretro_host_shutdown();
        return false;
    }
    if (!libretro_host_mkdir_p(libretro_host.systemDir) || !libretro_host_mkdir_p(libretro_host.saveDir)) {
        fprintf(stderr, "libretro: failed to create directories\n");
        return false;
    }
    #ifdef _WIN32
    libretro_host.lib = (void*)LoadLibraryA(libretro_host.corePath);
    if (!libretro_host.lib) {
        DWORD err = GetLastError();
        fprintf(stderr, "libretro: LoadLibrary failed: winerr=%lu\n", (unsigned long)err);
        return false;
    }
    #else
    libretro_host.lib = dlopen(libretro_host.corePath, RTLD_NOW | RTLD_LOCAL);
    if (!libretro_host.lib) {
        fprintf(stderr, "libretro: dlopen failed: %s\n", dlerror());
        return false;
    }
    #endif
    libretro_host.setEnvironment = (retro_set_environment_fn_t)libretro_host_loadSymbol("retro_set_environment");
    libretro_host.setVideoRefresh = (retro_set_video_refresh_fn_t)libretro_host_loadSymbol("retro_set_video_refresh");
    libretro_host.setAudioSample = (retro_set_audio_sample_fn_t)libretro_host_loadSymbol("retro_set_audio_sample");
    libretro_host.setAudioSampleBatch = (retro_set_audio_sample_batch_fn_t)libretro_host_loadSymbol("retro_set_audio_sample_batch");
    libretro_host.setInputPoll = (retro_set_input_poll_fn_t)libretro_host_loadSymbol("retro_set_input_poll");
    libretro_host.setInputState = (retro_set_input_state_fn_t)libretro_host_loadSymbol("retro_set_input_state");
    libretro_host.init = (retro_init_fn_t)libretro_host_loadSymbol("retro_init");
    libretro_host.loadGame = (retro_load_game_fn_t)libretro_host_loadSymbol("retro_load_game");
    libretro_host.getSystemAvInfo = (retro_get_system_av_info_fn_t)libretro_host_loadSymbol("retro_get_system_av_info");
    libretro_host.run = (retro_run_fn_t)libretro_host_loadSymbol("retro_run");
    libretro_host.reset = (retro_reset_fn_t)libretro_host_loadSymbol("retro_reset");
    libretro_host.unloadGame = (retro_unload_game_fn_t)libretro_host_loadSymbol("retro_unload_game");
    libretro_host.deinit = (retro_deinit_fn_t)libretro_host_loadSymbol("retro_deinit");
    libretro_host.getMemoryData = (retro_get_memory_data_fn_t)libretro_host_loadSymbol("retro_get_memory_data");
    libretro_host.getMemorySize = (retro_get_memory_size_fn_t)libretro_host_loadSymbol("retro_get_memory_size");
    libretro_host.serializeSize = (retro_serialize_size_fn_t)libretro_host_loadSymbol("retro_serialize_size");
    libretro_host.serialize = (retro_serialize_fn_t)libretro_host_loadSymbol("retro_serialize");
    libretro_host.unserialize = (retro_unserialize_fn_t)libretro_host_loadSymbol("retro_unserialize");
    libretro_host.debugReadRegs = (geo_debug_read_regs_fn_t)libretro_host_loadSymbol("geo_debug_read_regs");
    libretro_host.debugPause = (geo_debug_pause_fn_t)libretro_host_loadSymbol("geo_debug_pause");
    libretro_host.debugResume = (geo_debug_resume_fn_t)libretro_host_loadSymbol("geo_debug_resume");
    libretro_host.debugIsPaused = (geo_debug_is_paused_fn_t)libretro_host_loadSymbol("geo_debug_is_paused");
    libretro_host.debugStepInstr = (geo_debug_step_instr_fn_t)libretro_host_loadSymbol("geo_debug_step_instr");
    libretro_host.debugStepLine = (geo_debug_step_line_fn_t)libretro_host_loadSymbol("geo_debug_step_line");
    libretro_host.debugStepNext = (geo_debug_step_next_fn_t)libretro_host_loadSymbol("geo_debug_step_next");
    libretro_host.debugAddBreakpoint = (geo_debug_add_breakpoint_fn_t)libretro_host_loadSymbol("geo_debug_add_breakpoint");
    libretro_host.debugRemoveBreakpoint = (geo_debug_remove_breakpoint_fn_t)libretro_host_loadSymbol("geo_debug_remove_breakpoint");
    libretro_host.debugAddTempBreakpoint = (geo_debug_add_temp_breakpoint_fn_t)libretro_host_loadSymbol("geo_debug_add_temp_breakpoint");
    libretro_host.debugRemoveTempBreakpoint = (geo_debug_remove_temp_breakpoint_fn_t)libretro_host_loadSymbol("geo_debug_remove_temp_breakpoint");
    libretro_host.debugResetWatchpoints = (geo_debug_reset_watchpoints_fn_t)libretro_host_loadSymbol("geo_debug_reset_watchpoints");
    libretro_host.debugAddWatchpoint = (geo_debug_add_watchpoint_fn_t)libretro_host_loadSymbol("geo_debug_add_watchpoint");
    libretro_host.debugRemoveWatchpoint = (geo_debug_remove_watchpoint_fn_t)libretro_host_loadSymbol("geo_debug_remove_watchpoint");
    libretro_host.debugReadWatchpoints = (geo_debug_read_watchpoints_fn_t)libretro_host_loadSymbol("geo_debug_read_watchpoints");
    libretro_host.debugGetWatchpointEnabledMask = (geo_debug_get_watchpoint_enabled_mask_fn_t)libretro_host_loadSymbol("geo_debug_get_watchpoint_enabled_mask");
    libretro_host.debugSetWatchpointEnabledMask = (geo_debug_set_watchpoint_enabled_mask_fn_t)libretro_host_loadSymbol("geo_debug_set_watchpoint_enabled_mask");
    libretro_host.debugConsumeWatchbreak = (geo_debug_consume_watchbreak_fn_t)libretro_host_loadSymbol("geo_debug_consume_watchbreak");
    libretro_host.debugResetProtects = (geo_debug_reset_protects_fn_t)libretro_host_loadSymbol("geo_debug_reset_protects");
    libretro_host.debugAddProtect = (geo_debug_add_protect_fn_t)libretro_host_loadSymbol("geo_debug_add_protect");
    libretro_host.debugRemoveProtect = (geo_debug_remove_protect_fn_t)libretro_host_loadSymbol("geo_debug_remove_protect");
    libretro_host.debugReadProtects = (geo_debug_read_protects_fn_t)libretro_host_loadSymbol("geo_debug_read_protects");
    libretro_host.debugGetProtectEnabledMask = (geo_debug_get_protect_enabled_mask_fn_t)libretro_host_loadSymbol("geo_debug_get_protect_enabled_mask");
    libretro_host.debugSetProtectEnabledMask = (geo_debug_set_protect_enabled_mask_fn_t)libretro_host_loadSymbol("geo_debug_set_protect_enabled_mask");
    libretro_host.debugReadCallstack = (geo_debug_read_callstack_fn_t)libretro_host_loadSymbol("geo_debug_read_callstack");
    libretro_host.debugReadMemory = (geo_debug_read_memory_fn_t)libretro_host_loadSymbol("geo_debug_read_memory");
    libretro_host.debugWriteMemory = (geo_debug_write_memory_fn_t)libretro_host_loadSymbol("geo_debug_write_memory");
    libretro_host.debugProfilerStart = (geo_debug_profiler_start_fn_t)libretro_host_loadSymbol("geo_debug_profiler_start");
    libretro_host.debugProfilerStop = (geo_debug_profiler_stop_fn_t)libretro_host_loadSymbol("geo_debug_profiler_stop");
    libretro_host.debugProfilerIsEnabled = (geo_debug_profiler_is_enabled_fn_t)libretro_host_loadSymbol("geo_debug_profiler_is_enabled");
    libretro_host.debugProfilerStreamNext = (geo_debug_profiler_stream_next_fn_t)libretro_host_loadSymbol("geo_debug_profiler_stream_next");
    libretro_host.debugTextRead = (geo_debug_text_read_fn_t)libretro_host_loadSymbol("geo_debug_text_read");
    libretro_host.debugGetSpriteState = (geo_debug_get_sprite_state_fn_t)libretro_host_loadSymbol("geo_debug_get_sprite_state");
    libretro_host.debugGetP1Rom = (geo_debug_get_p1_rom_fn_t)libretro_host_loadSymbol("geo_debug_get_p1_rom");
    libretro_host.debugDisassembleQuick = (geo_debug_disassemble_quick_fn_t)libretro_host_loadSymbol("geo_debug_disassemble_quick");
    libretro_host.debugReadCheckpoints = (geo_debug_read_checkpoints_fn_t)libretro_host_loadSymbol("geo_debug_read_checkpoints");
    libretro_host.debugResetCheckpoints = (geo_debug_reset_checkpoints_fn_t)libretro_host_loadSymbol("geo_debug_reset_checkpoints");
    libretro_host.debugSetCheckpointEnabled = (geo_debug_set_checkpoint_enabled_fn_t)libretro_host_loadSymbol("geo_debug_set_checkpoint_enabled");
    libretro_host.debugGetCheckpointEnabled = (geo_debug_get_checkpoint_enabled_fn_t)libretro_host_loadSymbol("geo_debug_get_checkpoint_enabled");
    libretro_host.debugReadCycleCount = (geo_debug_read_cycle_count_fn_t)libretro_host_loadSymbol("geo_debug_read_cycle_count");
    libretro_host.setVblankCallback = (geo_set_vblank_callback_fn_t)libretro_host_loadSymbol("geo_set_vblank_callback");
    if (!libretro_host.setEnvironment || !libretro_host.setVideoRefresh ||
        !libretro_host.setInputPoll || !libretro_host.setInputState ||
        !libretro_host.init || !libretro_host.loadGame || !libretro_host.getSystemAvInfo ||
        !libretro_host.run || !libretro_host.deinit) {
        libretro_host_shutdown();
        return false;
    }
    libretro_host.setEnvironment(libretro_host_environment);
    libretro_host.setVideoRefresh(libretro_host_videoCallback);
    libretro_host.setAudioSample(libretro_host_audioSample);
    libretro_host.setAudioSampleBatch(libretro_host_audioSampleBatch);
    libretro_host.setInputPoll(libretro_host_inputPoll);
    libretro_host.setInputState(libretro_host_inputState);
    libretro_host_applyOptionOverrides();
    libretro_host.init();
    struct retro_game_info info = {
        .path = libretro_host.romPath,
        .data = libretro_host.romData,
        .size = libretro_host.romSize,
        .meta = NULL,
    };
    if (!libretro_host.loadGame(&info)) {
        fprintf(stderr, "libretro: failed to load rom %s\n", libretro_host.romPath);
        libretro_host_shutdown();
        return false;
    }
    if (libretro_host.reset) {
        libretro_host.reset();
    }
    if (debugger.config.skipBiosLogo) {
        libretro_host.autoPressDelayFrames = 85;
        libretro_host.autoPressHoldFrames = 5;
    } else {
        libretro_host.autoPressDelayFrames = 0;
        libretro_host.autoPressHoldFrames = 0;
        libretro_host.autoInputMask[0] = 0;
    }
    libretro_host.gameLoaded = true;
    libretro_host.getSystemAvInfo(&libretro_host.avInfo);
    libretro_host.audioEnabled = debugger.config.audioEnabled ? 1 : 0;
    libretro_host_openAudio();
    if (libretro_host.avInfo.geometry.base_width && libretro_host.avInfo.geometry.base_height) {
        libretro_host_destroyTexture();
        libretro_host.textureWidth = libretro_host.avInfo.geometry.base_width;
        libretro_host.textureHeight = libretro_host.avInfo.geometry.base_height;
    }
    libretro_host.running = true;
    return true;
}

void
libretro_host_shutdown(void)
{
    if (libretro_host.gameLoaded && libretro_host.unloadGame) {
        libretro_host.unloadGame();
    }
    if (libretro_host.deinit) {
        libretro_host.deinit();
    }
    if (libretro_host.lib) {
        #ifdef _WIN32
        FreeLibrary((HMODULE)libretro_host.lib);
        #else
        dlclose(libretro_host.lib);
        #endif
        libretro_host.lib = NULL;
    }
    libretro_host_destroyTexture();
    libretro_host_clearFrame();
    libretro_host_closeAudio();
    if (libretro_host.romData) {
        free(libretro_host.romData);
        libretro_host.romData = NULL;
        libretro_host.romSize = 0;
    }
    if (libretro_host.stateData) {
        alloc_free(libretro_host.stateData);
        libretro_host.stateData = NULL;
        libretro_host.stateSize = 0;
    }
    libretro_host_clearOptions();
    libretro_host.running = false;
    libretro_host.gameLoaded = false;
    memset(&libretro_host, 0, sizeof(libretro_host));
}

void
_libretro_host_runOnce(void)
{
  libretro_host_runFrame();
}

SDL_Texture *
libretro_host_getTexture(SDL_Renderer *renderer)
{
    if (!renderer) {
        return NULL;
    }
    SDL_Texture *tex = libretro_host.texture;
    uint64_t seq = 0;
    if (!libretro_host.frameData || libretro_host.frameWidth <= 0 || libretro_host.frameHeight <= 0) {
        return tex;
    }
    seq = libretro_host.frameSeq;
    if (!tex ||
        libretro_host.frameWidth != libretro_host.textureWidth ||
        libretro_host.frameHeight != libretro_host.textureHeight) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
            SDL_TEXTUREACCESS_STREAMING, libretro_host.frameWidth, libretro_host.frameHeight);
        if (!tex) {
            fprintf(stderr, "libretro: SDL_CreateTexture failed: %s\n", SDL_GetError());
            libretro_host.texture = NULL;
            libretro_host.textureWidth = 0;
            libretro_host.textureHeight = 0;
            libretro_host.textureSeq = 0;
            return NULL;
        }
        libretro_host.texture = tex;
        libretro_host.textureWidth = libretro_host.frameWidth;
        libretro_host.textureHeight = libretro_host.frameHeight;
        libretro_host.textureSeq = 0;
    }
    if (seq != libretro_host.textureSeq) {
        SDL_UpdateTexture(tex, NULL, libretro_host.frameData, (int)libretro_host.framePitch);
        libretro_host.textureSeq = seq;
    }
    return libretro_host.texture;
}

bool
libretro_host_getFrame(const uint8_t **out_data, int *out_width, int *out_height, size_t *out_pitch)
{
    if (!out_data || !out_width || !out_height || !out_pitch) {
        return false;
    }
    if (!libretro_host.frameData || libretro_host.frameWidth <= 0 || libretro_host.frameHeight <= 0) {
        return false;
    }
    *out_data = libretro_host.frameData;
    *out_width = libretro_host.frameWidth;
    *out_height = libretro_host.frameHeight;
    *out_pitch = libretro_host.framePitch;
    return true;
}

void
libretro_host_setJoypadState(unsigned port, unsigned id, int pressed)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    if (port >= LIBRETRO_HOST_MAX_PORTS || id >= 32) {
        return;
    }
    uint32_t bit = 1u << id;
    if (pressed) {
        libretro_host.inputMask[port] |= bit;
    } else {
        libretro_host.inputMask[port] &= ~bit;
    }
    input_record_recordJoypad(debugger.frameCounter + 1, port, id, pressed);
}

void
libretro_host_clearJoypadState(void)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    memset(libretro_host.inputMask, 0, sizeof(libretro_host.inputMask));
    input_record_recordClear(debugger.frameCounter + 1);
}

void
libretro_host_sendKeyEvent(unsigned keycode, uint32_t character,
                           uint16_t modifiers, int pressed)
{
    if (input_record_isPlayback() && !input_record_isInjecting()) {
        return;
    }
    if (keycode < RETROK_LAST) {
        libretro_host.keyboardState[keycode] = pressed ? 1 : 0;
    }
    if (libretro_host.keyboardCb) {
        libretro_host.keyboardCb(pressed ? true : false, keycode, character, modifiers);
    }
    input_record_recordKey(debugger.frameCounter + 1, keycode, character, modifiers, pressed);
}

bool
libretro_host_isRunning(void)
{
    return libretro_host.running;
}

const void *
libretro_host_getMemory(unsigned id, size_t *size)
{
    if (size) {
        *size = 0;
    }
    if (!libretro_host.gameLoaded || !libretro_host.getMemoryData) {
        return NULL;
    }
    if (size && libretro_host.getMemorySize) {
        *size = libretro_host.getMemorySize(id);
    }
    return libretro_host.getMemoryData(id);
}

bool
libretro_host_readRegs(uint32_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadRegs) {
        return false;
    }
    size_t n = libretro_host.debugReadRegs(out, cap);
    if (out_count) {
        *out_count = n;
    }
    return n > 0;
}

bool
libretro_host_debugPause(void)
{
    if (!libretro_host.debugPause) {
        return false;
    }
    libretro_host.debugPause();
    return true;
}

bool
libretro_host_debugResume(void)
{
    if (!libretro_host.debugResume) {
        return false;
    }
    libretro_host.debugResume();
    return true;
}

bool
libretro_host_debugIsPaused(int *out_paused)
{
    if (out_paused) {
        *out_paused = 0;
    }
    if (!libretro_host.debugIsPaused) {
        return false;
    }
    if (out_paused) {
        *out_paused = libretro_host.debugIsPaused() ? 1 : 0;
    }
    return true;
}

bool
libretro_host_debugStepInstr(void)
{
    if (!libretro_host.debugStepInstr) {
        return false;
    }
    libretro_host.debugStepInstr();
    return true;
}

bool
libretro_host_debugStepLine(void)
{
    if (!libretro_host.debugStepLine) {
        return false;
    }
    libretro_host.debugStepLine();
    return true;
}

bool
libretro_host_debugStepNext(void)
{
    if (!libretro_host.debugStepNext) {
        return false;
    }
    libretro_host.debugStepNext();
    return true;
}

bool
libretro_host_debugAddBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugAddBreakpoint) {
        return false;
    }
    libretro_host.debugAddBreakpoint(addr);
    return true;
}

bool
libretro_host_debugRemoveBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugRemoveBreakpoint) {
        return false;
    }
    libretro_host.debugRemoveBreakpoint(addr);
    return true;
}

bool
libretro_host_debugAddTempBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugAddTempBreakpoint) {
        return false;
    }
    libretro_host.debugAddTempBreakpoint(addr);
    return true;
}

bool
libretro_host_debugRemoveTempBreakpoint(uint32_t addr)
{
    if (!libretro_host.debugRemoveTempBreakpoint) {
        return false;
    }
    libretro_host.debugRemoveTempBreakpoint(addr);
    return true;
}

bool
libretro_host_debugResetWatchpoints(void)
{
    if (!libretro_host.debugResetWatchpoints) {
        return false;
    }
    libretro_host.debugResetWatchpoints();
    return true;
}

bool
libretro_host_debugAddWatchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand, uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand, uint32_t *out_index)
{
    if (out_index) {
        *out_index = 0;
    }
    if (!libretro_host.debugAddWatchpoint) {
        return false;
    }
    int r = libretro_host.debugAddWatchpoint(addr, op_mask, diff_operand, value_operand, old_value_operand, size_operand, addr_mask_operand);
    if (r < 0) {
        return false;
    }
    if (out_index) {
        *out_index = (uint32_t)r;
    }
    return true;
}

bool
libretro_host_debugRemoveWatchpoint(uint32_t index)
{
    if (!libretro_host.debugRemoveWatchpoint) {
        return false;
    }
    libretro_host.debugRemoveWatchpoint(index);
    return true;
}

bool
libretro_host_debugReadWatchpoints(geo_debug_watchpoint_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadWatchpoints) {
        return false;
    }
    size_t count = libretro_host.debugReadWatchpoints(out, cap);
    if (out_count) {
        *out_count = count;
    }
    return true;
}

bool
libretro_host_debugGetWatchpointEnabledMask(uint64_t *out_mask)
{
    if (out_mask) {
        *out_mask = 0;
    }
    if (!libretro_host.debugGetWatchpointEnabledMask) {
        return false;
    }
    uint64_t mask = libretro_host.debugGetWatchpointEnabledMask();
    if (out_mask) {
        *out_mask = mask;
    }
    return true;
}

bool
libretro_host_debugSetWatchpointEnabledMask(uint64_t mask)
{
    if (!libretro_host.debugSetWatchpointEnabledMask) {
        return false;
    }
    libretro_host.debugSetWatchpointEnabledMask(mask);
    return true;
}

bool
libretro_host_debugConsumeWatchbreak(geo_debug_watchbreak_t *out)
{
    if (!out || !libretro_host.debugConsumeWatchbreak) {
        return false;
    }
    return libretro_host.debugConsumeWatchbreak(out) ? true : false;
}

bool
libretro_host_debugResetProtects(void)
{
    if (!libretro_host.debugResetProtects) {
        return false;
    }
    libretro_host.debugResetProtects();
    return true;
}

bool
libretro_host_debugAddProtect(uint32_t addr, uint32_t size_bits, uint32_t mode, uint32_t value, uint32_t *out_index)
{
    if (out_index) {
        *out_index = 0;
    }
    if (!libretro_host.debugAddProtect) {
        return false;
    }
    int r = libretro_host.debugAddProtect(addr, size_bits, mode, value);
    if (r < 0) {
        return false;
    }
    if (out_index) {
        *out_index = (uint32_t)r;
    }
    return true;
}

bool
libretro_host_debugRemoveProtect(uint32_t index)
{
    if (!libretro_host.debugRemoveProtect) {
        return false;
    }
    libretro_host.debugRemoveProtect(index);
    return true;
}

bool
libretro_host_debugReadProtects(geo_debug_protect_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadProtects) {
        return false;
    }
    size_t count = libretro_host.debugReadProtects(out, cap);
    if (out_count) {
        *out_count = count;
    }
    return true;
}

bool
libretro_host_debugGetProtectEnabledMask(uint64_t *out_mask)
{
    if (out_mask) {
        *out_mask = 0;
    }
    if (!libretro_host.debugGetProtectEnabledMask) {
        return false;
    }
    uint64_t mask = libretro_host.debugGetProtectEnabledMask();
    if (out_mask) {
        *out_mask = mask;
    }
    return true;
}

bool
libretro_host_debugSetProtectEnabledMask(uint64_t mask)
{
    if (!libretro_host.debugSetProtectEnabledMask) {
        return false;
    }
    libretro_host.debugSetProtectEnabledMask(mask);
    return true;
}

bool
libretro_host_debugReadCallstack(uint32_t *out, size_t cap, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugReadCallstack) {
        return false;
    }
    size_t count = libretro_host.debugReadCallstack(out, cap);
    if (out_count) {
        *out_count = count;
    }
    return true;
}

bool
libretro_host_debugReadMemory(uint32_t addr, void *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugReadMemory) {
        return false;
    }
    size_t read = libretro_host.debugReadMemory(addr, (uint8_t *)out, cap);
    return read == cap;
}

bool
libretro_host_debugWriteMemory(uint32_t addr, uint32_t value, size_t size)
{
    if (size == 0 || size > sizeof(uint32_t) || !libretro_host.debugWriteMemory) {
        return false;
    }
    return libretro_host.debugWriteMemory(addr, value, size) ? true : false;
}

bool
libretro_host_profilerStart(int stream)
{
    if (!libretro_host.debugProfilerStart) {
        return false;
    }
    libretro_host.debugProfilerStart(stream ? 1 : 0);
    return true;
}

bool
libretro_host_profilerStop(void)
{
    if (!libretro_host.debugProfilerStop) {
        return false;
    }
    libretro_host.debugProfilerStop();
    return true;
}

bool
libretro_host_profilerIsEnabled(int *out_enabled)
{
    if (out_enabled) {
        *out_enabled = 0;
    }
    if (!libretro_host.debugProfilerIsEnabled) {
        return false;
    }
    if (out_enabled) {
        *out_enabled = libretro_host.debugProfilerIsEnabled() ? 1 : 0;
    }
    return true;
}

bool
libretro_host_profilerStreamNext(char *out, size_t cap, size_t *out_len)
{
    if (out_len) {
        *out_len = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugProfilerStreamNext) {
        return false;
    }
    size_t len = libretro_host.debugProfilerStreamNext(out, cap);
    if (out_len) {
        *out_len = len;
    }
    return len > 0;
}

size_t
libretro_host_debugTextRead(char *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugTextRead) {
        return 0;
    }
    return libretro_host.debugTextRead(out, cap);
}

bool
libretro_host_debugGetSpriteState(geo_debug_sprite_state_t *out)
{
    if (!out || !libretro_host.debugGetSpriteState) {
        return false;
    }
    size_t n = libretro_host.debugGetSpriteState(out, sizeof(*out));
    return n == sizeof(*out);
}

bool
libretro_host_debugGetP1Rom(geo_debug_rom_region_t *out)
{
    if (!out || !libretro_host.debugGetP1Rom) {
        return false;
    }
    size_t n = libretro_host.debugGetP1Rom(out, sizeof(*out));
    if (n != sizeof(*out) || !out->data || out->size == 0) {
        fprintf(stderr, "libretro: debugGetP1Rom failed (n=%zu data=%p size=%zu)\n",
                n, out ? (void *)out->data : NULL, out ? (size_t)out->size : 0);
        return false;
    }
    return true;
}

size_t
libretro_host_debugReadCheckpoints(geo_debug_checkpoint_t *out, size_t cap)
{
    if (!out || cap == 0 || !libretro_host.debugReadCheckpoints) {
        return 0;
    }
    return libretro_host.debugReadCheckpoints(out, cap);
}

bool
libretro_host_debugResetCheckpoints(void)
{
    if (!libretro_host.debugResetCheckpoints) {
        return false;
    }
    libretro_host.debugResetCheckpoints();
    return true;
}

uint64_t
libretro_host_debugReadCycleCount(void)
{
    if (!libretro_host.debugReadCycleCount) {
        return 0;
    }
    return libretro_host.debugReadCycleCount();
}

bool
libretro_host_debugSetCheckpointEnabled(int enabled)
{
    if (!libretro_host.debugSetCheckpointEnabled) {
        return false;
    }
    libretro_host.debugSetCheckpointEnabled(enabled);
    return true;
}

bool
libretro_host_debugGetCheckpointEnabled(int *out_enabled)
{
    if (out_enabled) {
        *out_enabled = 0;
    }
    if (!libretro_host.debugGetCheckpointEnabled) {
        return false;
    }
    if (out_enabled) {
        *out_enabled = libretro_host.debugGetCheckpointEnabled() ? 1 : 0;
    }
    return true;
}

bool
libretro_host_debugDisassembleQuick(uint32_t pc, char *out, size_t cap, size_t *out_len)
{
    if (out_len) {
        *out_len = 0;
    }
    if (!out || cap == 0 || !libretro_host.debugDisassembleQuick) {
        return false;
    }
    size_t len = libretro_host.debugDisassembleQuick(pc, out, cap);
    if (out_len) {
        *out_len = len;
    }
    return len > 0;
}

bool
libretro_host_getSerializeSize(size_t *out_size)
{
    if (out_size) {
        *out_size = 0;
    }
    if (!libretro_host.serializeSize) {
        return false;
    }
    size_t size = libretro_host.serializeSize();
    if (out_size) {
        *out_size = size;
    }
    return size > 0;
}

bool
libretro_host_serializeTo(void *out, size_t size)
{
    if (!out || size == 0 || !libretro_host.serialize) {
        return false;
    }
    return libretro_host.serialize(out, size) ? true : false;
}

bool
libretro_host_unserializeFrom(const void *data, size_t size)
{
    if (!data || size == 0 || !libretro_host.unserialize) {
        return false;
    }
    return libretro_host.unserialize(data, size) ? true : false;
}

bool
libretro_host_setStateData(const void *data, size_t size)
{
    if (!data || size == 0) {
        return false;
    }
    uint8_t *buf = (uint8_t*)alloc_realloc(libretro_host.stateData, size);
    if (!buf) {
        return false;
    }
    memcpy(buf, data, size);
    libretro_host.stateData = buf;
    libretro_host.stateSize = size;
    return true;
}

bool
libretro_host_resetCore(void)
{
    if (!libretro_host.reset) {
        return false;
    }
    libretro_host.reset();
    if (debugger.config.skipBiosLogo) {
        libretro_host.autoPressDelayFrames = 80;
        libretro_host.autoPressHoldFrames = 3;
    } else {
        libretro_host.autoPressDelayFrames = 0;
        libretro_host.autoPressHoldFrames = 0;
        libretro_host.autoInputMask[0] = 0;
    }
    return true;
}

uint64_t
libretro_host_getFrameCount(void)
{
    return libretro_host.frameSeq;
}

const char *
libretro_host_getRomPath(void)
{
    return libretro_host.romPath[0] ? libretro_host.romPath : NULL;
}

bool
libretro_host_setVblankCallback(void (*cb)(void *), void *user)
{
    if (!libretro_host.setVblankCallback) {
        return false;
    }
    libretro_host.setVblankCallback(cb, user);
    return true;
}

bool
libretro_host_setAudioEnabled(int enabled)
{
    libretro_host.audioEnabled = enabled ? 1 : 0;
    if (libretro_host.gameLoaded) {
        if (libretro_host.audioEnabled) {
            libretro_host_openAudio();
        } else {
            libretro_host_closeAudio();
        }
    }
    return true;
}

bool
libretro_host_saveState(size_t *out_size, size_t *out_diff)
{
    if (out_size) {
        *out_size = 0;
    }
    if (out_diff) {
        *out_diff = 0;
    }
    if (!libretro_host.serializeSize || !libretro_host.serialize) {
        return false;
    }
    size_t size = libretro_host.serializeSize();
    if (size == 0) {
        return false;
    }
    uint8_t *prev = NULL;
    if (libretro_host.stateData && libretro_host.stateSize == size) {
        prev = (uint8_t*)alloc_alloc(size);
        if (prev) {
            memcpy(prev, libretro_host.stateData, size);
        }
    }
    if (!libretro_host.stateData || libretro_host.stateSize != size) {
        uint8_t *buf = (uint8_t*)alloc_realloc(libretro_host.stateData, size);
        if (!buf) {
            alloc_free(prev);
            return false;
        }
        libretro_host.stateData = buf;
        libretro_host.stateSize = size;
    }
    if (!libretro_host.serialize(libretro_host.stateData, libretro_host.stateSize)) {
        alloc_free(prev);
        return false;
    }
    if (out_size) {
        *out_size = libretro_host.stateSize;
    }
    if (out_diff && prev) {
        size_t diff = 0;
        for (size_t i = 0; i < size; ++i) {
            if (libretro_host.stateData[i] != prev[i]) {
                diff++;
            }
        }
        *out_diff = diff;
    }
    alloc_free(prev);
    return true;
}

bool
libretro_host_restoreState(size_t *out_size)
{
    if (out_size) {
        *out_size = 0;
    }
    if (!libretro_host.unserialize || !libretro_host.stateData || libretro_host.stateSize == 0) {
        return false;
    }
    if (!libretro_host.unserialize(libretro_host.stateData, libretro_host.stateSize)) {
        return false;
    }
    if (out_size) {
        *out_size = libretro_host.stateSize;
    }
    return true;
}
