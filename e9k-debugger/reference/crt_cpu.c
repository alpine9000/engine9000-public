#include "crt.h"

#include <math.h>

static SDL_Texture *crt_texture = NULL;
static int crt_texW = 0;
static int crt_texH = 0;
static uint8_t *crt_buffer = NULL;
static size_t crt_bufferSize = 0;
static int crt_enabled = 1;
static int crt_geometryEnabled = 1;
static int crt_bloomEnabled = 1;
static int crt_maskEnabled = 0;
static int crt_gammaEnabled = 0;
static int crt_chromaEnabled = 0;
static float crt_renderScale = 0.25f;
static float crt_scanlineBorder = 0.42f;

int
crt_isEnabled(void)
{
    return crt_enabled ? 1 : 0;
}

void
crt_setEnabled(int enabled)
{
    crt_enabled = enabled ? 1 : 0;
}

int
crt_isGeometryEnabled(void)
{
    return crt_geometryEnabled ? 1 : 0;
}

int
crt_isBloomEnabled(void)
{
    return crt_bloomEnabled ? 1 : 0;
}

int
crt_isMaskEnabled(void)
{
    return crt_maskEnabled ? 1 : 0;
}

int
crt_isGammaEnabled(void)
{
    return crt_gammaEnabled ? 1 : 0;
}

int
crt_isChromaEnabled(void)
{
    return crt_chromaEnabled ? 1 : 0;
}

int
crt_toggleGeometry(void)
{
    crt_geometryEnabled = !crt_geometryEnabled;
    return crt_geometryEnabled ? 1 : 0;
}

int
crt_toggleBloom(void)
{
    crt_bloomEnabled = !crt_bloomEnabled;
    return crt_bloomEnabled ? 1 : 0;
}

int
crt_toggleMask(void)
{
    crt_maskEnabled = !crt_maskEnabled;
    return crt_maskEnabled ? 1 : 0;
}

int
crt_toggleGamma(void)
{
    crt_gammaEnabled = !crt_gammaEnabled;
    return crt_gammaEnabled ? 1 : 0;
}

int
crt_toggleChroma(void)
{
    crt_chromaEnabled = !crt_chromaEnabled;
    return crt_chromaEnabled ? 1 : 0;
}

float
crt_getRenderScale(void)
{
    return crt_renderScale;
}

void
crt_setRenderScale(float scale)
{
    if (scale < 0.15f) {
        scale = 0.15f;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    crt_renderScale = scale;
}

float
crt_getScanlineBorder(void)
{
    return crt_scanlineBorder;
}

void
crt_setScanlineBorder(float border)
{
    if (border < 0.0f) {
        border = 0.0f;
    }
    if (border > 0.45f) {
        border = 0.45f;
    }
    crt_scanlineBorder = border;
}

static uint32_t
crt_sampleBilinear(const uint8_t *data, int width, int height, size_t pitch, float sx, float sy)
{
    if (sx < 0.0f || sy < 0.0f || sx > (float)(width - 1) || sy > (float)(height - 1)) {
        return 0xFF000000u;
    }
    int x0 = (int)floorf(sx);
    int y0 = (int)floorf(sy);
    int x1 = (x0 + 1 < width) ? x0 + 1 : x0;
    int y1 = (y0 + 1 < height) ? y0 + 1 : y0;
    float tx = sx - (float)x0;
    float ty = sy - (float)y0;

    const uint32_t *row0 = (const uint32_t *)(data + (size_t)y0 * pitch);
    const uint32_t *row1 = (const uint32_t *)(data + (size_t)y1 * pitch);
    uint32_t p00 = row0[x0];
    uint32_t p10 = row0[x1];
    uint32_t p01 = row1[x0];
    uint32_t p11 = row1[x1];

    float r00 = (float)((p00 >> 16) & 0xFF);
    float g00 = (float)((p00 >> 8) & 0xFF);
    float b00 = (float)(p00 & 0xFF);
    float r10 = (float)((p10 >> 16) & 0xFF);
    float g10 = (float)((p10 >> 8) & 0xFF);
    float b10 = (float)(p10 & 0xFF);
    float r01 = (float)((p01 >> 16) & 0xFF);
    float g01 = (float)((p01 >> 8) & 0xFF);
    float b01 = (float)(p01 & 0xFF);
    float r11 = (float)((p11 >> 16) & 0xFF);
    float g11 = (float)((p11 >> 8) & 0xFF);
    float b11 = (float)(p11 & 0xFF);

    float r0 = r00 + (r10 - r00) * tx;
    float g0 = g00 + (g10 - g00) * tx;
    float b0 = b00 + (b10 - b00) * tx;
    float r1 = r01 + (r11 - r01) * tx;
    float g1 = g01 + (g11 - g01) * tx;
    float b1 = b01 + (b11 - b01) * tx;

    int r = (int)(r0 + (r1 - r0) * ty);
    int g = (int)(g0 + (g1 - g0) * ty);
    int b = (int)(b0 + (b1 - b0) * ty);

    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void
crt_renderFrame(SDL_Renderer *renderer, const uint8_t *data, int width, int height,
                size_t pitch, const SDL_Rect *dst)
{
    if (!renderer || !data || width <= 0 || height <= 0 || !dst) {
        return;
    }

    float renderScale = crt_renderScale;
    int outW = (int)((float)dst->w * renderScale + 0.5f);
    int outH = (int)((float)dst->h * renderScale + 0.5f);
    if (outW <= 0 || outH <= 0) {
        return;
    }

    size_t needed = (size_t)outW * (size_t)outH * 4;
    if (needed > crt_bufferSize) {
        uint8_t *next = (uint8_t *)realloc(crt_buffer, needed);
        if (!next) {
            return;
        }
        crt_buffer = next;
        crt_bufferSize = needed;
    }
    if (!crt_texture || crt_texW != outW || crt_texH != outH) {
        if (crt_texture) {
            SDL_DestroyTexture(crt_texture);
            crt_texture = NULL;
        }
        crt_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, outW, outH);
        if (!crt_texture) {
            return;
        }
        crt_texW = outW;
        crt_texH = outH;
    }

    float invW = (outW > 1) ? (1.0f / (float)(outW - 1)) : 1.0f;
    float invH = (outH > 1) ? (1.0f / (float)(outH - 1)) : 1.0f;
    const float k = 0.04f;
    const float beamSigma = 0.35f;
    const float bleed = 0.08f;
    const float maskStrength = 0.03f;
    const float gamma = 1.12f;
    const float chromaShift = 0.35f;
    const float scanlineStrength = 0.22f;
    const float overscan = 0.88f;
    for (int y = 0; y < outH; ++y) {
        uint8_t *dstRow = crt_buffer + (size_t)y * (size_t)outW * 4;
        float ny = ((float)y * invH) * 2.0f - 1.0f;
        for (int x = 0; x < outW; ++x) {
            float nx = ((float)x * invW) * 2.0f - 1.0f;
            float r2 = nx * nx + ny * ny;
            float sx = (float)x;
            float sy = (float)y;
            float intensity = 1.0f;
            if (crt_geometryEnabled) {
                float scale = 1.0f - k * r2;
                if (scale < 0.5f) {
                    scale = 0.5f;
                }
                float sxn = (nx / scale) / overscan;
                float syn = (ny / scale) / overscan;
                sx = (sxn + 1.0f) * 0.5f * (float)(width - 1);
                sy = (syn + 1.0f) * 0.5f * (float)(height - 1);
                float frac = sy - floorf(sy);
                float beam = 0.55f + 0.45f * expf(-((frac - 0.5f) * (frac - 0.5f)) / (beamSigma * beamSigma));
                float vignette = 1.0f - 0.25f * r2;
                if (vignette < 0.6f) {
                    vignette = 0.6f;
                }
                float scan = (y & 1) ? (1.0f - scanlineStrength) : 1.0f;
                intensity = beam * vignette * scan;
            }

            float useBleed = 0.0f;
            if (crt_bloomEnabled) {
                float fracX = sx - floorf(sx);
                float fracY = sy - floorf(sy);
                useBleed = bleed * (1.0f - 0.5f * (fracX + fracY));
            }
            if (useBleed < 0.0f) {
                useBleed = 0.0f;
            }

            uint32_t sample = crt_sampleBilinear(data, width, height, pitch, sx, sy);
            uint32_t sampleRShift = sample;
            uint32_t sampleBShift = sample;
            if (crt_chromaEnabled) {
                sampleRShift = crt_sampleBilinear(data, width, height, pitch, sx - chromaShift, sy);
                sampleBShift = crt_sampleBilinear(data, width, height, pitch, sx + chromaShift, sy);
            }
            uint32_t sampleL = sample;
            uint32_t sampleR = sample;
            uint32_t sampleU = sample;
            uint32_t sampleD = sample;
            if (useBleed > 0.0f) {
                sampleL = crt_sampleBilinear(data, width, height, pitch, sx - 1.0f, sy);
                sampleR = crt_sampleBilinear(data, width, height, pitch, sx + 1.0f, sy);
                sampleU = crt_sampleBilinear(data, width, height, pitch, sx, sy - 1.0f);
                sampleD = crt_sampleBilinear(data, width, height, pitch, sx, sy + 1.0f);
            }

            int r = (sampleRShift >> 16) & 0xFF;
            int g = (sample >> 8) & 0xFF;
            int b = sampleBShift & 0xFF;
            int lr = (sampleL >> 16) & 0xFF;
            int lg = (sampleL >> 8) & 0xFF;
            int lb = sampleL & 0xFF;
            int rr = (sampleR >> 16) & 0xFF;
            int rg = (sampleR >> 8) & 0xFF;
            int rb = sampleR & 0xFF;
            int ur = (sampleU >> 16) & 0xFF;
            int ug = (sampleU >> 8) & 0xFF;
            int ub = sampleU & 0xFF;
            int dr = (sampleD >> 16) & 0xFF;
            int dg = (sampleD >> 8) & 0xFF;
            int db = sampleD & 0xFF;

            float rBloom = (float)r + useBleed * (float)(lr + rr + ur + dr);
            float gBloom = (float)g + useBleed * (float)(lg + rg + ug + dg);
            float bBloom = (float)b + useBleed * (float)(lb + rb + ub + db);
            r = (int)(rBloom / (1.0f + useBleed * 4.0f));
            g = (int)(gBloom / (1.0f + useBleed * 4.0f));
            b = (int)(bBloom / (1.0f + useBleed * 4.0f));

            if (crt_maskEnabled) {
                int phase = ((y & 1) != 0) ? 1 : 0;
                int mask = (x + phase) % 3;
                float damp = 1.0f - maskStrength;
                if (mask == 0) {
                    g = (int)((float)g * damp);
                    b = (int)((float)b * damp);
                } else if (mask == 1) {
                    r = (int)((float)r * damp);
                    b = (int)((float)b * damp);
                } else {
                    r = (int)((float)r * damp);
                    g = (int)((float)g * damp);
                }
            }
            r = (int)((float)r * intensity);
            g = (int)((float)g * intensity);
            b = (int)((float)b * intensity);

            if (crt_gammaEnabled) {
                float rf = powf((float)r / 255.0f, gamma) * 255.0f;
                float gf = powf((float)g / 255.0f, gamma) * 255.0f;
                float bf = powf((float)b / 255.0f, gamma) * 255.0f;
                r = (int)rf;
                g = (int)gf;
                b = (int)bf;
            }
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            ((uint32_t *)dstRow)[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    SDL_UpdateTexture(crt_texture, NULL, crt_buffer, outW * 4);
    SDL_RenderCopy(renderer, crt_texture, NULL, dst);
}
