/* test_select.c — `select` (select.h): the menu file's formats,
 * the screen it draws on the terminal's grid, and the keys it takes.
 *
 * A program that exists is told from one that does not by what the
 * shell says when it cannot run it: the loader here has a problem, so an
 * existing program gives "cannot run programs" and a missing one "not
 * found". */

#include <stdio.h>
#include <string.h>

#include "files.h"
#include "host_disk.h"
#include "keycodes.h"
#include "run.h"
#include "select_const.h"
#include "shell.h"
#include "term.h"
#include "test_util.h"

static term_t      t;
static shell_t     sh;
static char        con[1 << 14];
static size_t      con_len;
static const char *keys;
static unsigned    waits;      /* how many times select asked for a key */
static term_t      shots[8];   /* the screen at each of the first waits */

static shell_loader_t loader = { "no loader in the test", { 0 }, NULL, NULL };

static void sink(void *ctx, const char *buf, size_t n) {
    (void)ctx;
    term_write(&t, buf, n);
    for (size_t i = 0; i < n; i++)
        if (buf[i] != '\r' && con_len + 1 < sizeof con) con[con_len++] = buf[i];
    con[con_len] = '\0';
}

static uint64_t now(void *ctx) {
    (void)ctx;
    return 0;
}

static bool interrupted(void *ctx) {
    (void)ctx;
    return false;
}

static int getkey(void *ctx) {
    (void)ctx;
    if (waits < sizeof shots / sizeof shots[0]) shots[waits] = t;
    waits++;
    if (!keys || !*keys) return -1;
    return (unsigned char)*keys++;
}

/* Run a line with the keys typed from `script`, on a clear screen. */
static int run_keys(const char *line, const char *script) {
    con_len = 0;
    con[0] = '\0';
    keys = script;
    waits = 0;
    term_init(&t);
    if (sh.wide) term_write(&t, "\033[?4264h", 8);
    return shell_run(&sh, line);
}

static const char *row(const term_t *s, unsigned r) {
    static char buf[TERM_COLS_MAX + 1];
    term_row_text(s, r, buf);
    return buf;
}

static bool put(const char *path, const char *text) {
    FIL f;
    UINT n;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    FRESULT r = f_write(&f, text, (UINT)strlen(text), &n);
    return f_close(&f) == FR_OK && r == FR_OK && n == strlen(text);
}

/* The same menu with LF line ends and with CR LF: eight rows. */
#define MENU_LF                                                         \
    "#TITLE My Programs\n"                                              \
    "# a comment\n"                                                     \
    "a prog   Oric Atmos  emulator  \n"                                 \
    "B\tprog\tAcorn Atom\n"                                             \
    "; another comment\n"                                               \
    "\n"                                                                \
    "  c   nosuch  Not there\n"                                         \
    "d prog\n"                                                          \
    "b prog  Same key as B\n"                                           \
    "x\n"                                                               \
    "l prog Last line, no line end"

static const char *crlf(const char *lf) {
    static char buf[1024];
    size_t n = 0;
    for (; *lf; lf++) {
        if (*lf == '\n') buf[n++] = '\r';
        buf[n++] = *lf;
    }
    buf[n] = '\0';
    return buf;
}

/* The screen the menu above draws, row by row. */
static int check_menu_screen(const term_t *s, const char *what) {
    CHECK(strcmp(row(s, 0), "My Programs") == 0, "%s title: '%s'", what, row(s, 0));
    CHECK(strcmp(row(s, 1), "") == 0, "%s gap", what);
    CHECK(strcmp(row(s, 2), "a  Oric Atmos  emulator") == 0, "%s: '%s'", what, row(s, 2));
    CHECK(strcmp(row(s, 3), "B  Acorn Atom") == 0, "%s: '%s'", what, row(s, 3));
    CHECK(strcmp(row(s, 4), "c  Not there") == 0, "%s: '%s'", what, row(s, 4));
    CHECK(strcmp(row(s, 5), "d  prog") == 0, "%s: '%s'", what, row(s, 5));
    CHECK(strcmp(row(s, 6), "?  b prog  Same key as B") == 0, "%s: '%s'", what, row(s, 6));
    CHECK(strcmp(row(s, 7), "?  x") == 0, "%s: '%s'", what, row(s, 7));
    CHECK(strcmp(row(s, 8), "l  Last line, no line end") == 0, "%s: '%s'", what,
          row(s, 8));
    CHECK(strcmp(row(s, 9), "") == 0, "%s: nothing more", what);
    CHECK(strcmp(row(s, 31), "ESC=QUIT") == 0, "%s footer: '%s'", what, row(s, 31));
    return 0;
}

int main(void) {
    const out_t out = { sink, NULL, false };
    const shell_hooks_t hooks = { now, interrupted, getkey, NULL, NULL };
    shell_init(&sh, &out, &hooks, "test");
    shell_set_loader(&sh, &loader);
    CHECK(host_disk_create(NULL, 32u << 20, false), "image");
    files_mount();
    shell_start(&sh);
    CHECK(f_mkdir("/pico2-apps") == FR_OK && put("/pico2-apps/prog.uf2", "x"), "prog");

    /* ---- the screen, from either line end ---- */
    CHECK(put("/m.txt", MENU_LF) && put("/mcr.txt", crlf(MENU_LF)), "menus");
    CHECK(run_keys("select m.txt", "\x1b") == 0 && waits == 1, "esc: %u", waits);
    check_menu_screen(&shots[0], "LF");
    CHECK(strcmp(row(&t, 0), "") == 0 && strcmp(row(&t, 31), "") == 0,
          "cleared on leaving: '%s'", row(&t, 0));
    CHECK(run_keys("select mcr.txt", "\x1b") == 0, "esc");
    check_menu_screen(&shots[0], "CRLF");

    /* ---- the default title; a title only on the first line ---- */
    CHECK(put("/plain", "a prog Plain\n#TITLE not first\n") &&
              put("/titled", "#Title\ta   long title\t \na prog x\n") &&
              put("/notitle", "#TITLEX not a title\na prog x\n"),
          "files");
    CHECK(run_keys("select plain", "\x1b") == 0 &&
              strcmp(row(&shots[0], 0), "SELECT A PROGRAM") == 0 &&
              strcmp(row(&shots[0], 2), "a  Plain") == 0 && strcmp(row(&shots[0], 3), "") == 0,
          "default: '%s' '%s'", row(&shots[0], 0), row(&shots[0], 3));
    CHECK(run_keys("select titled", "\x1b") == 0 &&
              strcmp(row(&shots[0], 0), "a   long title") == 0,
          "title: '%s'", row(&shots[0], 0));
    CHECK(run_keys("select notitle", "\x1b") == 0 &&
              strcmp(row(&shots[0], 0), "SELECT A PROGRAM") == 0,
          "TITLEX: '%s'", row(&shots[0], 0));

    /* ---- long lines are cut at 40 columns, in either font ---- */
    char longmenu[600] = "a prog ";
    strcat(longmenu, "0123456789012345678901234567890123456789012345678901234567890123456789"
                     "0123456789\n");
    strcat(longmenu, "#TITLE 0123456789012345678901234567890123456789ABCDEFGH\n");
    CHECK(put("/long", longmenu), "long");
    for (int pass = 0; pass < 2; pass++) {
        if (pass) CHECK(run_keys("font 5x10", "") == 0 && sh.wide, "font");
        CHECK(run_keys("select long", "\x1b") == 0 &&
                  strcmp(row(&shots[0], 2), "a  0123456789012345678901234567890123456") == 0,
              "pass %d: '%s'", pass, row(&shots[0], 2));
        CHECK(strlen(row(&shots[0], 2)) == 40 && strcmp(row(&shots[0], 3), "") == 0,
              "no wrap: '%s'", row(&shots[0], 3));
    }
    CHECK(put("/longtitle", "#TITLE 0123456789012345678901234567890123456789ABCDEFGH\na prog x\n") &&
              run_keys("select longtitle", "\x1b") == 0 &&
              strcmp(row(&shots[0], 0), "0123456789012345678901234567890123456789") == 0 &&
              strcmp(row(&shots[0], 1), "") == 0 && strcmp(row(&shots[0], 2), "a  x") == 0,
          "long title: '%s'", row(&shots[0], 0));
    CHECK(run_keys("font 8x10", "") == 0 && !sh.wide, "font back");

    /* A line longer than the line buffer does not upset the ones after. */
    {
        static char huge[2000];
        memset(huge, 'Z', sizeof huge - 20);
        memcpy(huge, "a prog ", 7);
        strcpy(huge + sizeof huge - 20, "\nb prog Next\n");
        CHECK(put("/huge", huge), "huge");
        CHECK(run_keys("select huge", "\x1b") == 0 &&
                  strncmp(row(&shots[0], 2), "a  ZZZZ", 7) == 0,
              "huge a: '%s'", row(&shots[0], 2));
        CHECK(strcmp(row(&shots[0], 3), "b  Next") == 0, "huge b: '%s'", row(&shots[0], 3));
    }

    /* ---- keys ---- */
    /* A key found (in either case) runs the program. Here it cannot be
     * started, so the error shows, a key brings the menu back, and Esc
     * leaves. */
    CHECK(run_keys("select m.txt", "a \x1b") == 0 && waits == 3 &&
              strstr(con, "prog: cannot run programs: no loader in the test\n") &&
              strstr(con, "PRESS A KEY"),
          "a: '%s' %u", con, waits);
    check_menu_screen(&shots[2], "redrawn");
    CHECK(run_keys("select m.txt", "A \x1b") == 0 && waits == 3 &&
              strstr(con, "cannot run programs"),
          "A: '%s'", con);
    CHECK(run_keys("select m.txt", "b \x1b") == 0 && strstr(con, "cannot run programs"),
          "b is B's: '%s'", con);
    CHECK(run_keys("select m.txt", "d \x1b") == 0 && strstr(con, "cannot run programs"),
          "d: '%s'", con);
    /* A program that is not there is said so; the menu comes back. */
    CHECK(run_keys("select m.txt", "c \x1b") == 0 && waits == 3 &&
              strstr(con, "nosuch: not found\n") && !strstr(con, "cannot run"),
          "c: '%s'", con);
    /* Keys with no entry are ignored, as are a bad row's: nothing runs. */
    CHECK(run_keys("select m.txt", "z?x1 \x1b") == 0 && waits == 6 &&
              !strstr(con, "cannot run") && !strstr(con, "not found"),
          "ignored: '%s' %u", con, waits);
    /* The last line has no line end after it. */
    CHECK(run_keys("select m.txt", "l \x1b") == 0 && strstr(con, "prog: cannot run"),
          "last line: '%s'", con);
    /* Each way to leave. */
    const char *leave[] = { "\x1b", "\x03", "\xd0", "" };
    for (size_t i = 0; i < 4; i++)
        CHECK(run_keys("select m.txt", leave[i]) == 0 && waits == 1 &&
                  strcmp(row(&t, 0), "") == 0 && !strstr(con, "cannot"),
              "leave %zu", i);
    /* The input ends on the "press a key" line. */
    CHECK(run_keys("select m.txt", "a") == 1 && waits == 2, "ends after an error");

    /* ---- as many entries as fit; the rest are not shown or found ---- */
    {
        static char many[2000];
        size_t n = 0;
        char last = 0, hidden = 0;
        for (char k = '!', i = 0; i < 35; k++) {
            /* Not the comment characters, and not the key of bad rows. */
            if (k == '?' || k == '#' || k == ';') continue;
            n += (size_t)snprintf(many + n, sizeof many - n, "%c nosuch%d\n", k, i);
            if (i == SELECT_ROWS - 1) last = k;
            if (i == SELECT_ROWS) hidden = k;
            i++;
        }
        CHECK(put("/many", many), "many");
        CHECK(run_keys("select many", "\x1b") == 0, "many");
        char want[32];
        snprintf(want, sizeof want, "%c  nosuch%u", last, SELECT_ROWS - 1u);
        CHECK(strcmp(row(&shots[0], 30), want) == 0 &&
                  strcmp(row(&shots[0], 31), "ESC=QUIT") == 0,
              "last row: '%s' wanted '%s'", row(&shots[0], 30), want);
        /* The first entry that does not fit is not shown or found. */
        char hide[3] = { hidden, '\x1b', 0 }, show[4] = { last, ' ', '\x1b', 0 };
        CHECK(run_keys("select many", hide) == 0 && waits == 2 &&
                  !strstr(con, "not found"),
              "hidden entry: '%s'", con);
        CHECK(run_keys("select many", show) == 0 && strstr(con, "not found"),
              "last shown: '%s'", con);
    }

    /* ---- folders, both slashes, and the .uf2 ending ---- */
    /* Programs are found along $PATH (here /pico2-apps), a folder in the
     * name kept; the ending is optional; a name from the root or from ./
     * is used as it is. */
    CHECK(f_mkdir("/pico2-apps/games") == FR_OK && f_mkdir("/sub") == FR_OK &&
              put("/pico2-apps/games/g1.uf2", "x"),
          "folders");
    CHECK(put("/sub/m.txt", "1 games/g1 forward slash\n"
                            "2 games\\g1 back slash\n"
                            "3 games/g1.UF2 ending typed\n"
                            "4 /pico2-apps/prog from the root\n"
                            "5 \\pico2-apps\\games\\g1.uf2 root, back slashes\n"
                            "6 ./pico2-apps/prog from here\n"
                            "7 games/none no such program\n"
                            "8 g1 only in a folder, not on the path\n"
                            "9 ../pico2-apps/prog above here\n"),
          "folder menu");
    {
        const struct { const char *key; bool found; } p[] = {
            { "1", true }, { "2", true }, { "3", true }, { "4", true }, { "5", true },
            { "6", true }, { "7", false }, { "8", false }, { "9", true },
        };
        for (size_t i = 0; i < sizeof p / sizeof p[0]; i++) {
            char script[8];
            snprintf(script, sizeof script, "%s \x1b", p[i].key);
            CHECK(run_keys("select /sub/m.txt", script) == 0 &&
                      (strstr(con, "cannot run programs") != NULL) == p[i].found &&
                      (strstr(con, "not found") != NULL) == !p[i].found,
                  "entry %s: '%s'", p[i].key, con);
        }
    }
    /* Either slash in the menu file's own name (a backslash only reaches
     * the command when it is quoted). */
    CHECK(run_keys("select '\\sub\\m.txt'", "\x1b") == 0 &&
              strcmp(row(&shots[0], 2), "1  forward slash") == 0,
          "back slashes: '%s'", row(&shots[0], 2));
    CHECK(run_keys("select '/sub\\m.txt'", "\x1b") == 0 &&
              strcmp(row(&shots[0], 2), "1  forward slash") == 0,
          "mixed slashes: '%s'", row(&shots[0], 2));
    /* A program in the current directory's folder, not on $PATH. */
    CHECK(run_keys("cd /sub", "") == 0 && run_keys("select m.txt", "\x1b") == 0 &&
              strcmp(row(&shots[0], 2), "1  forward slash") == 0,
          "relative menu: '%s'", row(&shots[0], 2));
    CHECK(run_keys("cd /", "") == 0, "cd back");

    /* ---- the empty and the odd ---- */
    CHECK(put("/empty", "") && put("/blank", "\n\n# only a comment\n\r\n"), "empty files");
    CHECK(run_keys("select empty", "\x1b") == 0 &&
              strcmp(row(&shots[0], 0), "SELECT A PROGRAM") == 0 &&
              strcmp(row(&shots[0], 2), "") == 0 && strcmp(row(&shots[0], 31), "ESC=QUIT") == 0,
          "empty: '%s'", row(&shots[0], 0));
    CHECK(run_keys("select blank", "a\x1b") == 0 && waits == 2 && !strstr(con, "cannot"),
          "blank");

    /* ---- the command's own errors ---- */
    CHECK(run_keys("select nofile", "") == 1 && waits == 0 &&
              strstr(con, "select: nofile:"),
          "missing: '%s'", con);
    CHECK(run_keys("select", "") == 1 && strstr(con, "usage: select file"), "usage: '%s'", con);
    CHECK(run_keys("select a b", "") == 1 && strstr(con, "usage: select file"), "two");
    CHECK(run_keys("select m.txt > out", "") == 1 && waits == 0 &&
              strstr(con, "not with a redirection or a pipe"),
          "redirected: '%s'", con);
    CHECK(run_keys("echo x | select m.txt", "") == 1 && waits == 0, "piped: '%s'", con);
    CHECK(run_keys("select /pico2-apps", "") == 1 && strstr(con, "is a directory"),
          "directory: '%s'", con);
    sh.hooks.getkey = NULL;
    CHECK(run_keys("select m.txt", "") == 1 && strstr(con, "no keyboard"), "no keyboard: '%s'", con);
    sh.hooks.getkey = getkey;

    host_disk_close();
    TEST_DONE();
}
