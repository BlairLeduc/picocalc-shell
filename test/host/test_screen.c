/* test_screen.c — the renderer, src/port/screen.c, against a model of the
 * panel (fake_lcd.c). After every flush, every pixel the panel shows must
 * be the one the grid calls for, drawn by the starter's rules: glyph bits
 * MSB first, bold smearing one pixel right, and the last
 * pixel row the underline's and the cursor's. The cursor must show at the
 * terminal's cursor when it is on and its blink is on, and nowhere else,
 * however the screen scrolled under it. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_lcd.h"
#include "font.h"
#include "screen.h"
#include "term.h"
#include "test_util.h"

static term_t t;
static uint64_t now;

static void feed(const char *s) {
    term_write(&t, s, strlen(s));
}

static void flush(void) {
    now += 1000;
    screen_flush(&t, now);
}

static uint16_t expected(unsigned x, unsigned y, bool cursor) {
    unsigned c = x / 8u, b = x % 8u, r = y / GLYPH_HEIGHT, i = y % GLYPH_HEIGHT;
    const term_cell_t *cell = &t.cells[r][c];
    uint8_t bits = font_bbc_master_512.glyphs[cell->ch * GLYPH_HEIGHT + i];
    bool on = bits & (1u << (7u - b));
    if (i < GLYPH_HEIGHT - 1u && (cell->attr & TERM_ATTR_BOLD) && b > 0 &&
        (bits & (1u << (8u - b))))
        on = true;
    if (i == GLYPH_HEIGHT - 1u &&
        ((cell->attr & TERM_ATTR_UNDERLINE) || (cursor && r == t.row && c == t.col)))
        on = true;
    return on ? cell->fg : cell->bg;
}

/* The first pixel that differs, as "x,y", or NULL. */
static const char *panel_diff(bool cursor) {
    static char where[32];
    for (unsigned y = 0; y < SHELL_PANEL_H; y++)
        for (unsigned x = 0; x < SHELL_PANEL_W; x++)
            if (fake_lcd_panel(x, y) != expected(x, y, cursor)) {
                snprintf(where, sizeof where, "%u,%u (cell %u,%u)", x, y, y / GLYPH_HEIGHT,
                         x / 8u);
                return where;
            }
    return NULL;
}

static void start(void) {
    fake_lcd_reset(0x0000);   /* lcd_init leaves the panel black at offset 0 */
    fake_lcd_ignore_scroll = false;
    term_init(&t);
    screen_init();
    flush();
}

static const char *pieces[] = {
    "hello ", "\r\n", "\n", "\033M", "\033D", "\033E", "\033[S", "\033[3T", "\033[2;9r",
    "\033[r", "\033[10;20r", "\033[H", "\033[32;1H", "\033[5A", "\033[K", "\033[1J", "\033[J",
    "\033[2J", "\033[7m", "\033[0m", "\033[1m", "\033[4m", "\033[33m", "\033[38;5;100m",
    "\033[?25l", "\033[?25h", "\t", "\b",
    "0123456789abcdefghijklmnopqrstuvwxyz0123", "\033(0lqkxmj\033(B", "\xC3\xA9", "\033[2S",
    "\033[5;5H", "\033[30;1H", "\033[6S", "\033[40T",
};
#define NPIECES (sizeof pieces / sizeof pieces[0])

/* Random runs of sequences, a flush after each, the whole panel checked. */
static int test_random(void) {
    start();
    srand(4242);
    for (int step = 0; step < 1500; step++) {
        int n = 1 + rand() % 4;
        while (n--) feed(pieces[(unsigned)rand() % NPIECES]);
        flush();
        const char *d = panel_diff(t.cursor_on);
        CHECK(d == NULL, "step %d: the panel differs at %s", step, d);
        if (d) return 1;
    }
    CHECK(fake_lcd_faults == 0, "%lu blits broke the window rules", fake_lcd_faults);
    TEST_DONE();
}

/* The control: the same run on a panel that ignores the scroll command
 * must differ from the grid, or the check above sees nothing. */
static int test_control(void) {
    start();
    fake_lcd_ignore_scroll = true;
    int mismatched = 0;
    for (int i = 0; i < 40; i++) {
        char line[16];
        snprintf(line, sizeof line, "line %d\r\n", i);
        feed(line);
        flush();
        mismatched += panel_diff(t.cursor_on) != NULL;
    }
    CHECK(mismatched > 0, "a panel that ignores the scroll matched the grid");
    fake_lcd_ignore_scroll = false;
    TEST_DONE();
}

/* The cursor on a scrolled screen, and its blink. */
static int test_cursor(void) {
    start();
    for (int i = 0; i < 40; i++) {
        feed("ab\r\n");
        flush();
    }
    feed("\033[3;5H");
    flush();
    for (int i = 0; i < 10; i++) {   /* scroll down under a visible cursor, and back up */
        feed("\033[T");
        flush();
    }
    feed("\033[32;1H\n\n\n");
    flush();
    const char *d = panel_diff(true);
    CHECK(d == NULL, "after scrolling under the cursor: %s", d);

    screen_tick(&t, now + 499999);
    CHECK(panel_diff(true) == NULL, "the cursor blinked before 500 ms");
    screen_tick(&t, now + 500000);
    d = panel_diff(false);
    CHECK(d == NULL, "the blink's off phase still shows the cursor: %s", d);
    screen_tick(&t, now + 1000000);
    d = panel_diff(true);
    CHECK(d == NULL, "the blink's on phase: %s", d);

    /* Off phase, then output: the cursor shows at once. */
    screen_tick(&t, now + 1500000);
    now += 1500000;
    feed("x");
    flush();
    d = panel_diff(true);
    CHECK(d == NULL, "output while the blink was off: %s", d);

    feed("\033[?25l");
    flush();
    d = panel_diff(false);
    CHECK(d == NULL, "DECTCEM off: %s", d);
    TEST_DONE();
}

/* A one-cell change sends about one cell, not a row (design.md §4.1). */
static int test_spans(void) {
    start();
    feed("\033[?25l\033[5;5Hx");
    flush();
    unsigned long before = fake_lcd_pixels;
    feed("\033[5;5Hy");
    flush();
    unsigned long sent = fake_lcd_pixels - before;
    CHECK(sent == 8u * GLYPH_HEIGHT, "a one-cell change sent %lu pixels", sent);
    before = fake_lcd_pixels;
    feed("\033[5;5Hy");
    flush();
    CHECK(fake_lcd_pixels == before, "an unchanged cell was redrawn");
    CHECK(panel_diff(false) == NULL, "the panel after the spans");
    TEST_DONE();
}

int main(void) {
    int (*tests[])(void) = { test_random, test_control, test_cursor, test_spans };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++)
        if (tests[i]() != 0) return 1;
    return 0;
}
