/* text.c — `more` and `grep` (text.h). */

#include "text.h"

#include <string.h>

#include "editor_pattern.h"
#include "files.h"
#include "keycodes.h"
#include "term.h"

static FIL      s_file;
static reader_t s_rd;
static char     s_line[SHELL_TEXT_LINE];
static uint8_t  s_buf[SHELL_COPY_BUF];

/* ---- more ------------------------------------------------------------- */

typedef struct {
    unsigned row, col;      /* rows shown since the last --More--, and the column */
    unsigned page;          /* rows to show before the next one */
    size_t   n;             /* bytes held in s_buf */
    bool     quit;
} pager_t;

static void flush(shell_t *sh, pager_t *pg) {
    out_write(&sh->con, (const char *)s_buf, pg->n);
    pg->n = 0;
}

/* --More-- and a key: whether to go on. */
static bool wait(shell_t *sh, pager_t *pg) {
    flush(sh, pg);
    out_puts(&sh->con, "\033[7m--More--\033[0m");
    int key = sh->hooks.getkey(sh->hooks.ctx);
    out_puts(&sh->con, "\r\033[K");
    pg->row = 0;
    pg->page = TERM_ROWS - 1u;
    switch (key) {
    case 'q':
    case 'Q':
    case 0x1B:
    case 0x03:
    case KEY_BREAK:
    case -1:
        return false;
    case '\r':
    case KEY_DOWN:
        pg->page = 1;
        return true;
    default:
        return true;
    }
}

/* One byte to the screen, counting the rows it fills as term.c does. */
static void page_byte(shell_t *sh, pager_t *pg, char c) {
    unsigned cols = shell_cols(sh);
    if (pg->row >= pg->page && !wait(sh, pg)) {
        pg->quit = true;
        return;
    }
    if (pg->n == sizeof s_buf) flush(sh, pg);
    s_buf[pg->n++] = (uint8_t)c;
    unsigned char u = (unsigned char)c;
    if (u == '\n') {
        pg->row++;
        pg->col = 0;
    } else if (u == '\r') {
        pg->col = 0;
    } else if (u == '\b') {
        if (pg->col > 0) pg->col--;
    } else if (u == '\t') {
        pg->col = (pg->col / 8u + 1u) * 8u;
        if (pg->col > cols - 1u) pg->col = cols - 1u;
    } else if (u >= 0x20 && u != 0x7F) {
        if (++pg->col == cols) {
            pg->row++;
            pg->col = 0;
        }
    }
}

/* All of `in` to the screen a page at a time, or to sh->out as it is. */
static int more_in(shell_t *sh, const input_t *in, const char *shown, pager_t *pg) {
    for (;;) {
        UINT n;
        FRESULT r = input_read(in, s_line, sizeof s_line, &n);
        if (r != FR_OK) {
            if (pg) flush(sh, pg);
            shell_error(sh, "more: %s: %s\n", shown, files_why(r));
            return 1;
        }
        if (n == 0) return 0;
        if (!pg) {
            out_write(&sh->out, s_line, n);
            if (shell_interrupted(sh)) return 1;
            continue;
        }
        for (UINT i = 0; i < n && !pg->quit; i++) page_byte(sh, pg, s_line[i]);
        if (pg->quit) return 0;
    }
}

int cmd_more(shell_t *sh, int argc, char **argv) {
    pager_t pager = { .page = TERM_ROWS - 1u };
    pager_t *pg = sh->out_is_con && sh->hooks.getkey ? &pager : NULL;
    if (argc == 1) {
        if (!input_any(&sh->in)) {
            shell_error(sh, "usage: more file... (or more < file, or ... | more)\n");
            return 1;
        }
        int status = more_in(sh, &sh->in, "input", pg);
        if (pg) flush(sh, pg);
        return status;
    }
    int status = 0;
    for (int i = 1; i < argc && !sh->stop && !(pg && pg->quit); i++) {
        if (files_open_read(sh, "more", argv[i], &s_file) != FR_OK) {
            status = 1;
            continue;
        }
        if (argc > 2) {
            static const char rule[] = "::::::::::::::\n";
            const char *parts[] = { rule, argv[i], "\n", rule };
            for (unsigned k = 0; k < 4; k++) {
                if (!pg) out_puts(&sh->out, parts[k]);
                else
                    for (const char *c = parts[k]; *c && !pg->quit; c++) page_byte(sh, pg, *c);
            }
        }
        status |= more_in(sh, &(input_t){ &s_file, NULL }, argv[i], pg);
        f_close(&s_file);
    }
    if (pg) flush(sh, pg);
    return status;
}

/* ---- grep ------------------------------------------------------------- */

enum { GREP_V = 1u << 0, GREP_N = 1u << 1, GREP_C = 1u << 2, GREP_I = 1u << 3 };

/* The lines of `in` that are wanted; how many, or -1 on an error. */
static long grep_in(shell_t *sh, const input_t *in, const char *name, const char *pat,
                    unsigned flags) {
    reader_init(&s_rd, in);
    size_t plen = strlen(pat);
    long found = 0;
    unsigned long lineno = 0;
    bool whole = true;
    long n;
    for (;;) {
        bool start = whole;   /* a line's first piece */
        n = reader_line(&s_rd, s_line, sizeof s_line, &whole);
        if (n < 0) break;
        if (start) lineno++;
        EditorPatternGroups g;
        bool complex_ = false;
        bool hit = editor_pattern_search(pat, plen, s_line, (size_t)n, 0, g, &complex_);
        if (complex_) {
            shell_error(sh, "grep: the pattern is too complex for line %lu\n", lineno);
            return -1;
        }
        if (hit == !(flags & GREP_V)) {
            found++;
            if (!(flags & GREP_C)) {
                if (name) out_printf(&sh->out, "%s:", name);
                if (flags & GREP_N) out_printf(&sh->out, "%lu:", lineno);
                out_write(&sh->out, s_line, (size_t)n);
                out_puts(&sh->out, "\n");
            }
        }
        if (shell_interrupted(sh)) return -1;
    }
    if (s_rd.r != FR_OK) {
        shell_error(sh, "grep: %s: %s\n", name ? name : "input", files_why(s_rd.r));
        return -1;
    }
    if (flags & GREP_C) {
        if (name) out_printf(&sh->out, "%s:", name);
        out_printf(&sh->out, "%ld\n", found);
    }
    return found;
}

int cmd_grep(shell_t *sh, int argc, char **argv) {
    static const char use[] = "grep [-v] [-n] [-c] [-i] pattern [file...]";
    unsigned flags;
    int first = files_options(sh, argc, argv, "vnci", &flags, use);
    if (first < 0) return 2;
    if (first == argc) {
        shell_error(sh, "usage: %s\n", use);
        return 2;
    }
    const char *pat = argv[first++];
    if (!editor_pattern_valid(pat, strlen(pat))) {
        shell_error(sh, "grep: %s: not a pattern\n", pat);
        return 2;
    }
    if (first == argc) {
        if (!input_any(&sh->in)) {
            shell_error(sh, "usage: %s (or < file, or ... | grep)\n", use);
            return 2;
        }
        long n = grep_in(sh, &sh->in, NULL, pat, flags);
        return n < 0 ? 2 : n > 0 ? 0 : 1;
    }
    bool names = argc - first > 1;
    bool any = false, failed = false;
    for (int i = first; i < argc && !sh->stop; i++) {
        if (files_open_read(sh, "grep", argv[i], &s_file) != FR_OK) {
            failed = true;
            continue;
        }
        long n = grep_in(sh, &(input_t){ &s_file, NULL }, names ? argv[i] : NULL, pat, flags);
        f_close(&s_file);
        if (n < 0) failed = true;
        if (n > 0) any = true;
    }
    return failed ? 2 : any ? 0 : 1;
}
