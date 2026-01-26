/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <errno.h>
#include "console_cmd.h"
#include "debugger.h"
#include "config.h"
#include "alloc.h"
#include "breakpoints.h"
#include "libretro_host.h"
#include "transition.h"
#include "machine.h"
#include "print_eval.h"
#include "train.h"
#include "state_buffer.h"
#include "protect.h"


typedef int (*console_cmd_handler_t)(int argc, char **argv);
typedef int (*console_cmd_complete_t)(const char *prefix, char ***out_list, int *out_count);

typedef struct console_cmd_entry {
    const char *name;
    const char *shortcut;
    const char *usage;
    const char *help;
    console_cmd_handler_t handler;
    console_cmd_complete_t complete;
} console_cmd_entry_t;

static int console_cmd_help(int argc, char **argv);
static int console_cmd_break(int argc, char **argv);
static int console_cmd_completeBreak(const char *prefix, char ***out_list, int *out_count);
static int console_cmd_continue(int argc, char **argv);
static int console_cmd_cls(int argc, char **argv);
static int console_cmd_step(int argc, char **argv);
static int console_cmd_stepi(int argc, char **argv);
static int console_cmd_next(int argc, char **argv);
static int console_cmd_write(int argc, char **argv);
static int console_cmd_completeWrite(const char *prefix, char ***outList, int *outCount);
static int console_cmd_transition(int argc, char **argv);
static int console_cmd_completeTransition(const char *prefix, char ***out_list, int *out_count);
static int console_cmd_print(int argc, char **argv);
static int console_cmd_completePrint(const char *prefix, char ***outList, int *outCount);
static int console_cmd_watch(int argc, char **argv);
static int console_cmd_train(int argc, char **argv);
static int console_cmd_loop(int argc, char **argv);
static int console_cmd_protect(int argc, char **argv);
static int console_cmd_diff(int argc, char **argv);

static const console_cmd_entry_t console_cmd[] = {
    { "help",  "h", "help [command]", "Show available commands or detailed help.", console_cmd_help, NULL },
    { "break", "b", "break <addr|symbol|file:line>", "Set a breakpoint at an address, symbol, or file:line.", console_cmd_break, console_cmd_completeBreak },    
    { "cls",  NULL, "cls", "Clear the console output.", console_cmd_cls, NULL },    
    { "continue", "c", "continue", "Continue execution and defocus the prompt.", console_cmd_continue, NULL },
    { "diff", NULL, "diff <fromFrame> <toFrame> [size=8|16|32]", "Show RAM addresses that differ between two recorded frames.", console_cmd_diff, NULL },    
    { "loop", NULL, "loop <from> <to>\nloop\nloop clear", "Loop between two recorded frame numbers (decimal).", console_cmd_loop, NULL },
    { "print", "p", "print <expr>", "Print an expression using DWARF + symbol info.", console_cmd_print, console_cmd_completePrint },   
    { "protect", NULL, "protect\nprotect clear\nprotect del <addr> [size=8|16|32]\nprotect <addr> block [size=8|16|32]\nprotect <addr> set=0x... [size=8|16|32]", "Protect addresses by blocking writes or forcing a value (core-side).", console_cmd_protect, NULL },    
    { "next", "n", "next", "Step over the next line.", console_cmd_next, NULL },    
    { "step", "s", "step", "Step to next source line.", console_cmd_step, NULL },
    { "stepi", "i", "stepi", "Step one instruction.", console_cmd_stepi, NULL },
    { "train", NULL, "train <from> <to> [size=8|16|32]\ntrain ignore\ntrain clear", "Train by breaking on a value transition (from/to accept decimal or 0x...).", console_cmd_train, NULL },    
    { "transition", NULL, "transition <slide|explode|doom|flip|rbar|random|cycle|none>", "Set the transition mode for startup and fullscreen.", console_cmd_transition, console_cmd_completeTransition },
    { "watch", "wa", "watch [addr] [r|w|rw] [size=8|16|32] [mask=0x...] [val=0x...] [old=0x...] [diff=0x...]\nwatch del <idx> \nwatch clear", "Set or list watchpoints.", console_cmd_watch, NULL },    
    { "write", NULL, "write <dest> <value>", "Write a hex value to an address or symbol.", console_cmd_write, console_cmd_completeWrite },

};

typedef struct console_completion {
    char **items;
    int count;
    int cap;
} console_completion_t;

static void
console_cmd_completionAdd(console_completion_t *list, const char *s)
{
    if (!list || !s || !*s) {
        return;
    }
    if (list->count >= list->cap) {
        int next = list->cap ? list->cap * 2 : 32;
        char **tmp = (char**)alloc_realloc(list->items, (size_t)next * sizeof(char*));
        if (!tmp) {
            return;
        }
        list->items = tmp;
        list->cap = next;
    }
    size_t len = strlen(s);
    char *dup = (char*)alloc_alloc(len + 1);
    if (!dup) {
        return;
    }
    memcpy(dup, s, len + 1);
    list->items[list->count++] = dup;
}

static int
console_cmd_tokenize(char *buf, char **argv, int cap)
{
    int argc = 0;
    char *p = buf;
    while (p && *p) {
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }
        if (!*p) {
            break;
        }
        if (argc < cap) {
            argv[argc++] = p;
        }
        while (*p && !isspace((unsigned char)*p)) {
            ++p;
        }
        if (*p) {
            *p++ = '\0';
        }
    }
    return argc;
}

static const console_cmd_entry_t *
console_cmd_find(const char *name)
{
    if (!name || !*name) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(console_cmd) / sizeof(console_cmd[0]); ++i) {
        const console_cmd_entry_t *cmd = &console_cmd[i];
        if ((cmd->name && strcasecmp(cmd->name, name) == 0) ||
            (cmd->shortcut && strcasecmp(cmd->shortcut, name) == 0)) {
            return cmd;
        }
    }
    return NULL;
}

static int
console_cmd_parseHex(const char *s, uint32_t *out)
{
    if (!s || !*s || !out) {
        return 0;
    }
    const char *p = s;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    if (!*p) {
        return 0;
    }
    for (const char *q = p; *q; ++q) {
        if (!isxdigit((unsigned char)*q)) {
            return 0;
        }
    }
    errno = 0;
    unsigned long v = strtoul(s, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *out = (uint32_t)(v & 0x00ffffffu);
    return 1;
}

static int
console_cmd_parseHexStrict(const char *s, uint64_t *out, int *outDigits)
{
    if (out) {
        *out = 0;
    }
    if (outDigits) {
        *outDigits = 0;
    }
    if (!s || !*s || !out || !outDigits) {
        return 0;
    }
    if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) {
        return 0;
    }
    const char *p = s + 2;
    if (!*p) {
        return 0;
    }
    int digits = 0;
    while (*p) {
        if (!isxdigit((unsigned char)*p)) {
            return 0;
        }
        ++digits;
        ++p;
    }
    errno = 0;
    unsigned long long v = strtoull(s, NULL, 16);
    if (errno != 0) {
        return 0;
    }
    *out = (uint64_t)v;
    *outDigits = digits;
    return 1;
}

static int
console_cmd_parseU32Strict(const char *s, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    uint64_t v = 0;
    int digits = 0;
    if (!console_cmd_parseHexStrict(s, &v, &digits)) {
        return 0;
    }
    if (v > 0xffffffffULL) {
        return 0;
    }
    if (out) {
        *out = (uint32_t)v;
    }
    return 1;
}

static int
console_cmd_parseU32Auto(const char *s, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    if (!s || !*s || !out) {
        return 0;
    }
    if (s[0] == '-') {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno != 0 || !end || end == s || *end != '\0') {
        return 0;
    }
    if (v > 0xffffffffULL) {
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int
console_cmd_parseU64Dec(const char *s, uint64_t *out)
{
    if (out) {
        *out = 0;
    }
    if (!s || !*s || !out) {
        return 0;
    }
    for (const char *p = s; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    errno = 0;
    unsigned long long v = strtoull(s, NULL, 10);
    if (errno != 0) {
        return 0;
    }
    *out = (uint64_t)v;
    return 1;
}

static size_t
console_cmd_sizeFromHexDigits(int digits)
{
    if (digits <= 0) {
        return 0;
    }
    if (digits <= 2) {
        return 1;
    }
    if (digits <= 4) {
        return 2;
    }
    if (digits <= 8) {
        return 4;
    }
    return 0;
}

static int
console_cmd_parseSizeBitsOpt(const char *tok, uint32_t *out_size_bits)
{
    if (!tok || !out_size_bits) {
        return 0;
    }
    if (strncasecmp(tok, "size=", 5) != 0) {
        return 0;
    }
    int sz = atoi(tok + 5);
    if (sz != 8 && sz != 16 && sz != 32) {
        return -1;
    }
    *out_size_bits = (uint32_t)sz;
    return 1;
}

static const char *
console_cmd_basename(const char *path)
{
    if (!path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static int
console_cmd_fileMatches(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    if (strcmp(a, b) == 0) {
        return 1;
    }
    const char *ba = console_cmd_basename(a);
    const char *bb = console_cmd_basename(b);
    if (!ba || !bb) {
        return 0;
    }
    return strcmp(ba, bb) == 0;
}

static int
console_cmd_symbolMatch(const char *name, const char *symbol)
{
    if (!name || !symbol) {
        return 0;
    }
    if (strcmp(name, symbol) == 0) {
        return 1;
    }
    size_t base_len = strlen(symbol);
    if (base_len == 0) {
        return 0;
    }
    if (strncmp(name, symbol, base_len) == 0) {
        char next = name[base_len];
        if (next == '.' || next == '$') {
            return 1;
        }
    }
    if (symbol[0] != '_' && name[0] == '_' && strcmp(name + 1, symbol) == 0) {
        return 1;
    }
    return 0;
}

static int
console_cmd_resolveSymbol(const char *elf, const char *symbol, uint32_t *out_addr)
{
    if (!elf || !*elf || !symbol || !*symbol || !out_addr) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "m68k-neogeo-elf-objdump --syms '%s'", elf);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[8];
        int count = 0;
        char *cursor = line;
        while (count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (!*cursor) {
                break;
            }
            tokens[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (count < 2) {
            continue;
        }
        const char *name = tokens[count - 1];
        if (!console_cmd_symbolMatch(name, symbol)) {
            continue;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(tokens[0], &addr)) {
            continue;
        }
        *out_addr = addr;
        pclose(fp);
        return 1;
    }
    pclose(fp);
    return 0;
}

static int
console_cmd_resolveFileLine(const char *elf, const char *file, int line_no, uint32_t *out_addr)
{
    if (!elf || !*elf || !file || !*file || line_no <= 0 || !out_addr) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "m68k-neogeo-elf-objdump -l -d '%s'", elf);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        debug_error("break: failed to run objdump");
        return 0;
    }
    char line[1024];
    int want_addr = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        char *colon = strrchr(line, ':');
        if (colon && isdigit((unsigned char)colon[1])) {
            int ln = atoi(colon + 1);
            if (ln == line_no) {
                *colon = '\0';
                if (console_cmd_fileMatches(line, file)) {
                    want_addr = 1;
                    continue;
                }
            }
        }
        if (want_addr) {
            char *p = line;
            while (*p && isspace((unsigned char)*p)) {
                ++p;
            }
            if (isxdigit((unsigned char)*p)) {
                char *end = p;
                while (*end && isxdigit((unsigned char)*end)) {
                    ++end;
                }
                if (*end == ':') {
                    char addr_buf[32];
                    size_t len = (size_t)(end - p);
                    if (len < sizeof(addr_buf)) {
                        memcpy(addr_buf, p, len);
                        addr_buf[len] = '\0';
                        uint32_t addr = 0;
                        if (console_cmd_parseHex(addr_buf, &addr)) {
                            *out_addr = addr;
                            pclose(fp);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    pclose(fp);
    return 0;
}

static int
console_cmd_help(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Commands:\n");
        for (size_t i = 0; i < sizeof(console_cmd) / sizeof(console_cmd[0]); ++i) {
            const console_cmd_entry_t *cmd = &console_cmd[i];
            if (cmd->shortcut) {
                debug_printf("  %s (%s)\n", cmd->name, cmd->shortcut, cmd->usage);
            } else {
                debug_printf("  %s\n", cmd->name, cmd->usage);
            }
        }
        return 1;
    }
    const console_cmd_entry_t *cmd = console_cmd_find(argv[1]);
    if (!cmd) {
        debug_error("help: unknown command '%s'", argv[1]);
        return 0;
    }
    debug_printf("\n%s\n\n%s\n", cmd->help, cmd->usage);
    return 1;
}

static int
console_cmd_break(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Usage: break <addr|symbol|file:line>\n");
        return 0;
    }
    const char *elf = debugger.config.elfPath;
    if (!elf || !*elf) {
        debug_error("break: no ELF path configured (set --elf or Settings)");
        return 0;
    }
    const char *arg = argv[1];
    uint32_t addr = 0;
    int ok = 0;
    const char *colon = strrchr(arg, ':');
    if (colon && colon[1]) {
        int line_no = atoi(colon + 1);
        if (line_no > 0) {
            size_t len = (size_t)(colon - arg);
            char file_buf[PATH_MAX];
            if (len < sizeof(file_buf)) {
                memcpy(file_buf, arg, len);
                file_buf[len] = '\0';
                ok = console_cmd_resolveFileLine(elf, file_buf, line_no, &addr);
            }
        }
    }
    if (!ok) {
        ok = console_cmd_parseHex(arg, &addr);
    }
    if (!ok) {
        ok = console_cmd_resolveSymbol(elf, arg, &addr);
    }
    if (!ok) {
        debug_error("break: failed to resolve '%s'", arg);
        return 0;
    }
    machine_breakpoint_t *bp = machine_addBreakpoint(&debugger.machine, addr, 1);
    if (!bp) {
        debug_error("break: failed to add breakpoint");
        return 0;
    }
    breakpoints_resolveLocation(bp);
    libretro_host_debugAddBreakpoint(addr);
    breakpoints_markDirty();
    debug_printf("break: added at 0x%06X\n", addr);
    return 1;
}

static int
console_cmd_watchList(void)
{
    geo_debug_watchpoint_t wps[GEO_WATCHPOINT_COUNT];
    memset(wps, 0, sizeof(wps));
    size_t count = 0;
    if (!libretro_host_debugReadWatchpoints(wps, GEO_WATCHPOINT_COUNT, &count)) {
        debug_error("watch: libretro core does not expose watchpoints");
        return 0;
    }
    uint64_t enabled = 0;
    (void)libretro_host_debugGetWatchpointEnabledMask(&enabled);

    debug_printf("Watchpoints (enabled=0x%016llX):\n", (unsigned long long)enabled);
    for (size_t i = 0; i < count; ++i) {
        const geo_debug_watchpoint_t *wp = &wps[i];
        int is_enabled = ((enabled >> i) & 1ull) ? 1 : 0;
        if (!is_enabled && wp->op_mask == 0) {
            continue;
        }

        const char *rw = "";
        if ((wp->op_mask & GEO_WATCH_OP_READ) && (wp->op_mask & GEO_WATCH_OP_WRITE)) {
            rw = "rw";
        } else if (wp->op_mask & GEO_WATCH_OP_READ) {
            rw = "r";
        } else if (wp->op_mask & GEO_WATCH_OP_WRITE) {
            rw = "w";
        }

        debug_printf("  [%02u] %s addr=0x%06X op=0x%08X %s",
                     (unsigned)i, is_enabled ? "on " : "off",
                     (unsigned)(wp->addr & 0x00ffffffu), (unsigned)wp->op_mask, rw);
        if (wp->op_mask & GEO_WATCH_OP_ACCESS_SIZE) {
            debug_printf(" size=%u", (unsigned)wp->size_operand);
        }
        if (wp->op_mask & GEO_WATCH_OP_ADDR_COMPARE_MASK) {
            debug_printf(" mask=0x%08X", (unsigned)wp->addr_mask_operand);
        }
        if (wp->op_mask & GEO_WATCH_OP_VALUE_EQ) {
            debug_printf(" val=0x%08X", (unsigned)wp->value_operand);
        }
        if (wp->op_mask & GEO_WATCH_OP_OLD_VALUE_EQ) {
            debug_printf(" old=0x%08X", (unsigned)wp->old_value_operand);
        }
        if (wp->op_mask & GEO_WATCH_OP_VALUE_NEQ_OLD) {
            debug_printf(" diff=0x%08X", (unsigned)wp->diff_operand);
        }
        debug_printf("\n");
    }

    return 1;
}

static int
console_cmd_watch(int argc, char **argv)
{
    if (argc < 2) {
        return console_cmd_watchList();
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        if (!libretro_host_debugResetWatchpoints()) {
            debug_error("watch: libretro core does not expose watchpoints");
            return 0;
        }
        debug_printf("watch: cleared\n");
        return 1;
    }

    if (strcasecmp(argv[1], "del") == 0 || strcasecmp(argv[1], "rm") == 0 || strcasecmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            debug_printf("Usage: watch del <idx>\n");
            return 0;
        }
        char *end = NULL;
        unsigned long idx = strtoul(argv[2], &end, 0);
        if (!end || *end != '\0') {
            debug_error("watch: invalid index '%s'", argv[2]);
            return 0;
        }
        if (!libretro_host_debugRemoveWatchpoint((uint32_t)idx)) {
            debug_error("watch: remove failed (unsupported?)");
            return 0;
        }
        debug_printf("watch: removed %lu\n", idx);
        return 1;
    }

    uint32_t addr = 0;
    if (!console_cmd_parseHex(argv[1], &addr)) {
        debug_error("watch: expected address, got '%s'", argv[1]);
        return 0;
    }

    uint32_t op_mask = 0;
    uint32_t diff_operand = 0;
    uint32_t value_operand = 0;
    uint32_t old_value_operand = 0;
    uint32_t size_operand = 0;
    uint32_t addr_mask_operand = 0;
    int have_rw = 0;

    for (int i = 2; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strcasecmp(tok, "r") == 0 || strcasecmp(tok, "read") == 0) {
            op_mask |= GEO_WATCH_OP_READ;
            have_rw = 1;
            continue;
        }
        if (strcasecmp(tok, "w") == 0 || strcasecmp(tok, "write") == 0) {
            op_mask |= GEO_WATCH_OP_WRITE;
            have_rw = 1;
            continue;
        }
        if (strcasecmp(tok, "rw") == 0 || strcasecmp(tok, "wr") == 0) {
            op_mask |= (GEO_WATCH_OP_READ | GEO_WATCH_OP_WRITE);
            have_rw = 1;
            continue;
        }
        if (strncasecmp(tok, "size=", 5) == 0) {
            int sz = atoi(tok + 5);
            if (sz != 8 && sz != 16 && sz != 32) {
                debug_error("watch: invalid size '%s' (expected 8/16/32)", tok + 5);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_ACCESS_SIZE;
            size_operand = (uint32_t)sz;
            continue;
        }
        if (strncasecmp(tok, "mask=", 5) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 5, &v)) {
                debug_error("watch: invalid mask '%s' (expected 0x...)", tok + 5);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_ADDR_COMPARE_MASK;
            addr_mask_operand = v;
            continue;
        }
        if (strncasecmp(tok, "val=", 4) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 4, &v)) {
                debug_error("watch: invalid val '%s' (expected 0x...)", tok + 4);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_VALUE_EQ;
            value_operand = v;
            continue;
        }
        if (strncasecmp(tok, "value=", 6) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 6, &v)) {
                debug_error("watch: invalid value '%s' (expected 0x...)", tok + 6);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_VALUE_EQ;
            value_operand = v;
            continue;
        }
        if (strncasecmp(tok, "old=", 4) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 4, &v)) {
                debug_error("watch: invalid old '%s' (expected 0x...)", tok + 4);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_OLD_VALUE_EQ;
            old_value_operand = v;
            continue;
        }
        if (strncasecmp(tok, "diff=", 5) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 5, &v)) {
                debug_error("watch: invalid diff '%s' (expected 0x...)", tok + 5);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_VALUE_NEQ_OLD;
            diff_operand = v;
            continue;
        }
        if (strncasecmp(tok, "neq=", 4) == 0) {
            uint32_t v = 0;
            if (!console_cmd_parseU32Strict(tok + 4, &v)) {
                debug_error("watch: invalid neq '%s' (expected 0x...)", tok + 4);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_VALUE_NEQ_OLD;
            diff_operand = v;
            continue;
        }

        debug_error("watch: unknown option '%s'", tok);
        return 0;
    }

    if (!have_rw) {
        op_mask |= (GEO_WATCH_OP_READ | GEO_WATCH_OP_WRITE);
    }

    uint32_t index = 0;
    if (!libretro_host_debugAddWatchpoint(addr, op_mask, diff_operand, value_operand, old_value_operand, size_operand, addr_mask_operand, &index)) {
        debug_error("watch: failed to add (table full or unsupported)");
        return 0;
    }
    debug_printf("watch: added [%u] at 0x%06X\n", (unsigned)index, (unsigned)addr);
    return 1;
}

static int
console_cmd_train(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Usage: train <from> <to> [size=8|16|32] | train ignore | train clear\n");
        return 0;
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        train_clearIgnoreList();
        debug_printf("train: ignore list cleared\n");
        return 1;
    }

    if (strcasecmp(argv[1], "ignore") == 0) {
        uint32_t addr24 = 0;
        if (!train_getLastWatchbreakAddr(&addr24)) {
            debug_error("train: no watchbreak to ignore yet");
            return 0;
        }
        if (!train_addIgnoreAddr(addr24)) {
            debug_error("train: ignore list full");
            return 0;
        }
        debug_printf("train: ignoring 0x%06X\n", (unsigned)addr24);
        return 1;
    }

    if (argc < 3) {
        debug_printf("Usage: train <from> <to> [size=8|16|32]\n");
        return 0;
    }

    uint32_t from = 0;
    uint32_t to = 0;
    if (!console_cmd_parseU32Auto(argv[1], &from)) {
        debug_error("train: invalid from '%s' (expected decimal or 0x...)", argv[1]);
        return 0;
    }
    if (!console_cmd_parseU32Auto(argv[2], &to)) {
        debug_error("train: invalid to '%s' (expected decimal or 0x...)", argv[2]);
        return 0;
    }

    uint32_t op_mask = 0;
    uint32_t size_operand = 0;
    for (int i = 3; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strncasecmp(tok, "size=", 5) == 0) {
            int sz = atoi(tok + 5);
            if (sz != 8 && sz != 16 && sz != 32) {
                debug_error("train: invalid size '%s' (expected 8/16/32)", tok + 5);
                return 0;
            }
            op_mask |= GEO_WATCH_OP_ACCESS_SIZE;
            size_operand = (uint32_t)sz;
            continue;
        }
        debug_error("train: unknown option '%s'", tok);
        return 0;
    }

    // Any address: enable address compare mask with mask=0 (always matches).
    op_mask |= GEO_WATCH_OP_ADDR_COMPARE_MASK;
    op_mask |= GEO_WATCH_OP_WRITE;
    op_mask |= GEO_WATCH_OP_OLD_VALUE_EQ;
    op_mask |= GEO_WATCH_OP_VALUE_EQ;

    uint32_t index = 0;
    if (!libretro_host_debugAddWatchpoint(0, op_mask, 0, to, from, size_operand, 0, &index)) {
        debug_error("train: failed to add watchpoint (table full or unsupported)");
        return 0;
    }
    train_setWatchIndex(index);

    debug_printf("train: watchpoint [%u] old=0x%08X -> val=0x%08X\n", (unsigned)index, (unsigned)from, (unsigned)to);
    return 1;
}

static int
console_cmd_loop(int argc, char **argv)
{
    if (argc < 2) {
        if (!debugger.loopEnabled) {
            debug_printf("loop: disabled\n");
            return 1;
        }
        debug_printf("loop: enabled from=%llu to=%llu\n",
                     (unsigned long long)debugger.loopFrom,
                     (unsigned long long)debugger.loopTo);
        return 1;
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        debugger.loopEnabled = 0;
        debugger.loopFrom = 0;
        debugger.loopTo = 0;
        debug_printf("loop: cleared\n");
        return 1;
    }

    if (argc < 3) {
        debug_printf("Usage: loop <from> <to>\n");
        return 0;
    }

    uint64_t from = 0;
    uint64_t to = 0;
    if (!console_cmd_parseU64Dec(argv[1], &from)) {
        debug_error("loop: invalid from '%s' (expected decimal integer)", argv[1]);
        return 0;
    }
    if (!console_cmd_parseU64Dec(argv[2], &to)) {
        debug_error("loop: invalid to '%s' (expected decimal integer)", argv[2]);
        return 0;
    }
    if (from >= to) {
        debug_error("loop: expected from < to");
        return 0;
    }

    if (!state_buffer_hasFrameNo(from)) {
        debug_error("loop: from frame %llu not in state buffer", (unsigned long long)from);
        return 0;
    }
    if (!state_buffer_hasFrameNo(to)) {
        debug_error("loop: to frame %llu not in state buffer", (unsigned long long)to);
        return 0;
    }

    debugger.loopEnabled = 1;
    debugger.loopFrom = from;
    debugger.loopTo = to;

    return 1;
}

static int
console_cmd_protect(int argc, char **argv)
{
    if (argc < 2) {
        protect_debugList();
        return 1;
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        protect_clear();
        debug_printf("protect: cleared\n");
        return 1;
    }

    if (strcasecmp(argv[1], "del") == 0 || strcasecmp(argv[1], "rm") == 0 || strcasecmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            debug_printf("Usage: protect del <addr> [size=8|16|32]\n");
            return 0;
        }
        uint32_t addr = 0;
        if (!console_cmd_parseHex(argv[2], &addr)) {
            debug_error("protect: invalid address '%s'", argv[2]);
            return 0;
        }
        uint32_t size_bits = 8;
        for (int i = 3; i < argc; ++i) {
            int r = console_cmd_parseSizeBitsOpt(argv[i], &size_bits);
            if (r == 1) {
                continue;
            }
            if (r == -1) {
                debug_error("protect: invalid size '%s'", argv[i]);
                return 0;
            }
            debug_error("protect: unknown option '%s'", argv[i]);
            return 0;
        }
        if (!protect_remove(addr, size_bits)) {
            debug_error("protect: not found");
            return 0;
        }
        debug_printf("protect: removed\n");
        return 1;
    }

    uint32_t addr = 0;
    if (!console_cmd_parseHex(argv[1], &addr)) {
        debug_error("protect: invalid address '%s'", argv[1]);
        return 0;
    }

    uint32_t size_bits = 8;
    int mode_set = 0;
    uint32_t set_value = 0;
    int mode_block = 0;

    for (int i = 2; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strcasecmp(tok, "block") == 0 || strcasecmp(tok, "deny") == 0) {
            mode_block = 1;
            continue;
        }
        if (strncasecmp(tok, "set=", 4) == 0) {
            if (!console_cmd_parseU32Strict(tok + 4, &set_value)) {
                debug_error("protect: invalid set value '%s' (expected 0x...)", tok + 4);
                return 0;
            }
            mode_set = 1;
            continue;
        }
        if (strncasecmp(tok, "value=", 6) == 0) {
            if (!console_cmd_parseU32Strict(tok + 6, &set_value)) {
                debug_error("protect: invalid value '%s' (expected 0x...)", tok + 6);
                return 0;
            }
            mode_set = 1;
            continue;
        }
        int r = console_cmd_parseSizeBitsOpt(tok, &size_bits);
        if (r == 1) {
            continue;
        }
        if (r == -1) {
            debug_error("protect: invalid size '%s'", tok);
            return 0;
        }

        debug_error("protect: unknown option '%s'", tok);
        return 0;
    }

    if (mode_set && mode_block) {
        debug_error("protect: choose either block or set=...");
        return 0;
    }
    if (!mode_set && !mode_block) {
        debug_printf("Usage: protect <addr> block [size=8|16|32]\nprotect <addr> set=0x... [size=8|16|32]\n");
        return 0;
    }

    int ok = 0;
    if (mode_block) {
        ok = protect_addBlock(addr, size_bits);
    } else {
        ok = protect_addSet(addr, set_value, size_bits);
    }
    if (!ok) {
        debug_error("protect: failed (core protect API missing?)");
        return 0;
    }
    debug_printf("protect: added\n");
    return 1;
}

static int
console_cmd_continue(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (libretro_host_debugResume()) {
        machine_setRunning(&debugger.machine, 1);
        e9ui_setFocus(&debugger.ui.ctx, NULL);
        return 1;
    }
    debug_error("continue: resume failed");
    return 0;
}

static int
console_cmd_cls(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    linebuf_clear(&debugger.console);
    debugger.consoleScrollLines = 0;
    return 1;
}

static int
console_cmd_step(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepLine()) {
        machine_setRunning(&debugger.machine, 1);
        return 1;
    }
    debug_error("step line: libretro core does not expose debug step line");
    return 0;
}

static int
console_cmd_stepi(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepInstr()) {
        machine_setRunning(&debugger.machine, 1);
        return 1;
    }
    debug_error("step instruction: libretro core does not expose debug step");
    return 0;
}

static int
console_cmd_next(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debugger_suppressBreakpointAtPC();
    if (libretro_host_debugStepNext()) {
        machine_setRunning(&debugger.machine, 1);
        return 1;
    }
    debug_error("step next: libretro core does not expose debug next");
    return 0;
}

static int
console_cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        debug_printf("Usage: write <dest> <value>\n");
        return 0;
    }
    const char *dest = argv[1];
    const char *value_str = argv[2];
    uint64_t value = 0;
    int value_digits = 0;
    if (!console_cmd_parseHexStrict(value_str, &value, &value_digits)) {
        debug_error("write: value must be hex (0x...)");
        return 0;
    }
    size_t value_size = console_cmd_sizeFromHexDigits(value_digits);
    if (value_size == 0) {
        debug_error("write: value too wide (max 32-bit hex)");
        return 0;
    }
    if (dest && dest[0] == '0' && (dest[1] == 'x' || dest[1] == 'X')) {
        uint64_t addr64 = 0;
        int addr_digits = 0;
        if (!console_cmd_parseHexStrict(dest, &addr64, &addr_digits)) {
            debug_error("write: address must be hex (0x...)");
            return 0;
        }
        if (addr64 > 0xffffffffu) {
            debug_error("write: address out of range");
            return 0;
        }
        uint32_t addr = (uint32_t)addr64;
        if (!libretro_host_debugWriteMemory(addr, (uint32_t)value, value_size)) {
            debug_error("write: failed to write 0x%llX to 0x%08X",
                        (unsigned long long)value, addr);
            return 0;
        }
        debug_printf("%s = 0x%llX (%zu bits)\n", dest, (unsigned long long)value, value_size * 8);
        return 1;
    }
    uint32_t sym_addr = 0;
    size_t sym_size = 0;
    if (!print_eval_resolveAddress(dest, &sym_addr, &sym_size)) {
        debug_error("write: unknown symbol '%s'", dest);
        return 0;
    }
    if (sym_size > 4) {
        debug_error("write: can't write to %s (size %zu); use \"write 0x%08X %s\" to write the address directly",
                    dest, sym_size, sym_addr, value_str);
        return 0;
    }
    if (value_digits > (int)(sym_size * 2)) {
        debug_error("write: value too large for %s (%zu bytes)", dest, sym_size);
        return 0;
    }
    if (!libretro_host_debugWriteMemory(sym_addr, (uint32_t)value, sym_size)) {
        debug_error("write: failed to write 0x%llX to %s", (unsigned long long)value, dest);
        return 0;
    }
    debug_printf("%s = 0x%llX (%zu bits)\n", dest, (unsigned long long)value, sym_size * 8);
    return 1;
}

static int
console_cmd_transition(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("transition: %s\n", transition_modeName(debugger.transitionMode));
        debug_printf("Usage: transition <slide|explode|doom|flip|rbar|random|cycle|none>\n");
        return 1;
    }
    e9k_transition_mode_t mode = e9k_transition_none;
    if (!transition_parseMode(argv[1], &mode)) {
        debug_error("transition: unknown mode '%s'", argv[1]);
        return 0;
    }
    debugger.transitionMode = mode;
    debugger.transitionFullscreenModeSet = 0;
    config_saveConfig();
    debug_printf("transition: %s\n", transition_modeName(debugger.transitionMode));
    return 1;
}

static int
console_cmd_print(int argc, char **argv)
{
    if (argc < 2) {
        debug_printf("Usage: print <expr>\n");
        return 0;
    }
    char expr[512];
    expr[0] = '\0';
    size_t used = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!arg) {
            continue;
        }
        size_t len = strlen(arg);
        if (used > 0 && used < sizeof(expr) - 1) {
            expr[used++] = ' ';
            expr[used] = '\0';
        }
        if (used + len >= sizeof(expr)) {
            len = sizeof(expr) - 1 - used;
        }
        memcpy(expr + used, arg, len);
        used += len;
        expr[used] = '\0';
    }
    if (expr[0] == '\0') {
        debug_printf("Usage: print <expr>\n");
        return 0;
    }

    // Fast-path simple numeric expressions so `print *0xADDR` works without an ELF.
    {
        const char *p = expr;
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }

        int deref = 0;
        if (*p == '*') {
            deref = 1;
            ++p;
        }

        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }

        int paren = 0;
        if (*p == '(') {
            paren = 1;
            ++p;
            while (*p && isspace((unsigned char)*p)) {
                ++p;
            }
        }

        errno = 0;
        char *end = NULL;
        unsigned long long number = strtoull(p, &end, 0);
        if (errno == 0 && end && end != p) {
            const char *q = end;
            while (*q && isspace((unsigned char)*q)) {
                ++q;
            }
            if (paren) {
                if (*q != ')') {
                    q = NULL;
                } else {
                    ++q;
                    while (*q && isspace((unsigned char)*q)) {
                        ++q;
                    }
                }
            }
            if (q && *q == '\0') {
                if (deref) {
                    uint32_t addr = (uint32_t)number & 0x00ffffffu;
                    uint8_t buf[4];
                    if (!libretro_host_debugReadMemory(addr, buf, sizeof(buf))) {
                        debug_error("print: failed to read memory at 0x%06X", (unsigned)addr);
                        return 0;
                    }
                    uint32_t val = ((uint32_t)buf[0] << 24) |
                                   ((uint32_t)buf[1] << 16) |
                                   ((uint32_t)buf[2] << 8) |
                                   (uint32_t)buf[3];
                    debug_printf("*0x%06X: 0x%08X\n", (unsigned)addr, (unsigned)val);
                    return 1;
                }
                debug_printf("%s: %llu (0x%llX)\n", expr,
                             (unsigned long long)number,
                             (unsigned long long)number);
                return 1;
            }
        }
    }

    return print_eval_print(expr) ? 1 : 0;
}

static int
console_cmd_completeBreak(const char *prefix, char ***out_list, int *out_count)
{
    if (out_list) {
        *out_list = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    const char *elf = debugger.config.elfPath;
    if (!elf || !*elf || !out_list || !out_count) {
        return 0;
    }
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "m68k-neogeo-elf-objdump --syms '%s'", elf);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    console_completion_t list = {0};
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *tokens[8];
        int count = 0;
        char *cursor = line;
        while (count < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
            while (*cursor && isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (!*cursor) {
                break;
            }
            tokens[count++] = cursor;
            while (*cursor && !isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            if (*cursor) {
                *cursor++ = '\0';
            }
        }
        if (count < 2) {
            continue;
        }
        const char *name = tokens[count - 1];
        if (prefix && *prefix) {
            if (strncmp(name, prefix, strlen(prefix)) != 0) {
                continue;
            }
        }
        console_cmd_completionAdd(&list, name);
    }
    pclose(fp);
    if (list.count == 0) {
        alloc_free(list.items);
        return 0;
    }
    *out_list = list.items;
    *out_count = list.count;
    return 1;
}

static int
console_cmd_completeTransition(const char *prefix, char ***out_list, int *out_count)
{
    const char *options[] = { "slide", "explode", "doom", "flip", "rbar", "random", "cycle", "none" };
    console_completion_t list = {0};
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        const char *opt = options[i];
        if (!prefix || !*prefix) {
            console_cmd_completionAdd(&list, opt);
        } else if (strncasecmp(opt, prefix, strlen(prefix)) == 0) {
            console_cmd_completionAdd(&list, opt);
        }
    }
    *out_list = list.items;
    *out_count = list.count;
    return list.count > 0;
}

static int
console_cmd_completePrint(const char *prefix, char ***outList, int *outCount)
{
    return print_eval_complete(prefix, outList, outCount);
}

static int
console_cmd_completeWrite(const char *prefix, char ***outList, int *outCount)
{
    if (!prefix || !*prefix) {
        return 0;
    }
    if (prefix[0] == '0' && (prefix[1] == 'x' || prefix[1] == 'X')) {
        return 0;
    }
    return print_eval_complete(prefix, outList, outCount);
}

static int
console_cmd_completeCommands(const char *prefix, char ***out_list, int *out_count)
{
    if (out_list) {
        *out_list = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!out_list || !out_count) {
        return 0;
    }
    console_completion_t list = {0};
    for (size_t i = 0; i < sizeof(console_cmd) / sizeof(console_cmd[0]); ++i) {
        const console_cmd_entry_t *cmd = &console_cmd[i];
        if (!cmd->name) {
            continue;
        }
        if (prefix && *prefix && strncmp(cmd->name, prefix, strlen(prefix)) != 0) {
            continue;
        }
        console_cmd_completionAdd(&list, cmd->name);
        if (cmd->shortcut) {
            if (!prefix || !*prefix || strncmp(cmd->shortcut, prefix, strlen(prefix)) == 0) {
                console_cmd_completionAdd(&list, cmd->shortcut);
            }
        }
    }
    if (list.count == 0) {
        alloc_free(list.items);
        return 0;
    }
    *out_list = list.items;
    *out_count = list.count;
    return 1;
}

int
console_cmd_complete(const char *line, int cursor, char ***out_list, int *out_count, int *out_prefix_pos)
{
    if (out_list) {
        *out_list = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (out_prefix_pos) {
        *out_prefix_pos = 0;
    }
    if (!line || !out_list || !out_count || !out_prefix_pos) {
        return 0;
    }
    int len = (int)strlen(line);
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > len) {
        cursor = len;
    }
    int token_start = cursor;
    while (token_start > 0 && !isspace((unsigned char)line[token_start - 1])) {
        token_start--;
    }
    int token_len = cursor - token_start;
    int cmd_start = 0;
    while (cmd_start < len && isspace((unsigned char)line[cmd_start])) {
        cmd_start++;
    }
    int cmd_end = cmd_start;
    while (cmd_end < len && !isspace((unsigned char)line[cmd_end])) {
        cmd_end++;
    }
    if (cursor <= cmd_end) {
        *out_prefix_pos = token_start;
        char prefix[256];
        if (token_len >= (int)sizeof(prefix)) {
            token_len = (int)sizeof(prefix) - 1;
        }
        memcpy(prefix, line + token_start, (size_t)token_len);
        prefix[token_len] = '\0';
        return console_cmd_completeCommands(prefix, out_list, out_count);
    }
    char cmd_buf[64];
    int cmd_len = cmd_end - cmd_start;
    if (cmd_len <= 0 || cmd_len >= (int)sizeof(cmd_buf)) {
        return 0;
    }
    memcpy(cmd_buf, line + cmd_start, (size_t)cmd_len);
    cmd_buf[cmd_len] = '\0';
    const console_cmd_entry_t *cmd = console_cmd_find(cmd_buf);
    if (!cmd || !cmd->complete) {
        return 0;
    }
    *out_prefix_pos = token_start;
    char prefix[256];
    if (token_len >= (int)sizeof(prefix)) {
        token_len = (int)sizeof(prefix) - 1;
    }
    memcpy(prefix, line + token_start, (size_t)token_len);
    prefix[token_len] = '\0';
    return cmd->complete(prefix, out_list, out_count);
}

void
console_cmd_freeCompletions(char **list, int count)
{
    if (!list || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        alloc_free(list[i]);
    }
    alloc_free(list);
}

void
console_cmd_sendLine(const char *s)
{
    if (!s) {
        return;
    }
    char buf[1024];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *argv[16];
    int argc = console_cmd_tokenize(buf, argv, (int)(sizeof(argv) / sizeof(argv[0])));
    if (argc <= 0) {
        return;
    }
    const console_cmd_entry_t *cmd = console_cmd_find(argv[0]);
    if (!cmd) {
        debug_error("console: unknown command '%s'", argv[0]);
        return;
    }
    cmd->handler(argc, argv);
}

void
console_cmd_sendInterrupt(void)
{
    if (libretro_host_debugPause()) {
        machine_setRunning(&debugger.machine, 0);
        debugger_clearFrameStep();
        return;
    }
    debug_error("console: interrupt failed");
}

static int
console_cmd_diffReadBytes(uint64_t frame, uint32_t base, uint8_t *out, size_t size)
{
    if (!state_buffer_restoreFrameNo(frame)) {
        return 0;
    }
    if (!libretro_host_debugReadMemory(base, out, size)) {
        return 0;
    }
    return 1;
}

static uint32_t
console_cmd_diffReadValueBE(const uint8_t *p, size_t size)
{
    if (size == 1) {
        return (uint32_t)p[0];
    }
    if (size == 2) {
        return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
    }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int
console_cmd_diff(int argc, char **argv)
{
    if (argc < 3) {
        debug_error("diff: usage: diff <fromFrame> <toFrame> [size=8|16|32]");
        return 0;
    }
    uint64_t from = 0;
    uint64_t to = 0;
    if (!console_cmd_parseU64Dec(argv[1], &from)) {
        debug_error("diff: invalid fromFrame (decimal expected)");
        return 0;
    }
    if (!console_cmd_parseU64Dec(argv[2], &to)) {
        debug_error("diff: invalid toFrame (decimal expected)");
        return 0;
    }
    size_t accessSize = 1;
    for (int i = 3; i < argc; ++i) {
        const char *tok = argv[i];
        if (!tok || !*tok) {
            continue;
        }
        if (strncmp(tok, "size=", 5) == 0) {
            uint32_t bits = 0;
            if (!console_cmd_parseU32Auto(tok + 5, &bits)) {
                debug_error("diff: invalid size");
                return 0;
            }
            if (bits == 8) {
                accessSize = 1;
            } else if (bits == 16) {
                accessSize = 2;
            } else if (bits == 32) {
                accessSize = 4;
            } else {
                debug_error("diff: size must be 8, 16, or 32");
                return 0;
            }
            continue;
        }
        debug_error("diff: unknown option '%s'", tok);
        return 0;
    }

    if (!state_buffer_hasFrameNo(from)) {
        debug_error("diff: frame %llu not in state buffer", (unsigned long long)from);
        return 0;
    }
    if (!state_buffer_hasFrameNo(to)) {
        debug_error("diff: frame %llu not in state buffer", (unsigned long long)to);
        return 0;
    }

    const uint64_t restoreFrame = state_buffer_getCurrentFrameNo();

    // Neo Geo main + backup RAM (68k map). Keep it simple; we can extend later.
    struct {
        uint32_t base;
        size_t size;
        const char *name;
    } regions[] = {
        { 0x00100000u, 0x10000u, "ram" },
        { 0x00d00000u, 0x10000u, "backup" }
    };

    enum { kMaxLines = 4096 };
    unsigned long long changed = 0;
    unsigned long long printed = 0;

    debug_printf("diff: frames %llu -> %llu (size=%u)\n",
                 (unsigned long long)from, (unsigned long long)to,
                 (unsigned)(accessSize * 8));

    for (size_t r = 0; r < sizeof(regions) / sizeof(regions[0]); ++r) {
        const uint32_t base = regions[r].base;
        const size_t size = regions[r].size;
        uint8_t *a = (uint8_t*)malloc(size);
        uint8_t *b = (uint8_t*)malloc(size);
        if (!a || !b) {
            free(a);
            free(b);
            debug_error("diff: out of memory");
            (void)state_buffer_restoreFrameNo(restoreFrame);
            debugger.frameCounter = restoreFrame;
            state_buffer_setCurrentFrameNo(restoreFrame);
            return 0;
        }

        if (!console_cmd_diffReadBytes(from, base, a, size) ||
            !console_cmd_diffReadBytes(to, base, b, size)) {
            free(a);
            free(b);
            debug_error("diff: failed to read %s memory", regions[r].name);
            (void)state_buffer_restoreFrameNo(restoreFrame);
            debugger.frameCounter = restoreFrame;
            state_buffer_setCurrentFrameNo(restoreFrame);
            return 0;
        }

        for (size_t off = 0; off + accessSize <= size; off += accessSize) {
            uint32_t va = console_cmd_diffReadValueBE(a + off, accessSize);
            uint32_t vb = console_cmd_diffReadValueBE(b + off, accessSize);
            if (va == vb) {
                continue;
            }
            changed++;
            if (printed < kMaxLines) {
                unsigned digits = (unsigned)(accessSize * 2);
                long long delta = (long long)((long long)vb - (long long)va);
                debug_printf("0x%06X: 0x%0*X -> 0x%0*X  delta=%+lld\n",
                             (unsigned)((base + (uint32_t)off) & 0x00ffffffu),
                             (int)digits, (unsigned)va,
                             (int)digits, (unsigned)vb,
                             delta);
                printed++;
            }
        }

        free(a);
        free(b);
    }

    if (changed == 0) {
        debug_printf("diff: no changes\n");
    } else if (changed > printed) {
        debug_printf("diff: %llu changes (showing %llu, truncated)\n",
                     changed, printed);
    } else {
        debug_printf("diff: %llu changes\n", changed);
    }

    (void)state_buffer_restoreFrameNo(restoreFrame);
    debugger.frameCounter = restoreFrame;
    state_buffer_setCurrentFrameNo(restoreFrame);
    return 1;
}
