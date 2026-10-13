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
#include <stddef.h>

#include "shell.h"

/* $HOME's default: cd with no operand, and the starting directory when
 * the card has it (design.md §4.3). */
#define SHELL_HOME "/home"

/* Register the volume; FatFs mounts it on first use, and again after
 * the port's disk layer reports the card changed. */
void files_mount(void);

/* Let the volume go, for usbdisk: nothing may be open. */
void files_unmount(void);

/* FatFs's result in words: "no card", "no such file or directory". */
const char *files_why(FRESULT r);

/* f_stat, with the root, which FatFs cannot stat, as a directory. */
FRESULT files_stat(const char *path, FILINFO *fno);

/* Whether ls and globs leave the name out: `.name`, macOS's `._name`
 * (hardware-notes.md §7.1), and anything FAT marks hidden or system,
 * such as Windows's `System Volume Information` (design.md §4.2). */
bool files_hidden(const FILINFO *fno);

/* Whether `path` (absolute) is a directory on the card. */
bool files_is_dir(const char *path);

/* A write through name.new (EL §8.6), for any command that saves a file. */
typedef struct {
    FIL         fil;
    const char *why;                        /* the first failure, or NULL */
    char        path[SHELL_PATH_MAX + 1];
    char        tmp[SHELL_PATH_MAX + 5];    /* path + ".new" */
} files_safe_t;

/* Open `path`.new for writing: NULL, or why not. A directory or a file
 * marked read-only is refused. */
const char *files_safe_open(files_safe_t *w, const char *path);
/* Write to it; the first failure is kept in w->why and later writes do
 * nothing. */
void files_safe_write(files_safe_t *w, const void *buf, size_t n);
/* Close and remove name.new, leaving the old file. */
void files_safe_abort(files_safe_t *w);
/* Close name.new and put it in place: NULL, or why the old file stays. */
const char *files_safe_commit(files_safe_t *w);

/* Open a file the shell reads at startup (/etc/profile, the history):
 * if it is missing but name.new is there, the power went between the
 * unlink and the rename of a safe write, and name.new is whole. */
FRESULT files_open_startup(FIL *f, const char *path);

/* `> arg` or `>> arg` for the next command: sh->out becomes the file,
 * one for each level of sh->depth. False, with the error printed, if it
 * cannot be opened. */
bool files_redirect(shell_t *sh, const char *arg, bool append);
/* After the command: put the file in place, or keep the old one and say
 * why. False if the output was lost. The caller puts sh->out back. */
bool files_redirect_end(shell_t *sh);

/* `< arg` for the next command: sh->in. */
bool files_input(shell_t *sh, const char *arg);
/* After it: close the file. The caller puts sh->in back. */
void files_input_end(shell_t *sh);

/* Open `arg` to read, for a command that takes files: FR_OK, or the
 * failure with the error printed as "cmd: arg: why". */
FRESULT files_open_read(shell_t *sh, const char *cmd, const char *arg, FIL *f);

/* Leading options, such as -l -a or -la, up to "--" or the first
 * operand: each letter of `allowed` found sets its bit in *flags. The
 * index of the first operand, or -1 after printing the usage. */
int files_options(shell_t *sh, int argc, char **argv, const char *allowed, unsigned *flags,
                  const char *usage);

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
