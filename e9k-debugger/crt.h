/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <stdio.h>

int
crt_isEnabled(void);

void
crt_setEnabled(int enabled);

int
crt_isGeometryEnabled(void);

int
crt_isBloomEnabled(void);

int
crt_isHalationEnabled(void);

int
crt_isMaskEnabled(void);

int
crt_isGammaEnabled(void);

int
crt_isChromaEnabled(void);

int
crt_toggleGeometry(void);

int
crt_toggleBloom(void);

int
crt_toggleHalation(void);

int
crt_toggleMask(void);

int
crt_toggleGamma(void);

int
crt_toggleChroma(void);

void
crt_setGeometryEnabled(int enabled);

void
crt_setBloomEnabled(int enabled);

void
crt_setHalationEnabled(int enabled);

void
crt_setMaskEnabled(int enabled);

void
crt_setGammaEnabled(int enabled);

void
crt_setChromaEnabled(int enabled);

float
crt_getScanStrength(void);

void
crt_setScanStrength(float strength);

float
crt_getHalationStrength(void);

void
crt_setHalationStrength(float strength);

float
crt_getHalationThreshold(void);

void
crt_setHalationThreshold(float threshold);

float
crt_getHalationRadius(void);

void
crt_setHalationRadius(float radius);

float
crt_getMaskStrength(void);

void
crt_setMaskStrength(float strength);

float
crt_getMaskScale(void);

void
crt_setMaskScale(float scale);

int
crt_getMaskType(void);

void
crt_setMaskType(int type);

int
crt_isGrilleEnabled(void);

void
crt_setGrilleEnabled(int enabled);

float
crt_getGrilleStrength(void);

void
crt_setGrilleStrength(float strength);

float
crt_getBeamStrength(void);

void
crt_setBeamStrength(float strength);

float
crt_getBeamWidth(void);

void
crt_setBeamWidth(float width);

float
crt_getCurvatureK(void);

void
crt_setCurvatureK(float k);

float
crt_getOverscan(void);

void
crt_setOverscan(float overscan);

void
crt_setAdvancedDefaults(void);

float
crt_getRenderScale(void);

void
crt_setRenderScale(float scale);

float
crt_getScanlineBorder(void);

void
crt_setScanlineBorder(float border);

void
crt_persistConfig(FILE *file);

int
crt_loadConfigProperty(const char *prop, const char *value);

int
crt_hasPersistedConfig(void);


