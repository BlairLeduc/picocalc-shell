/* unix.c — the UNIX flavour's command language (flavour.h; design.md
 * §4.4): the parser (parse.h) and expansion (expand.h), `;`, `&&` and
 * `||`, NAME=value, `sh` scripts and /etc/profile, and its builtins:
 * the file commands (files.h), `more`, `grep`, `head`, `tail` and `wc`
 * (text.h), the PicoCalc's own (system.h), `echo`, `set`, `unset`,
 * `export`, `sh`, `history`, `menu`, `usbdisk`, `vi` and `view` (vi.h),
 * and the development command `displaytest`. */

#include <stdio.h>
#include <string.h>

#include "displaytest.h"
#include "expand.h"
#include "files.h"
#include "flavour.h"
#include "history.h"
#include "parse.h"
#include "path.h"
#include "pipe.h"
#include "run.h"
#include "system.h"
#include "text.h"
#include "vars.h"
#include "vi.h"

#define PROFILE "/etc/profile"

static int script(shell_t *sh, const char *name);

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

/* NAME=value, as typed, into a variable. */
static int assign(shell_t *sh, const char *word, expand_t *e) {
    const char *eq;
    expand_is_assignment(word, &eq);
    e->used = 0;
    const char *value = expand_one(sh, eq + 1, strlen(eq + 1), e);
    if (!value) return 1;
    if (!vars_set(word, (size_t)(eq - word), value)) {
        shell_error(sh, "shell: no room for %.*s\n", (int)(eq - word), word);
        return 1;
    }
    return 0;
}

static int cmd_set(shell_t *sh, int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        shell_error(sh, "usage: set, to list the variables; NAME=value sets one\n");
        return 1;
    }
    const char *v;
    for (unsigned i = 0; (v = vars_at(i)) != NULL; i++) out_printf(&sh->out, "%s\n", v);
    return 0;
}

static int cmd_unset(shell_t *sh, int argc, char **argv) {
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (!vars_is_name(argv[i], strlen(argv[i]))) {
            shell_error(sh, "unset: %s: not a name\n", argv[i]);
            status = 1;
            continue;
        }
        vars_unset(argv[i], strlen(argv[i]));
    }
    return status;
}

/* export NAME=value sets it; export NAME is accepted and does nothing,
 * since every variable is the session's (vars.h); export alone lists. */
static int cmd_export(shell_t *sh, int argc, char **argv) {
    if (argc == 1) return cmd_set(sh, argc, argv);
    int status = 0;
    for (int i = 1; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        size_t n = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
        if (!vars_is_name(argv[i], n)) {
            shell_error(sh, "export: %s: not a name\n", argv[i]);
            status = 1;
        } else if (eq && !vars_set(argv[i], n, eq + 1)) {
            shell_error(sh, "export: no room for %.*s\n", (int)n, argv[i]);
            status = 1;
        }
    }
    return status;
}

static int cmd_sh(shell_t *sh, int argc, char **argv) {
    if (argc != 2) {
        shell_error(sh, "usage: sh file\n");
        return 1;
    }
    return script(sh, argv[1]);
}

/* Each builtin, with its usage and a line on what it does for `help`;
 * in order, as `help` and Tab list them. */
const shell_builtin_t flavour_builtins[] = {
    { "backlight",   cmd_backlight,   "backlight [lcd|kbd] [0-255]", "show or set a backlight" },
    { "battery",     cmd_battery,     "battery", "the battery's charge" },
    { "bootsel",     cmd_bootsel,     "bootsel", "reboot into BOOTSEL, as a USB drive" },
    { "cat",         cmd_cat,         "cat file... (or cat < file)", "show or join files" },
    { "cd",          cmd_cd,          "cd [dir]", "change directory; $HOME with none" },
    { "clear",       cmd_clear,       "clear", "clear the screen" },
    { "cp",          cmd_cp,          "cp [-r] from... to", "copy files, -r directories" },
    { "df",          cmd_df,          "df", "the card's size and free space" },
    { "displaytest", cmd_displaytest, "displaytest", "the display's stress test" },
    { "echo",        cmd_echo,        "echo [-n] [word...]", "print words; -n no line break" },
    { "export",      cmd_export,      "export [NAME=value...]", "set variables, or list them" },
    { "font",        cmd_font,        "font [8x10|5x10]", "40 or 64 columns; swap with none" },
    { "grep",        cmd_grep,        "grep [-v] [-n] [-c] [-i] pattern [file...]", "lines that match a pattern" },
    { "head",        cmd_head,        "head [-n N] [file...]", "the first lines, 10 by default" },
    { "help",        cmd_help,        "help [command]", "list the commands, or explain one" },
    { "history",     cmd_history,     "history", "the lines typed before" },
    { "info",        cmd_info,        "info", "the shell, board, loader and card" },
    { "ls",          cmd_ls,          "ls [-l] [-a] [path...]", "list a directory" },
    { "menu",        cmd_menu,        "menu", "UF2 Loader's own menu" },
    { "mkdir",       cmd_mkdir,       "mkdir [-p] dir...", "make directories" },
    { "more",        cmd_more,        "more file... (or < file, or | more)", "show text a screen at a time" },
    { "mv",          cmd_mv,          "mv from... to", "move or rename" },
    { "phosphor",    cmd_phosphor,    "phosphor [white|green|amber|default]", "the colour of the text" },
    { "poweroff",    cmd_poweroff,    "poweroff", "switch the PicoCalc off" },
    { "pwd",         cmd_pwd,         "pwd", "print the current directory" },
    { "reboot",      cmd_reboot,      "reboot", "restart the shell" },
    { "rm",          cmd_rm,          "rm [-r] path...", "remove files, -r directories" },
    { "rmdir",       cmd_rmdir,       "rmdir dir...", "remove empty directories" },
    { "set",         cmd_set,         "set", "list the variables" },
    { "sh",          cmd_sh,          "sh file", "run a script" },
    { "tail",        cmd_tail,        "tail [-n N] [file...]", "the last lines, 10 by default" },
    { "touch",       cmd_touch,       "touch file...", "make empty files" },
    { "unset",       cmd_unset,       "unset NAME...", "remove variables" },
    { "usbdisk",     cmd_usbdisk,     "usbdisk", "offer the card to a computer" },
    { "vi",          cmd_vi,          "vi [-R] file", "edit a text file" },
    { "view",        cmd_view,        "view file", "read a text file in vi" },
    { "wc",          cmd_wc,          "wc [-l] [-w] [-c] [file...]", "count lines, words and bytes" },
};

const unsigned flavour_nbuiltins = sizeof flavour_builtins / sizeof flavour_builtins[0];

/* ---- running a line --------------------------------------------------- */

/* Each level's line, parsed, and the words of the command it is running.
 * The parser's output is large, and the shell's stack small. */
static struct {
    parse_t  parse;
    expand_t exp;
} s_frames[SHELL_DEPTH];

/* One command with its redirections, reading `from` and writing `to`
 * when it is in a pipeline. */
static int run_cmd(shell_t *sh, const parse_cmd_t *c, pipe_t *from, pipe_t *to) {
    expand_t *e = &s_frames[sh->depth].exp;
    const char *eq;
    if (expand_is_assignment(c->argv[0], &eq)) {
        if (c->in || c->out) {
            shell_error(sh, "shell: %s: NAME=value with a redirection is not supported\n",
                        c->argv[0]);
            return 1;
        }
        for (int i = 0; i < c->argc; i++) {
            if (!expand_is_assignment(c->argv[i], &eq)) {
                shell_error(sh, "shell: %s: NAME=value before a command is not supported\n",
                            c->argv[0]);
                return 1;
            }
        }
        for (int i = 0; i < c->argc; i++)
            if (assign(sh, c->argv[i], e)) return 1;
        return 0;
    }

    if (!expand_args(sh, c->argv, e)) return 1;
    char *in = NULL, *out = NULL;
    if (c->in && !(in = expand_one(sh, c->in, strlen(c->in), e))) return 1;
    if (c->out && !(out = expand_one(sh, c->out, strlen(c->out), e))) return 1;
    if ((in && !*in) || (out && !*out)) {
        shell_error(sh, "shell: a redirection with no file\n");
        return 1;
    }
    if (e->argc == 0) return 0;   /* only empty variables */
    return shell_command(sh, e->argc, e->argv, in, out, c->append, from, to);
}

/* The k-th command of the pipeline that starts at `first`. */
typedef struct {
    const parse_t *p;
    int            first;
} stages_t;

static int stage(shell_t *sh, int k, pipe_t *from, pipe_t *to, void *ctx) {
    const stages_t *s = ctx;
    return run_cmd(sh, &s->p->cmds[s->first + k], from, to);
}

/* A line at this level. `where` names a script's file and line for a
 * mistake in it; NULL for a typed line. */
static int run_line(shell_t *sh, const char *line, const char *where) {
    parse_t *p = &s_frames[sh->depth].parse;
    if (!parse_line(p, line)) {
        if (where) shell_error(sh, "sh: %s: %s\n", where, p->error);
        else shell_error(sh, "shell: %s\n", p->error);
        return sh->status = 2;
    }
    for (int i = 0; i < p->ncmds && !sh->stop;) {
        int end = i + 1;
        while (end < p->ncmds && p->cmds[end].sep == SEP_PIPE) end++;
        /* A pipeline skipped by && or || leaves the status as it was. */
        sep_t sep = p->cmds[i].sep;
        if (!(sep == SEP_AND && sh->status != 0) && !(sep == SEP_OR && sh->status == 0)) {
            stages_t s = { p, i };
            sh->status = shell_pipeline(sh, end - i, stage, &s);
        }
        i = end;
    }
    return sh->status;
}

int flavour_run_line(shell_t *sh, const char *line) {
    return run_line(sh, line, NULL);
}

/* Each level's script and its reader. */
static FIL      s_script[SHELL_DEPTH];
static reader_t s_reader[SHELL_DEPTH];
static char     s_text[SHELL_DEPTH][SHELL_LINE_MAX + 2];
static char     s_where[SHELL_DEPTH][SHELL_PATH_MAX + 16];

/* Run the file `name` a line at a time at the level below, to its end
 * or a stop; the last command's status. /etc/profile, if there is none,
 * is nothing. */
static int script(shell_t *sh, const char *name) {
    unsigned d = sh->depth + 1u;
    bool profile = strcmp(name, PROFILE) == 0 && sh->depth == 0;
    if (d >= SHELL_DEPTH) {
        shell_error(sh, "sh: %s: scripts nested more than %u deep\n", name, SHELL_DEPTH - 1u);
        return 2;
    }
    FRESULT r = profile ? files_open_startup(&s_script[d], name)
                        : files_open_read(sh, "sh", name, &s_script[d]);
    if (r != FR_OK) {
        if (profile && r != FR_NO_FILE && r != FR_NO_PATH && r != FR_NOT_READY)
            shell_error(sh, "sh: %s: %s\n", name, files_why(r));
        return profile ? 0 : 1;
    }
    reader_init(&s_reader[d], &(input_t){ &s_script[d], NULL });
    sh->depth = d;
    int status = 0;
    unsigned lineno = 0;
    bool whole = true;
    long n;
    while (!sh->stop && (n = reader_line(&s_reader[d], s_text[d], sizeof s_text[d], &whole)) >= 0) {
        bool piece = !whole;
        lineno++;
        while (!whole && reader_line(&s_reader[d], s_text[d], sizeof s_text[d], &whole) >= 0) {}
        snprintf(s_where[d], sizeof s_where[d], "%s: line %u", name, lineno);
        if (piece || (size_t)n > SHELL_LINE_MAX) {
            shell_error(sh, "sh: %s: line too long\n", s_where[d]);
            status = sh->status = 2;
            continue;
        }
        status = run_line(sh, s_text[d], s_where[d]);
    }
    if (s_reader[d].r != FR_OK) {
        shell_error(sh, "sh: %s: %s\n", name, files_why(s_reader[d].r));
        status = 1;
    }
    f_close(&s_script[d]);
    sh->depth = d - 1u;
    return status;
}

/* ---- the rest of flavour.h -------------------------------------------- */

void flavour_init(shell_t *sh) {
    (void)sh;
    vars_set("HOME", 4, SHELL_HOME);
    vars_set("PATH", 4, SHELL_PROGRAM_DIR);
}

void flavour_start(shell_t *sh) {
    out_printf(&sh->con, "\033[1mPicoCalc shell\033[0m %s\n\n", sh->version);
    script(sh, PROFILE);
}

/* The current directory followed by '>' (design.md §4.4). */
void flavour_prompt(shell_t *sh, char *buf, size_t size) {
    snprintf(buf, size, "%s>", sh->cwd);
}

bool flavour_resolve(char out[SHELL_PATH_MAX + 1], const char *cwd, const char *arg) {
    return path_resolve(out, cwd, arg);
}

bool flavour_is_path(const char *name) {
    return strchr(name, '/') != NULL;
}

void flavour_file_error(shell_t *sh, const char *cmd, const char *name, const char *why) {
    shell_error(sh, "%s: %s: %s\n", cmd, name, why);
}

void flavour_not_found(shell_t *sh, const char *name) {
    shell_error(sh, "%s: not found\n", name);
}
