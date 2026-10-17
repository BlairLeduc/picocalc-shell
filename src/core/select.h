/* select.h — `select file`: a one-key menu of programs.
 *
 * The file has one entry per line: a key, a program, and a description
 * of any number of words, separated by spaces and tabs. Lines end in LF
 * or CR LF. The screen shows "K  description" for each entry, cut at 40
 * columns; the key (either case) runs the program, found along $PATH
 * (run.h). Either slash may be used in the file's name and in a
 * program's. See select_const.h for what else the file may hold.
 * Portable. */
#ifndef SHELL_SELECT_H
#define SHELL_SELECT_H

#include "shell.h"

int cmd_select(shell_t *sh, int argc, char **argv);

#endif /* SHELL_SELECT_H */
