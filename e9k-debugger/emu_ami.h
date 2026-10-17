/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9k-lib.h"
#include "e9ui.h"
#include "debugger.h"


extern const emu_system_iface_t emu_ami_iface;

int
emu_ami_mouseCaptureOnClickEnabled(void);

size_t
emu_ami_rangeBarCount(void);

int
emu_ami_rangeBarDescribe(size_t index, emu_range_bar_desc_t *outDesc);

void
emu_ami_rangeBarChanged(size_t index, float startPercent, float endPercent);

void
emu_ami_rangeBarDragging(size_t index, int dragging, float startPercent, float endPercent);

void
emu_ami_rangeBarTooltip(size_t index, float startPercent, float endPercent, char *out, size_t cap);

int
emu_ami_rangeBarSync(size_t index, e9ui_component_t *bar);

int
emu_ami_getCopperDebugEnabled(void);

void
emu_ami_setCopperDebugEnabled(int enabled);
