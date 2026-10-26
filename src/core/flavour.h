/* flavour.h — what each flavour of the command line gives the rest of
 * the shell (design.md §4.4, §4.7).
 *
 * The UNIX flavour (unix.c, with parse.c, expand.c and vi) and the DOS
 * flavour (dos.c and its dos_*.c) each implement what is declared
 * here; a build links one of them (CMake's PICO_SHELL_FLAVOUR, and both
 * on the host). Everything else, from the engine in shell.c to files.c,
 * text.c and run.c, is shared. SHELL_DOS is 1 in the DOS flavour's
 * build, for the few shared places that differ by a character, such as
 * the path separator Tab puts after a directory. */
#ifndef SHELL_FLAVOUR_H
#define SHELL_FLAVOUR_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "shell.h"

#ifndef SHELL_DOS
#define SHELL_DOS 0
#endif

struct pipe;

/* ---- the flavour's ---------------------------------------------------- */

/* Its builtins, in the order `help` and Tab list them. */
typedef struct {
    const char  *name;
    shell_cmd_fn fn;
    const char  *usage;
    const char  *about;
} shell_builtin_t;

extern const shell_builtin_t flavour_builtins[];
extern const unsigned flavour_nbuiltins;

/* The variables' defaults, at shell_init. */
void flavour_init(shell_t *sh);

/* At shell_start, after the history is read: the banner, then the
 * startup script, if the flavour has one. */
void flavour_start(shell_t *sh);

/* The prompt's text, for shell_prompt and Tab's listing. */
void flavour_prompt(shell_t *sh, char *buf, size_t size);

/* A line typed at the prompt, at sh->depth; the last command's status. */
int flavour_run_line(shell_t *sh, const char *line);

/* `arg` as typed, made an absolute, normal path (path.h) against `cwd`.
 * False if it cannot be: too long, or in the DOS flavour, another drive. */
bool flavour_resolve(char out[SHELL_PATH_MAX + 1], const char *cwd, const char *arg);

/* Whether a command name is a path rather than a name to look up. */
bool flavour_is_path(const char *name);

/* A file a command could not use, in the flavour's words:
 * "cmd: name: why", or DOS's "Why - name". */
void flavour_file_error(shell_t *sh, const char *cmd, const char *name, const char *why);

/* A command found neither as a builtin nor as a program. */
void flavour_not_found(shell_t *sh, const char *name);

/* $PATH's separator. */
#define FLAVOUR_PATH_SEP (SHELL_DOS ? ';' : ':')

/* ---- shell.c's, for the flavours -------------------------------------- */

/* The commands both flavours have. */
int cmd_usbdisk(shell_t *sh, int argc, char **argv);

/* The builtin called `name`, or NULL: by its exact name in the UNIX
 * flavour, without regard to case in DOS's. */
shell_cmd_fn shell_builtin(const char *name);

/* Run one command, expanded, with its redirections as typed (NULL for
 * none), reading `from` and writing `to` when it is in a pipeline. */
int shell_command(shell_t *sh, int argc, char **argv, const char *in, const char *out,
                  bool append, struct pipe *from, struct pipe *to);

/* Run it with its output thrown away: DOS's `> NUL`, UNIX's
 * `> /dev/null`. */
int shell_command_null(shell_t *sh, int argc, char **argv, const char *in,
                       struct pipe *from, struct pipe *to);

/* Run commands 0..n-1 of a pipeline, each reading the one before's
 * output; `stage` runs the k-th. The last one's status. */
typedef int (*shell_stage_fn)(shell_t *sh, int k, struct pipe *from, struct pipe *to, void *ctx);
int shell_pipeline(shell_t *sh, int n, shell_stage_fn stage, void *ctx);

#endif /* SHELL_FLAVOUR_H */
