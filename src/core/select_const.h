/* select_const.h — the constants of `select` (select.h). */
#ifndef SHELL_SELECT_CONST_H
#define SHELL_SELECT_CONST_H

#include "config.h"

/* The menu is cut at this many columns, whatever the font. */
#define SELECT_WIDTH        40u

/* Row 1 is the title, row 2 is empty, the entries follow, and the last
 * row of the grid is the footer. */
#define SELECT_FIRST_ROW    3u
#define SELECT_ROWS         (TERM_ROWS - SELECT_FIRST_ROW)

/* The menu file: a line starting with a comment character is skipped, and
 * a first line of "#title words" (in either case) is the title. A line that
 * cannot be an entry is shown with BAD_KEY in place of its key. */
#define SELECT_COMMENT_1    '#'
#define SELECT_COMMENT_2    ';'
#define SELECT_TITLE_TAG    "#title"
#define SELECT_BAD_KEY      '?'

/* A line longer than SHELL_TEXT_LINE is read in pieces of this size and
 * the rest of it dropped. */
#define SELECT_SKIP         32u

/* Keys that leave the menu, besides Break. */
#define SELECT_KEY_ESC      0x1B
#define SELECT_KEY_INTR     0x03

/* Between the key and its description on a row. */
#define SELECT_GAP          "  "

#define SELECT_TITLE        "SELECT A PROGRAM"
#define SELECT_FOOTER       "ESC=QUIT"
#define SELECT_PRESS        "PRESS A KEY"

#define SELECT_CLEAR        "\033[H\033[2J"
#define SELECT_BOLD         "\033[1m"
#define SELECT_PLAIN        "\033[0m"

#endif /* SHELL_SELECT_CONST_H */
