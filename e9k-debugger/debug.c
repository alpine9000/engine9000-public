// debug printing utilities implementation
#include "debug.h"
#include "debugger.h"
#include "linebuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

extern e9k_debugger_t debugger;

void
debug_printf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    linebuf_push(&debugger.console, buf);
    if (debugger.opts.redirectStdout) {
        fputs(buf, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
}

void
debug_error(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    linebuf_pushErr(&debugger.console, buf);
    if (debugger.opts.redirectStderr) {
        fputs(buf, stderr);
        fputc('\n', stderr);
        fflush(stderr);
    }
}

void
debug_gdb(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    linebuf_push(&debugger.console, buf);
    if (debugger.opts.redirectGdbStdout) {
        fputs(buf, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
}

void
debug_trace(const char *fmt, ...)
{
    if (!debugger.opts.enableTrace) return;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    linebuf_push(&debugger.console, buf);
    if (debugger.opts.redirectStdout) {
        fputs(buf, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
}
