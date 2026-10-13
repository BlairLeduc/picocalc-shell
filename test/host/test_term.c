/* test_term.c — the terminal's decoding and its renderer contract
 * (design.md §4.1; term.h). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panel_model.h"
#include "term.h"
#include "test_util.h"

static term_t t;

static void feed(const char *s) {
    term_write(&t, s, strlen(s));
}

static const char *row_text(unsigned row) {
    static char buf[TERM_COLS_MAX + 1];
    term_row_text(&t, row, buf);
    return buf;
}

static void fresh(void) {
    term_init(&t);
}

/* The colour an SGR sequence leaves as the ink, read off a scratch term. */
static uint16_t ink_of(const char *sgr) {
    static term_t s;
    term_init(&s);
    term_write(&s, sgr, strlen(sgr));
    term_putc(&s, 'X');
    return s.cells[0][0].fg;
}

static char last_report[32];
static void on_report(void *ctx, const char *reply) {
    (void)ctx;
    snprintf(last_report, sizeof last_report, "%s", reply);
}

static int bells;
static void on_bell(void *ctx) {
    (void)ctx;
    bells++;
}

static int test_text(void) {
    fresh();
    CHECK(t.cols == 40 && t.row == 0 && t.col == 0, "init: %u cols at %u,%u", t.cols, t.row, t.col);
    CHECK(t.cursor_on, "the cursor starts on");
    int s;
    CHECK(term_take(&t, &s) == 0xFFFFFFFFu && s == 0, "a fresh screen is all dirty");

    feed("Hello");
    CHECK(strcmp(row_text(0), "Hello") == 0, "row 0 is '%s'", row_text(0));
    CHECK(t.col == 5, "cursor at column %u", t.col);
    CHECK(term_take(&t, &s) == 1u, "only row 0 is dirty");

    /* The starter's wrap: at once, when the 40th column is written. */
    fresh();
    for (int i = 0; i < 40; i++) term_putc(&t, (uint8_t)('a' + i % 26));
    CHECK(t.row == 1 && t.col == 0, "after 40 columns the cursor is at %u,%u", t.row, t.col);
    term_putc(&t, 'x');
    CHECK(strcmp(row_text(1), "x") == 0, "row 1 is '%s'", row_text(1));

    /* LF only moves down; CR returns. */
    fresh();
    feed("ab\ncd\r\nef");
    CHECK(strcmp(row_text(0), "ab") == 0 && strcmp(row_text(1), "  cd") == 0 &&
              strcmp(row_text(2), "ef") == 0,
          "rows '%s' '%s'", row_text(1), row_text(2));

    /* Backspace stops at column 0. */
    fresh();
    feed("\b\bab\b\bX");
    CHECK(strcmp(row_text(0), "Xb") == 0, "row 0 is '%s'", row_text(0));

    /* Bytes with no glyph, and an unknown sequence, show the checkerboard. */
    fresh();
    feed("a\xC3\xA9z");
    CHECK(t.cells[0][1].ch == TERM_GLYPH_ERROR && t.cells[0][2].ch == TERM_GLYPH_ERROR &&
              t.cells[0][3].ch == 'z',
          "UTF-8 bytes: %02x %02x", t.cells[0][1].ch, t.cells[0][2].ch);
    fresh();
    feed("\033[1y\033Q\033=\033>k");
    CHECK(t.cells[0][0].ch == TERM_GLYPH_ERROR && t.cells[0][1].ch == TERM_GLYPH_ERROR,
          "unknown CSI and ESC draw the checkerboard");
    CHECK(strcmp(row_text(0), "..k") == 0, "DECKPAM/DECKPNM draw nothing: '%s'", row_text(0));
    fresh();
    feed("\033[?7777h");
    CHECK(t.cells[0][0].ch == 0x20, "an unknown DEC mode with h/l is ignored");
    feed("\033[?5y");
    CHECK(t.cells[0][0].ch == TERM_GLYPH_ERROR, "an unknown DEC final draws the checkerboard");
    fresh();
    feed("\033[12\030b");
    CHECK(t.cells[0][0].ch == TERM_GLYPH_ERROR && t.cells[0][1].ch == 'b',
          "CAN cancels a CSI with the checkerboard");

    /* DEL and other controls are ignored. */
    fresh();
    feed("a\x7f\x01\x1f" "b");
    CHECK(strcmp(row_text(0), "ab") == 0, "row 0 is '%s'", row_text(0));

    TEST_DONE();
}

static int test_cursor(void) {
    fresh();
    feed("\033[5;10H");
    CHECK(t.row == 4 && t.col == 9, "CUP to %u,%u", t.row, t.col);
    feed("\033[A");
    CHECK(t.row == 3, "CUU with no count moves one, to row %u", t.row);
    feed("\033[3B");
    CHECK(t.row == 6, "CUD 3 to row %u", t.row);
    feed("\033[C\033[2D");
    CHECK(t.col == 8, "CUF then CUB 2 to column %u", t.col);
    feed("\033[99;99H");
    CHECK(t.row == 31 && t.col == 39, "CUP clamps to %u,%u", t.row, t.col);
    feed("\033[H");
    CHECK(t.row == 0 && t.col == 0, "CUP home");
    feed("\033[65537;9999999999H");
    CHECK(t.row == 31 && t.col == 39, "huge counts saturate, not wrap: %u,%u", t.row, t.col);
    feed("\033[12G\033[7d");
    CHECK(t.row == 6 && t.col == 11, "CHA and VPA to %u,%u", t.row, t.col);
    feed("\0337\033[H\0338");
    CHECK(t.row == 6 && t.col == 11, "DECSC/DECRC to %u,%u", t.row, t.col);
    feed("\033[s\033[H\033[u");
    CHECK(t.row == 6 && t.col == 11, "ANSI save/restore to %u,%u", t.row, t.col);
    feed("\033[2E");
    CHECK(t.row == 8 && t.col == 0, "CNL to %u,%u", t.row, t.col);
    feed("\033[5C\033[F");
    CHECK(t.row == 7 && t.col == 0, "CPL to %u,%u", t.row, t.col);

    /* Tabs: every 8 columns, then as set. */
    fresh();
    feed("\tx");
    CHECK(t.cells[0][8].ch == 'x', "HT to column 8");
    feed("\r\033[3g\033[5G\033H\r\ty");
    CHECK(t.cells[0][4].ch == 'y', "TBC 3 then HTS at column 4");
    feed("\r\033[39G\t");
    CHECK(t.col == 39, "HT with no stop left goes to the last column, %u", t.col);

    TEST_DONE();
}

static int test_erase(void) {
    fresh();
    for (int r = 0; r < 4; r++) feed("0123456789\r\n");
    feed("\033[2;5H\033[K");
    CHECK(strcmp(row_text(1), "0123") == 0, "EL 0: '%s'", row_text(1));
    feed("\033[3;5H\033[1K");
    CHECK(strcmp(row_text(2), "     56789") == 0, "EL 1: '%s'", row_text(2));
    feed("\033[4;1H\033[2K");
    CHECK(strcmp(row_text(3), "") == 0, "EL 2: '%s'", row_text(3));
    feed("\033[1;3H\033[J");
    CHECK(strcmp(row_text(0), "01") == 0 && strcmp(row_text(2), "") == 0, "ED 0");
    fresh();
    for (int r = 0; r < 3; r++) feed("0123456789\r\n");
    feed("\033[2;3H\033[1J");
    CHECK(strcmp(row_text(0), "") == 0 && strcmp(row_text(1), "   3456789") == 0 &&
              strcmp(row_text(2), "0123456789") == 0,
          "ED 1: '%s'", row_text(1));
    feed("\033[2J");
    CHECK(strcmp(row_text(2), "") == 0 && t.row == 1 && t.col == 2, "ED 2 leaves the cursor");

    /* Erasing uses the background in force. */
    fresh();
    feed("\033[44m\033[2K");
    CHECK(t.cells[0][0].bg == ink_of("\033[34m"), "EL in blue paper");

    TEST_DONE();
}

static int test_sgr(void) {
    fresh();
    CHECK(ink_of("") == TERM_FOREGROUND, "default ink");
    CHECK(ink_of("\033[31m") == TERM_RGB(205, 0, 0), "SGR 31");
    CHECK(ink_of("\033[91m") == TERM_RGB(255, 0, 0), "SGR 91");
    CHECK(ink_of("\033[38;2;255;0;0m") == TERM_RGB(255, 0, 0), "SGR 38;2");
    CHECK(ink_of("\033[38;5;9m") == 0xF800, "SGR 38;5;9");
    CHECK(ink_of("\033[38;5;232m") == 0x0000 && ink_of("\033[38;5;255m") == 0x7BCF,
          "the grey ramp, as the starter has it");
    CHECK(ink_of("\033[2m") == TERM_DIM, "SGR 2 is dim");
    CHECK(ink_of("\033[31;0m") == TERM_FOREGROUND, "SGR 0 resets");
    CHECK(ink_of("\033[31;39m") == TERM_FOREGROUND, "SGR 39");

    /* Out of range: ignored, never taken modulo 256 or 65536. 65792 wraps
     * a 16-bit parameter to 256, and 256 wraps a byte to black. */
    CHECK(ink_of("\033[38;5;300m") == TERM_FOREGROUND, "SGR 38;5;300 is ignored");
    CHECK(ink_of("\033[38;5;65792m") == TERM_FOREGROUND, "SGR 38;5;65792 is ignored");
    CHECK(ink_of("\033[38;2;300;0;0m") == TERM_FOREGROUND, "SGR 38;2 with 300 is ignored");
    CHECK(ink_of("\033[38;5;300;31m") == TERM_RGB(205, 0, 0),
          "an ignored colour still consumes its parameters");

    fresh();
    feed("\033[1;4ma\033[22;24mb");
    CHECK(t.cells[0][0].attr == (TERM_ATTR_BOLD | TERM_ATTR_UNDERLINE) && t.cells[0][1].attr == 0,
          "bold and underline on, then off: %02x %02x", t.cells[0][0].attr, t.cells[0][1].attr);

    /* Reverse swaps the colours, and a colour set while reversed lands on
     * the other one, as in the starter. */
    fresh();
    feed("\033[7ma\033[31mb\033[27mc\033[0md");
    CHECK(t.cells[0][0].fg == TERM_BACKGROUND && t.cells[0][0].bg == TERM_FOREGROUND, "reverse");
    CHECK(t.cells[0][1].bg == TERM_RGB(205, 0, 0) && t.cells[0][1].fg == TERM_BACKGROUND,
          "red under reverse is the paper");
    CHECK(t.cells[0][2].fg == TERM_RGB(205, 0, 0) && t.cells[0][2].bg == TERM_BACKGROUND,
          "and the ink once reverse is off");
    CHECK(t.cells[0][3].fg == TERM_FOREGROUND && t.cells[0][3].bg == TERM_BACKGROUND, "SGR 0");
    fresh();
    feed("\033[7;31m\033[0mx");
    CHECK(t.cells[0][0].fg == TERM_FOREGROUND && t.cells[0][0].bg == TERM_BACKGROUND,
          "SGR 0 from reverse");

    fresh();
    term_set_phosphor(&t, TERM_AMBER_PHOSPHOR);
    feed("\033[0mx");
    CHECK(t.cells[0][0].fg == TERM_AMBER_PHOSPHOR, "the phosphor is the default ink");

    TEST_DONE();
}

static int test_charsets(void) {
    fresh();
    feed("\033(0lqk\033(Bq");
    CHECK(t.cells[0][0].ch == 0x0D && t.cells[0][1].ch == 0x12 && t.cells[0][2].ch == 0x0C &&
              t.cells[0][3].ch == 'q',
          "DEC graphics in G0: %02x %02x %02x", t.cells[0][0].ch, t.cells[0][1].ch,
          t.cells[0][2].ch);
    fresh();
    feed("\033)0x\016x\017x");
    CHECK(t.cells[0][0].ch == 'x' && t.cells[0][1].ch == 0x19 && t.cells[0][2].ch == 'x',
          "G1 by SO and SI");
    fresh();
    feed("\033(A#\033(B#");
    CHECK(t.cells[0][0].ch == 0x1E && t.cells[0][1].ch == '#', "the UK pound sign");
    TEST_DONE();
}

static int test_scroll(void) {
    int s;
    fresh();
    for (int i = 0; i < 34; i++) {
        char line[16];
        snprintf(line, sizeof line, "line %d\r\n", i);
        feed(line);
    }
    CHECK(strcmp(row_text(0), "line 3") == 0, "row 0 is '%s'", row_text(0));
    CHECK(strcmp(row_text(30), "line 33") == 0 && t.row == 31, "row 30 is '%s'", row_text(30));
    term_take(&t, &s);
    CHECK(s == 3, "three lines scrolled, counted %d", s);

    /* RI at the top scrolls down. */
    feed("\033[H\033M");
    term_take(&t, &s);
    CHECK(s == -1 && strcmp(row_text(1), "line 3") == 0, "RI: scroll %d, row 1 '%s'", s,
          row_text(1));

    /* SU and SD. */
    feed("\033[2S");
    term_take(&t, &s);
    CHECK(s == 2 && strcmp(row_text(0), "line 4") == 0, "SU 2: '%s'", row_text(0));
    feed("\033[T");
    term_take(&t, &s);
    CHECK(s == -1 && strcmp(row_text(1), "line 4") == 0, "SD: '%s'", row_text(1));

    /* A region scrolls inside itself, and is not counted. */
    fresh();
    for (int i = 0; i < 6; i++) {
        char line[16];
        snprintf(line, sizeof line, "r%d\r\n", i);
        feed(line);
    }
    term_take(&t, &s);
    feed("\033[2;4r");
    CHECK(t.row == 1 && t.col == 0, "DECSTBM puts the cursor at the top margin");
    feed("\033[4;1H\nX");
    uint32_t d = term_take(&t, &s);
    CHECK(s == 0, "a region's scroll is not counted (%d)", s);
    CHECK(strcmp(row_text(0), "r0") == 0 && strcmp(row_text(1), "r2") == 0 &&
              strcmp(row_text(2), "r3") == 0 && strcmp(row_text(3), "X") == 0 &&
              strcmp(row_text(4), "r4") == 0,
          "region rows '%s' '%s' '%s' '%s'", row_text(1), row_text(2), row_text(3), row_text(4));
    CHECK(d == 0x0Eu, "rows 1-3 dirty, mask %08x", d);
    feed("\033[2;1H\033M");
    CHECK(strcmp(row_text(1), "") == 0 && strcmp(row_text(2), "r2") == 0 &&
              strcmp(row_text(4), "r4") == 0,
          "RI in a region");
    feed("\033[5r");
    CHECK(t.top == 4 && t.bottom == 31, "DECSTBM with no bottom: %u-%u", t.top, t.bottom);
    feed("\033[r");
    CHECK(t.top == 0 && t.bottom == 31, "DECSTBM with nothing resets: %u-%u", t.top, t.bottom);

    TEST_DONE();
}

static int test_modes(void) {
    fresh();
    feed("\033[?25l");
    CHECK(!t.cursor_on, "DECTCEM off");
    feed("\033[?25h");
    CHECK(t.cursor_on, "DECTCEM on");

    feed("\033[1;40H\033[?4264h");
    CHECK(t.cols == 64 && t.col == 39, "64 columns, cursor kept at %u", t.col);
    feed("\033[1;60H\033[?4264l");
    CHECK(t.cols == 40 && t.col == 39, "40 columns, cursor clamped to %u", t.col);
    feed("\033[?25;4264h");
    CHECK(t.cols == 64, "every DECSET parameter is applied");

    /* Reports and the bell. */
    fresh();
    t.report = on_report;
    t.bell = on_bell;
    feed("\033[3;5H\033[6n");
    CHECK(strcmp(last_report, "\033[3;5R") == 0, "DSR 6");
    feed("\033[5n");
    CHECK(strcmp(last_report, "\033[0n") == 0, "DSR 5");
    feed("\033[c");
    CHECK(strcmp(last_report, "\033[?1;c") == 0, "DA");
    feed("\a\a");
    CHECK(bells == 2, "BEL rang %d times", bells);

    /* OSC strings are swallowed, by BEL or ST. */
    fresh();
    feed("\033]0;title\aA\033]0;more\033\\B");
    CHECK(strcmp(row_text(0), "AB") == 0, "OSC swallowed: '%s'", row_text(0));

    /* RIS clears and homes; DECSTR clears. */
    feed("\033[31m\033[5;5Hx\033c");
    CHECK(strcmp(row_text(4), "") == 0 && t.row == 0 && t.col == 0 && t.fg == TERM_FOREGROUND,
          "RIS");
    feed("\033[1mab\033[!p");
    CHECK(strcmp(row_text(0), "") == 0 && t.attr == 0, "DECSTR");

    TEST_DONE();
}

/* The renderer contract: replay bytes through the panel model and require
 * the panel to match the grid after every flush, then run a renderer that
 * forgets the scroll and require that it does not. */
static int test_renderer_contract(void) {
    static const char *pieces[] = {
        "hello ", "\r\n", "\n", "\033M", "\033D", "\033E", "\033[S", "\033[3T", "\033[2;9r",
        "\033[r", "\033[10;20r", "\033[H", "\033[32;1H", "\033[5A", "\033[K", "\033[1J",
        "\033[J", "\033[2J", "\033[7m", "\033[0m", "\033[33m", "\033[?4264h", "\033[?4264l",
        "\t", "\b", "0123456789abcdefghijklmnopqrstuvwxyz0123", "\033(0lqk\033(B", "\xC3\xA9",
        "\033[2S", "\033[5;5H", "\033[30;1H",
    };
    const unsigned npieces = sizeof pieces / sizeof pieces[0];

    for (int control = 0; control < 2; control++) {
        static panel_model_t p;
        panel_model_init(&p);
        p.ignore_scroll = control;
        fresh();
        srand(12345);
        int mismatches = 0;
        for (int i = 0; i < 20000; i++) {
            int n = 1 + rand() % 4;
            while (n--) feed(pieces[(unsigned)rand() % npieces]);
            panel_model_flush(&p, &t);
            int r = panel_model_diff(&p, &t);
            if (r >= 0) {
                if (!control)
                    CHECK(false, "step %d: the panel differs from the grid at row %d", i, r);
                mismatches++;
            }
        }
        if (control)
            CHECK(mismatches > 0, "the control renderer, which ignores the scroll, matched");
        else
            CHECK(p.rows_drawn < 20000ul * TERM_ROWS / 2,
                  "%lu rows drawn: the dirty mask is redrawing most of the screen",
                  p.rows_drawn);
    }
    TEST_DONE();
}

int main(void) {
    int (*tests[])(void) = {
        test_text, test_cursor, test_erase, test_sgr, test_charsets, test_scroll, test_modes,
        test_renderer_contract,
    };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++)
        if (tests[i]() != 0) return 1;
    return 0;
}
