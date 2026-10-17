/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"
#include "cli.h"
int
main_impl(int argc, char **argv)
{
  int rc = 0;
  int loadTestTempConfig = 0;
  int testRestartCount = 0;
  int currentArgc = argc;
  char **currentArgv = argv;
  char *restartArgv[CLI_RESTART_ARGV_CAP];

  cli_setArgv0((argc > 0 && argv) ? argv[0] : NULL);
  do {
    debugger_setLoadTestTempConfig(loadTestTempConfig);
    debugger_setTestRestartCount(testRestartCount);
    rc = debugger_main(currentArgc, currentArgv);
    if (rc == 2) {
      if (cli_shouldClearRestartArgs()) {
        // Settings replace startup overrides, but the test must continue into
        // its next recording/replay segment using the saved temporary config.
        currentArgc = cli_buildRestartArgs(restartArgv);
        currentArgv = restartArgv;
      }
      loadTestTempConfig = 1;
      testRestartCount++;
    } else {
      loadTestTempConfig = 0;
    }
  } while (rc == 2);
  return rc;
}

int
main(int argc, char **argv)
{
  return main_impl(argc, argv);
}
