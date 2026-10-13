/* line.h — the line typed at the prompt (design.md §4.4).
 *
 * Keys:
 *   Left, Right, Home, End, and Ctrl-B, F, A, E   move
 *   Backspace, Del                                delete
 *   Ctrl-U, Ctrl-K                                delete to the start, to the end
 *   Up, Down, and Ctrl-P, N                       the history (history.h)
 *   Tab                                           complete (below)
 *   Ctrl-C                                        drop the line
 *   Enter                                         run it
 *
 * Tab completes the word before the cursor: a command name (a builtin or
 * a program in $PATH) as the first word of a command, and a path
 * anywhere else. One match is put in whole, followed by '/' for a
 * directory and a space for anything else; several put in what they
 * share, and a second Tab with nothing to add lists them. Names are
 * matched without regard to case and put in as the card spells them,
 * with a '\' before any character the shell would take as special.
 *
 * The line may run over several rows. The terminal wraps as soon as a
 * row is full (term.c), so the cursor's place is worked out from the
 * prompt's width, and moved there with relative escape sequences. */
#ifndef SHELL_LINE_H
#define SHELL_LINE_H

#include <stdbool.h>

#include "shell.h"

/* A new, empty line after a prompt `prompt` columns wide. */
void line_start(shell_t *sh, size_t prompt);

/* One key. True on Enter, with the line in sh->line.text and the cursor
 * at the start of the next row. */
bool line_key(shell_t *sh, int key);

#endif /* SHELL_LINE_H */
