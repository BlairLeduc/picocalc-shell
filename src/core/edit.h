/* edit.h — EDIT, the DOS flavour's editor: `EDIT [file]` (design.md §4.7).
 *
 * The core of MS-DOS 5's EDIT, not all of it, with vi's rules for the
 * file (editor_file.h): the whole file in one buffer of VI_BUF_SIZE
 * bytes, its line breaks kept, a read-only file opened read-only, and
 * saving through name.new and a rename.
 *
 *   - The screen: the 64-column font while EDIT runs; a menu bar, the
 *     file's name in the title row, EDIT_TEXT_ROWS of text, white on
 *     blue, and a status row. Long lines scroll sideways, as in vi.
 *   - Menus: tap Alt, or Alt and a menu's letter. File (New, Open, Save,
 *     Save As, Exit), Edit (Cut, Copy, Paste, Clear), Search (Find,
 *     Repeat Last Find, Change), Options (Display) and Help.
 *   - Keys: the arrows, Home, End, PgUp and PgDn; Ctrl with the arrows by
 *     words, Ctrl with Home and End to either end of the file; Alt with
 *     the arrows selects (the keyboard swallows Shift with them,
 *     hardware-notes.md §6.3); Ctrl+X cuts, Ctrl+C copies, Ctrl+V
 *     pastes, Del clears; Ctrl+Y cuts the line; Insert (Alt+I) toggles
 *     overtype; F1 is help, F3 finds again; Esc closes a menu or a box.
 *     Brk is File Exit. The end of input leaves without saving.
 *   - Find and Change look for the text as typed, in either case. */
#ifndef SHELL_EDIT_H
#define SHELL_EDIT_H

#include "shell.h"

int cmd_edit(shell_t *sh, int argc, char **argv);

#endif /* SHELL_EDIT_H */
