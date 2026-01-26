/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"
#include "cli.h"
#ifdef _WIN32
#include <SDL.h>
#endif

int
#ifdef _WIN32
SDL_main(int argc, char **argv)
#else
main(int argc, char **argv)
#endif
{
  int rc = 0;
  cli_setArgv0((argc > 0 && argv) ? argv[0] : NULL);
  do {
    rc = debugger_main(argc, argv);
  } while (rc == 2);
  return rc;
}
