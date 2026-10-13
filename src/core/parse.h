/* parse.h — one typed line into commands (design.md §4.4).
 *
 * M1's part of the language: words with '…' and "…" quoting and \
 * escapes, `#` comments, the separators `;`, `&&` and `||`, and the
 * redirections `>`, `>>` and `<`. Variables, globs and pipes come later
 * (§10, M4); `|` is refused for now rather than taken as a word.
 *
 * The whole line is parsed before anything runs, so a mistake at its end
 * stops all of it, as in sh. */
#ifndef SHELL_PARSE_H
#define SHELL_PARSE_H

#include <stdbool.h>

#include "config.h"

typedef enum {
    SEP_FIRST,      /* the line's first command */
    SEP_SEMI,       /* ;  */
    SEP_AND,        /* && */
    SEP_OR,         /* || */
} sep_t;

typedef struct {
    sep_t  sep;                 /* what joins it to the command before */
    int    argc;
    char **argv;                /* into parse_t.words */
    char  *in;                  /* < file, or NULL */
    char  *out;                 /* > or >> file, or NULL */
    bool   append;              /* >> */
} parse_cmd_t;

typedef struct {
    int         ncmds;
    parse_cmd_t cmds[SHELL_CMDS_MAX];
    char       *words[SHELL_WORDS_MAX + SHELL_CMDS_MAX];   /* argv arrays, NULL-ended */
    char        text[SHELL_LINE_MAX + 1];                  /* the words' characters */
    const char *error;          /* what was wrong, when parse_line fails */
} parse_t;

/* Parse `line`, of at most SHELL_LINE_MAX characters. True with
 * p->ncmds commands, possibly none for a blank line or a comment; false
 * with p->error set. The line is not changed. */
bool parse_line(parse_t *p, const char *line);

#endif /* SHELL_PARSE_H */
