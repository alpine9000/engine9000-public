/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <string.h>

#include "e9ui_text_cache.h"
#include "alloc.h"

typedef struct e9ui_text_cache_entry {
    char *text;
    TTF_Font *font;
    Uint32 color_key;
    int use_utf8;
    SDL_Texture *tex;
    int w;
    int h;
    unsigned long long last_used;
} e9ui_text_cache_entry_t;

typedef struct e9ui_text_cache_bucket {
    SDL_Renderer *renderer;
    e9ui_text_cache_entry_t *entries;
    int count;
    int cap;
    int max_entries;
    unsigned long long tick;
} e9ui_text_cache_bucket_t;

static struct {
    e9ui_text_cache_bucket_t *buckets;
    int count;
    int cap;
    int max_entries;
} g_cache = {0};

static Uint32
e9ui_text_cache_colorKey(SDL_Color c)
{
    return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) | ((Uint32)c.b << 8) | (Uint32)c.a;
}

static void
e9ui_text_cache_entryFree(e9ui_text_cache_entry_t *e)
{
    if (!e) {
        return;
    }
    if (e->tex) {
        SDL_DestroyTexture(e->tex);
        e->tex = NULL;
    }
    if (e->text) {
        alloc_free(e->text);
        e->text = NULL;
    }
}

static void
e9ui_text_cache_bucketClear(e9ui_text_cache_bucket_t *bucket)
{
    if (!bucket) {
        return;
    }
    for (int i = 0; i < bucket->count; ++i) {
        e9ui_text_cache_entryFree(&bucket->entries[i]);
    }
    if (bucket->entries) {
        alloc_free(bucket->entries);
    }
    bucket->entries = NULL;
    bucket->count = 0;
    bucket->cap = 0;
    bucket->tick = 0;
}

static void
e9ui_text_cache_evictOne(e9ui_text_cache_bucket_t *bucket)
{
    if (!bucket || bucket->count <= 0) {
        return;
    }
    int idx = 0;
    unsigned long long best = bucket->entries[0].last_used;
    for (int i = 1; i < bucket->count; ++i) {
        if (bucket->entries[i].last_used < best) {
            best = bucket->entries[i].last_used;
            idx = i;
        }
    }
    e9ui_text_cache_entryFree(&bucket->entries[idx]);
    if (idx != bucket->count - 1) {
        bucket->entries[idx] = bucket->entries[bucket->count - 1];
    }
    bucket->count--;
}

static e9ui_text_cache_bucket_t *
e9ui_text_cache_bucketGet(SDL_Renderer *renderer, int create)
{
    if (!renderer) {
        return NULL;
    }
    for (int i = 0; i < g_cache.count; ++i) {
        if (g_cache.buckets[i].renderer == renderer) {
            return &g_cache.buckets[i];
        }
    }
    if (!create) {
        return NULL;
    }
    if (g_cache.max_entries <= 0) {
        g_cache.max_entries = E9UI_TEXT_CACHE_DEFAULT_MAX;
    }
    if (g_cache.count >= g_cache.cap) {
        int new_cap = g_cache.cap > 0 ? g_cache.cap * 2 : 4;
        e9ui_text_cache_bucket_t *next = (e9ui_text_cache_bucket_t*)alloc_realloc(
            g_cache.buckets, (size_t)new_cap * sizeof(*next));
        if (!next) {
            return NULL;
        }
        g_cache.buckets = next;
        g_cache.cap = new_cap;
    }
    e9ui_text_cache_bucket_t *bucket = &g_cache.buckets[g_cache.count++];
    memset(bucket, 0, sizeof(*bucket));
    bucket->renderer = renderer;
    bucket->max_entries = g_cache.max_entries;
    return bucket;
}

void
e9ui_text_cache_setMaxEntries(int max_entries)
{
    if (max_entries <= 0) {
        max_entries = E9UI_TEXT_CACHE_DEFAULT_MAX;
    }
    g_cache.max_entries = max_entries;
    for (int i = 0; i < g_cache.count; ++i) {
        e9ui_text_cache_bucket_t *bucket = &g_cache.buckets[i];
        bucket->max_entries = max_entries;
        while (bucket->count > bucket->max_entries) {
            e9ui_text_cache_evictOne(bucket);
        }
    }
}

void
e9ui_text_cache_clear(void)
{
    for (int i = 0; i < g_cache.count; ++i) {
        e9ui_text_cache_bucketClear(&g_cache.buckets[i]);
    }
    if (g_cache.buckets) {
        alloc_free(g_cache.buckets);
    }
    g_cache.buckets = NULL;
    g_cache.count = 0;
    g_cache.cap = 0;
    g_cache.max_entries = 0;
}

void
e9ui_text_cache_clearRenderer(SDL_Renderer *renderer)
{
    if (!renderer) {
        return;
    }
    for (int i = 0; i < g_cache.count; ++i) {
        if (g_cache.buckets[i].renderer == renderer) {
            e9ui_text_cache_bucketClear(&g_cache.buckets[i]);
            if (i != g_cache.count - 1) {
                g_cache.buckets[i] = g_cache.buckets[g_cache.count - 1];
            }
            g_cache.count--;
            return;
        }
    }
}

SDL_Texture *
e9ui_text_cache_get(SDL_Renderer *renderer,
                    TTF_Font *font,
                    const char *text,
                    SDL_Color color,
                    int use_utf8,
                    int *out_w,
                    int *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!renderer || !font || !text || !*text) {
        return NULL;
    }
    SDL_Texture *result = NULL;
    e9ui_text_cache_bucket_t *bucket = e9ui_text_cache_bucketGet(renderer, 1);
    if (!bucket) {
        return NULL;
    }
    if (bucket->max_entries <= 0) {
        bucket->max_entries = g_cache.max_entries > 0
            ? g_cache.max_entries
            : E9UI_TEXT_CACHE_DEFAULT_MAX;
    }
    Uint32 color_key = e9ui_text_cache_colorKey(color);
    for (int i = 0; i < bucket->count; ++i) {
        e9ui_text_cache_entry_t *e = &bucket->entries[i];
        if (e->font == font && e->color_key == color_key &&
            e->use_utf8 == use_utf8 && strcmp(e->text, text) == 0) {
            e->last_used = ++bucket->tick;
            if (out_w) *out_w = e->w;
            if (out_h) *out_h = e->h;
            result = e->tex;
            return result;
        }
    }

    SDL_Surface *surf = use_utf8
        ? TTF_RenderUTF8_Blended(font, text, color)
        : TTF_RenderText_Blended(font, text, color);
    if (!surf) {
        return NULL;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    int w = surf->w;
    int h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) {
        return NULL;
    }

    if (bucket->count >= bucket->max_entries) {
        e9ui_text_cache_evictOne(bucket);
    }
    if (bucket->count >= bucket->cap) {
        int new_cap = bucket->cap > 0 ? bucket->cap * 2 : 64;
        if (new_cap < bucket->max_entries) {
            new_cap = bucket->max_entries;
        }
        e9ui_text_cache_entry_t *next =
            (e9ui_text_cache_entry_t*)alloc_realloc(bucket->entries,
                                                    (size_t)new_cap * sizeof(*next));
        if (!next) {
            SDL_DestroyTexture(tex);
            return NULL;
        }
        bucket->entries = next;
        bucket->cap = new_cap;
    }

    e9ui_text_cache_entry_t *entry = &bucket->entries[bucket->count++];
    memset(entry, 0, sizeof(*entry));
    entry->text = alloc_strdup(text);
    if (!entry->text) {
        SDL_DestroyTexture(tex);
        bucket->count--;
        return NULL;
    }
    entry->font = font;
    entry->color_key = color_key;
    entry->use_utf8 = use_utf8 ? 1 : 0;
    entry->tex = tex;
    entry->w = w;
    entry->h = h;
    entry->last_used = ++bucket->tick;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    result = tex;
    return result;
}
