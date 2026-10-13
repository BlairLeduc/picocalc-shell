/* shell.c — the command line, M0's part of it (shell.h). */

#include "shell.h"

#include <stdio.h>
#include <string.h>

#include "displaytest.h"

void shell_init(shell_t *sh, const out_t *out, const shell_hooks_t *hooks, const char *version) {
    memset(sh, 0, sizeof *sh);
    sh->out = *out;
    sh->hooks = *hooks;
    sh->version = version;
    sh->cwd = "/";
}

/* The current directory followed by '>' (design.md §4.4). */
static void prompt(shell_t *sh) {
    out_printf(&sh->out, "%s>", sh->cwd);
}

void shell_start(shell_t *sh) {
    out_puts(&sh->out, "\033c");   /* RIS: a clean screen whatever ran before */
    out_printf(&sh->out, "\033[1mPicoCalc shell\033[0m %s\n\n", sh->version);
    prompt(sh);
}

/* ---- commands --------------------------------------------------------- */

static void cmd_font(shell_t *sh, int argc, char **argv) {
    bool wide = !sh->wide;
    if (argc > 2) {
        out_puts(&sh->out, "usage: font [8x10|5x10]\n");
        return;
    }
    if (argc == 2) {
        if (strcmp(argv[1], "8x10") == 0) wide = false;
        else if (strcmp(argv[1], "5x10") == 0) wide = true;
        else {
            out_printf(&sh->out, "font: %s: no such font; 8x10 or 5x10\n", argv[1]);
            return;
        }
    }
    sh->wide = wide;
    out_puts(&sh->out, wide ? "\033[?4264h" : "\033[?4264l");
}

static bool stop_requested(void *ctx) {
    shell_t *sh = ctx;
    return sh->hooks.interrupted && sh->hooks.interrupted(sh->hooks.ctx);
}

/* "123.45" for n * 100 / d, without floating point. */
static void rate(char *buf, size_t size, uint64_t n, uint64_t us) {
    uint64_t r = us ? n * 100000000u / us : 0;
    snprintf(buf, size, "%lu.%02lu", (unsigned long)(r / 100u), (unsigned long)(r % 100u));
}

static void cmd_displaytest(shell_t *sh) {
    const out_t *o = &sh->out;
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
}

/* M0 splits on spaces only; M1's parser replaces this. */
static int split(char *s, char **argv) {
    int argc = 0;
    while (*s && argc < (int)SHELL_WORDS_MAX) {
        while (*s == ' ') *s++ = '\0';
        if (!*s) break;
        argv[argc++] = s;
        while (*s && *s != ' ') s++;
    }
    return argc;
}

static void run(shell_t *sh, char *line) {
    char *argv[SHELL_WORDS_MAX];
    int argc = split(line, argv);
    if (argc == 0) return;
    if (strcmp(argv[0], "font") == 0) cmd_font(sh, argc, argv);
    else if (strcmp(argv[0], "displaytest") == 0) cmd_displaytest(sh);
    else out_printf(&sh->out, "%s: not found\n", argv[0]);
}

/* ---- the line --------------------------------------------------------- */

void shell_input(shell_t *sh, int key) {
    if (key == '\r') {
        out_puts(&sh->out, "\n");
        sh->line[sh->len] = '\0';
        sh->len = 0;
        run(sh, sh->line);
        prompt(sh);
    } else if (key == 0x08) {
        if (sh->len > 0) {
            sh->len--;
            out_puts(&sh->out, "\b \b");
        }
    } else if (key == 0x03) {   /* Ctrl-C: drop the line */
        sh->len = 0;
        out_puts(&sh->out, "^C\n");
        prompt(sh);
    } else if (key >= 0x20 && key < 0x7F && sh->len < SHELL_LINE_MAX) {
        char c = (char)key;
        sh->line[sh->len++] = c;
        out_write(&sh->out, &c, 1);
    }
}
