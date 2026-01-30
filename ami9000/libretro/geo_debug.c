#include "geo_debug.h"
#include "geo_checkpoint.h"
#include "geo_debug_rom.h"
#include "geo_debug_sprite.h"
#include "geo_protect.h"
#include "geo_watchpoint.h"

#include "libretro.h"

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "events.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"

#define GEO_DEBUG_CALLSTACK_MAX 256
#define GEO_DEBUG_BREAKPOINT_MAX 4096

extern bool libretro_frame_end;

#define GEO_DEBUG_EXPORT RETRO_API

// Fake debug output register support (written by target code, consumed by e9k-debugger)
#define GEO_DEBUG_TEXT_CAP 8192

static int geo_debug_paused = 0;
static uint32_t geo_debug_callstack[GEO_DEBUG_CALLSTACK_MAX];
static size_t geo_debug_callstackDepth = 0;

static int geo_debug_stepInstr = 0;
static int geo_debug_stepInstrAfter = 0;

static int geo_debug_stepNext = 0;
static size_t geo_debug_stepNextDepth = 0;
static uint32_t geo_debug_stepStartPc = 0;
static int geo_debug_stepNextSkipOnce = 0;

static int geo_debug_skipBreakpointOnce = 0;
static uint32_t geo_debug_skipBreakpointPc = 0;

static uint32_t geo_debug_breakpoints[GEO_DEBUG_BREAKPOINT_MAX];
static size_t geo_debug_breakpointCount = 0;
static uint32_t geo_debug_tempBreakpoints[GEO_DEBUG_BREAKPOINT_MAX];
static size_t geo_debug_tempBreakpointCount = 0;

static void (*geo_debug_vblankCb)(void *) = NULL;
static void *geo_debug_vblankUser = NULL;

void debug_enableGeoHooks(void);

static int geo_debug_memhooksEnabled = 0;

static geo_debug_watchpoint_t geo_debug_watchpoints[GEO_WATCHPOINT_COUNT];
static uint64_t geo_debug_watchpointEnabledMask = 0;
static geo_debug_watchbreak_t geo_debug_watchbreak = {0};
static int geo_debug_watchbreakPending = 0;
static int geo_debug_watchpointSuspend = 0;

static geo_debug_protect_t geo_debug_protects[GEO_PROTECT_COUNT];
static uint64_t geo_debug_protectEnabledMask = 0;

static int geo_debug_checkpointEnabled = 0;
static geo_debug_checkpoint_t geo_debug_checkpoints[GEO_CHECKPOINT_COUNT];

static int geo_debug_profilerEnabled = 0;

// Minimal PC-sampling profiler used by e9k-debugger. The debugger resolves PCs to symbols/lines.
// We stream aggregated PC hits as JSON in geo_debug_profiler_stream_next(), matching geo9000.
#define GEO_DEBUG_PROF_EMPTY_PC 0xffffffffu
#define GEO_DEBUG_PROF_TABLE_CAP 4096u
#define GEO_DEBUG_PROF_SAMPLE_DIV 64u
static uint32_t geo_debug_prof_pcs[GEO_DEBUG_PROF_TABLE_CAP];
static uint64_t geo_debug_prof_samples[GEO_DEBUG_PROF_TABLE_CAP];
static uint64_t geo_debug_prof_cycles[GEO_DEBUG_PROF_TABLE_CAP];
static uint32_t geo_debug_prof_entryEpoch[GEO_DEBUG_PROF_TABLE_CAP];
static uint32_t geo_debug_prof_dirtyIdx[GEO_DEBUG_PROF_TABLE_CAP];
static uint32_t geo_debug_prof_dirtyCount = 0;
static uint32_t geo_debug_prof_epoch = 1;
static uint32_t geo_debug_prof_tick = 0;
static uint32_t geo_debug_prof_lastTickAtFrame = 0;
static int geo_debug_prof_streamEnabled = 0;
static int geo_debug_prof_lastValid = 0;
static uint32_t geo_debug_prof_lastPc = 0;
static evt_t geo_debug_prof_lastCycle = 0;
#ifdef JIT
static int geo_debug_prof_savedCachesize = -1;
#endif

static void (*geo_debug_setDebugBaseCb)(uint32_t section, uint32_t base) = NULL;

static char geo_debug_textBuf[GEO_DEBUG_TEXT_CAP];
static size_t geo_debug_textHead = 0;
static size_t geo_debug_textTail = 0;
static size_t geo_debug_textCount = 0;

static void geo_debug_requestBreak(void);

static void
geo_debug_profiler_reset(void)
{
	memset(geo_debug_prof_pcs, 0xff, sizeof(geo_debug_prof_pcs));
	memset(geo_debug_prof_samples, 0, sizeof(geo_debug_prof_samples));
	memset(geo_debug_prof_cycles, 0, sizeof(geo_debug_prof_cycles));
	memset(geo_debug_prof_entryEpoch, 0, sizeof(geo_debug_prof_entryEpoch));
	geo_debug_prof_dirtyCount = 0;
	geo_debug_prof_epoch = 1;
	geo_debug_prof_tick = 0;
	geo_debug_prof_lastTickAtFrame = 0;
	geo_debug_prof_lastValid = 0;
	geo_debug_prof_lastPc = 0;
	geo_debug_prof_lastCycle = 0;
}

static void
geo_debug_profiler_markDirtySlot(uint32_t slot)
{
	if (slot >= GEO_DEBUG_PROF_TABLE_CAP) {
		return;
	}
	if (geo_debug_prof_entryEpoch[slot] == geo_debug_prof_epoch) {
		return;
	}
	geo_debug_prof_entryEpoch[slot] = geo_debug_prof_epoch;
	if (geo_debug_prof_dirtyCount < GEO_DEBUG_PROF_TABLE_CAP) {
		geo_debug_prof_dirtyIdx[geo_debug_prof_dirtyCount++] = slot;
	}
}

static int
geo_debug_profiler_findSlot(uint32_t pc24, int create, uint32_t *out_slot)
{
	if (out_slot) {
		*out_slot = 0;
	}
	pc24 &= 0x00ffffffu;
	uint32_t mask = GEO_DEBUG_PROF_TABLE_CAP - 1u;
	uint32_t idx = (pc24 * 2654435761u) & mask;
	for (uint32_t probe = 0; probe < GEO_DEBUG_PROF_TABLE_CAP; ++probe) {
		uint32_t slot = (idx + probe) & mask;
		uint32_t cur = geo_debug_prof_pcs[slot];
		if (cur == pc24) {
			if (out_slot) {
				*out_slot = slot;
			}
			return 1;
		}
		if (cur == GEO_DEBUG_PROF_EMPTY_PC) {
			if (!create) {
				return 0;
			}
			geo_debug_prof_pcs[slot] = pc24 & 0x00ffffffu;
			geo_debug_prof_samples[slot] = 0;
			geo_debug_prof_cycles[slot] = 0;
			if (out_slot) {
				*out_slot = slot;
			}
			return 1;
		}
	}
	return 0;
}

static void
geo_debug_profiler_accountCycles(uint32_t pc24, uint64_t cycles)
{
	if (cycles == 0) {
		return;
	}
	uint32_t slot = 0;
	if (!geo_debug_profiler_findSlot(pc24, 1, &slot)) {
		return;
	}
	geo_debug_prof_cycles[slot] += cycles;
	geo_debug_profiler_markDirtySlot(slot);
}

static void
geo_debug_profiler_samplePc(uint32_t pc24)
{
	uint32_t slot = 0;
	if (!geo_debug_profiler_findSlot(pc24, 1, &slot)) {
		return;
	}
	geo_debug_prof_samples[slot] += 1;
	geo_debug_profiler_markDirtySlot(slot);
}

static void
geo_debug_profiler_instrHook(uint32_t pc24)
{
	if (!geo_debug_profilerEnabled) {
		return;
	}
	if (geo_debug_paused) {
		return;
	}

	evt_t now = get_cycles();
	if (geo_debug_prof_lastValid) {
		evt_t deltaUnits = now - geo_debug_prof_lastCycle;
		if (deltaUnits > 0) {
			uint64_t deltaCycles = 0;
			if (CYCLE_UNIT > 0) {
				deltaCycles = (uint64_t)(deltaUnits / (evt_t)CYCLE_UNIT);
			} else {
				deltaCycles = (uint64_t)deltaUnits;
			}
			if (deltaCycles) {
				geo_debug_profiler_accountCycles(geo_debug_prof_lastPc, deltaCycles);
			}
		}
	}
	geo_debug_prof_lastCycle = now;
	geo_debug_prof_lastPc = pc24 & 0x00ffffffu;
	geo_debug_prof_lastValid = 1;

	geo_debug_prof_tick++;
	if ((geo_debug_prof_tick % GEO_DEBUG_PROF_SAMPLE_DIV) == 0u) {
		geo_debug_profiler_samplePc(pc24);
	}
}

void
geo_debug_text_write(uae_u8 byte)
{
		if (geo_debug_textCount == GEO_DEBUG_TEXT_CAP) {
			geo_debug_textTail = (geo_debug_textTail + 1) % GEO_DEBUG_TEXT_CAP;
		geo_debug_textCount--;
	}
	geo_debug_textBuf[geo_debug_textHead] = (char)byte;
	geo_debug_textHead = (geo_debug_textHead + 1) % GEO_DEBUG_TEXT_CAP;
	geo_debug_textCount++;
}

void
geo_debug_set_debug_base(uint32_t section, uae_u32 base)
{
	if (geo_debug_setDebugBaseCb) {
		geo_debug_setDebugBaseCb(section, (uint32_t)base);
	}
}

static uint32_t
geo_debug_maskAddr(uaecptr addr)
{
	return (uint32_t)addr & 0x00ffffffu;
}

static uint32_t
geo_debug_maskValue(uint32_t v, uint32_t sizeBits)
{
	if (sizeBits == 8u) {
		return v & 0xffu;
	}
	if (sizeBits == 16u) {
		return v & 0xffffu;
	}
	return v;
}

static void
geo_debug_ensureMemhooks(void)
{
	if (geo_debug_memhooksEnabled) {
		return;
	}
	debug_enableGeoHooks();
	geo_debug_memhooksEnabled = 1;
}

static int
geo_debug_watchpointMatch(const geo_debug_watchpoint_t *wp, uint32_t accessAddr, uint32_t accessKind,
                          uint32_t accessSizeBits, uint32_t value, uint32_t oldValue, int oldValueValid)
{
	if (!wp) {
		return 0;
	}
	uint32_t op = wp->op_mask;

	if (accessKind == GEO_WATCH_ACCESS_READ) {
		if ((op & GEO_WATCH_OP_READ) == 0u) {
			return 0;
		}
	} else if (accessKind == GEO_WATCH_ACCESS_WRITE) {
		if ((op & GEO_WATCH_OP_WRITE) == 0u) {
			return 0;
		}
	} else {
		return 0;
	}

	if (op & GEO_WATCH_OP_ADDR_COMPARE_MASK) {
		uint32_t mask = wp->addr_mask_operand;
		if ((accessAddr & mask) != (wp->addr & mask)) {
			return 0;
		}
	}

	if (op & GEO_WATCH_OP_ACCESS_SIZE) {
		if (wp->size_operand != 8u && wp->size_operand != 16u && wp->size_operand != 32u) {
			return 0;
		}
		if (accessSizeBits != wp->size_operand) {
			return 0;
		}
	}

	uint32_t v = geo_debug_maskValue(value, accessSizeBits);
	uint32_t ov = geo_debug_maskValue(oldValue, accessSizeBits);

	if (op & GEO_WATCH_OP_VALUE_EQ) {
		if (v != geo_debug_maskValue(wp->value_operand, accessSizeBits)) {
			return 0;
		}
	}
	if (op & GEO_WATCH_OP_OLD_VALUE_EQ) {
		if (!oldValueValid) {
			return 0;
		}
		if (ov != geo_debug_maskValue(wp->old_value_operand, accessSizeBits)) {
			return 0;
		}
	}
	if (op & GEO_WATCH_OP_VALUE_NEQ_OLD) {
		if (!oldValueValid) {
			return 0;
		}
		if (ov == geo_debug_maskValue(wp->diff_operand, accessSizeBits)) {
			return 0;
		}
	}

	return 1;
}

static void
geo_debug_watchbreakRequest(uint32_t index, uint32_t accessAddr, uint32_t accessKind, uint32_t accessSizeBits,
                            uint32_t value, uint32_t oldValue, int oldValueValid)
{
	if (geo_debug_watchbreakPending) {
		return;
	}
	if (index >= GEO_WATCHPOINT_COUNT) {
		return;
	}

	geo_debug_watchpoint_t *wp = &geo_debug_watchpoints[index];

	memset(&geo_debug_watchbreak, 0, sizeof(geo_debug_watchbreak));
	geo_debug_watchbreak.index = index;
	geo_debug_watchbreak.watch_addr = wp->addr;
	geo_debug_watchbreak.op_mask = wp->op_mask;
	geo_debug_watchbreak.diff_operand = wp->diff_operand;
	geo_debug_watchbreak.value_operand = wp->value_operand;
	geo_debug_watchbreak.old_value_operand = wp->old_value_operand;
	geo_debug_watchbreak.size_operand = wp->size_operand;
	geo_debug_watchbreak.addr_mask_operand = wp->addr_mask_operand;

	geo_debug_watchbreak.access_addr = accessAddr;
	geo_debug_watchbreak.access_kind = accessKind;
	geo_debug_watchbreak.access_size = accessSizeBits;
	geo_debug_watchbreak.value = geo_debug_maskValue(value, accessSizeBits);
	geo_debug_watchbreak.old_value = geo_debug_maskValue(oldValue, accessSizeBits);
	geo_debug_watchbreak.old_value_valid = oldValueValid ? 1u : 0u;

	geo_debug_watchbreakPending = 1;
	geo_debug_requestBreak();
}

static int
geo_debug_hasBreakpoint(uint32_t addr)
{
	for (size_t i = 0; i < geo_debug_breakpointCount; ++i) {
		if (geo_debug_breakpoints[i] == addr) {
			return 1;
		}
	}
	return 0;
}

static int
geo_debug_consumeTempBreakpoint(uint32_t addr)
{
	for (size_t i = 0; i < geo_debug_tempBreakpointCount; ++i) {
		if (geo_debug_tempBreakpoints[i] == addr) {
			size_t remain = geo_debug_tempBreakpointCount - (i + 1u);
			if (remain) {
				memmove(&geo_debug_tempBreakpoints[i], &geo_debug_tempBreakpoints[i + 1u], remain * sizeof(geo_debug_tempBreakpoints[0]));
			}
			geo_debug_tempBreakpointCount--;
			return 1;
		}
	}
	return 0;
}

GEO_DEBUG_EXPORT void
geo_debug_pause(void)
{
	// Use the same break mechanism as instruction/watch breaks so execution halts immediately
	// (important when running with threaded CPU/event loops).
	geo_debug_requestBreak();
}

GEO_DEBUG_EXPORT void
geo_debug_resume(void)
{
	geo_debug_paused = 0;
	geo_debug_stepInstr = 0;
	geo_debug_stepInstrAfter = 0;
	geo_debug_stepNext = 0;

	uint32_t pc24 = geo_debug_maskAddr(m68k_getpc());
	if (geo_debug_hasBreakpoint(pc24)) {
		geo_debug_skipBreakpointOnce = 1;
		geo_debug_skipBreakpointPc = pc24;
	}
}

GEO_DEBUG_EXPORT int
geo_debug_is_paused(void)
{
	return geo_debug_paused;
}

GEO_DEBUG_EXPORT void
geo_debug_step_instr(void)
{
	geo_debug_paused = 0;
	geo_debug_stepNext = 0;
	geo_debug_stepInstr = 1;
	geo_debug_stepInstrAfter = 0;
}

GEO_DEBUG_EXPORT void
geo_debug_step_line(void)
{
	geo_debug_step_instr();
}

GEO_DEBUG_EXPORT void
geo_debug_step_next(void)
{
	geo_debug_paused = 0;
	geo_debug_stepInstr = 0;
	geo_debug_stepInstrAfter = 0;
	geo_debug_stepNext = 1;
	geo_debug_stepNextDepth = geo_debug_callstackDepth;
	geo_debug_stepStartPc = geo_debug_maskAddr(m68k_getpc());
	geo_debug_stepNextSkipOnce = 0;
}

GEO_DEBUG_EXPORT size_t
geo_debug_read_callstack(uint32_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = geo_debug_callstackDepth;
	if (count > cap) {
		count = cap;
	}
	for (size_t i = 0; i < count; ++i) {
		out[i] = geo_debug_callstack[i];
	}
	return count;
}

GEO_DEBUG_EXPORT size_t
geo_debug_read_regs(uint32_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	MakeSR();
	size_t count = 0;
	for (int i = 0; i < 16 && count < cap; ++i) {
		out[count++] = regs.regs[i];
	}
	if (count < cap) {
		out[count++] = regs.sr;
	}
	if (count < cap) {
		out[count++] = geo_debug_maskAddr(m68k_getpc());
	}
	return count;
}

GEO_DEBUG_EXPORT size_t
geo_debug_read_memory(uint32_t addr, uint8_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	geo_debug_watchpointSuspend++;
	uaecptr base = (uaecptr)addr;
	for (size_t i = 0; i < cap; ++i) {
		out[i] = (uint8_t)get_byte(munge24(base + (uaecptr)i));
	}
	geo_debug_watchpointSuspend--;
	return cap;
}

GEO_DEBUG_EXPORT int
geo_debug_write_memory(uint32_t addr, uint32_t value, size_t size)
{
	geo_debug_watchpointSuspend++;
	uaecptr a = munge24((uaecptr)addr);
	if (size == 1) {
		put_byte(a, value & 0xffu);
		geo_debug_watchpointSuspend--;
		return 1;
	}
	if (size == 2) {
		put_word(a, value & 0xffffu);
		geo_debug_watchpointSuspend--;
		return 1;
	}
	if (size == 4) {
		put_long(a, value);
		geo_debug_watchpointSuspend--;
		return 1;
	}
	geo_debug_watchpointSuspend--;
	return 0;
}

GEO_DEBUG_EXPORT size_t
geo_debug_disassemble_quick(uint32_t pc, char *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	geo_debug_watchpointSuspend++;
	uaecptr nextpc = 0xffffffffu;
	int bufsize = (cap > 0x7fffffffU) ? 0x7fffffff : (int)cap;
	uaecptr addr = munge24((uaecptr)pc);
	m68k_disasm_2(out, bufsize, addr, NULL, 0, &nextpc, 1, NULL, NULL, 0xffffffffu, 0);
	out[bufsize - 1] = '\0';

	if (nextpc != 0xffffffffu && nextpc > addr) {
		geo_debug_watchpointSuspend--;
		return (size_t)(nextpc - addr);
	}
	geo_debug_watchpointSuspend--;
	return 2;
}

GEO_DEBUG_EXPORT uint64_t
geo_debug_read_cycle_count(void)
{
	// get_cycles() returns UAE internal "cycle units" (CYCLE_UNIT = 512), not raw CPU cycles.
	// Convert to a more intuitive cycle count for the debugger UI.
	evt_t c = get_cycles();
	if (CYCLE_UNIT > 0) {
		return (uint64_t)(c / (evt_t)CYCLE_UNIT);
	}
	return (uint64_t)c;
}

GEO_DEBUG_EXPORT void
geo_debug_add_breakpoint(uint32_t addr)
{
	uint32_t addr24 = geo_debug_maskAddr((uaecptr)addr);
	if (geo_debug_hasBreakpoint(addr24)) {
		return;
	}
	if (geo_debug_breakpointCount >= GEO_DEBUG_BREAKPOINT_MAX) {
		return;
	}
	geo_debug_breakpoints[geo_debug_breakpointCount++] = addr24;
}

GEO_DEBUG_EXPORT void
geo_debug_remove_breakpoint(uint32_t addr)
{
	uint32_t addr24 = geo_debug_maskAddr((uaecptr)addr);
	for (size_t i = 0; i < geo_debug_breakpointCount; ++i) {
		if (geo_debug_breakpoints[i] == addr24) {
			size_t remain = geo_debug_breakpointCount - (i + 1u);
			if (remain) {
				memmove(&geo_debug_breakpoints[i], &geo_debug_breakpoints[i + 1u], remain * sizeof(geo_debug_breakpoints[0]));
			}
			geo_debug_breakpointCount--;
			return;
		}
	}
}

GEO_DEBUG_EXPORT void
geo_debug_add_temp_breakpoint(uint32_t addr)
{
	uint32_t addr24 = geo_debug_maskAddr((uaecptr)addr);
	for (size_t i = 0; i < geo_debug_tempBreakpointCount; ++i) {
		if (geo_debug_tempBreakpoints[i] == addr24) {
			return;
		}
	}
	if (geo_debug_tempBreakpointCount >= GEO_DEBUG_BREAKPOINT_MAX) {
		return;
	}
	geo_debug_tempBreakpoints[geo_debug_tempBreakpointCount++] = addr24;
}

GEO_DEBUG_EXPORT void
geo_debug_remove_temp_breakpoint(uint32_t addr)
{
	uint32_t addr24 = geo_debug_maskAddr((uaecptr)addr);
	(void)geo_debug_consumeTempBreakpoint(addr24);
}

GEO_DEBUG_EXPORT void
geo_set_vblank_callback(void (*cb)(void *), void *user)
{
	geo_debug_vblankCb = cb;
	geo_debug_vblankUser = user;
}

GEO_DEBUG_EXPORT void
geo_set_debug_base_callback(void (*cb)(uint32_t section, uint32_t base))
{
	geo_debug_setDebugBaseCb = cb;
}

GEO_DEBUG_EXPORT void
geo_vblank_notify(void)
{
	if (geo_debug_profilerEnabled && !geo_debug_paused) {
		if (geo_debug_prof_tick == geo_debug_prof_lastTickAtFrame) {
			uaecptr pc = m68k_getpc();
			geo_debug_profiler_samplePc(geo_debug_maskAddr(pc));
		}
		geo_debug_prof_lastTickAtFrame = geo_debug_prof_tick;
	}
	if (geo_debug_vblankCb) {
		geo_debug_vblankCb(geo_debug_vblankUser);
	}
}

static void
geo_debug_requestBreak(void)
{
	geo_debug_paused = 1;
	geo_debug_stepInstr = 0;
	geo_debug_stepInstrAfter = 0;
	geo_debug_stepNext = 0;
	libretro_frame_end = true;
	set_special(SPCFLAG_BRK);
}

static void
geo_debug_watchpointRead(uint32_t addr24, uint32_t value, uint32_t sizeBits)
{
	if (geo_debug_watchpointSuspend > 0) {
		return;
	}
	if (geo_debug_paused) {
		return;
	}
	if (geo_debug_watchpointEnabledMask == 0) {
		return;
	}

	for (uint32_t index = 0; index < GEO_WATCHPOINT_COUNT; ++index) {
		if ((geo_debug_watchpointEnabledMask & (1ull << index)) == 0ull) {
			continue;
		}
		if (geo_debug_watchpointMatch(&geo_debug_watchpoints[index], addr24, GEO_WATCH_ACCESS_READ, sizeBits, value, value, 1)) {
			geo_debug_watchbreakRequest(index, addr24, GEO_WATCH_ACCESS_READ, sizeBits, value, value, 1);
			return;
		}
	}
}

static void
geo_debug_watchpointWrite(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid)
{
	if (geo_debug_watchpointSuspend > 0) {
		return;
	}
	if (geo_debug_paused) {
		return;
	}
	if (geo_debug_watchpointEnabledMask == 0) {
		return;
	}

	for (uint32_t index = 0; index < GEO_WATCHPOINT_COUNT; ++index) {
		if ((geo_debug_watchpointEnabledMask & (1ull << index)) == 0ull) {
			continue;
		}
		if (geo_debug_watchpointMatch(&geo_debug_watchpoints[index], addr24, GEO_WATCH_ACCESS_WRITE, sizeBits, value, oldValue, oldValueValid)) {
			geo_debug_watchbreakRequest(index, addr24, GEO_WATCH_ACCESS_WRITE, sizeBits, value, oldValue, oldValueValid);
			return;
		}
	}
}

static int
geo_debug_protectFilterWrite(uint32_t addr24, uint32_t sizeBits, uint32_t oldValue, int oldValueValid, uint32_t *inoutValue)
{
	if (!inoutValue) {
		return 1;
	}
	if (geo_debug_watchpointSuspend > 0) {
		return 1;
	}
	if (geo_debug_protectEnabledMask == 0) {
		return 1;
	}

	uint32_t sizeBytes = 0;
	if (sizeBits == 8u) {
		sizeBytes = 1;
	} else if (sizeBits == 16u) {
		sizeBytes = 2;
	} else if (sizeBits == 32u) {
		sizeBytes = 4;
	} else {
		return 1;
	}

	uint8_t bytes[4] = {0};
	uint8_t oldBytes[4] = {0};
	uint32_t v = geo_debug_maskValue(*inoutValue, sizeBits);
	uint32_t ov = geo_debug_maskValue(oldValue, sizeBits);

	for (uint32_t i = 0; i < sizeBytes; ++i) {
		uint32_t shift = (sizeBytes - 1u - i) * 8u;
		bytes[i] = (uint8_t)((v >> shift) & 0xffu);
		if (oldValueValid) {
			oldBytes[i] = (uint8_t)((ov >> shift) & 0xffu);
		}
	}

	for (uint32_t writeIndex = 0; writeIndex < sizeBytes; ++writeIndex) {
		uint32_t writeAddr = (addr24 + writeIndex) & 0x00ffffffu;
		for (uint32_t entryIndex = 0; entryIndex < GEO_PROTECT_COUNT; ++entryIndex) {
			if ((geo_debug_protectEnabledMask & (1ull << entryIndex)) == 0ull) {
				continue;
			}
			const geo_debug_protect_t *p = &geo_debug_protects[entryIndex];
			if (p->sizeBits != sizeBits) {
				continue;
			}

			uint32_t mask = p->addrMask ? p->addrMask : 0x00ffffffu;
			for (uint32_t byteIndex = 0; byteIndex < sizeBytes; ++byteIndex) {
				uint32_t pa = (p->addr + byteIndex) & 0x00ffffffu;
				if ((writeAddr & mask) != (pa & mask)) {
					continue;
				}

				if (p->mode == GEO_PROTECT_MODE_SET) {
					uint32_t pshift = (sizeBytes - 1u - byteIndex) * 8u;
					bytes[writeIndex] = (uint8_t)((p->value >> pshift) & 0xffu);
				} else if (oldValueValid) {
					bytes[writeIndex] = oldBytes[writeIndex];
				} else {
					return 1;
				}
				goto next_write_byte;
			}
		}
next_write_byte:
		;
	}

	uint32_t outValue = 0;
	for (uint32_t i = 0; i < sizeBytes; ++i) {
		outValue = (outValue << 8) | (uint32_t)bytes[i];
	}
	*inoutValue = outValue;
	return 1;
}

GEO_DEBUG_EXPORT void
geo_debug_memhook_afterRead(uint32_t addr24, uint32_t value, uint32_t sizeBits)
{
	addr24 &= 0x00ffffffu;
	geo_debug_watchpointRead(addr24, value, sizeBits);
}

GEO_DEBUG_EXPORT int
geo_debug_memhook_filterWrite(uint32_t addr24, uint32_t sizeBits, uint32_t oldValue, int oldValueValid, uint32_t *inoutValue)
{
	addr24 &= 0x00ffffffu;
	return geo_debug_protectFilterWrite(addr24, sizeBits, oldValue, oldValueValid, inoutValue);
}

GEO_DEBUG_EXPORT void
geo_debug_memhook_afterWrite(uint32_t addr24, uint32_t value, uint32_t oldValue, uint32_t sizeBits, int oldValueValid)
{
	addr24 &= 0x00ffffffu;
	geo_debug_watchpointWrite(addr24, value, oldValue, sizeBits, oldValueValid);
}

GEO_DEBUG_EXPORT int
geo_debug_instructionHook(uaecptr pc, uae_u16 opcode)
{
	uint32_t pc24 = geo_debug_maskAddr(pc);

	geo_debug_profiler_instrHook(pc24);

	if (geo_debug_stepInstrAfter) {
		geo_debug_requestBreak();
		return 1;
	}

	if ((opcode & 0xFFC0u) == 0x4E80u) {
		int mode = (opcode >> 3) & 7;
		int reg = opcode & 7;
		int ext = 0;
		if (mode == 5 || mode == 6) {
			ext = 2;
		} else if (mode == 7) {
			if (reg == 0 || reg == 2 || reg == 3) {
				ext = 2;
			} else if (reg == 1) {
				ext = 4;
			} else {
				ext = -1;
			}
		} else if (mode < 2) {
			ext = -1;
		}
		if (ext >= 0) {
			if (geo_debug_callstackDepth < GEO_DEBUG_CALLSTACK_MAX) {
				geo_debug_callstack[geo_debug_callstackDepth++] = pc24;
			}
		}
	} else if ((opcode & 0xFF00u) == 0x6100u) {
		if (geo_debug_callstackDepth < GEO_DEBUG_CALLSTACK_MAX) {
			geo_debug_callstack[geo_debug_callstackDepth++] = pc24;
		}
	} else if (opcode == 0x4E75u || opcode == 0x4E74u || opcode == 0x4E73u || opcode == 0x4E77u) {
		if (geo_debug_callstackDepth > 0) {
			geo_debug_callstackDepth--;
		}
		geo_debug_stepNextSkipOnce = 1;
	}

	if (geo_debug_stepInstr) {
		geo_debug_stepInstr = 0;
		geo_debug_stepInstrAfter = 1;
		return 0;
	}

	if (geo_debug_stepNext) {
		if (geo_debug_stepNextSkipOnce) {
			geo_debug_stepNextSkipOnce = 0;
			return 0;
		}
		if (pc24 != geo_debug_stepStartPc && geo_debug_callstackDepth <= geo_debug_stepNextDepth) {
			geo_debug_requestBreak();
			return 1;
		}
	}

	if (geo_debug_skipBreakpointOnce) {
		geo_debug_skipBreakpointOnce = 0;
		if (pc24 == geo_debug_skipBreakpointPc) {
			return 0;
		}
	}

	if (geo_debug_consumeTempBreakpoint(pc24) || geo_debug_hasBreakpoint(pc24)) {
		geo_debug_requestBreak();
		return 1;
	}

	return 0;
}

GEO_DEBUG_EXPORT void
geo_debug_reset_watchpoints(void)
{
	memset(geo_debug_watchpoints, 0, sizeof(geo_debug_watchpoints));
	geo_debug_watchpointEnabledMask = 0;
	memset(&geo_debug_watchbreak, 0, sizeof(geo_debug_watchbreak));
	geo_debug_watchbreakPending = 0;
	geo_debug_watchpointSuspend = 0;
}

GEO_DEBUG_EXPORT int
geo_debug_add_watchpoint(uint32_t addr, uint32_t op_mask, uint32_t diff_operand, uint32_t value_operand,
                         uint32_t old_value_operand, uint32_t size_operand, uint32_t addr_mask_operand)
{
	geo_debug_ensureMemhooks();
	for (uint32_t i = 0; i < GEO_WATCHPOINT_COUNT; ++i) {
		uint64_t bit = 1ull << i;
		if ((geo_debug_watchpointEnabledMask & bit) != 0ull) {
			continue;
		}
		if (geo_debug_watchpoints[i].op_mask != 0u) {
			continue;
		}
		geo_debug_watchpoints[i].addr = addr & 0x00ffffffu;
		geo_debug_watchpoints[i].op_mask = op_mask;
		geo_debug_watchpoints[i].diff_operand = diff_operand;
		geo_debug_watchpoints[i].value_operand = value_operand;
		geo_debug_watchpoints[i].old_value_operand = old_value_operand;
		geo_debug_watchpoints[i].size_operand = size_operand;
		geo_debug_watchpoints[i].addr_mask_operand = addr_mask_operand;
		geo_debug_watchpointEnabledMask |= bit;
		return (int)i;
	}
	return -1;
}

GEO_DEBUG_EXPORT void
geo_debug_remove_watchpoint(uint32_t index)
{
	if (index >= GEO_WATCHPOINT_COUNT) {
		return;
	}
	geo_debug_watchpointEnabledMask &= ~(1ull << index);
	memset(&geo_debug_watchpoints[index], 0, sizeof(geo_debug_watchpoints[index]));
}

GEO_DEBUG_EXPORT size_t
geo_debug_read_watchpoints(geo_debug_watchpoint_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = GEO_WATCHPOINT_COUNT;
	if (count > cap) {
		count = cap;
	}
	memcpy(out, geo_debug_watchpoints, count * sizeof(out[0]));
	return count;
}

GEO_DEBUG_EXPORT uint64_t
geo_debug_get_watchpoint_enabled_mask(void)
{
	return geo_debug_watchpointEnabledMask;
}

GEO_DEBUG_EXPORT void
geo_debug_set_watchpoint_enabled_mask(uint64_t mask)
{
	if (mask) {
		geo_debug_ensureMemhooks();
	}
	geo_debug_watchpointEnabledMask = mask;
}

GEO_DEBUG_EXPORT int
geo_debug_consume_watchbreak(geo_debug_watchbreak_t *out)
{
	if (!out) {
		return 0;
	}
	if (!geo_debug_watchbreakPending) {
		return 0;
	}
	*out = geo_debug_watchbreak;
	geo_debug_watchbreakPending = 0;
	return 1;
}

GEO_DEBUG_EXPORT void
geo_debug_reset_protects(void)
{
	memset(geo_debug_protects, 0, sizeof(geo_debug_protects));
	geo_debug_protectEnabledMask = 0;
}

GEO_DEBUG_EXPORT int
geo_debug_add_protect(uint32_t addr, uint32_t size_bits, uint32_t mode, uint32_t value)
{
	geo_debug_ensureMemhooks();
	if (size_bits != 8u && size_bits != 16u && size_bits != 32u) {
		return -1;
	}
	if (mode != GEO_PROTECT_MODE_BLOCK && mode != GEO_PROTECT_MODE_SET) {
		return -1;
	}

	uint32_t addr24 = addr & 0x00ffffffu;
	uint32_t addrMask = 0x00ffffffu;
	uint32_t maskedValue = geo_debug_maskValue(value, size_bits);

	for (uint32_t i = 0; i < GEO_PROTECT_COUNT; ++i) {
		if ((geo_debug_protectEnabledMask & (1ull << i)) == 0ull) {
			continue;
		}
		const geo_debug_protect_t *p = &geo_debug_protects[i];
		if (p->addr == addr24 &&
		    p->addrMask == addrMask &&
		    p->sizeBits == size_bits &&
		    p->mode == mode &&
		    p->value == maskedValue) {
			return (int)i;
		}
	}

	for (uint32_t i = 0; i < GEO_PROTECT_COUNT; ++i) {
		if (geo_debug_protects[i].sizeBits != 0u) {
			continue;
		}
		geo_debug_protects[i].addr = addr24;
		geo_debug_protects[i].addrMask = addrMask;
		geo_debug_protects[i].sizeBits = size_bits;
		geo_debug_protects[i].mode = mode;
		geo_debug_protects[i].value = maskedValue;
		geo_debug_protectEnabledMask |= (1ull << i);
		return (int)i;
	}

	return -1;
}

GEO_DEBUG_EXPORT void
geo_debug_remove_protect(uint32_t index)
{
	if (index >= GEO_PROTECT_COUNT) {
		return;
	}
	memset(&geo_debug_protects[index], 0, sizeof(geo_debug_protects[index]));
	geo_debug_protectEnabledMask &= ~(1ull << index);
}

GEO_DEBUG_EXPORT size_t
geo_debug_read_protects(geo_debug_protect_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = GEO_PROTECT_COUNT;
	if (count > cap) {
		count = cap;
	}
	memcpy(out, geo_debug_protects, count * sizeof(out[0]));
	return count;
}

GEO_DEBUG_EXPORT uint64_t
geo_debug_get_protect_enabled_mask(void)
{
	return geo_debug_protectEnabledMask;
}

GEO_DEBUG_EXPORT void
geo_debug_set_protect_enabled_mask(uint64_t mask)
{
	if (mask) {
		geo_debug_ensureMemhooks();
	}
	geo_debug_protectEnabledMask = mask;
}

GEO_DEBUG_EXPORT void
geo_debug_profiler_start(int stream)
{
	geo_debug_profiler_reset();
	geo_debug_prof_streamEnabled = stream ? 1 : 0;
	geo_debug_profilerEnabled = 1;
#ifdef JIT
	if (geo_debug_prof_savedCachesize < 0) {
		geo_debug_prof_savedCachesize = currprefs.cachesize;
	}
	if (currprefs.cachesize) {
		currprefs.cachesize = 0;
		flush_icache(3);
		set_special(SPCFLAG_END_COMPILE);
	}
#endif
}

GEO_DEBUG_EXPORT void
geo_debug_profiler_stop(void)
{
	geo_debug_profilerEnabled = 0;
	geo_debug_prof_streamEnabled = 0;
#ifdef JIT
	if (geo_debug_prof_savedCachesize >= 0) {
		if (currprefs.cachesize != geo_debug_prof_savedCachesize) {
			currprefs.cachesize = geo_debug_prof_savedCachesize;
			flush_icache(3);
			set_special(SPCFLAG_END_COMPILE);
		}
		geo_debug_prof_savedCachesize = -1;
	}
#endif
}

GEO_DEBUG_EXPORT int
geo_debug_profiler_is_enabled(void)
{
	return geo_debug_profilerEnabled;
}

GEO_DEBUG_EXPORT size_t
geo_debug_profiler_stream_next(char *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}

	if (!geo_debug_prof_streamEnabled) {
		return 0;
	}
	if (geo_debug_prof_dirtyCount == 0) {
		return 0;
	}

	const char *enabled = geo_debug_profilerEnabled ? "enabled" : "disabled";
	size_t pos = 0;
	int written = snprintf(out, cap, "{\"stream\":\"profiler\",\"enabled\":\"%s\",\"hits\":[", enabled);
	if (written <= 0 || (size_t)written >= cap) {
		return 0;
	}
	pos = (size_t)written;

	int first = 1;
	uint32_t newDirtyCount = 0;
	for (uint32_t i = 0; i < geo_debug_prof_dirtyCount; ++i) {
		uint32_t slot = geo_debug_prof_dirtyIdx[i];
		if (slot >= GEO_DEBUG_PROF_TABLE_CAP) {
			continue;
		}
		uint32_t pc24 = geo_debug_prof_pcs[slot];
		if (pc24 == GEO_DEBUG_PROF_EMPTY_PC) {
			geo_debug_prof_entryEpoch[slot] = 0;
			continue;
		}
		unsigned long long samples = (unsigned long long)geo_debug_prof_samples[slot];
		unsigned long long cycles = (unsigned long long)geo_debug_prof_cycles[slot];
		if (samples == 0 && cycles == 0) {
			geo_debug_prof_entryEpoch[slot] = 0;
			continue;
		}

		char entry[96];
		if (first) {
			written = snprintf(entry, sizeof(entry), "{\"pc\":\"0x%06X\",\"samples\":%llu,\"cycles\":%llu}",
			                   (unsigned)(pc24 & 0x00ffffffu), samples, cycles);
			first = 0;
		} else {
			written = snprintf(entry, sizeof(entry), ",{\"pc\":\"0x%06X\",\"samples\":%llu,\"cycles\":%llu}",
			                   (unsigned)(pc24 & 0x00ffffffu), samples, cycles);
		}
		if (written <= 0) {
			geo_debug_prof_entryEpoch[slot] = 0;
			continue;
		}
		size_t need = (size_t)written;
		if (pos + need + 2 >= cap) {
			geo_debug_prof_dirtyIdx[newDirtyCount++] = slot;
			continue;
		}
		memcpy(out + pos, entry, need);
		pos += need;
		geo_debug_prof_entryEpoch[slot] = 0;
	}
	geo_debug_prof_dirtyCount = newDirtyCount;

	if (pos + 2 >= cap) {
		return 0;
	}
	out[pos++] = ']';
	out[pos++] = '}';
	out[pos] = '\0';

	if (geo_debug_prof_dirtyCount == 0) {
		geo_debug_prof_epoch++;
		if (geo_debug_prof_epoch == 0) {
			memset(geo_debug_prof_entryEpoch, 0, sizeof(geo_debug_prof_entryEpoch));
			geo_debug_prof_epoch = 1;
		}
	}
	return pos;
}

GEO_DEBUG_EXPORT size_t
geo_debug_text_read(char *out, size_t cap)
{
	if (!out || cap == 0 || geo_debug_textCount == 0) {
		return 0;
	}
	size_t n = geo_debug_textCount < cap ? geo_debug_textCount : cap;
	for (size_t i = 0; i < n; ++i) {
		out[i] = geo_debug_textBuf[geo_debug_textTail];
		geo_debug_textTail = (geo_debug_textTail + 1) % GEO_DEBUG_TEXT_CAP;
	}
	geo_debug_textCount -= n;
	return n;
}

GEO_DEBUG_EXPORT size_t
geo_debug_neogeo_get_sprite_state(geo_debug_sprite_state_t *out, size_t cap)
{
	(void)out;
	(void)cap;
	return 0;
}

GEO_DEBUG_EXPORT size_t
geo_debug_get_sprite_state(geo_debug_sprite_state_t *out, size_t cap)
{
	return geo_debug_neogeo_get_sprite_state(out, cap);
}

GEO_DEBUG_EXPORT size_t
geo_debug_neogeo_get_p1_rom(geo_debug_rom_region_t *out, size_t cap)
{
	(void)out;
	(void)cap;
	return 0;
}

GEO_DEBUG_EXPORT size_t
geo_debug_get_p1_rom(geo_debug_rom_region_t *out, size_t cap)
{
	return geo_debug_neogeo_get_p1_rom(out, cap);
}

GEO_DEBUG_EXPORT size_t
geo_debug_read_checkpoints(geo_debug_checkpoint_t *out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	size_t count = GEO_CHECKPOINT_COUNT;
	if (count > cap) {
		count = cap;
	}
	memcpy(out, geo_debug_checkpoints, count * sizeof(out[0]));
	return count;
}

GEO_DEBUG_EXPORT void
geo_debug_reset_checkpoints(void)
{
	memset(geo_debug_checkpoints, 0, sizeof(geo_debug_checkpoints));
}

GEO_DEBUG_EXPORT void
geo_debug_set_checkpoint_enabled(int enabled)
{
	geo_debug_checkpointEnabled = enabled ? 1 : 0;
}

GEO_DEBUG_EXPORT int
geo_debug_get_checkpoint_enabled(void)
{
	return geo_debug_checkpointEnabled;
}

GEO_DEBUG_EXPORT int *
geo_debug_amiga_get_debug_dma_addr(void)
{
	return &debug_dma;
}
