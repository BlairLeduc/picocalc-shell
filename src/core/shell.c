/* shell.c — the command line (shell.h). */

#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "displaytest.h"
#include "expand.h"
#include "files.h"
#include "history.h"
#include "line.h"
#include "parse.h"
#include "pipe.h"
#include "run.h"
#include "text.h"
#include "vars.h"
#include "vi.h"

#define PROFILE "/etc/profile"

/* Whether the terminal's cursor is at a line's start, from the bytes
 * sent to it: a CR or LF puts it there, a character takes it away, and
 * of the escape sequences only RIS and a cursor move to column 1 put it
 * there. Enough to start the prompt on a line of its own after output
 * with no final line break. */
static uint8_t  s_esc;        /* 0, after ESC, or in a CSI */
static unsigned s_esc_col;    /* a CSI's second parameter so far */
static bool     s_esc_semi;

static void con_write(void *ctx, const char *buf, size_t n) {
    shell_t *sh = ctx;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (s_esc == 1) {
            s_esc = c == '[' ? 2 : 0;
            s_esc_col = 0;
            s_esc_semi = false;
            if (c == 'c') sh->at_bol = true;
        } else if (s_esc == 2) {
            if (c == ';') s_esc_semi = true;
            else if (c >= '0' && c <= '9' && s_esc_semi) s_esc_col = s_esc_col * 10u + (c - '0');
            else if (c >= 0x40 && c <= 0x7E) {
                if (c == 'H' || c == 'f') sh->at_bol = s_esc_col <= 1u;
                s_esc = 0;
            }
        } else if (c == 0x1B) {
            s_esc = 1;
        } else if (c == '\r' || c == '\n') {
            sh->at_bol = true;
        } else if (c >= 0x20) {
            sh->at_bol = false;
        }
    }
    sh->term.write(sh->term.ctx, buf, n);
}

void shell_init(shell_t *sh, const out_t *out, const shell_hooks_t *hooks, const char *version) {
    memset(sh, 0, sizeof *sh);
    sh->term = *out;
    sh->con = (out_t){ con_write, sh, out->raw };
    sh->out = sh->con;
    sh->out_is_con = true;
    sh->at_bol = true;
    sh->hooks = *hooks;
    sh->version = version;
    strcpy(sh->cwd, "/");
    s_esc = 0;
    vars_init();
    vars_set("HOME", 4, SHELL_HOME);
    vars_set("PATH", 4, SHELL_PROGRAM_DIR);
    history_clear();
    line_start(sh, 0);
}

void shell_set_loader(shell_t *sh, const struct shell_loader *loader) {
    sh->loader = loader;
}

/* The current directory followed by '>' (design.md §4.4). */
void shell_prompt(shell_t *sh) {
    if (!sh->at_bol) out_puts(&sh->con, "\n");
    out_printf(&sh->con, "%s>", sh->cwd);
    line_start(sh, strlen(sh->cwd) + 1u);
}

static int script(shell_t *sh, const char *name);

void shell_start(shell_t *sh) {
    files_mount();
    const char *home = vars_get("HOME", 4);
    strcpy(sh->cwd, home && files_is_dir(home) ? home : "/");
    out_puts(&sh->con, "\033c");   /* RIS: a clean screen whatever ran before */
    out_printf(&sh->con, "\033[1mPicoCalc shell\033[0m %s\n\n", sh->version);
    history_load();
    script(sh, PROFILE);
    sh->stop = false;
    shell_prompt(sh);
}

void shell_leaving(shell_t *sh) {
    const char *err = history_save();
    if (err) shell_error(sh, "shell: %s: %s\n", HISTORY_FILE, err);
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

static int cmd_usbdisk(shell_t *sh, int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        shell_error(sh, "usage: usbdisk\n");
        return 1;
    }
    if (!sh->hooks.usbdisk) {
        shell_error(sh, "usbdisk: no USB here\n");
        return 1;
    }
    shell_leaving(sh);
    out_puts(&sh->con, "The card is on USB. Eject it on the computer to come back;\n"
                       "Esc comes back at once, which may lose what it was writing.\n");
    files_unmount();
    const char *err = sh->hooks.usbdisk(sh->hooks.ctx);
    files_mount();
    sh->stop = false;   /* Esc is how it ends, not a reason to drop the line */
    if (err) {
        shell_error(sh, "usbdisk: %s\n", err);
        return 1;
    }
    /* The card may have changed under the shell. */
    if (!files_is_dir(sh->cwd)) strcpy(sh->cwd, "/");
    history_load();
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
    { "export",      cmd_export },
    { "font",        cmd_font },
    { "grep",        cmd_grep },
    { "history",     cmd_history },
    { "ls",          cmd_ls },
    { "menu",        cmd_menu },
    { "mkdir",       cmd_mkdir },
    { "more",        cmd_more },
    { "mv",          cmd_mv },
    { "pwd",         cmd_pwd },
    { "rm",          cmd_rm },
    { "rmdir",       cmd_rmdir },
    { "set",         cmd_set },
    { "sh",          cmd_sh },
    { "touch",       cmd_touch },
    { "unset",       cmd_unset },
    { "usbdisk",     cmd_usbdisk },
    { "vi",          cmd_vi },
    { "view",        cmd_view },
};

#define NBUILTINS (sizeof k_builtins / sizeof k_builtins[0])

static shell_cmd_fn builtin(const char *name) {
    for (size_t i = 0; i < NBUILTINS; i++)
        if (strcmp(name, k_builtins[i].name) == 0) return k_builtins[i].fn;
    return NULL;
}

bool shell_is_builtin(const char *name) {
    return builtin(name) != NULL;
}

const char *shell_builtin_at(unsigned i) {
    return i < NBUILTINS ? k_builtins[i].name : NULL;
}

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

    out_t saved_out = sh->out;
    bool saved_is_con = sh->out_is_con;
    input_t saved_in = sh->in;
    if (from) sh->in = (input_t){ NULL, from };
    if (to) {
        sh->out = pipe_out(to);
        sh->out_is_con = false;
    }
    /* Anything that leaves the shell must not leave a redirection's
     * name.new or a pipe's file behind, here or at a level above. */
    bool redirected = in || out || !sh->out_is_con || input_any(&sh->in);

    int status;
    shell_cmd_fn fn = builtin(e->argv[0]);
    if (!fn) {
        status = run_program(sh, e->argc, e->argv, redirected);
    } else if ((fn == cmd_menu || fn == cmd_usbdisk) && redirected) {
        shell_error(sh, "%s: not with a redirection or a pipe\n", e->argv[0]);
        status = 1;
    } else if (in && !files_input(sh, in)) {
        status = 1;
    } else if (out && !files_redirect(sh, out, c->append)) {
        if (in) files_input_end(sh);
        status = 1;
    } else {
        status = fn(sh, e->argc, e->argv);
        if (out && !files_redirect_end(sh) && status == 0) status = 1;
        if (in) files_input_end(sh);
    }
    sh->out = saved_out;
    sh->out_is_con = saved_is_con;
    sh->in = saved_in;
    return status;
}

/* Commands first..end-1, joined by pipes; the last one's status. */
static int run_pipeline(shell_t *sh, const parse_t *p, int first, int end) {
    pipe_t *from = NULL;
    int status = 0;
    for (int k = first; k < end; k++) {
        pipe_t *to = NULL;
        const char *why;
        if (k + 1 < end && !(to = pipe_open(&why))) {
            shell_error(sh, "shell: %s\n", why);
            status = 1;
            break;
        }
        status = run_cmd(sh, &p->cmds[k], from, to);
        if (from) pipe_close(from);
        from = to;
        /* Output lost on the way: the next stage would read part of it. */
        if (to && (why = pipe_written(to)) != NULL) {
            shell_error(sh, "shell: pipe: %s\n", why);
            status = 1;
            break;
        }
        if (sh->stop) break;
    }
    if (from) pipe_close(from);
    return status;
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
        if (!(sep == SEP_AND && sh->status != 0) && !(sep == SEP_OR && sh->status == 0))
            sh->status = run_pipeline(sh, p, i, end);
        i = end;
    }
    return sh->status;
}

int shell_run(shell_t *sh, const char *line) {
    sh->stop = false;
    run_line(sh, line, NULL);
    if (sh->stop) {
        out_puts(&sh->con, "^C\n");
        sh->status = 130;
    }
    sh->stop = false;
    return sh->status;
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

/* ---- the prompt ------------------------------------------------------- */

void shell_input(shell_t *sh, int key) {
    if (!line_key(sh, key)) return;
    history_add(sh->line.text);
    shell_run(sh, sh->line.text);
    shell_prompt(sh);
}
