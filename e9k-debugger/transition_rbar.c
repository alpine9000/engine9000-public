/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <SDL_version.h>
#include <math.h>
#include "transition.h"
#include "debugger.h"

typedef struct transition_rbar_point {
    float x;
    float y;
} transition_rbar_point_t;

static int
transition_rbar_clipLeft(const transition_rbar_point_t *in, int count, transition_rbar_point_t *out, float edge)
{
    int outCount = 0;
    if (count < 1) {
        return 0;
    }
    transition_rbar_point_t prev = in[count - 1];
    int prevInside = (prev.x >= edge);
    for (int i = 0; i < count; ++i) {
        transition_rbar_point_t cur = in[i];
        int curInside = (cur.x >= edge);
        if (prevInside && curInside) {
            out[outCount++] = cur;
        } else if (prevInside && !curInside) {
            float dx = cur.x - prev.x;
            if (dx != 0.0f) {
                float t = (edge - prev.x) / dx;
                transition_rbar_point_t ip = { edge, prev.y + (cur.y - prev.y) * t };
                out[outCount++] = ip;
            }
        } else if (!prevInside && curInside) {
            float dx = cur.x - prev.x;
            if (dx != 0.0f) {
                float t = (edge - prev.x) / dx;
                transition_rbar_point_t ip = { edge, prev.y + (cur.y - prev.y) * t };
                out[outCount++] = ip;
            }
            out[outCount++] = cur;
        }
        prev = cur;
        prevInside = curInside;
    }
    return outCount;
}

static int
transition_rbar_clipRight(const transition_rbar_point_t *in, int count, transition_rbar_point_t *out, float edge)
{
    int outCount = 0;
    if (count < 1) {
        return 0;
    }
    transition_rbar_point_t prev = in[count - 1];
    int prevInside = (prev.x <= edge);
    for (int i = 0; i < count; ++i) {
        transition_rbar_point_t cur = in[i];
        int curInside = (cur.x <= edge);
        if (prevInside && curInside) {
            out[outCount++] = cur;
        } else if (prevInside && !curInside) {
            float dx = cur.x - prev.x;
            if (dx != 0.0f) {
                float t = (edge - prev.x) / dx;
                transition_rbar_point_t ip = { edge, prev.y + (cur.y - prev.y) * t };
                out[outCount++] = ip;
            }
        } else if (!prevInside && curInside) {
            float dx = cur.x - prev.x;
            if (dx != 0.0f) {
                float t = (edge - prev.x) / dx;
                transition_rbar_point_t ip = { edge, prev.y + (cur.y - prev.y) * t };
                out[outCount++] = ip;
            }
            out[outCount++] = cur;
        }
        prev = cur;
        prevInside = curInside;
    }
    return outCount;
}

static int
transition_rbar_clipTop(const transition_rbar_point_t *in, int count, transition_rbar_point_t *out, float edge)
{
    int outCount = 0;
    if (count < 1) {
        return 0;
    }
    transition_rbar_point_t prev = in[count - 1];
    int prevInside = (prev.y >= edge);
    for (int i = 0; i < count; ++i) {
        transition_rbar_point_t cur = in[i];
        int curInside = (cur.y >= edge);
        if (prevInside && curInside) {
            out[outCount++] = cur;
        } else if (prevInside && !curInside) {
            float dy = cur.y - prev.y;
            if (dy != 0.0f) {
                float t = (edge - prev.y) / dy;
                transition_rbar_point_t ip = { prev.x + (cur.x - prev.x) * t, edge };
                out[outCount++] = ip;
            }
        } else if (!prevInside && curInside) {
            float dy = cur.y - prev.y;
            if (dy != 0.0f) {
                float t = (edge - prev.y) / dy;
                transition_rbar_point_t ip = { prev.x + (cur.x - prev.x) * t, edge };
                out[outCount++] = ip;
            }
            out[outCount++] = cur;
        }
        prev = cur;
        prevInside = curInside;
    }
    return outCount;
}

static int
transition_rbar_clipBottom(const transition_rbar_point_t *in, int count, transition_rbar_point_t *out, float edge)
{
    int outCount = 0;
    if (count < 1) {
        return 0;
    }
    transition_rbar_point_t prev = in[count - 1];
    int prevInside = (prev.y <= edge);
    for (int i = 0; i < count; ++i) {
        transition_rbar_point_t cur = in[i];
        int curInside = (cur.y <= edge);
        if (prevInside && curInside) {
            out[outCount++] = cur;
        } else if (prevInside && !curInside) {
            float dy = cur.y - prev.y;
            if (dy != 0.0f) {
                float t = (edge - prev.y) / dy;
                transition_rbar_point_t ip = { prev.x + (cur.x - prev.x) * t, edge };
                out[outCount++] = ip;
            }
        } else if (!prevInside && curInside) {
            float dy = cur.y - prev.y;
            if (dy != 0.0f) {
                float t = (edge - prev.y) / dy;
                transition_rbar_point_t ip = { prev.x + (cur.x - prev.x) * t, edge };
                out[outCount++] = ip;
            }
            out[outCount++] = cur;
        }
        prev = cur;
        prevInside = curInside;
    }
    return outCount;
}

static int
transition_rbar_clipToScreen(const transition_rbar_point_t *in, int count,
                             transition_rbar_point_t *out, int w, int h)
{
    transition_rbar_point_t tmpA[12];
    transition_rbar_point_t tmpB[12];
    int outCount = transition_rbar_clipLeft(in, count, tmpA, 0.0f);
    outCount = transition_rbar_clipRight(tmpA, outCount, tmpB, (float)(w - 1));
    outCount = transition_rbar_clipTop(tmpB, outCount, tmpA, 0.0f);
    outCount = transition_rbar_clipBottom(tmpA, outCount, out, (float)(h - 1));
    return outCount;
}

static void
transition_rbar_renderToTexture(e9ui_component_t *comp, SDL_Texture *target,
                                e9ui_component_t *fullscreenComp, int w, int h)
{
    if (!target) {
        return;
    }
    SDL_Texture *prev = SDL_GetRenderTarget(e9ui->ctx.renderer);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(e9ui->ctx.renderer, target);
    SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, 255);
    SDL_RenderClear(e9ui->ctx.renderer);
    if (!comp) {
        SDL_SetRenderTarget(e9ui->ctx.renderer, prev);
        return;
    }
    e9ui_component_t *prevRoot = e9ui->root;
    e9ui_component_t *prevFullscreen = e9ui->fullscreen;
    e9ui->fullscreen = fullscreenComp;
    e9ui->root = comp;
    if (comp->layout) {
        e9ui_rect_t full = (e9ui_rect_t){0, 0, w, h};
        comp->layout(comp, &e9ui->ctx, full);
    }
    e9ui_renderFrameNoLayoutNoPresent();
    e9ui->root = prevRoot;
    e9ui->fullscreen = prevFullscreen;
    SDL_SetRenderTarget(e9ui->ctx.renderer, prev);
}

void
transition_rbar_run(e9ui_component_t *from, e9ui_component_t *to, int w, int h)
{
    transition_rbar_runTo(from, to, w, h);
}

void
transition_rbar_runTo(e9ui_component_t *from, e9ui_component_t *to, int w, int h)
{
    if (!e9ui->ctx.renderer || (!from && !to)) {
        return;
    }

    SDL_Texture *prevTarget = SDL_GetRenderTarget(e9ui->ctx.renderer);
    SDL_Texture *fromTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_Texture *toTex = SDL_CreateTexture(e9ui->ctx.renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, w, h);
    if (!fromTex || !toTex) {
        if (fromTex) {
            SDL_DestroyTexture(fromTex);
        }
        if (toTex) {
            SDL_DestroyTexture(toTex);
        }
        e9ui->transition.inTransition = 0;
        return;
    }

    e9ui_component_t *fromFullscreen = (from == e9ui->fullscreen) ? from : NULL;
    e9ui_component_t *toFullscreen = (to && to != e9ui->root) ? to : NULL;
    transition_rbar_renderToTexture(from, fromTex, fromFullscreen, w, h);
    transition_rbar_renderToTexture(to, toTex, toFullscreen, w, h);

    SDL_SetTextureBlendMode(fromTex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(toTex, SDL_BLENDMODE_BLEND);
    const int frames = 40;
    const double frameMs = 1000.0 / 60.0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();
    float diag = sqrtf((float)w * (float)w + (float)h * (float)h);
    float minWidth = (float)w * 0.08f;
    if (minWidth < 12.0f) {
        minWidth = 12.0f;
    }
    float maxWidth = diag * 1.2f;
    float barLength = diag * 1.2f;
    float cx = (float)w * 0.5f;
    float cy = (float)h * 0.5f;
    SDL_Rect dst = { 0, 0, w, h };
    const float pi = 3.14159265f;
    const float startAngle = 45.0f;
    const float angleSpan = 180.0f;

    for (int f = 0; f < frames; ++f) {
        SDL_PumpEvents();
        float t = (frames > 1) ? (float)f / (float)(frames - 1) : 1.0f;
        float angle = startAngle + angleSpan * t;
        float width = minWidth + (maxWidth - minWidth) * t;
        float halfW = width * 0.5f;
        float halfH = barLength * 0.5f;
        float rad = angle * (pi / 180.0f);
        float cosA = cosf(rad);
        float sinA = sinf(rad);

        SDL_SetRenderTarget(e9ui->ctx.renderer, prevTarget);
        SDL_SetRenderDrawColor(e9ui->ctx.renderer, 0, 0, 0, 255);
        SDL_RenderClear(e9ui->ctx.renderer);
        SDL_RenderCopy(e9ui->ctx.renderer, fromTex, NULL, &dst);

#if SDL_VERSION_ATLEAST(2, 0, 18)
        transition_rbar_point_t quad[4];
        float localX[4] = { -halfW, halfW, halfW, -halfW };
        float localY[4] = { -halfH, -halfH, halfH, halfH };
        for (int i = 0; i < 4; ++i) {
            float rx = localX[i] * cosA - localY[i] * sinA;
            float ry = localX[i] * sinA + localY[i] * cosA;
            quad[i].x = cx + rx;
            quad[i].y = cy + ry;
        }
        transition_rbar_point_t clipped[12];
        int clippedCount = transition_rbar_clipToScreen(quad, 4, clipped, w, h);
        if (clippedCount >= 3) {
            SDL_Vertex verts[12];
            int tris[30];
            int triCount = 0;
            for (int i = 0; i < clippedCount; ++i) {
                float x = clipped[i].x;
                float y = clipped[i].y;
                float u = (w > 0) ? (x / (float)w) : 0.0f;
                float v = (h > 0) ? (y / (float)h) : 0.0f;
                verts[i].position.x = x;
                verts[i].position.y = y;
                verts[i].color.r = 255;
                verts[i].color.g = 255;
                verts[i].color.b = 255;
                verts[i].color.a = 255;
                verts[i].tex_coord.x = u;
                verts[i].tex_coord.y = v;
            }
            for (int i = 1; i < clippedCount - 1; ++i) {
                tris[triCount++] = 0;
                tris[triCount++] = i;
                tris[triCount++] = i + 1;
            }
            SDL_RenderGeometry(e9ui->ctx.renderer, toTex, verts, clippedCount, tris, triCount);
        }
#else
        Uint8 alpha = (Uint8)(255.0f * t);
        SDL_SetTextureAlphaMod(toTex, alpha);
        SDL_RenderCopy(e9ui->ctx.renderer, toTex, NULL, &dst);
#endif

        SDL_RenderPresent(e9ui->ctx.renderer);
        uint64_t now = SDL_GetPerformanceCounter();
        double elapsedMs = (double)(now - last) * 1000.0 / (double)freq;
        if (elapsedMs < frameMs) {
            SDL_Delay((Uint32)(frameMs - elapsedMs));
        }
        last = SDL_GetPerformanceCounter();
    }

    SDL_DestroyTexture(fromTex);
    SDL_DestroyTexture(toTex);
    e9ui->transition.inTransition = -100;
}
