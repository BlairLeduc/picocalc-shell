/* screen.c — draws the terminal's grid on the panel (screen.h). */

#include "screen.h"

#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "font.h"
#include "lcd.h"

#define MEM_ROWS (SHELL_PANEL_MEM_H / SHELL_GLYPH_H)   /* 48 text rows of frame memory */
#define BLINK_US 500000u

/* What frame memory holds, cell by cell, indexed by frame memory text row
 * so a hardware scroll moves nothing here. The cursor is not in it: it is
 * drawn over a cell and erased by redrawing the cell from here. */
static term_cell_t s_shadow[MEM_ROWS][TERM_COLS_MAX];
static bool        s_valid[MEM_ROWS];      /* drawn in the current font */
static unsigned    s_top;                  /* frame memory row at panel row 0 */
static unsigned    s_cols;                 /* the font the shadow was drawn in */

static bool        s_cursor_shown;
static unsigned    s_cursor_row, s_cursor_col;
static bool        s_blink_on = true;
static uint64_t    s_blink_at;

static uint32_t    s_rows_drawn, s_cells_drawn;

static uint16_t    s_line[2][SHELL_PANEL_W];   /* ping-pong (hardware-notes.md §4.6) */

static term_cell_t *shadow_row(unsigned row) {
    return s_shadow[(s_top + row) % MEM_ROWS];
}

/* One pixel row of a glyph, as the starter draws it: bold smears each
 * set pixel one to the right (8x10 only), and the last row is the
 * underline's, and the cursor's. */
static uint16_t *glyph_line(uint16_t *px, const term_cell_t *c, unsigned i, bool cursor) {
    const font_t *font = s_cols == TERM_COLS_5X10 ? &font_5x10 : &font_8x10;
    unsigned w = font->width;
    uint8_t bits = font->glyphs[c->ch * GLYPH_HEIGHT + i];
    bool last = i == GLYPH_HEIGHT - 1u;
    bool solid = last && (cursor || (c->attr & TERM_ATTR_UNDERLINE));
    if (w == 8 && !last && (c->attr & TERM_ATTR_BOLD)) bits |= (uint8_t)(bits >> 1);
    for (unsigned b = 0; b < w; b++) {
        bool on = solid || (bits & (1u << (w - 1u - b)));
        *px++ = on ? c->fg : c->bg;
    }
    return px;
}

/* Cells c0..c1 of a panel row, from the shadow, with the cursor at column
 * `cursor` (or none, if it is outside). */
static void draw_span(unsigned row, unsigned c0, unsigned c1, unsigned cursor) {
    const term_cell_t *cells = shadow_row(row);
    unsigned w = SHELL_PANEL_W / s_cols;
    unsigned n = (c1 - c0 + 1u) * w;
    lcd_blit_begin(c0 * w, row * SHELL_GLYPH_H, n, SHELL_GLYPH_H);
    for (unsigned i = 0; i < GLYPH_HEIGHT; i++) {
        uint16_t *px = s_line[i & 1u];
        for (unsigned c = c0; c <= c1; c++) px = glyph_line(px, &cells[c], i, c == cursor);
        lcd_blit_row(s_line[i & 1u], n);
    }
    lcd_blit_end();
    s_cells_drawn += c1 - c0 + 1u;
}

void screen_init(void) {
    memset(s_valid, 0, sizeof s_valid);
    s_top = 0;
    s_cols = TERM_COLS_8X10;
    s_cursor_shown = false;
}

static void flush(term_t *t) {
    int scroll;
    uint32_t dirty = term_take(t, &scroll);
    bool font_change = t->cols != s_cols;
    unsigned row = t->row, col = t->col;
    bool want = t->cursor_on && s_blink_on;
    bool keep = s_cursor_shown && want && !scroll && !font_change &&
                s_cursor_row == row && s_cursor_col == col;

    /* Erase the cursor before anything moves under it, so it never
     * scrolls away into frame memory the shadow does not know about. */
    if (s_cursor_shown && !keep) draw_span(s_cursor_row, s_cursor_col, s_cursor_col, ~0u);
    s_cursor_shown = keep;

    if (font_change) {
        s_cols = t->cols;
        memset(s_valid, 0, sizeof s_valid);
        dirty = 0xFFFFFFFFu;
    }
    if (scroll) {
        int m = (int)MEM_ROWS;
        s_top = (unsigned)((((int)s_top + scroll) % m + m) % m);
        lcd_scroll(scroll * (int)SHELL_GLYPH_H);
    }

    for (unsigned r = 0; r < TERM_ROWS; r++) {
        if (!(dirty & (1u << r))) continue;
        term_cell_t *shadow = shadow_row(r);
        const term_cell_t *cells = t->cells[r];
        unsigned c0 = 0, c1 = s_cols - 1u;
        if (s_valid[(s_top + r) % MEM_ROWS]) {
            while (c0 < s_cols && memcmp(&cells[c0], &shadow[c0], sizeof *cells) == 0) c0++;
            if (c0 == s_cols) continue;   /* marked, but the same as drawn */
            while (memcmp(&cells[c1], &shadow[c1], sizeof *cells) == 0) c1--;
        }
        memcpy(&shadow[c0], &cells[c0], (c1 - c0 + 1u) * sizeof *cells);
        s_valid[(s_top + r) % MEM_ROWS] = true;
        draw_span(r, c0, c1, (keep && r == row) ? col : ~0u);
        s_rows_drawn++;
    }

    if (want && !keep) draw_span(row, col, col, col);
    s_cursor_shown = want;
    s_cursor_row = row;
    s_cursor_col = col;
}

void screen_flush(term_t *t, uint64_t now_us) {
    s_blink_on = true;
    s_blink_at = now_us + BLINK_US;
    flush(t);
}

void screen_tick(term_t *t, uint64_t now_us) {
    if (now_us < s_blink_at) return;
    s_blink_at = now_us + BLINK_US;
    s_blink_on = !s_blink_on;
    flush(t);
}

void screen_counts(uint32_t *rows, uint32_t *cells) {
    *rows = s_rows_drawn;
    *cells = s_cells_drawn;
}
