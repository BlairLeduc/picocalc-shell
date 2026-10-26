/* dos.h — the DOS flavour of the command line (design.md §4.7): what
 * its files share.
 *
 *   dos.c        the flavour (flavour.h): its builtins, the prompt,
 *                running a line, and CLS, VER, MEM, SET, PATH, PROMPT,
 *                ECHO and PAUSE
 *   dos_parse.c  %NAME% and the parser
 *   dos_path.c   C:\ paths, wildcards, and DOS's words for failures
 *   dos_files.c  CD, MD, RD, DIR, COPY, XCOPY, DEL, DELTREE, REN, MOVE,
 *                ATTRIB and VOL
 *   dos_text.c   TYPE, MORE and FIND
 *   dos_batch.c  batch files and AUTOEXEC.BAT: %0-%9, REM, IF, FOR,
 *                GOTO, SHIFT and CALL
 *   dos_time.c   DATE, TIME, and CONFIG.SYS's COUNTRY= (design.md §3.8)
 *
 * Underneath, the card is the UNIX flavour's: absolute paths with '/'
 * (path.h), resolved from what is typed by dos_resolve. */
#ifndef SHELL_DOS_H
#define SHELL_DOS_H

#include <stdbool.h>
#include <stddef.h>

#include "clock.h"
#include "config.h"
#include "ff.h"
#include "shell.h"

/* ---- paths (dos_path.c) ----------------------------------------------- */

/* `arg` as typed, `C:\HOME\X.TXT`, `..\X`, `/home/x` or `C:X`, made an
 * absolute path for FatFs: `/HOME/X.TXT`. A leading `C:` is dropped,
 * and '\' and '/' both separate names. False if it names another drive
 * or is too long. */
bool dos_resolve(char out[SHELL_PATH_MAX + 1], const char *cwd, const char *arg);

/* Whether `arg` starts with a drive other than C:. */
bool dos_other_drive(const char *arg);

/* An absolute path as DOS shows it: `C:\HOME\X.TXT`, and `C:\` for
 * the root. */
void dos_show(char *out, size_t size, const char *path);

/* Whether a name has a wildcard, '*' or '?'. */
bool dos_wild(const char *s);

/* Whether `name` matches the wildcard `pat`, letters without regard to
 * case. A pattern with no '.' matches the whole name; one with a '.'
 * matches the name's part before its last '.' and the part after it
 * separately, so `*.*` matches every name, `*.` names with no
 * extension, and `*.TXT` names ending .TXT. '*' matches the rest of a
 * part, and '?' one character, or none at a part's end, as in DOS. */
bool dos_match(const char *pat, const char *name);

/* REN's and COPY's new name for `name` from `mask`: `*.BAK` gives
 * README.BAK for README.TXT. Each part of the mask, before and after
 * its last '.', is taken against the name's: '?' keeps the name's
 * character there, '*' the rest of the name's part, and anything else
 * is itself. False if it does not fit. */
bool dos_rename(char *out, size_t size, const char *mask, const char *name);

/* A FatFs failure in DOS's words: "File not found", "Access denied". */
const char *dos_why(FRESULT r);

/* A reason the shared code gives (files_why, files_safe_*) in DOS's
 * words: "no such file or directory" is "File not found". */
const char *dos_say(const char *why);

/* ---- the parser (dos_parse.c) ----------------------------------------- */

typedef struct {
    int    argc;
    char **argv;        /* argv[0] the name; quotes removed */
    char  *tail;        /* what followed the name, as typed, less any
                         * redirection, for ECHO, SET, PATH and PROMPT */
    char  *in;          /* < file, or NULL */
    char  *out;         /* > or >> file, or NULL */
    bool   append;      /* >> */
} dos_cmd_t;

typedef struct {
    int         ncmds;
    dos_cmd_t   cmds[SHELL_CMDS_MAX];
    char       *words[SHELL_WORDS_MAX + SHELL_CMDS_MAX];
    char        text[2u * (SHELL_LINE_MAX + 1u) + SHELL_WORDS_MAX + 4u * SHELL_CMDS_MAX];
    const char *error;
} dos_parse_t;

/* `line` with each %NAME% replaced by the variable's value, the name
 * taken without regard to case, and empty if it is not set; %% is one
 * %, and any other % is itself. In a batch file, `params` are %0 to %9,
 * each NULL if it was not given, and so empty; NULL at the prompt,
 * where %1 is itself. False if the result does not fit `size`. */
bool dos_expand(char *out, size_t size, const char *line, const char *const *params);

/* Parse `line` (already expanded) into commands joined by '|'. Words
 * are split at blanks; "..." keeps blanks in a word and is removed. `\`
 * is a path's separator, never an escape. `<`, `>` and `>>` take the
 * word after them, anywhere in a command. A command's name ends at a
 * '/', '\' or '.' when what comes before it is a builtin, so `DIR/W`,
 * `CD..` and `CD\` work as in DOS. True, with p->ncmds commands (none
 * for a blank line); false with p->error. */
bool dos_parse(dos_parse_t *p, const char *line);

/* ---- running a line (dos.c, dos_batch.c) ------------------------------- */

/* ECHO's state: ON shows the prompt, and in a batch file each line. */
extern bool dos_echo_on;

/* What followed the running command's name, as typed, less any
 * redirection: for ECHO, SET, PATH, PROMPT and CALL. */
extern const char *dos_tail;

/* A line at sh->depth, typed or from a batch file: %0-%9 (in a batch
 * file) and %NAME% replaced, then REM, IF and FOR, and the rest through
 * the parser. With `echo`, the line is shown after the prompt first, as
 * a batch file does with ECHO ON. The last command's status. */
int dos_line(shell_t *sh, const char *line, bool echo);

/* A line already expanded, through the parser and its pipeline (dos.c). */
int dos_commands(shell_t *sh, const char *text);

/* The batch file `name` runs as, if it is one: NAME.BAT, or NAME as
 * typed if it ends .BAT, in the current directory, then each of PATH's;
 * a name with a path is looked for there alone. Its absolute path, or
 * NULL. */
const char *dos_batch_find(shell_t *sh, const char *name);

/* Run the batch file at `path`, typed as `name`, with the parameters in
 * `tail`, split at blanks, ',', ';' and '=' outside "...". From a batch
 * file, without `call`, it takes the place of the one running, as in
 * DOS; otherwise it runs at the level below and comes back. */
int dos_batch_run(shell_t *sh, const char *path, const char *name, const char *tail, bool call);

/* At start: C:\AUTOEXEC.BAT, if the card has it. */
void dos_autoexec(shell_t *sh);

/* ---- for the commands (dos.c) ------------------------------------------ */

/* Take the switches out of argv: each word that starts with '/', with
 * one or more of them (`/W/P`), or with `within`, the rest of any word
 * from its first '/', as DOS read them (`\/W` is `\` and `/W`). Letters
 * in `allowed` set their bit in *flags, without regard to case; argv
 * keeps the rest, *argc counting them. False, after "Invalid switch -
 * /X", for any other. Without `within`, a '/' inside a word is the
 * word's: DATE's 10/22/26, FIND's string. */
bool dos_switches(shell_t *sh, int *argc, char **argv, const char *allowed, bool within,
                  unsigned *flags);

/* An error in DOS's form, "why - name", or "why" with no name. */
void dos_error(shell_t *sh, const char *why, const char *name);

/* "Too many parameters - X" if argc is more than `max`; true if it is. */
bool dos_too_many(shell_t *sh, int argc, char **argv, int max);

/* "Press any key to continue . . ." and a key: false if it was Break,
 * Esc or Ctrl-C, or there is no keyboard. */
bool dos_pause(shell_t *sh);

/* `question` and a key, Y or N, without regard to case: true for Y.
 * Anything else asks again; Break, Esc, Ctrl-C and no keyboard are N. */
bool dos_ask(shell_t *sh, const char *question);

/* Whether the output is the screen, for /P and the pauses. */
bool dos_paging(const shell_t *sh);

/* ---- commands --------------------------------------------------------- */

int dos_cd(shell_t *sh, int argc, char **argv);
int dos_md(shell_t *sh, int argc, char **argv);
int dos_rd(shell_t *sh, int argc, char **argv);
int dos_dir(shell_t *sh, int argc, char **argv);
int dos_copy(shell_t *sh, int argc, char **argv);
int dos_xcopy(shell_t *sh, int argc, char **argv);
int dos_del(shell_t *sh, int argc, char **argv);
int dos_deltree(shell_t *sh, int argc, char **argv);
int dos_ren(shell_t *sh, int argc, char **argv);
int dos_move(shell_t *sh, int argc, char **argv);
int dos_attrib(shell_t *sh, int argc, char **argv);
int dos_vol(shell_t *sh, int argc, char **argv);
int dos_type(shell_t *sh, int argc, char **argv);
int dos_more(shell_t *sh, int argc, char **argv);
int dos_find(shell_t *sh, int argc, char **argv);
int dos_call(shell_t *sh, int argc, char **argv);
int dos_goto(shell_t *sh, int argc, char **argv);
int dos_shift(shell_t *sh, int argc, char **argv);
int dos_rem(shell_t *sh, int argc, char **argv);
int dos_control(shell_t *sh, int argc, char **argv);
int dos_choice(shell_t *sh, int argc, char **argv);
int dos_date(shell_t *sh, int argc, char **argv);
int dos_time(shell_t *sh, int argc, char **argv);

/* ---- the country (dos_time.c) ----------------------------------------- */

/* How a country writes a date, a time and a number, as COUNTRY= set
 * them in DOS: the date's order ('M' month-day-year, 'D' day-month-year,
 * 'Y' year-month-day), its separator and the time's, a 24-hour clock or
 * 12 with a or p, and the separators of thousands and of decimals. */
typedef struct {
    unsigned code;
    char     order, date_sep, time_sep;
    bool     h24;
    char     thousands, decimal;
} dos_country_t;

/* The country in effect: 001, the United States, until CONFIG.SYS says. */
const dos_country_t *dos_country(void);
/* False, and no change, for a code not in the table. */
bool dos_set_country(unsigned code);

/* C:\CONFIG.SYS, if there is one, before AUTOEXEC.BAT: COUNTRY=, and the
 * lines DOS's had that mean nothing here, passed over. */
void dos_config(shell_t *sh);

/* A date in the country's way, with a year of four digits or two; a
 * time, hours and minutes, with seconds and hundredths if asked, and a
 * or p on a 12-hour clock. */
void dos_format_date(char *buf, size_t size, const clock_tm_t *tm, bool long_year);
void dos_format_time(char *buf, size_t size, const clock_tm_t *tm, bool seconds);

#endif /* SHELL_DOS_H */
