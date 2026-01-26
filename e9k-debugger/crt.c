/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <limits.h>
#include "crt.h"

static int crt_enabled = 1;
static int crt_geometryEnabled = 1;
static int crt_bloomEnabled = 1;
static int crt_halationEnabled = 0;
static int crt_maskEnabled = 0;
static int crt_gammaEnabled = 0;
static int crt_chromaEnabled = 0;
static float crt_renderScale = 0.25f;
static float crt_scanlineBorder = 0.42f;
static float crt_scanStrength = 0.65f;
static float crt_halationStrength = 0.15f;
static float crt_halationThreshold = 0.75f;
static float crt_halationRadius = 10.0f;
static float crt_maskStrength = 0.12f;
static float crt_maskScale = 2.0f;
static int crt_maskType = 1;
static int crt_grilleEnabled = 1;
static float crt_grilleStrength = 0.35f;
static float crt_beamStrength = 0.90f;
static float crt_beamWidth = 1.0f;
static float crt_curvatureK = 0.04f;
static float crt_overscan = 1.02f;
static int crt_persistedConfig = 0;

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
crt_isHalationEnabled(void)
{
    return crt_halationEnabled ? 1 : 0;
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
crt_toggleHalation(void)
{
    crt_halationEnabled = !crt_halationEnabled;
    return crt_halationEnabled ? 1 : 0;
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

void
crt_setGeometryEnabled(int enabled)
{
    crt_geometryEnabled = enabled ? 1 : 0;
}

void
crt_setBloomEnabled(int enabled)
{
    crt_bloomEnabled = enabled ? 1 : 0;
}

void
crt_setHalationEnabled(int enabled)
{
    crt_halationEnabled = enabled ? 1 : 0;
}

void
crt_setMaskEnabled(int enabled)
{
    crt_maskEnabled = enabled ? 1 : 0;
}

void
crt_setGammaEnabled(int enabled)
{
    crt_gammaEnabled = enabled ? 1 : 0;
}

void
crt_setChromaEnabled(int enabled)
{
    crt_chromaEnabled = enabled ? 1 : 0;
}

static float
crt_clampF(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

float
crt_getScanStrength(void)
{
    return crt_scanStrength;
}

void
crt_setScanStrength(float strength)
{
    crt_scanStrength = crt_clampF(strength, 0.0f, 1.0f);
}

float
crt_getHalationStrength(void)
{
    return crt_halationStrength;
}

void
crt_setHalationStrength(float strength)
{
    crt_halationStrength = crt_clampF(strength, 0.0f, 1.0f);
}

float
crt_getHalationThreshold(void)
{
    return crt_halationThreshold;
}

void
crt_setHalationThreshold(float threshold)
{
    crt_halationThreshold = crt_clampF(threshold, 0.0f, 1.0f);
}

float
crt_getHalationRadius(void)
{
    return crt_halationRadius;
}

void
crt_setHalationRadius(float radius)
{
    crt_halationRadius = crt_clampF(radius, 0.0f, 64.0f);
}

float
crt_getMaskStrength(void)
{
    return crt_maskStrength;
}

void
crt_setMaskStrength(float strength)
{
    crt_maskStrength = crt_clampF(strength, 0.0f, 1.0f);
}

float
crt_getMaskScale(void)
{
    return crt_maskScale;
}

void
crt_setMaskScale(float scale)
{
    crt_maskScale = crt_clampF(scale, 0.25f, 32.0f);
}

int
crt_getMaskType(void)
{
    return crt_maskType;
}

void
crt_setMaskType(int type)
{
    if (type < 0) {
        type = 0;
    }
    if (type > 2) {
        type = 2;
    }
    crt_maskType = type;
}

int
crt_isGrilleEnabled(void)
{
    return crt_grilleEnabled ? 1 : 0;
}

void
crt_setGrilleEnabled(int enabled)
{
    crt_grilleEnabled = enabled ? 1 : 0;
}

float
crt_getGrilleStrength(void)
{
    return crt_grilleStrength;
}

void
crt_setGrilleStrength(float strength)
{
    crt_grilleStrength = crt_clampF(strength, 0.0f, 1.0f);
}

float
crt_getBeamStrength(void)
{
    return crt_beamStrength;
}

void
crt_setBeamStrength(float strength)
{
    crt_beamStrength = crt_clampF(strength, 0.0f, 1.0f);
}

float
crt_getBeamWidth(void)
{
    return crt_beamWidth;
}

void
crt_setBeamWidth(float width)
{
    crt_beamWidth = crt_clampF(width, 0.25f, 4.0f);
}

float
crt_getCurvatureK(void)
{
    return crt_curvatureK;
}

void
crt_setCurvatureK(float k)
{
    crt_curvatureK = crt_clampF(k, 0.0f, 0.20f);
}

float
crt_getOverscan(void)
{
    return crt_overscan;
}

void
crt_setOverscan(float overscan)
{
    crt_overscan = crt_clampF(overscan, 0.50f, 1.50f);
}

void
crt_setAdvancedDefaults(void)
{
    crt_setEnabled(1);
    crt_setGeometryEnabled(1);
    crt_setBloomEnabled(1);
    crt_setHalationEnabled(1);
    crt_setMaskEnabled(1);
    crt_setGammaEnabled(1);
    crt_setChromaEnabled(1);

    crt_setScanStrength(0.65f);
    crt_setHalationStrength(0.15f);
    crt_setHalationThreshold(0.75f);
    crt_setHalationRadius(10.0f);
    crt_setMaskStrength(0.12f);
    crt_setMaskScale(2.0f);
    crt_setMaskType(1);
    crt_setGrilleEnabled(1);
    crt_setGrilleStrength(0.35f);
    crt_setBeamStrength(0.90f);
    crt_setBeamWidth(1.0f);
    crt_setCurvatureK(0.04f);
    crt_setOverscan(1.02f);
}

float
crt_getRenderScale(void)
{
    return crt_renderScale;
}

void
crt_setRenderScale(float scale)
{
    if (scale < 0.0f) {
        scale = 0.0f;
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
    if (border > 1.0f) {
        border = 1.0f;
    }
    crt_scanlineBorder = border;
}

static int
crt_parseInt(const char *value, int *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || end == value) {
        return 0;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int
crt_parseFloat(const char *value, float *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    float parsed = strtof(value, &end);
    if (!end || end == value) {
        return 0;
    }
    *out = parsed;
    return 1;
}

void
crt_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    fprintf(file, "comp.crt.geometry_enabled=%d\n", crt_isGeometryEnabled());
    fprintf(file, "comp.crt.bloom_enabled=%d\n", crt_isBloomEnabled());
    fprintf(file, "comp.crt.halation_enabled=%d\n", crt_isHalationEnabled());
    fprintf(file, "comp.crt.mask_enabled=%d\n", crt_isMaskEnabled());
    fprintf(file, "comp.crt.gamma_enabled=%d\n", crt_isGammaEnabled());
    fprintf(file, "comp.crt.chroma_enabled=%d\n", crt_isChromaEnabled());
    fprintf(file, "comp.crt.grille_enabled=%d\n", crt_isGrilleEnabled());
    fprintf(file, "comp.crt.scan_strength=%.6f\n", crt_getScanStrength());
    fprintf(file, "comp.crt.halation_strength=%.6f\n", crt_getHalationStrength());
    fprintf(file, "comp.crt.halation_threshold=%.6f\n", crt_getHalationThreshold());
    fprintf(file, "comp.crt.halation_radius=%.6f\n", crt_getHalationRadius());
    fprintf(file, "comp.crt.mask_strength=%.6f\n", crt_getMaskStrength());
    fprintf(file, "comp.crt.mask_scale=%.6f\n", crt_getMaskScale());
    fprintf(file, "comp.crt.mask_type=%d\n", crt_getMaskType());
    fprintf(file, "comp.crt.grille_strength=%.6f\n", crt_getGrilleStrength());
    fprintf(file, "comp.crt.beam_strength=%.6f\n", crt_getBeamStrength());
    fprintf(file, "comp.crt.beam_width=%.6f\n", crt_getBeamWidth());
    fprintf(file, "comp.crt.curvature=%.6f\n", crt_getCurvatureK());
    fprintf(file, "comp.crt.overscan=%.6f\n", crt_getOverscan());
    fprintf(file, "comp.crt.scanline_border=%.6f\n", crt_getScanlineBorder());
}

int
crt_loadConfigProperty(const char *prop, const char *value)
{
    if (!prop || !value) {
        return 0;
    }
    int intValue = 0;
    float floatValue = 0.0f;
    if (strcmp(prop, "geometry_enabled") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setGeometryEnabled(intValue ? 1 : 0);
    } else if (strcmp(prop, "bloom_enabled") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setBloomEnabled(intValue ? 1 : 0);
    } else if (strcmp(prop, "halation_enabled") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setHalationEnabled(intValue ? 1 : 0);
    } else if (strcmp(prop, "mask_enabled") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setMaskEnabled(intValue ? 1 : 0);
    } else if (strcmp(prop, "gamma_enabled") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setGammaEnabled(intValue ? 1 : 0);
    } else if (strcmp(prop, "chroma_enabled") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setChromaEnabled(intValue ? 1 : 0);
    } else if (strcmp(prop, "grille_enabled") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setGrilleEnabled(intValue ? 1 : 0);
    } else if (strcmp(prop, "scan_strength") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setScanStrength(floatValue);
    } else if (strcmp(prop, "halation_strength") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setHalationStrength(floatValue);
    } else if (strcmp(prop, "halation_threshold") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setHalationThreshold(floatValue);
    } else if (strcmp(prop, "halation_radius") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setHalationRadius(floatValue);
    } else if (strcmp(prop, "mask_strength") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setMaskStrength(floatValue);
    } else if (strcmp(prop, "mask_scale") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setMaskScale(floatValue);
    } else if (strcmp(prop, "mask_type") == 0) {
        if (!crt_parseInt(value, &intValue)) {
            return 0;
        }
        crt_setMaskType(intValue);
    } else if (strcmp(prop, "grille_strength") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setGrilleStrength(floatValue);
    } else if (strcmp(prop, "beam_strength") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setBeamStrength(floatValue);
    } else if (strcmp(prop, "beam_width") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setBeamWidth(floatValue);
    } else if (strcmp(prop, "curvature") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setCurvatureK(floatValue);
    } else if (strcmp(prop, "overscan") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setOverscan(floatValue);
    } else if (strcmp(prop, "scanline_border") == 0) {
        if (!crt_parseFloat(value, &floatValue)) {
            return 0;
        }
        crt_setScanlineBorder(floatValue);
    } else {
        return 0;
    }
    crt_persistedConfig = 1;
    return 1;
}

int
crt_hasPersistedConfig(void)
{
    return crt_persistedConfig ? 1 : 0;
}
