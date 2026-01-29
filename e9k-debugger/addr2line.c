/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "addr2line.h"

#ifdef _WIN32
#include <stdint.h>
#include <stddef.h>

int
addr2line_start(const char *elf_path)
{
    (void)elf_path;
    return 0;
}

void
addr2line_stop(void)
{
}

int
addr2line_resolve(uint64_t addr, char *out_file, size_t file_cap, int *out_line)
{
    (void)addr;
    if (out_file && file_cap > 0) {
        out_file[0] = '\0';
    }
    if (out_line) {
        *out_line = 0;
    }
    return 0;
}
#else

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "debugger.h"

typedef struct {
  pid_t pid;
  FILE *in;
  int outFD;
  char elf[PATH_MAX];
  char *pending;
  char buf[4096];
  size_t bufLen;
  int expectFunc;
  int expectFile;
} addr2line_t;

static addr2line_t addr2line = {
    .pid = -1,
    .outFD = -1,
};

static void
addr2line_clearPending(void)
{
    if (addr2line.pending) {
        free(addr2line.pending);
        addr2line.pending = NULL;
    }
}

static int
addr2line_isAddressLine(const char *line)
{
    if (!line || line[0] != '0' || (line[1] != 'x' && line[1] != 'X')) {
        return 0;
    }
    const char *p = line + 2;
    if (!*p) {
        return 0;
    }
    while (*p) {
        if ((*p >= '0' && *p <= '9') ||
            (*p >= 'a' && *p <= 'f') ||
            (*p >= 'A' && *p <= 'F')) {
            p++;
            continue;
        }
        return 0;
    }
    return 1;
}

static int
addr2line_readLine(char **out)
{
    if (!out) {
        return 0;
    }
    if (addr2line.pending) {
        *out = addr2line.pending;
        addr2line.pending = NULL;
        return 1;
    }
    if (addr2line.outFD < 0) {
        return 0;
    }
    for (;;) {
        for (size_t i = 0; i < addr2line.bufLen; ++i) {
            if (addr2line.buf[i] == '\n') {
                size_t len = i;
                if (len > 0 && addr2line.buf[len - 1] == '\r') {
                    len--;
                }
                char *line = (char*)malloc(len + 1);
                if (!line) {
                    return 0;
                }
                memcpy(line, addr2line.buf, len);
                line[len] = '\0';
                size_t remain = addr2line.bufLen - (i + 1);
                memmove(addr2line.buf, addr2line.buf + i + 1, remain);
                addr2line.bufLen = remain;
                *out = line;
                return 1;
            }
        }
        if (addr2line.bufLen >= sizeof(addr2line.buf) - 1) {
            addr2line.bufLen = 0;
        }
        ssize_t n = read(addr2line.outFD,
                         addr2line.buf + addr2line.bufLen,
                         sizeof(addr2line.buf) - 1 - addr2line.bufLen);
        if (n > 0) {
            addr2line.bufLen += (size_t)n;
            continue;
        }
        if (n == 0) {
            return 0;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return 0;
    }
}

static void
addr2line_closeStreams(void)
{
    if (addr2line.in) {
        fclose(addr2line.in);
        addr2line.in = NULL;
    }
    if (addr2line.outFD >= 0) {
        close(addr2line.outFD);
        addr2line.outFD = -1;
    }
}

int
addr2line_start(const char *elf_path)
{
    if (!elf_path || !*elf_path) {
        return 0;
    }
    if (addr2line.pid > 0 && strcmp(addr2line.elf, elf_path) == 0) {
        return 1;
    }
    addr2line_stop();

    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0) {
        return 0;
    }
    if (pipe(from_child) != 0) {
        close(to_child[0]);
        close(to_child[1]);
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        dup2(from_child[1], STDERR_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        char bin[PATH_MAX];
        if (!debugger_toolchainBuildBinary(bin, sizeof(bin), "addr2line")) {
            _exit(127);
        }
        char *const argv[] = {
            bin,
            (char*)"-e",
            (char*)elf_path,
            (char*)"-a",
            (char*)"-f",
            (char*)"-C",
            NULL
        };
        execvp(bin, argv);
        _exit(127);
    }
    if (pid < 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return 0;
    }

    close(to_child[0]);
    close(from_child[1]);
    addr2line.in = fdopen(to_child[1], "w");
    addr2line.outFD = from_child[0];
    if (!addr2line.in || addr2line.outFD < 0) {
        addr2line_closeStreams();
        close(to_child[1]);
        if (addr2line.outFD >= 0) {
            close(addr2line.outFD);
            addr2line.outFD = -1;
        }
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 0;
    }
    setvbuf(addr2line.in, NULL, _IOLBF, 0);
    addr2line.pid = pid;
    strncpy(addr2line.elf, elf_path, sizeof(addr2line.elf) - 1);
    addr2line.elf[sizeof(addr2line.elf) - 1] = '\0';
    addr2line_clearPending();
    addr2line.bufLen = 0;
    addr2line.expectFunc = 0;
    addr2line.expectFile = 0;
    return 1;
}

void
addr2line_stop(void)
{
    addr2line_clearPending();
    addr2line_closeStreams();
    if (addr2line.pid > 0) {
        kill(addr2line.pid, SIGTERM);
        waitpid(addr2line.pid, NULL, 0);
        addr2line.pid = -1;
    }
    addr2line.elf[0] = '\0';
    addr2line.bufLen = 0;
    addr2line.expectFunc = 0;
    addr2line.expectFile = 0;
}

int
addr2line_resolve(uint64_t addr, char *out_file, size_t file_cap, int *out_line)
{
    if (out_file && file_cap > 0) {
        out_file[0] = '\0';
    }
    if (out_line) {
        *out_line = 0;
    }
    if (!addr2line.in || addr2line.outFD < 0) {
        return 0;
    }
    uint64_t queryAddr = addr;
    uint64_t base = (uint64_t)debugger.machine.textBaseAddr;
    if (base != 0 && queryAddr >= base) {
        queryAddr -= base;
    }
    if (fprintf(addr2line.in, "0x%llx\n", (unsigned long long)queryAddr) < 0) {
        return 0;
    }
    fflush(addr2line.in);

    char *line = NULL;
    int ok = 0;
    int got_addr = 0;
    for (int i = 0; i < 128; ++i) {
        if (!addr2line_readLine(&line)) {
            break;
        }
        if (addr2line_isAddressLine(line)) {
            unsigned long long got = strtoull(line, NULL, 16);
            if ((uint64_t)got == queryAddr) {
                got_addr = 1;
                addr2line.expectFunc = 1;
                addr2line.expectFile = 0;
            } else {
                got_addr = 0;
                addr2line.expectFunc = 0;
                addr2line.expectFile = 0;
            }
            free(line);
            line = NULL;
            continue;
        }
        if (addr2line.expectFunc) {
            addr2line.expectFunc = 0;
            addr2line.expectFile = 1;
            free(line);
            line = NULL;
            continue;
        }
        if (addr2line.expectFile && got_addr && out_file && file_cap > 0) {
            char *colon = strrchr(line, ':');
            if (colon && colon[1]) {
                int line_no = atoi(colon + 1);
                if (line_no > 0) {
                    *colon = '\0';
                    strncpy(out_file, line, file_cap - 1);
                    out_file[file_cap - 1] = '\0';
                    if (out_line) {
                        *out_line = line_no;
                    }
                    ok = 1;
                }
            }
            addr2line.expectFile = 0;
            free(line);
            line = NULL;
            break;
        }
        free(line);
        line = NULL;
        if (ok) {
            break;
        }
    }
    if (line) {
        free(line);
    }
    return ok;
}

#endif
