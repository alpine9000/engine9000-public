/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "core_config.h"

#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "alloc.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef void (RETRO_CALLCONV *core_config_retro_set_environment_fn_t)(retro_environment_t);
typedef void (RETRO_CALLCONV *core_config_retro_init_fn_t)(void);
typedef void (RETRO_CALLCONV *core_config_retro_deinit_fn_t)(void);

typedef struct core_config_probe {
    core_config_options_v2_t opts;
    char systemDir[PATH_MAX];
    char saveDir[PATH_MAX];
} core_config_probe_t;

static core_config_probe_t *core_config_activeProbe = NULL;

static void
core_config_freeV2Options(core_config_options_v2_t *opts);

static char *
core_config_dupString(const char *s)
{
    if (!s) {
        return NULL;
    }
    if (!*s) {
        return alloc_strdup("");
    }
    return alloc_strdup(s);
}

static int
core_config_copyFromV2(const struct retro_core_options_v2 *in, core_config_options_v2_t *out)
{
    if (!in || !out || !in->definitions) {
        return 0;
    }
    size_t defCount = 0;
    while (in->definitions[defCount].key) {
        defCount++;
    }
    if (defCount == 0) {
        return 1;
    }

    struct retro_core_option_v2_definition *defs =
        (struct retro_core_option_v2_definition *)alloc_calloc(defCount + 1, sizeof(*defs));
    if (!defs) {
        return 0;
    }
    for (size_t i = 0; i < defCount; ++i) {
        defs[i].key = core_config_dupString(in->definitions[i].key);
        defs[i].desc = core_config_dupString(in->definitions[i].desc);
        defs[i].info = core_config_dupString(in->definitions[i].info);
        defs[i].category_key = core_config_dupString(in->definitions[i].category_key);
        defs[i].default_value = core_config_dupString(in->definitions[i].default_value);
        for (int v = 0; v < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++v) {
            if (!in->definitions[i].values[v].value) {
                break;
            }
            defs[i].values[v].value = core_config_dupString(in->definitions[i].values[v].value);
            defs[i].values[v].label = core_config_dupString(in->definitions[i].values[v].label);
        }
    }

    size_t catCount = 0;
    if (in->categories) {
        while (in->categories[catCount].key) {
            catCount++;
        }
    }
    struct retro_core_option_v2_category *cats = NULL;
    if (catCount > 0) {
        cats = (struct retro_core_option_v2_category *)alloc_calloc(catCount + 1, sizeof(*cats));
        if (!cats) {
            core_config_freeV2Options(&(core_config_options_v2_t){ .defs = defs, .defCount = defCount });
            return 0;
        }
        for (size_t i = 0; i < catCount; ++i) {
            cats[i].key = core_config_dupString(in->categories[i].key);
            cats[i].desc = core_config_dupString(in->categories[i].desc);
            cats[i].info = core_config_dupString(in->categories[i].info);
        }
    }

    out->defs = defs;
    out->defCount = defCount;
    out->cats = cats;
    out->catCount = catCount;
    return 1;
}

static int
core_config_copyFromV1(const struct retro_core_option_definition *in, core_config_options_v2_t *out)
{
    if (!in || !out) {
        return 0;
    }
    size_t defCount = 0;
    while (in[defCount].key) {
        defCount++;
    }
    if (defCount == 0) {
        return 1;
    }

    struct retro_core_option_v2_definition *defs =
        (struct retro_core_option_v2_definition *)alloc_calloc(defCount + 1, sizeof(*defs));
    if (!defs) {
        return 0;
    }
    for (size_t i = 0; i < defCount; ++i) {
        defs[i].key = core_config_dupString(in[i].key);
        defs[i].desc = core_config_dupString(in[i].desc);
        defs[i].info = core_config_dupString(in[i].info);
        defs[i].category_key = NULL;
        defs[i].default_value = core_config_dupString(in[i].default_value);
        for (int v = 0; v < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++v) {
            if (!in[i].values[v].value) {
                break;
            }
            defs[i].values[v].value = core_config_dupString(in[i].values[v].value);
            defs[i].values[v].label = core_config_dupString(in[i].values[v].label);
        }
    }
    out->defs = defs;
    out->defCount = defCount;
    out->cats = NULL;
    out->catCount = 0;
    return 1;
}

static const char *
core_config_findDefaultValue(const core_config_options_v2_t *opts, const char *key)
{
    if (!opts || !opts->defs || !key) {
        return NULL;
    }
    for (size_t i = 0; i < opts->defCount; ++i) {
        const struct retro_core_option_v2_definition *d = &opts->defs[i];
        if (d->key && strcmp(d->key, key) == 0) {
            return d->default_value;
        }
    }
    return NULL;
}

static void RETRO_CALLCONV
core_config_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    (void)fmt;
}

static bool
core_config_environment(unsigned cmd, void *data)
{
    core_config_probe_t *p = core_config_activeProbe;
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!data) {
            return false;
        }
        {
            struct retro_log_callback *log = (struct retro_log_callback *)data;
            log->log = core_config_log;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (!data) {
            return false;
        }
        *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        if (!data) {
            return false;
        }
        *(unsigned *)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        if (!data || !p) {
            return false;
        }
        core_config_freeV2Options(&p->opts);
        return core_config_copyFromV2((const struct retro_core_options_v2 *)data, &p->opts) ? true : false;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        if (!data || !p) {
            return false;
        }
        {
            const struct retro_core_options_v2_intl *intl = (const struct retro_core_options_v2_intl *)data;
            const struct retro_core_options_v2 *opts = (intl && intl->local) ? intl->local : (intl ? intl->us : NULL);
            if (!opts) {
                return false;
            }
            core_config_freeV2Options(&p->opts);
            return core_config_copyFromV2(opts, &p->opts) ? true : false;
        }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        if (!data || !p) {
            return false;
        }
        core_config_freeV2Options(&p->opts);
        return core_config_copyFromV1((const struct retro_core_option_definition *)data, &p->opts) ? true : false;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        if (!data || !p) {
            return false;
        }
        {
            const struct retro_core_options_intl *intl = (const struct retro_core_options_intl *)data;
            const struct retro_core_option_definition *defs = (intl && intl->local) ? intl->local : (intl ? intl->us : NULL);
            if (!defs) {
                return false;
            }
            core_config_freeV2Options(&p->opts);
            return core_config_copyFromV1(defs, &p->opts) ? true : false;
        }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (!data || !p || !p->systemDir[0]) {
            return false;
        }
        *(const char **)data = p->systemDir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!data || !p || !p->saveDir[0]) {
            return false;
        }
        *(const char **)data = p->saveDir;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        if (!data || !p) {
            return false;
        }
        {
            struct retro_variable *var = (struct retro_variable *)data;
            if (!var->key) {
                return false;
            }
            const char *defValue = core_config_findDefaultValue(&p->opts, var->key);
            if (!defValue) {
                return false;
            }
            var->value = defValue;
            return true;
        }
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_ROTATION:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    default:
        return false;
    }
}

static void *
core_config_loadLibSymbol(void *lib, const char *name)
{
    if (!lib || !name) {
        return NULL;
    }
#ifdef _WIN32
    FARPROC symbol = GetProcAddress((HMODULE)lib, name);
    return (void *)symbol;
#else
    dlerror();
    void *symbol = dlsym(lib, name);
    return symbol;
#endif
}

static void
core_config_freeV2Options(core_config_options_v2_t *opts)
{
    if (!opts) {
        return;
    }
    if (opts->defs) {
        for (size_t i = 0; i < opts->defCount; ++i) {
            struct retro_core_option_v2_definition *d = &opts->defs[i];
            alloc_free((void *)d->key);
            alloc_free((void *)d->desc);
            alloc_free((void *)d->info);
            alloc_free((void *)d->category_key);
            alloc_free((void *)d->default_value);
            for (int v = 0; v < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++v) {
                if (!d->values[v].value) {
                    break;
                }
                alloc_free((void *)d->values[v].value);
                alloc_free((void *)d->values[v].label);
            }
        }
        alloc_free(opts->defs);
    }
    if (opts->cats) {
        for (size_t i = 0; i < opts->catCount; ++i) {
            struct retro_core_option_v2_category *c = &opts->cats[i];
            alloc_free((void *)c->key);
            alloc_free((void *)c->desc);
            alloc_free((void *)c->info);
        }
        alloc_free(opts->cats);
    }
    memset(opts, 0, sizeof(*opts));
}

void
core_config_freeCoreOptionsV2(core_config_options_v2_t *opts)
{
    core_config_freeV2Options(opts);
}

bool
core_config_probeCoreOptionsV2(const char *corePath,
                               const char *systemDir,
                               const char *saveDir,
                               core_config_options_v2_t *out)
{
    if (!corePath || !*corePath || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    void *lib = NULL;
#ifdef _WIN32
    lib = (void *)LoadLibraryA(corePath);
#else
    lib = dlopen(corePath, RTLD_NOW | RTLD_LOCAL);
#endif
    if (!lib) {
        return false;
    }

    core_config_retro_set_environment_fn_t setEnvironment =
        (core_config_retro_set_environment_fn_t)core_config_loadLibSymbol(lib, "retro_set_environment");
    core_config_retro_init_fn_t init =
        (core_config_retro_init_fn_t)core_config_loadLibSymbol(lib, "retro_init");
    (void)core_config_loadLibSymbol(lib, "retro_deinit");

    if (!setEnvironment) {
#ifdef _WIN32
        FreeLibrary((HMODULE)lib);
#else
        dlclose(lib);
#endif
        return false;
    }

    core_config_probe_t probe;
    memset(&probe, 0, sizeof(probe));
    if (systemDir && *systemDir) {
        strncpy(probe.systemDir, systemDir, sizeof(probe.systemDir) - 1);
        probe.systemDir[sizeof(probe.systemDir) - 1] = '\0';
    }
    if (saveDir && *saveDir) {
        strncpy(probe.saveDir, saveDir, sizeof(probe.saveDir) - 1);
        probe.saveDir[sizeof(probe.saveDir) - 1] = '\0';
    }

    core_config_activeProbe = &probe;
    setEnvironment(core_config_environment);
    if (!probe.opts.defs && init) {
        init();
    }
    core_config_activeProbe = NULL;

#ifdef _WIN32
    FreeLibrary((HMODULE)lib);
#else
    dlclose(lib);
#endif

    if (!probe.opts.defs || probe.opts.defCount == 0) {
        core_config_freeV2Options(&probe.opts);
        return false;
    }

    *out = probe.opts;
    return true;
}
