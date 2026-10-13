/* displaytest.c — the starter's display stress test (displaytest.h). */

#include "displaytest.h"

#include <stdio.h>
#include <string.h>

int displaytest_rows(const out_t *o, bool wide, displaytest_stop_fn stop, void *ctx) {
    int row = 1;
    out_puts(o, "\033[?25l");   /* hide the cursor */
    while (!(stop && stop(ctx)) && row <= DISPLAYTEST_ROWS) {
        int colour = 16 + (row % 215);
        if (wide)
            out_printf(o, "\033[38;5;%dmRow: %04d 01234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFG",
                       colour, row++);
        else
            out_printf(o, "\033[38;5;%dmRow: %04d 01234567890ABCDEFGHIJKLMNOPQRS", colour, row++);
    }
    return row - 1;
}

int displaytest_chars(const out_t *o, displaytest_stop_fn stop, void *ctx, unsigned long *bytes) {
    out_puts(o, "\033[m\033[2J\033[H");   /* colours off, clear, home */
    out_puts(o, "Character stress test:\n\n");
    out_puts(o, "\033(0");                /* DEC special graphics in G0 */
    out_puts(o, "lqqqk\n");               /* top:    ┌───┐ */
    out_puts(o, "x   x\n");               /* sides:  │   │ */
    out_puts(o, "mqqqj\n");               /* bottom: └───┘ */

    char buf[32];
    int chars = 0;
    *bytes = 0;
    while (!(stop && stop(ctx)) && chars < DISPLAYTEST_CHARS) {
        int colour = 16 + (chars % 215);
        snprintf(buf, sizeof buf, "\033[4;3H\033[38;5;%dm%c", colour, 'A' + (chars % 26));
        *bytes += strlen(buf);
        out_puts(o, buf);
        chars++;
    }
    return chars;
}

void displaytest_finish(const out_t *o) {
    out_puts(o, "\n\n\n\033(B\033[m\033[?25h");
}
