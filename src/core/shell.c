/* shell.c — the command line (shell.h). */

#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "displaytest.h"
#include "files.h"
#include "parse.h"

void shell_init(shell_t *sh, const out_t *out, const shell_hooks_t *hooks, const char *version) {
    memset(sh, 0, sizeof *sh);
    sh->con = *out;
    sh->out = *out;
    sh->out_is_con = true;
    sh->hooks = *hooks;
    sh->version = version;
    strcpy(sh->cwd, "/");
}

/* The current directory followed by '>' (design.md §4.4). */
static void prompt(shell_t *sh) {
    out_printf(&sh->con, "%s>", sh->cwd);
}

void shell_start(shell_t *sh) {
    files_mount();
    strcpy(sh->cwd, files_is_dir(SHELL_HOME) ? SHELL_HOME : "/");
    out_puts(&sh->con, "\033c");   /* RIS: a clean screen whatever ran before */
    out_printf(&sh->con, "\033[1mPicoCalc shell\033[0m %s\n\n", sh->version);
    prompt(sh);
}

void shell_error(shell_t *sh, const char *fmt, ...) {
    char buf[SHELL_PRINTF_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
    out_write(&sh->con, buf, (size_t)n);
}

bool shell_interrupted(shell_t *sh) {
    if (!sh->stop && sh->hooks.interrupted && sh->hooks.interrupted(sh->hooks.ctx)) sh->stop = true;
    return sh->stop;
}

unsigned shell_cols(const shell_t *sh) {
    return sh->wide ? TERM_COLS_5X10 : TERM_COLS_8X10;
}

/* ---- commands --------------------------------------------------------- */

static int cmd_echo(shell_t *sh, int argc, char **argv) {
    /* -n: no newline at the end. */
    int first = (argc > 1 && strcmp(argv[1], "-n") == 0) ? 2 : 1;
    for (int i = first; i < argc; i++) {
        if (i > first) out_puts(&sh->out, " ");
        out_puts(&sh->out, argv[i]);
    }
    if (first == 1) out_puts(&sh->out, "\n");
    return 0;
}

static int cmd_font(shell_t *sh, int argc, char **argv) {
    bool wide = !sh->wide;
    if (argc > 2) {
        shell_error(sh, "usage: font [8x10|5x10]\n");
        return 1;
    }
    if (argc == 2) {
        if (strcmp(argv[1], "8x10") == 0) wide = false;
        else if (strcmp(argv[1], "5x10") == 0) wide = true;
        else {
            shell_error(sh, "font: %s: no such font; 8x10 or 5x10\n", argv[1]);
            return 1;
        }
    }
    sh->wide = wide;
    out_puts(&sh->con, wide ? "\033[?4264h" : "\033[?4264l");
    return 0;
}

static bool stop_requested(void *ctx) {
    return shell_interrupted(ctx);
}

/* "123.45" for n * 100 / d, without floating point. */
static void rate(char *buf, size_t size, uint64_t n, uint64_t us) {
    uint64_t r = us ? n * 100000000u / us : 0;
    snprintf(buf, size, "%lu.%02lu", (unsigned long)(r / 100u), (unsigned long)(r % 100u));
}

/* The display test draws on the terminal itself, so it never writes to
 * a redirection. */
static int cmd_displaytest(shell_t *sh, int argc, char **argv) {
    (void)argc;
    (void)argv;
    const out_t *o = &sh->con;
    uint64_t t0 = sh->hooks.now_us(sh->hooks.ctx);
    int rows = displaytest_rows(o, sh->wide, stop_requested, sh);
    uint64_t t1 = sh->hooks.now_us(sh->hooks.ctx);
    unsigned long bytes;
    int chars = displaytest_chars(o, stop_requested, sh, &bytes);
    uint64_t t2 = sh->hooks.now_us(sh->hooks.ctx);
    displaytest_finish(o);

    char a[24], b[24], c[24];
    rate(a, sizeof a, (uint64_t)rows, t1 - t0);
    rate(b, sizeof b, bytes, t2 - t1);
    rate(c, sizeof c, (uint64_t)chars, t2 - t1);
    out_puts(o, "Display stress test complete.\n");
    out_printf(o, "\nRows processed: %d\n", rows);
    out_printf(o, "Rows time elapsed: %lu us\n", (unsigned long)(t1 - t0));
    out_printf(o, "Average rows per second: %s\n", a);
    out_printf(o, "\nCharacters processed: %lu\n", bytes);
    out_printf(o, "Characters time elapsed: %lu us\n", (unsigned long)(t2 - t1));
    out_printf(o, "Average characters per second: %s\n", b);
    out_printf(o, "Characters displayed: %d\n", chars);
    out_printf(o, "Average displayed cps: %s\n", c);
    sh->stop = false;   /* stopping it is how it ends, not a reason to drop the line */
    return 0;
}

static const struct {
    const char  *name;
    shell_cmd_fn fn;
} k_builtins[] = {
    { "cat",         cmd_cat },
    { "cd",          cmd_cd },
    { "cp",          cmd_cp },
    { "df",          cmd_df },
    { "displaytest", cmd_displaytest },
    { "echo",        cmd_echo },
    { "font",        cmd_font },
    { "ls",          cmd_ls },
    { "mkdir",       cmd_mkdir },
    { "mv",          cmd_mv },
    { "pwd",         cmd_pwd },
    { "rm",          cmd_rm },
    { "rmdir",       cmd_rmdir },
    { "touch",       cmd_touch },
};

/* One command with its redirections. */
static int run_cmd(shell_t *sh, const parse_cmd_t *c) {
    shell_cmd_fn fn = NULL;
    for (size_t i = 0; i < sizeof k_builtins / sizeof k_builtins[0]; i++)
        if (strcmp(c->argv[0], k_builtins[i].name) == 0) fn = k_builtins[i].fn;
    if (!fn) {
        shell_error(sh, "%s: not found\n", c->argv[0]);
        return 127;
    }
    if (c->in && !files_input(sh, c->in)) return 1;
    if (c->out && !files_redirect(sh, c->out, c->append)) {
        files_input_end(sh);
        return 1;
    }
    int status = fn(sh, c->argc, c->argv);
    if (c->out && !files_redirect_end(sh) && status == 0) status = 1;
    files_input_end(sh);
    return status;
}

/* The parser's output is large, and the shell's stack small. */
static parse_t s_parse;

int shell_run(shell_t *sh, const char *line) {
    sh->stop = false;
    if (!parse_line(&s_parse, line)) {
        shell_error(sh, "shell: %s\n", s_parse.error);
        return sh->status = 2;
    }
    for (int i = 0; i < s_parse.ncmds; i++) {
        const parse_cmd_t *c = &s_parse.cmds[i];
        /* A command skipped by && or || leaves the status as it was. */
        if (c->sep == SEP_AND && sh->status != 0) continue;
        if (c->sep == SEP_OR && sh->status == 0) continue;
        sh->status = run_cmd(sh, c);
        if (sh->stop) {
            out_puts(&sh->con, "^C\n");
            sh->status = 130;
            break;
        }
    }
    sh->stop = false;
    return sh->status;
}

/* ---- the line --------------------------------------------------------- */

void shell_input(shell_t *sh, int key) {
    if (key == '\r') {
        out_puts(&sh->con, "\n");
        sh->line[sh->len] = '\0';
        sh->len = 0;
        shell_run(sh, sh->line);
        prompt(sh);
    } else if (key == 0x08) {
        if (sh->len > 0) {
            sh->len--;
            out_puts(&sh->con, "\b \b");
        }
    } else if (key == 0x03) {   /* Ctrl-C: drop the line */
        sh->len = 0;
        out_puts(&sh->con, "^C\n");
        prompt(sh);
    } else if (key >= 0x20 && key < 0x7F && sh->len < SHELL_LINE_MAX) {
        char c = (char)key;
        sh->line[sh->len++] = c;
        out_write(&sh->con, &c, 1);
    }
}
