/* test_shell.c — M0's command line on the grid: the banner and the `/>`
 * prompt, the line, and no `font` (design.md §4.4, §10). */

#include <string.h>

#include "shell.h"
#include "term.h"
#include "test_util.h"

static term_t t;
static shell_t sh;

static void sink(void *ctx, const char *buf, size_t n) {
    (void)ctx;
    term_write(&t, buf, n);
}

static uint64_t clock_us;
static uint64_t now(void *ctx) {
    (void)ctx;
    return clock_us += 1000;
}

static int polls_left;
static bool interrupted(void *ctx) {
    (void)ctx;
    return polls_left-- <= 0;
}

static void type(const char *s) {
    while (*s) shell_input(&sh, (unsigned char)*s++);
}

static const char *row(unsigned r) {
    static char buf[TERM_COLS + 1];
    term_row_text(&t, r, buf);
    return buf;
}

/* Whether some row reads exactly `text`. */
static bool has_row(const char *text) {
    for (unsigned r = 0; r < TERM_ROWS; r++)
        if (strcmp(row(r), text) == 0) return true;
    return false;
}

int main(void) {
    const out_t out = { sink, NULL, false };
    const shell_hooks_t hooks = { now, interrupted, NULL, NULL, NULL };

    term_init(&t);
    term_write(&t, "junk from before", 16);
    shell_init(&sh, &out, &hooks, "v0-test");
    shell_start(&sh);
    CHECK(strcmp(row(0), "PicoCalc shell v0-test") == 0, "banner: '%s'", row(0));
    CHECK(t.cells[0][0].attr & TERM_ATTR_BOLD, "the name is bold");
    CHECK(strcmp(row(2), "/>") == 0 && t.row == 2 && t.col == 2, "prompt '%s' at %u,%u", row(2),
          t.row, t.col);

    /* Typing, Backspace, Enter. */
    type("halo\bp\r");
    CHECK(strcmp(row(2), "/>halp") == 0, "echo: '%s'", row(2));
    CHECK(strcmp(row(3), "halp: not found") == 0, "'%s'", row(3));
    CHECK(strcmp(row(4), "/>") == 0, "next prompt '%s'", row(4));
    type("\b\b\r");
    CHECK(strcmp(row(5), "/>") == 0, "Backspace stops at the line's start, '%s'", row(4));
    type("  \r");
    CHECK(strcmp(row(6), "/>") == 0, "a blank line is nothing");
    type("abc\003");
    CHECK(strcmp(row(6), "/>  ") != 0 && strcmp(row(7), "/>") == 0, "Ctrl-C: '%s'", row(6));
    type("\r");
    CHECK(strcmp(row(8), "/>") == 0, "the line was dropped: '%s'", row(8));

    /* One font, so no `font`. */
    type("font\r");
    CHECK(!shell_is_builtin("font") && has_row("font: not found"), "font is a builtin");

    /* The line holds SHELL_LINE_MAX characters and drops the rest. */
    for (unsigned i = 0; i < SHELL_LINE_MAX + 10; i++) shell_input(&sh, 'x');
    CHECK(sh.line.len == SHELL_LINE_MAX, "line length %zu", sh.line.len);
    type("\003");

    TEST_DONE();
}
