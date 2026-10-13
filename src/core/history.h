/* history.h — the lines typed at the prompt (design.md §4.3, §4.4).
 *
 * Kept packed in SHELL_HISTORY_BYTES, the oldest dropped to make room. A
 * blank line, and one the same as the line before it, are not kept.
 * The history is read from /.shell/history when the shell starts and
 * written back, through name.new, when the shell is left for a program,
 * the menu or USB (shell_leaving), and only if it changed: one write
 * per session, not one per command. Lines typed since the last such
 * write are lost if the power goes. */
#ifndef SHELL_HISTORY_H
#define SHELL_HISTORY_H

#include <stdbool.h>

#include "shell.h"

#define HISTORY_FILE "/.shell/history"

void history_clear(void);
void history_add(const char *line);

/* How many lines there are; the i-th, 0 the oldest, or NULL. */
unsigned history_count(void);
const char *history_get(unsigned i);

/* Read HISTORY_FILE, if there is one, in place of what is kept. */
void history_load(void);

/* Write HISTORY_FILE if the history changed since it was read or
 * written: NULL, or why not. */
const char *history_save(void);

int cmd_history(shell_t *sh, int argc, char **argv);

#endif /* SHELL_HISTORY_H */
