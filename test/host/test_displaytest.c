/* test_displaytest.c — the starter's display test against the cell grid
 * (design.md §10, M0), in both fonts. The bytes are the starter's; what
 * the screen should show after each phase is worked out here from the
 * test's own definition, and the panel model checks the renderer
 * contract throughout. */

#include <stdio.h>
#include <string.h>

#include "displaytest.h"
#include "panel_model.h"
#include "term.h"
#include "test_util.h"

static term_t t;
static panel_model_t p;
static unsigned long writes, mismatched_flushes;

/* The sink: the grid, then a flush to the panel model after every write,
 * as the port flushes after every write. */
static void sink(void *ctx, const char *buf, size_t n) {
    (void)ctx;
    term_write(&t, buf, n);
    panel_model_flush(&p, &t);
    if (panel_model_diff(&p, &t) >= 0) mismatched_flushes++;
    writes++;
}

static const out_t out = { sink, NULL };

static uint16_t xterm(int n) {
    static term_t s;
    char seq[24];
    term_init(&s);
    snprintf(seq, sizeof seq, "\033[38;5;%dmX", n);
    term_write(&s, seq, strlen(seq));
    return s.cells[0][0].fg;
}

static int run(bool wide) {
    const char *tail = wide ? "01234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFG"
                            : "01234567890ABCDEFGHIJKLMNOPQRS";
    unsigned cols = wide ? TERM_COLS_5X10 : TERM_COLS_8X10;

    term_init(&t);
    panel_model_init(&p);
    mismatched_flushes = 0;
    if (wide) out_puts(&out, "\033[?4264h");
    CHECK(t.cols == cols, "%u columns", t.cols);
    CHECK(strlen("Row: 0000 ") + strlen(tail) == cols, "a row is not one screen wide");

    /* Phase 1. Each row is exactly one line, so 2000 of them leave the
     * last 31 on rows 0-30 and the cursor on a blank row 31. */
    int rows = displaytest_rows(&out, wide, NULL, NULL);
    CHECK(rows == DISPLAYTEST_ROWS, "%d rows", rows);
    CHECK(!t.cursor_on, "the test hides the cursor");
    CHECK(t.row == 31 && t.col == 0, "cursor at %u,%u", t.row, t.col);
    for (int r = 0; r < 31; r++) {
        int n = DISPLAYTEST_ROWS - 30 + r;
        char want[TERM_COLS_MAX + 1], got[TERM_COLS_MAX + 1];
        snprintf(want, sizeof want, "Row: %04d %s", n, tail);
        term_row_text(&t, (unsigned)r, got);
        CHECK(strcmp(got, want) == 0, "row %d: '%s', want '%s'", r, got, want);
        CHECK(t.cells[r][0].fg == xterm(16 + n % 215) && t.cells[r][cols - 1].fg == t.cells[r][0].fg,
              "row %d is not in colour %d", r, 16 + n % 215);
    }
    char last[TERM_COLS_MAX + 1];
    term_row_text(&t, 31, last);
    CHECK(last[0] == '\0', "row 31 is '%s'", last);

    /* Phase 2: the box at rows 2-4, and the last of the 60000 letters
     * inside it at row 3, column 2, in its colour. */
    unsigned long bytes;
    int chars = displaytest_chars(&out, NULL, NULL, &bytes);
    CHECK(chars == DISPLAYTEST_CHARS, "%d chars", chars);
    char row0[TERM_COLS_MAX + 1];
    term_row_text(&t, 0, row0);
    CHECK(strcmp(row0, "Character stress test:") == 0, "row 0 is '%s'", row0);
    static const uint8_t box[3][5] = {
        { 0x0D, 0x12, 0x12, 0x12, 0x0C },     /* l q q q k */
        { 0x19, ' ', 'A', ' ', 0x19 },        /* x       x, the letter in the middle */
        { 0x0E, 0x12, 0x12, 0x12, 0x0B },     /* m q q q j */
    };
    int last_i = DISPLAYTEST_CHARS - 1;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 5; c++) {
            uint8_t want = box[r][c];
            if (r == 1 && c == 2) want = (uint8_t)('A' + last_i % 26);
            CHECK(t.cells[2 + r][c].ch == want, "box %d,%d: %02x, want %02x", r, c,
                  t.cells[2 + r][c].ch, want);
        }
    }
    CHECK(t.cells[3][2].fg == xterm(16 + last_i % 215), "the last letter's colour");
    /* Each pass is "\033[4;3H\033[38;5;Nm" and a letter, N of two or three digits. */
    unsigned long want_bytes = 0;
    for (int i = 0; i < DISPLAYTEST_CHARS; i++) {
        int n = 16 + i % 215;
        want_bytes += 6 + 7 + (n >= 100 ? 3 : 2) + 2;
    }
    CHECK(bytes == want_bytes, "%lu bytes, want %lu", bytes, want_bytes);

    displaytest_finish(&out);
    CHECK(t.cursor_on && t.row == 6 && t.col == 0, "after the test: cursor %u,%u", t.row, t.col);
    term_putc(&t, 'q');
    CHECK(t.cells[6][0].ch == 'q', "back to ASCII");

    CHECK(mismatched_flushes == 0, "the panel model differed from the grid after %lu writes",
          mismatched_flushes);
    TEST_DONE();
}

static int stop_after;
static bool stop(void *ctx) {
    (void)ctx;
    return stop_after-- <= 0;
}

int main(void) {
    if (run(false) != 0) return 1;
    if (run(true) != 0) return 1;

    /* A stop request ends each phase early. */
    term_init(&t);
    stop_after = 5;
    CHECK(displaytest_rows(&out, false, stop, NULL) == 5, "rows did not stop");
    stop_after = 7;
    unsigned long bytes;
    CHECK(displaytest_chars(&out, stop, NULL, &bytes) == 7, "chars did not stop");
    TEST_DONE();
}
