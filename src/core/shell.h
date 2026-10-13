/* shell.h — the command line (design.md §4.4, §4.5).
 *
 * The banner, the prompt, a line read with Backspace and Ctrl-C, the
 * parser (parse.h), and the builtins: M1's file commands (files.h),
 * `echo`, `font`, `menu` and the development command `displaytest`.
 * Any other command is a program (run.h). */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "ff.h"
#include "out.h"

/* What the shell needs from the port beyond its output. */
typedef struct {
    uint64_t (*now_us)(void *ctx);
    /* Break, Esc or Ctrl-C pressed since the last call; for long commands
     * to poll. The port keeps the keyboard polled while they run. */
    bool (*interrupted)(void *ctx);
    void *ctx;
} shell_hooks_t;

typedef struct shell {
    out_t         con;              /* the terminal: prompt, echo, errors */
    out_t         out;              /* a command's output: con, or its > file */
    bool          out_is_con;
    FIL          *in;               /* a command's < file, or NULL */
    bool          stop;             /* interrupted: the rest of the line is dropped */
    int           status;           /* the last command's, kept across lines */
    shell_hooks_t hooks;
    const struct shell_loader *loader;   /* run.h; NULL: programs cannot run */
    const char   *version;
    char          cwd[SHELL_PATH_MAX + 1];   /* absolute and normal (path.h) */
    bool          wide;             /* font_5x10, 64 columns */
    char          line[SHELL_LINE_MAX + 1];
    size_t        len;
} shell_t;

/* A builtin: its exit status, 0 for success. */
typedef int (*shell_cmd_fn)(shell_t *sh, int argc, char **argv);

void shell_init(shell_t *sh, const out_t *out, const shell_hooks_t *hooks, const char *version);

/* How programs are run (run.h): the port's link to UF2 Loader. */
void shell_set_loader(shell_t *sh, const struct shell_loader *loader);

/* Mount the card, reset the terminal, print the banner and the first
 * prompt. The starting directory is /home if the card has one. */
void shell_start(shell_t *sh);

/* One input byte (keys.h): a character, or a key code of 0x80 and above. */
void shell_input(shell_t *sh, int key);

/* Run one line as if typed, without echo or prompt; the last command's
 * status. For tests and, later, scripts. */
int shell_run(shell_t *sh, const char *line);

/* For builtins: an error to the terminal, never to a redirection. */
void shell_error(shell_t *sh, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* For builtins: true once Break, Esc or Ctrl-C is pressed, and from then
 * on for the rest of the line. */
bool shell_interrupted(shell_t *sh);

/* The terminal's width in columns. */
unsigned shell_cols(const shell_t *sh);

#endif /* SHELL_SHELL_H */
