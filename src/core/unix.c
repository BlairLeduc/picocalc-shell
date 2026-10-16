/* unix.c — the UNIX flavour's command language (flavour.h; design.md
 * §4.4): the parser (parse.h) and expansion (expand.h), `;`, `&&` and
 * `||`, NAME=value, `sh` scripts and /etc/profile, and its builtins:
 * the file commands (files.h), `more`, `grep`, `head`, `tail` and `wc`
 * (text.h), the PicoCalc's own (system.h), `echo`, `set`, `unset`,
 * `export`, `sh`, `history`, `menu`, `usbdisk`, `vi` and `view` (vi.h). */

#include <stdio.h>
#include <string.h>

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

/* Each builtin, with its usage and a line on what it does for `man`;
 * in order, as `man` and Tab list them. */
const shell_builtin_t flavour_builtins[] = {
    { "backlight",  cmd_backlight,  "backlight [lcd|kbd] [0-255]", "Shows or sets a backlight" },
    { "battery",    cmd_battery,    "battery", "Displays the battery's charge" },
    { "bootsel",    cmd_bootsel,    "bootsel", "Reboot into bootloader mode (BOOTSEL)" },
    { "cat",        cmd_cat,        "cat file... (or cat < file)", "Concatenate and print files" },
    { "cd",         cmd_cd,         "cd [dir]", "Change directory; $HOME with none" },
    { "clear",      cmd_clear,      "clear", "Clears the screen" },
    { "cp",         cmd_cp,         "cp [-r] from... to", "Copies files, -r directories" },
    { "df",         cmd_df,         "df", "Displays the card's size and free space" },
    { "echo",       cmd_echo,       "echo [-n] [word...]", "Print words; -n no line break" },
    { "export",     cmd_export,     "export [NAME=value...]", "Displays, sets or lists variables" },
    { "font",       cmd_font,       "font [8x10|5x10]", "40 or 64 columns; swap with none" },
    { "grep",       cmd_grep,       "grep [-v] [-n] [-c] [-i] pattern [file...]", "File pattern searcher" },
    { "head",       cmd_head,       "head [-n N] [file...]", "Displays the first lines of a file" },
    { "history",    cmd_history,    "history", "Displays the lines typed" },
    { "info",       cmd_info,       "info", "Displays diagnostic information" },
    { "ls",         cmd_ls,         "ls [-l] [-a] [path...]", "Lists directory contents" },
    { "man",        cmd_help,       "man [command]", "Display manual pages" },
    { "menu",       cmd_menu,       "menu", "UF2 Loader's menu" },
    { "mkdir",      cmd_mkdir,      "mkdir [-p] dir...", "Makes directories" },
    { "more",       cmd_more,       "more file... (or < file, or | more)", "Displays text a screen at a time" },
    { "mv",         cmd_mv,         "mv from... to", "Move or rename" },
    { "phosphor",   cmd_phosphor,   "phosphor [white|green|amber|default]", "Sets the colour of the text" },
    { "poweroff",   cmd_poweroff,   "poweroff", "Switches the PicoCalc off" },
    { "pwd",        cmd_pwd,        "pwd", "Print the current directory" },
    { "reboot",     cmd_reboot,     "reboot", "Restarts the shell" },
    { "rm",         cmd_rm,         "rm [-r] path...", "Removes files, -r directories" },
    { "rmdir",      cmd_rmdir,      "rmdir dir...", "Removes empty directories" },
    { "set",        cmd_set,        "set", "Lists the variables" },
    { "sh",         cmd_sh,         "sh file", "Runs a script" },
    { "tail",       cmd_tail,       "tail [-n N] [file...]", "Displays the last lines, 10 by default" },
    { "touch",      cmd_touch,      "touch file...", "Creates empty files" },
    { "unset",      cmd_unset,      "unset NAME...", "Removes variables" },
    { "usbdisk",    cmd_usbdisk,    "usbdisk", "Exposes the SD Card to a computer" },
    { "vi",         cmd_vi,         "vi [-R] file", "Edits a text file" },
    { "view",       cmd_view,       "view file", "Reads a text file in vi" },
    { "wc",         cmd_wc,         "wc [-l] [-w] [-c] [file...]", "Counts lines, words and bytes" },
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
