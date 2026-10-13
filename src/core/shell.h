/* shell.h — the command line (design.md §4.4, §4.5).
 *
 * M0's: the banner, the prompt, a line read with Backspace and Ctrl-C,
 * and two commands, `font` and the development command `displaytest`.
 * M1 brings the parser and the file builtins. */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "out.h"

/* What the shell needs from the port beyond its output. */
typedef struct {
    uint64_t (*now_us)(void *ctx);
    /* Break, Esc or Ctrl-C pressed since the last call; for long commands
     * to poll. The port keeps the keyboard polled while they run. */
    bool (*interrupted)(void *ctx);
    void *ctx;
} shell_hooks_t;

typedef struct {
    out_t         out;
    shell_hooks_t hooks;
    const char   *version;
    const char   *cwd;              /* "/" until M1 has a filesystem */
    bool          wide;             /* font_5x10, 64 columns */
    char          line[SHELL_LINE_MAX + 1];
    size_t        len;
} shell_t;

void shell_init(shell_t *sh, const out_t *out, const shell_hooks_t *hooks, const char *version);

/* Reset the terminal, print the banner and the first prompt. */
void shell_start(shell_t *sh);

/* One input byte (keys.h): a character, or a key code of 0x80 and above. */
void shell_input(shell_t *sh, int key);

#endif /* SHELL_SHELL_H */
