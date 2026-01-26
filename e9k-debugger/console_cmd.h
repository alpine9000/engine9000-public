/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

void
console_cmd_sendLine(const char *s);

void
console_cmd_sendInterrupt(void);

int
console_cmd_complete(const char *line, int cursor, char ***out_list, int *out_count, int *out_prefix_pos);

void
console_cmd_freeCompletions(char **list, int count);


