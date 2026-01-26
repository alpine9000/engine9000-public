// POSIX signal handling
#include "debugger_signal.h"

#include "debug.h"

#include <errno.h>
#include <signal.h>
#include <string.h>

static volatile sig_atomic_t debugger_exit_signal = 0;

static void
debugger_handleSignal(int sig)
{
    debugger_exit_signal = sig;
}

int
signal_getExitCode(void)
{
    return debugger_exit_signal;
}

void
signal_installHandlers(void)
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = debugger_handleSignal;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    const int signals[] = { SIGINT, SIGTERM, SIGQUIT };
    const int signal_count = (int)(sizeof(signals) / sizeof(signals[0]));
    for (int i = 0; i < signal_count; ++i) {
        if (sigaction(signals[i], &act, NULL) != 0) {
            debug_error("signal setup failed for %d: %s", signals[i], strerror(errno));
        }
    }
}
