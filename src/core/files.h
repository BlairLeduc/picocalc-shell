/* files.h — the card's files: M1's builtins and redirection
 * (design.md §4.2, §4.5).
 *
 * Portable: FatFs's API only, over whatever disk the port gives it (the
 * SD card on the board, an image file on the host). Paths are resolved
 * against the shell's own current directory (path.h). Every write goes
 * to `name.new`, which is closed, then the old file is unlinked and the
 * new one renamed into place (EL §8.6), so a write that fails leaves the
 * old file as it was. Nothing here holds the card between commands, so
 * a card changed at the prompt is simply a new card. */
#ifndef SHELL_FILES_H
#define SHELL_FILES_H

#include <stdbool.h>

#include "shell.h"

/* cd with no operand, and the starting directory when the card has it
 * (design.md §4.3). */
#define SHELL_HOME "/home"

/* Register the volume; FatFs mounts it on first use, and again after
 * the port's disk layer reports the card changed. */
void files_mount(void);

/* FatFs's result in words: "no card", "no such file or directory". */
const char *files_why(FRESULT r);

/* Whether `path` (absolute) is a directory on the card. */
bool files_is_dir(const char *path);

/* `> arg` or `>> arg` for the next command: sh->out becomes the file.
 * False, with the error printed, if it cannot be opened. */
bool files_redirect(shell_t *sh, const char *arg, bool append);
/* After the command: put the file in place, or keep the old one and say
 * why. False if the output was lost. sh->out is the terminal again. */
bool files_redirect_end(shell_t *sh);

/* `< arg` for the next command: sh->in. */
bool files_input(shell_t *sh, const char *arg);
void files_input_end(shell_t *sh);

int cmd_cat(shell_t *sh, int argc, char **argv);
int cmd_cd(shell_t *sh, int argc, char **argv);
int cmd_cp(shell_t *sh, int argc, char **argv);
int cmd_df(shell_t *sh, int argc, char **argv);
int cmd_ls(shell_t *sh, int argc, char **argv);
int cmd_mkdir(shell_t *sh, int argc, char **argv);
int cmd_mv(shell_t *sh, int argc, char **argv);
int cmd_pwd(shell_t *sh, int argc, char **argv);
int cmd_rm(shell_t *sh, int argc, char **argv);
int cmd_rmdir(shell_t *sh, int argc, char **argv);
int cmd_touch(shell_t *sh, int argc, char **argv);

#endif /* SHELL_FILES_H */
