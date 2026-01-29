/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "dasm.h"
#include "debugger.h"
#include "debug.h"
#include "elfutil.h"
#include "libretro_host.h"

#ifdef _WIN32
#define getline w64_getline
#endif

typedef struct dasm_cache {
  char **lines;
  uint64_t *addrs;
  int n;
  int cap;
  int addr_hex_width;
  int ready;
} dasm_cache_t;

static dasm_cache_t g_dasm;

static
void dasm_clear(void)
{
  if (g_dasm.lines) {
    for (int i=0;i<g_dasm.n;i++) alloc_free(g_dasm.lines[i]);
    alloc_free(g_dasm.lines);
  }
  if (g_dasm.addrs) {
    alloc_free(g_dasm.addrs);
  }
  memset(&g_dasm, 0, sizeof(g_dasm));
}

static void
dasm_geo_init(void)
{
  memset(&g_dasm, 0, sizeof(g_dasm));
}

static void
dasm_geo_shutdown(void)
{
  dasm_clear();
}

static
void dasm_push(uint64_t addr, const char *text)
{
  if (!text) {
    text = "";
  }
  if (g_dasm.n == g_dasm.cap) {
    int nc = g_dasm.cap ? g_dasm.cap * 2 : 1024;
    char **nl = (char**)alloc_realloc(g_dasm.lines, (size_t)nc * sizeof(char*));
    uint64_t *na = (uint64_t*)alloc_realloc(g_dasm.addrs, (size_t)nc * sizeof(uint64_t));
    if (!nl || !na) {
      alloc_free(nl);
      alloc_free(na);
      return;
    }
    g_dasm.lines = nl; g_dasm.addrs = na; g_dasm.cap = nc;
  }
  g_dasm.addrs[g_dasm.n] = addr;
  g_dasm.lines[g_dasm.n] = alloc_strdup(text);
  g_dasm.n++;
}

static int
dasm_parseLine(const char *ln, uint64_t *out_addr, const char **out_text)
{
  if (!ln || !*ln) {
    return 0;
  }
  while (*ln == ' ' || *ln == '\t') ln++;
  if (ln[0] == '=' && ln[1] == '>') {
    ln += 2;
    while (*ln == ' ' || *ln == '\t') ln++;
  }
  if (ln[0] == '0' && (ln[1] == 'x' || ln[1] == 'X')) {
    ln += 2;
  }
  char *ep = NULL;
  unsigned long long addr = strtoull(ln, &ep, 16);
  if (!ep || ep == ln) {
    return 0;
  }
  const char *colon = (*ep == ':') ? ep : strchr(ep, ':');
  if (!colon) {
    return 0;
  }
  const char *text = colon + 1;
  const char *tab = strrchr(text, '\t');
  if (tab && tab[1]) {
    text = tab + 1;
  } else {
    while (*text == ' ' || *text == '\t') text++;
    const char *q = text;
    int bytes_found = 0;
    while (isxdigit((unsigned char)q[0]) && isxdigit((unsigned char)q[1])) {
      bytes_found = 1;
      q += 2;
      while (*q == ' ' || *q == '\t') q++;
    }
    if (bytes_found && *q) {
      text = q;
    }
    while (*text == ' ' || *text == '\t') text++;
  }
  if (out_addr) {
    *out_addr = (uint64_t)addr;
  }
  if (out_text) {
    *out_text = text;
  }
  return 1;
}

static int
dasm_preloadFromCore(void)
{
  geo_debug_rom_region_t p1 = {0};
  if (!libretro_host_debugGetP1Rom(&p1)) {
    debug_error("dasm: P1 ROM unavailable from core");
    return 0;
  }
  if (!p1.data || p1.size == 0) {
    debug_error("dasm: empty P1 ROM from core");
    return 0;
  }
  {
    char test_buf[128];
    size_t test_len = 0;
    if (!libretro_host_debugDisassembleQuick(0, test_buf, sizeof(test_buf), &test_len) || test_len == 0) {
      debug_error("dasm: core disassembler unavailable");
      return 0;
    }
  }
  dasm_clear();
  uint32_t limit = (p1.size > 0x00ffffffu) ? 0x00ffffffu : (uint32_t)p1.size;
  g_dasm.addr_hex_width = (limit > 0x00ffffffu) ? 8 : 6;
  uint32_t addr = 0;
  char buf[128];
  while (addr < limit) {
    size_t ins_len = 0;
    buf[0] = '\0';
    if (!libretro_host_debugDisassembleQuick(addr, buf, sizeof(buf), &ins_len) || ins_len == 0) {
      ins_len = 2;
    }
    if (ins_len > 0x1000u) {
      ins_len = 2;
    }
    dasm_push(addr, buf);
    if (ins_len == 0) {
      break;
    }
    addr += (uint32_t)ins_len;
  }
  g_dasm.ready = (g_dasm.n > 0);
  return g_dasm.ready ? 1 : 0;
}

static int
dasm_geo_preloadText(void)
{
  uint64_t lo=0, hi=0;
  const char *elf = debugger.libretro.elfPath;
  if (!elf || !*elf) {
    return dasm_preloadFromCore();
  }
  if (!elfutil_getTextBounds(elf, &lo, &hi) || lo == 0 || hi <= lo) {
    debug_error("dasm: failed to read .text bounds from ELF (%s); falling back to core", elf);
    return dasm_preloadFromCore();
  }
  dasm_clear();
  char objdump[PATH_MAX];
  if (!debugger_toolchainBuildBinary(objdump, sizeof(objdump), "objdump")) {
    debug_error("dasm: failed to resolve objdump binary");
    return dasm_preloadFromCore();
  }
  char cmd[PATH_MAX + 256];
  snprintf(cmd, sizeof(cmd),
	   "%s -d -z -j .text --start-address=0x%llx --stop-address=0x%llx %s",
	   objdump, (unsigned long long)lo, (unsigned long long)hi, elf);
  FILE *pipe = popen(cmd, "r");
  if (!pipe) {
    debug_error("dasm: failed to run %s: %s", objdump, strerror(errno));
    return dasm_preloadFromCore();
  }
  int width = (hi > 0xFFFFFFFFull) ? 16 : 8;
  g_dasm.addr_hex_width = width;
  char *line = NULL;
  size_t cap = 0;
  ssize_t read = 0;
  while ((read = getline(&line, &cap, pipe)) != -1) {
    while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r')) {
      line[--read] = '\0';
    }
    if (read == 0) {
      continue;
    }
    uint64_t a = 0;
    const char *text = NULL;
    if (dasm_parseLine(line, &a, &text)) {
      dasm_push(a, text);
    }
  }
  if (line) {
    free(line);
  }
  if (pclose(pipe) == -1) {
    debug_error("dasm: failed to close %s pipe: %s", objdump, strerror(errno));
  }
  g_dasm.ready = (g_dasm.n > 0);
  if (!g_dasm.ready) {
    debug_error("dasm: disassembly capture returned no parsed lines");
    return dasm_preloadFromCore();
  }
  return 1;
}

static int
dasm_geo_getTotal(void)
{
  return g_dasm.n;
}

static int
dasm_geo_getAddrHexWidth(void)
{
  return g_dasm.addr_hex_width ? g_dasm.addr_hex_width : 8;
}

static int
dasm_lowerBoundAddr(uint64_t addr)
{
  int lo = 0, hi = g_dasm.n;
  while (lo < hi) {
    int mid = lo + (hi - lo)/2;
    if (g_dasm.addrs[mid] < addr) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static int
dasm_geo_findIndexForAddr(uint64_t addr, int *out_index)
{
  if (g_dasm.n <= 0) {
    return 0;
  }
  int lb = dasm_lowerBoundAddr(addr);
  int idx = lb;
  if (lb >= g_dasm.n) {
    idx = g_dasm.n - 1;
  } else if (g_dasm.addrs[lb] != addr && lb > 0) {
    idx = lb - 1;
  }
  if (out_index) {
    *out_index = idx;
  }
  return 1;
}

static int
dasm_geo_getRangeByIndex(int start_index, int end_index,
                     const char ***out_lines,
                     const uint64_t **out_addrs,
                     int *out_first_index,
                     int *out_count)
{
  if (g_dasm.n <= 0 || !out_lines || !out_addrs || !out_first_index || !out_count) {
    return 0;
  }
  if (start_index < 0) {
    start_index = 0;
  }
  if (end_index < start_index) {
    end_index = start_index;
  }
  if (start_index >= g_dasm.n) {
    start_index = g_dasm.n - 1;
  }
  if (end_index >= g_dasm.n) {
    end_index = g_dasm.n - 1;
  }
  *out_lines = (const char**)(g_dasm.lines + start_index);
  *out_addrs = (const uint64_t*)(g_dasm.addrs + start_index);
  *out_first_index = start_index;
  *out_count = end_index - start_index + 1;
  return 1;
}

const dasm_iface_t
dasm_geo_iface = {
    .flags = DASM_IFACE_FLAG_FINITE_TOTAL,
    .init = dasm_geo_init,
    .shutdown = dasm_geo_shutdown,
    .preloadText = dasm_geo_preloadText,
    .getTotal = dasm_geo_getTotal,
    .getAddrHexWidth = dasm_geo_getAddrHexWidth,
    .findIndexForAddr = dasm_geo_findIndexForAddr,
    .getRangeByIndex = dasm_geo_getRangeByIndex,
};
