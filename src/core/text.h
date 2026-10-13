/* text.h — `more` and `grep` (design.md §4.5).
 *
 * Both read files named, or else their input (`<` or a pipe).
 *
 * `more` shows a screenful, TERM_ROWS - 1 rows, then waits at
 * --More--: Space for the next screenful, Enter or Down for one more
 * row, q, Esc, Brk or Ctrl-C to stop. A long line counts as the rows it
 * wraps to. When its output is not the terminal it copies, as cat does.
 *
 * `grep [-v] [-n] [-c] [-i] pattern [file...]` prints the lines that
 * match. The pattern is vi's (editor_pattern.h: ^ $ . * [...] \< \>
 * \( \) \1-\9), so it matches without regard to case, as `/` in vi
 * does; -i is accepted and changes nothing. -v the lines that do not
 * match, -n with their numbers, -c only how many. With more than one
 * file each line starts with its file's name. A line longer than
 * SHELL_TEXT_LINE - 1 is matched in pieces of that length. The status
 * is 0 if a line was found, 1 if none, 2 on an error. */
#ifndef SHELL_TEXT_H
#define SHELL_TEXT_H

#include "shell.h"

int cmd_more(shell_t *sh, int argc, char **argv);
int cmd_grep(shell_t *sh, int argc, char **argv);

#endif /* SHELL_TEXT_H */
