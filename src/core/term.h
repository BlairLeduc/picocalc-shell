/* term.h — the terminal: VT100/ANSI decoding into a cell grid (design.md §4.1).
 *
 * The parser is picocalc-text-starter's drivers/display.c (MIT, the
 * owner's). The starter drew straight into the panel; here it writes a
 * grid of cells instead, and a renderer in src/port/ draws the rows that
 * changed. The grid is what makes the terminal testable on the host and
 * readable as text over the UART.
 *
 * The renderer's side of the contract, in term_take():
 *   - `scroll` is the net number of whole-screen lines scrolled up (down
 *     if negative) since the last call. The renderer moves what is already
 *     on the panel by that much, by hardware scroll or by redrawing.
 *   - the dirty mask then names every row whose cells differ from what
 *     the panel shows after that move. The mask moves with the scroll,
 *     so a row that was dirty before a scroll is dirty where it went.
 * A scroll inside a DECSTBM region moves cells and marks the region's
 * rows dirty instead; only a full-screen scroll is counted. */
#ifndef SHELL_TERM_H
#define SHELL_TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* RGB565, as the panel takes it (hardware-notes.md §4.5). */
#define TERM_RGB(r, g, b) \
    ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* The starter's colours (display.h), but for the phosphors, which are
 * the owner's of 2026-10-14: the starter's green and amber were never
 * drawn there, and its amber, RGB(255, 255, 51), is a yellow.
 * FOREGROUND is the default ink until a phosphor is chosen. */
#define TERM_WHITE_PHOSPHOR TERM_RGB(216, 240, 255)
#define TERM_GREEN_PHOSPHOR TERM_RGB(51, 255, 51)
#define TERM_AMBER_PHOSPHOR TERM_RGB(255, 183, 0)
#define TERM_FOREGROUND     TERM_RGB(255, 255, 255)
#define TERM_BACKGROUND     TERM_RGB(0, 0, 0)
#define TERM_DIM            TERM_RGB(128, 128, 128)

/* A cell's attributes. Reverse is not one: as in the starter, it swaps the
 * colours when it is set, so a cell holds the colours it is drawn in. */
#define TERM_ATTR_BOLD      0x01u
#define TERM_ATTR_UNDERLINE 0x02u

/* Glyph 0x02, the checkerboard: drawn for a byte with no glyph (0x80 and
 * above) and for an unknown or cancelled escape sequence. */
#define TERM_GLYPH_ERROR    0x02u

typedef struct {
    uint8_t  ch;     /* a glyph index, 0x00-0x7F */
    uint8_t  attr;
    uint16_t fg, bg;
} term_cell_t;

typedef struct term term_t;

typedef void (*term_bell_fn)(void *ctx);
typedef void (*term_report_fn)(void *ctx, const char *reply);
typedef void (*term_leds_fn)(void *ctx, uint8_t leds);

struct term {
    term_cell_t cells[TERM_ROWS][TERM_COLS_MAX];
    uint32_t    dirty;           /* one bit per row */
    int         scroll;          /* net full-screen lines up, see above */

    uint8_t     cols;            /* 40 or 64: the font in use */
    uint8_t     row, col;        /* the cursor */
    uint8_t     save_row, save_col;
    uint8_t     top, bottom;     /* the scroll region, inclusive */
    bool        cursor_on;       /* DECTCEM */
    bool        tab_stops[TERM_COLS_MAX];

    uint16_t    fg, bg;          /* as drawn: already swapped if reverse */
    uint16_t    default_fg;      /* the phosphor */
    uint8_t     attr;
    bool        reverse;

    uint8_t     g[2];            /* G0 and G1 character sets */
    uint8_t     gl;              /* which of them is active */
    uint8_t     leds;

    uint8_t     state;
    uint16_t    params[TERM_PARAMS];
    uint8_t     nparam;          /* index of the parameter being read */

    term_bell_fn   bell;
    term_report_fn report;
    term_leds_fn   on_leds;
    void          *ctx;
};

/* A cleared 40-column screen, every row dirty, no callbacks. */
void term_init(term_t *t);

void term_putc(term_t *t, uint8_t ch);
void term_write(term_t *t, const char *s, size_t n);

/* The default ink: one of the phosphors, or any RGB565. It applies from
 * the next reset of the colours (SGR 0, 39, RIS). */
void term_set_phosphor(term_t *t, uint16_t rgb565);

/* The renderer's call: the rows to draw, and the scroll to apply first
 * (see the top of this file). Clears both. */
uint32_t term_take(term_t *t, int *scroll);

/* Row `row` as text, for a dump over the UART and for host tests: glyphs
 * 0x20-0x7E as themselves, any other as '.', trailing blanks removed.
 * `buf` holds at least TERM_COLS_MAX + 1 bytes. Returns the length. */
size_t term_row_text(const term_t *t, unsigned row, char *buf);

#endif /* SHELL_TERM_H */
