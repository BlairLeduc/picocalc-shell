/* select.c — `select file` (select.h).
 *
 * The menu file is never held in RAM. One pass over it draws the screen
 * and a second finds the entry for the key pressed, so the cost is one
 * line buffer. Both passes number the rows the same way, so a key shown
 * is a key found. */

#include "select.h"

#include <string.h>

#include "files.h"
#include "input.h"
#include "keycodes.h"
#include "path.h"
#include "run.h"
#include "select_const.h"

/* What a line of the file is: nothing (blank or a comment), an entry, or
 * a line that cannot be one (shown with a '?' for its key). */
typedef enum { LINE_SKIP, LINE_ENTRY, LINE_BAD } kind_t;

/* A line taken apart. The strings point into the line, are not NUL-ended
 * and have the lengths beside them. */
typedef struct {
    kind_t      kind;
    char        key;
    const char *prog;       /* the program */
    size_t      plen;
    const char *desc;       /* the description; the program if none */
    size_t      dlen;
    const char *text;       /* the whole line without blanks at its ends */
    size_t      tlen;
} entry_t;

static FIL      s_file;                      /* the menu file, open while the menu is up */
static reader_t s_rd;                        /* its lines (input.h) */
static char     s_line[SHELL_TEXT_LINE];     /* the line being looked at */
static char     s_prog[SHELL_PATH_MAX + 1];  /* the program for the key pressed */

static bool blank(char c) {
    return c == ' ' || c == '\t';
}

/* To lower case, so that t and T are one key. */
static int fold(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 'a' - 'A' : c;
}

/* Start reading the file from its first line. */
static bool rewind_menu(void) {
    if (f_lseek(&s_file, 0) != FR_OK) return false;
    reader_init(&s_rd, &(input_t){ &s_file, NULL });
    return true;
}

/* The next line into s_line; what does not fit is dropped. False at the
 * end of the file. */
static bool next_line(void) {
    bool whole;
    if (reader_line(&s_rd, s_line, sizeof s_line, &whole) < 0) return false;
    while (!whole) {
        char junk[SELECT_SKIP];
        if (reader_line(&s_rd, junk, sizeof junk, &whole) < 0) break;
    }
    return true;
}

/* The words after "#title" if `s` starts with it, otherwise NULL. */
static const char *title_of(const char *s) {
    const char *tag = SELECT_TITLE_TAG;
    while (*tag && fold((unsigned char)*s) == *tag) s++, tag++;
    if (*tag || (*s && !blank(*s))) return NULL;
    while (blank(*s)) s++;
    return s;
}

/* Split a line into key, program and description at its blanks. An entry
 * needs a key of one printable character, which is not the '?' that marks
 * a bad line, and a program. */
static void parse(const char *s, entry_t *e) {
    e->kind = LINE_SKIP;
    while (blank(*s)) s++;
    if (!*s || *s == SELECT_COMMENT_1 || *s == SELECT_COMMENT_2) return;

    e->kind = LINE_BAD;
    e->key = SELECT_BAD_KEY;
    e->text = s;
    e->tlen = strlen(s);
    while (e->tlen && blank(s[e->tlen - 1])) e->tlen--;

    const char *k = s;
    while (*s && !blank(*s)) s++;
    size_t klen = (size_t)(s - k);
    while (blank(*s)) s++;
    e->prog = s;
    while (*s && !blank(*s)) s++;
    e->plen = (size_t)(s - e->prog);
    while (blank(*s)) s++;
    e->desc = s;
    e->dlen = e->tlen - (size_t)(s - e->text);
    if (e->dlen == 0) {
        e->desc = e->prog;
        e->dlen = e->plen;
    }
    if (klen == 1 && *k > ' ' && *k < 0x7F && *k != SELECT_BAD_KEY && e->plen) {
        e->kind = LINE_ENTRY;
        e->key = *k;
    }
}

/* One row: the key, a gap and `n` bytes of text, cut at `w` columns. What
 * is not printable shows as a '?'. */
static void row(shell_t *sh, unsigned w, char key, const char *text, size_t n) {
    char buf[SELECT_WIDTH + 2];
    size_t len = 0;
    buf[len++] = key;
    for (const char *g = SELECT_GAP; *g && len < w; g++) buf[len++] = *g;
    for (size_t i = 0; i < n && len < w; i++) {
        unsigned char c = (unsigned char)text[i];
        buf[len++] = (c < ' ' || c >= 0x7F) ? SELECT_BAD_KEY : (char)c;
    }
    /* The terminal wraps as the last column is written, so a row that
     * fills the line must not be followed by a line end of its own. */
    if (len < shell_cols(sh)) buf[len++] = '\n';
    out_write(&sh->con, buf, len);
}

/* One pass over the file. When `show`, draw each row. Otherwise look for
 * the entry whose key is `want`, put its program in s_prog and say
 * whether it was there. An entry whose key was used higher up is a bad
 * line, and a bad line takes a row like any other. */
static bool pass(shell_t *sh, unsigned w, bool show, int want) {
    uint8_t seen[16] = { 0 };   /* a bit for each key, 0 to 127, in lower case */
    unsigned rows = 0;
    if (!rewind_menu()) return false;
    while (rows < SELECT_ROWS && next_line()) {
        entry_t e;
        parse(s_line, &e);
        if (e.kind == LINE_SKIP) continue;
        if (e.kind == LINE_ENTRY) {
            int f = fold((unsigned char)e.key);
            if (seen[f >> 3] & (1u << (f & 7))) e.kind = LINE_BAD, e.key = SELECT_BAD_KEY;
            else seen[f >> 3] |= (uint8_t)(1u << (f & 7));
        }
        rows++;
        if (show) {
            if (e.kind == LINE_BAD) row(sh, w, e.key, e.text, e.tlen);
            else row(sh, w, e.key, e.desc, e.dlen);
        } else if (e.kind == LINE_ENTRY && fold((unsigned char)e.key) == want) {
            if (e.plen >= sizeof s_prog) return false;
            memcpy(s_prog, e.prog, e.plen);
            s_prog[e.plen] = '\0';
            return true;
        }
    }
    return show;
}

/* The whole screen: the title (the file's "#title words" first line, or
 * the default), a gap, one row for each entry, and the footer. */
static void draw(shell_t *sh, unsigned w) {
    const char *title = SELECT_TITLE;
    size_t tlen = strlen(title);
    if (rewind_menu() && next_line()) {
        const char *t = title_of(s_line);
        if (t && *t) {
            title = t;
            tlen = strlen(t);
            while (tlen && blank(t[tlen - 1])) tlen--;
        }
    }
    if (tlen > w) tlen = w;
    out_puts(&sh->con, SELECT_CLEAR SELECT_BOLD);
    out_write(&sh->con, title, tlen);
    out_puts(&sh->con, tlen < shell_cols(sh) ? SELECT_PLAIN "\n\n" : SELECT_PLAIN "\n");
    pass(sh, w, true, 0);
    /* The footer is on the last row, and ends there without a line end,
     * which would scroll the screen. */
    out_printf(&sh->con, "\033[%u;1H" SELECT_FOOTER, TERM_ROWS);
}

/* Wait for a key that has an entry, and leave its program in s_prog. False
 * if the menu is left instead (Esc, Ctrl-C, Break or no more keys). A key
 * with no entry is ignored. */
static bool choose(shell_t *sh, unsigned w) {
    for (;;) {
        int k = sh->hooks.getkey(sh->hooks.ctx);
        if (k < 0 || k == SELECT_KEY_ESC || k == SELECT_KEY_INTR || k == KEY_BREAK) return false;
        if (k > ' ' && k < 0x7F && pass(sh, w, false, fold(k))) return true;
    }
}

int cmd_select(shell_t *sh, int argc, char **argv) {
    if (argc != 2) {
        shell_error(sh, "usage: select file\n");
        return 1;
    }
    if (!sh->hooks.getkey) {
        shell_error(sh, "select: there is no keyboard\n");
        return 1;
    }
    /* Either slash in the file's name. */
    char name[SHELL_PATH_MAX + 1];
    if (!path_slashes(name, sizeof name, argv[1])) {
        shell_error(sh, "select: %s: name too long\n", argv[1]);
        return 1;
    }
    if (files_open_read(sh, "select", name, &s_file) != FR_OK) return 1;

    unsigned w = shell_cols(sh);
    if (w > SELECT_WIDTH) w = SELECT_WIDTH;
    int status = 0;
    while (draw(sh, w), choose(sh, w)) {
        /* The program starts and the shell is gone, or it says why not,
         * and the menu comes back after a key. */
        run_named(sh, s_prog);
        out_puts(&sh->con, "\n" SELECT_PRESS);
        if (sh->hooks.getkey(sh->hooks.ctx) < 0) {
            status = 1;     /* no more keys; leave the message on the screen */
            break;
        }
    }
    f_close(&s_file);
    if (!status) out_puts(&sh->con, SELECT_CLEAR);
    return status;
}
