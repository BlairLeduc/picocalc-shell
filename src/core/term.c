/* term.c — VT100/ANSI decoding into a cell grid (design.md §4.1).
 *
 * The state machine, the palettes and the sequences it answers are
 * picocalc-text-starter's drivers/display.c (MIT, the owner's). Where it
 * called the LCD driver, this writes cells and marks rows dirty. Changes
 * from the starter, each small:
 *
 *   - LF, IND, NEL and the wrap scroll the DECSTBM region when the cursor
 *     is on its bottom row, and RI when on its top row; SU and SD scroll
 *     the region. The starter scrolled the panel's whole scroll band.
 *   - CUU, CUD, CUF and CUB move one place when given no count, as VT100
 *     does; the starter did not move.
 *   - DECSTBM with no bottom margin takes the last row, as VT100 does;
 *     the starter reset the region.
 *   - HT goes to the next tab stop, so HTS and TBC take effect. The stops
 *     start every 8 columns, where the starter's HT always went.
 *   - A byte of 0x80 or above, an unknown ESC sequence and an unknown DEC
 *     private mode draw the checkerboard, glyph 0x02; the starter ignored
 *     the first two and drew 0x01 for the third. ESC = and ESC > (keypad
 *     modes) are accepted and ignored.
 *   - Reset also clears bold and selects G0, which the starter left set.
 *   - The CSI parameter index is bounded by the array's length; the
 *     starter bounded it by its size in bytes. A parameter saturates at
 *     65535 instead of wrapping, and a 256-colour index or an RGB
 *     component above 255 is ignored, not taken modulo 256.
 *   - DECSET and DECRST act on every parameter, not only the first.
 *   - The 256-colour palette's colours 16-255 are xterm's, by formula;
 *     the starter's table had a wrong cube and a grey ramp that fell
 *     back to black at 248. */

#include "term.h"

#include <stdio.h>
#include <string.h>

_Static_assert(TERM_ROWS <= 32u, "the dirty mask is one 32-bit word");

#define ALL_ROWS (TERM_ROWS == 32u ? 0xFFFFFFFFu : ((1u << TERM_ROWS) - 1u))
#define MAX_ROW  ((int)TERM_ROWS - 1)

enum {
    ST_NORMAL,
    ST_ESCAPE,
    ST_CSI,
    ST_DEC,      /* CSI ? */
    ST_G0_SET,   /* ESC ( */
    ST_G1_SET,   /* ESC ) */
    ST_OSC,
    ST_OSC_ESC,
    ST_TMC,      /* CSI ! */
};

enum { CHARSET_UK, CHARSET_ASCII, CHARSET_DEC };

#define CHR_BEL 0x07
#define CHR_BS  0x08
#define CHR_HT  0x09
#define CHR_LF  0x0A
#define CHR_VT  0x0B
#define CHR_FF  0x0C
#define CHR_CR  0x0D
#define CHR_SO  0x0E
#define CHR_SI  0x0F
#define CHR_CAN 0x18
#define CHR_SUB 0x1A
#define CHR_ESC 0x1B

/* 3-bit terminal colours and their bright versions (based on VS Code). */
static const uint16_t palette[8] = {
    TERM_RGB(0, 0, 0),       TERM_RGB(205, 0, 0),   TERM_RGB(0, 205, 0),
    TERM_RGB(205, 205, 0),   TERM_RGB(0, 0, 238),   TERM_RGB(205, 0, 205),
    TERM_RGB(0, 205, 205),   TERM_RGB(229, 229, 229),
};

static const uint16_t bright_palette[8] = {
    TERM_RGB(127, 127, 127), TERM_RGB(255, 0, 0),   TERM_RGB(0, 255, 0),
    TERM_RGB(255, 255, 0),   TERM_RGB(92, 92, 255), TERM_RGB(255, 0, 255),
    TERM_RGB(0, 255, 255),   TERM_RGB(255, 255, 255),
};

/* The xterm 256-colour palette: its first 16 as the starter has them, then
 * xterm's own formula, which the starter's table got wrong (its grey ramp
 * climbed to white at 247 and fell back to black at 248). */
static const uint16_t xterm_16[16] = {
    0x0000, 0x8000, 0x0400, 0x8400, 0x0010, 0x8010, 0x0410, 0xC618,
    0x8410, 0xF800, 0x07E0, 0xFFE0, 0x001F, 0xF81F, 0x07FF, 0xFFFF,
};

static uint16_t xterm_colour(unsigned n) {
    static const uint8_t level[6] = { 0, 95, 135, 175, 215, 255 };
    if (n < 16) return xterm_16[n];
    if (n < 232) {
        n -= 16;
        return TERM_RGB(level[n / 36], level[n / 6 % 6], level[n % 6]);
    }
    unsigned g = 8 + 10 * (n - 232);
    return TERM_RGB(g, g, g);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ---- colours and attributes, with the starter's reverse semantics ---- */

static void set_fg(term_t *t, uint16_t c) {
    if (t->reverse) t->bg = c;
    else t->fg = c;
}

static void set_bg(term_t *t, uint16_t c) {
    if (t->reverse) t->fg = c;
    else t->bg = c;
}

static void set_reverse(term_t *t, bool on) {
    if (t->reverse != on) {
        uint16_t c = t->fg;
        t->fg = t->bg;
        t->bg = c;
    }
    t->reverse = on;
}

/* ---- cells ------------------------------------------------------------ */

static void mark(term_t *t, int row) {
    t->dirty |= 1u << row;
}

/* Columns c0..c1 of a row, inclusive, to blanks in the current colours. */
static void erase(term_t *t, int row, int c0, int c1) {
    term_cell_t blank = { ' ', 0, t->fg, t->bg };
    for (int c = c0; c <= c1; c++) t->cells[row][c] = blank;
    mark(t, row);
}

/* Erasing to the end of a line clears the hidden columns past a 40-column
 * screen too, so a switch to 64 columns shows nothing stale. */
#define LAST_COL ((int)TERM_COLS_MAX - 1)

static void erase_all(term_t *t) {
    for (int r = 0; r <= MAX_ROW; r++) erase(t, r, 0, LAST_COL);
}

static void put_glyph(term_t *t, uint8_t glyph) {
    term_cell_t *c = &t->cells[t->row][t->col];
    c->ch = glyph;
    c->attr = t->attr;
    c->fg = t->fg;
    c->bg = t->bg;
    mark(t, t->row);
    t->col++;
}

/* Rows top..bottom up one line, a blank line in at the bottom. */
static void scroll_up(term_t *t) {
    int top = t->top, bottom = t->bottom;
    memmove(&t->cells[top], &t->cells[top + 1],
            (size_t)(bottom - top) * sizeof t->cells[0]);
    if (top == 0 && bottom == MAX_ROW) {
        t->dirty >>= 1;
        t->scroll++;
        erase(t, bottom, 0, LAST_COL);
    } else {
        erase(t, bottom, 0, LAST_COL);
        for (int r = top; r <= bottom; r++) mark(t, r);
    }
}

/* Rows top..bottom down one line, a blank line in at the top. */
static void scroll_down(term_t *t) {
    int top = t->top, bottom = t->bottom;
    memmove(&t->cells[top + 1], &t->cells[top],
            (size_t)(bottom - top) * sizeof t->cells[0]);
    if (top == 0 && bottom == MAX_ROW) {
        t->dirty = (t->dirty << 1) & ALL_ROWS;
        t->scroll--;
        erase(t, top, 0, LAST_COL);
    } else {
        erase(t, top, 0, LAST_COL);
        for (int r = top; r <= bottom; r++) mark(t, r);
    }
}

static void linefeed(term_t *t) {
    if (t->row == t->bottom) scroll_up(t);
    else if (t->row < MAX_ROW) t->row++;
}

static void reverse_index(term_t *t) {
    if (t->row == t->top) scroll_down(t);
    else if (t->row > 0) t->row--;
}

static void reset_region(term_t *t) {
    t->top = 0;
    t->bottom = (uint8_t)MAX_ROW;
}

static void set_cols(term_t *t, unsigned cols) {
    if (t->cols == cols) return;
    t->cols = (uint8_t)cols;
    t->col = (uint8_t)imin(t->col, (int)cols - 1);
    t->save_col = (uint8_t)imin(t->save_col, (int)cols - 1);
    t->dirty = ALL_ROWS;
}

static void update_leds(term_t *t, uint8_t leds) {
    t->leds = leds;
    if (t->on_leds) t->on_leds(t->ctx, leds);
}

static void report(term_t *t, const char *reply) {
    if (t->report) t->report(t->ctx, reply);
}

/* The starter's reset_terminal, plus bold and G0 (see the top). */
static void reset_terminal(term_t *t) {
    set_reverse(t, false);
    t->fg = t->default_fg;
    t->bg = TERM_BACKGROUND;
    t->attr = 0;
    t->cursor_on = true;
    t->g[0] = t->g[1] = CHARSET_ASCII;
    t->gl = 0;
    reset_region(t);
    erase_all(t);
    update_leds(t, 0);
}

/* ---- the state machine ------------------------------------------------ */

static void escape(term_t *t, uint8_t ch) {
    t->state = ST_NORMAL;
    switch (ch) {
    case CHR_CAN:   /* cancel the sequence */
    case CHR_SUB:
        put_glyph(t, TERM_GLYPH_ERROR);
        break;
    case CHR_ESC:
        t->state = ST_ESCAPE;
        break;
    case '7':       /* DECSC */
        t->save_col = t->col;
        t->save_row = t->row;
        break;
    case '8':       /* DECRC */
        t->col = t->save_col;
        t->row = t->save_row;
        break;
    case 'D':       /* IND */
        linefeed(t);
        break;
    case 'E':       /* NEL */
        t->col = 0;
        linefeed(t);
        break;
    case 'H':       /* HTS */
        t->tab_stops[t->col] = true;
        break;
    case 'M':       /* RI */
        reverse_index(t);
        break;
    case 'c':       /* RIS */
        t->col = t->row = 0;
        reset_terminal(t);
        break;
    case '[':       /* CSI */
        memset(t->params, 0, sizeof t->params);
        t->nparam = 0;
        t->state = ST_CSI;
        break;
    case ']':       /* OSC, and the strings treated like it */
    case 'X':
    case '^':
    case '_':
    case 'P':
        t->state = ST_OSC;
        break;
    case '(':
        t->state = ST_G0_SET;
        break;
    case ')':
        t->state = ST_G1_SET;
        break;
    case '=':       /* DECKPAM, DECKPNM: no keypad to put in a mode */
    case '>':
        break;
    default:
        put_glyph(t, TERM_GLYPH_ERROR);
        break;
    }
}

/* A digit or a ';' inside CSI or CSI ?; false for anything else. */
static bool param_byte(term_t *t, uint8_t ch) {
    if (ch >= '0' && ch <= '9') {
        unsigned v = t->params[t->nparam] * 10u + (unsigned)(ch - '0');
        t->params[t->nparam] = (uint16_t)(v > 0xFFFFu ? 0xFFFFu : v);
        return true;
    }
    if (ch == ';') {
        if (t->nparam < TERM_PARAMS - 1u) t->nparam++;
        return true;
    }
    return false;
}

/* Parameter i, or `def` when it was not given (or given as 0). */
static int param(const term_t *t, unsigned i, int def) {
    return t->params[i] ? t->params[i] : def;
}

static void sgr(term_t *t) {
    for (unsigned i = 0; i <= t->nparam; i++) {
        unsigned p = t->params[i];
        if (p == 0) {
            set_fg(t, t->default_fg);
            set_bg(t, TERM_BACKGROUND);
            t->attr = 0;
            set_reverse(t, false);
        } else if (p == 1) {
            t->attr |= TERM_ATTR_BOLD;
        } else if (p == 2) {
            set_fg(t, TERM_DIM);
        } else if (p == 4) {
            t->attr |= TERM_ATTR_UNDERLINE;
        } else if (p == 7) {
            set_reverse(t, true);
        } else if (p == 22) {
            set_fg(t, t->default_fg);
            t->attr &= (uint8_t)~TERM_ATTR_BOLD;
        } else if (p == 24) {
            t->attr &= (uint8_t)~TERM_ATTR_UNDERLINE;
        } else if (p == 27) {
            set_reverse(t, false);
        } else if (p >= 30 && p <= 37) {
            set_fg(t, palette[p - 30]);
        } else if ((p == 38 || p == 48) && i + 4 <= t->nparam && t->params[i + 1] == 2) {
            const uint16_t *rgb = &t->params[i + 2];
            if (rgb[0] <= 255 && rgb[1] <= 255 && rgb[2] <= 255) {
                uint16_t c = TERM_RGB(rgb[0], rgb[1], rgb[2]);
                if (p == 38) set_fg(t, c);
                else set_bg(t, c);
            }
            i += 4;
        } else if ((p == 38 || p == 48) && i + 2 <= t->nparam && t->params[i + 1] == 5) {
            if (t->params[i + 2] <= 255) {
                uint16_t c = xterm_colour(t->params[i + 2]);
                if (p == 38) set_fg(t, c);
                else set_bg(t, c);
            }
            i += 2;
        } else if (p == 39) {
            set_fg(t, t->default_fg);
        } else if (p >= 40 && p <= 47) {
            set_bg(t, palette[p - 40]);
        } else if (p == 49) {
            set_bg(t, TERM_BACKGROUND);
        } else if (p >= 90 && p <= 97) {
            set_fg(t, bright_palette[p - 90]);
        } else if (p >= 100 && p <= 107) {
            set_bg(t, bright_palette[p - 100]);
        }
        /* No italic (3) or blink (5, 6). */
    }
}

static void csi_final(term_t *t, uint8_t ch) {
    int max_col = t->cols - 1;
    int row = t->row, col = t->col;

    t->state = ST_NORMAL;
    switch (ch) {
    case 'A':   /* CUU */
        t->row = (uint8_t)imax(0, row - param(t, 0, 1));
        break;
    case 'B':   /* CUD */
        t->row = (uint8_t)imin(row + param(t, 0, 1), MAX_ROW);
        break;
    case 'C':   /* CUF */
        t->col = (uint8_t)imin(col + param(t, 0, 1), max_col);
        break;
    case 'D':   /* CUB */
        t->col = (uint8_t)imax(0, col - param(t, 0, 1));
        break;
    case 'E':   /* CNL */
        t->row = (uint8_t)imin(row + param(t, 0, 1), MAX_ROW);
        t->col = 0;
        break;
    case 'F':   /* CPL */
        t->row = (uint8_t)imax(0, row - param(t, 0, 1));
        t->col = 0;
        break;
    case 'G':   /* CHA */
        t->col = (uint8_t)imin(param(t, 0, 1) - 1, max_col);
        break;
    case 'H':   /* CUP */
    case 'f':   /* HVP */
        t->row = (uint8_t)imin(param(t, 0, 1) - 1, MAX_ROW);
        t->col = (uint8_t)imin(param(t, 1, 1) - 1, max_col);
        break;
    case 'J':   /* ED */
        if (t->params[0] == 0) {
            erase(t, row, col, LAST_COL);
            for (int r = row + 1; r <= MAX_ROW; r++) erase(t, r, 0, LAST_COL);
        } else if (t->params[0] == 1) {
            for (int r = 0; r < row; r++) erase(t, r, 0, LAST_COL);
            erase(t, row, 0, col);
        } else if (t->params[0] == 2) {
            erase_all(t);
        }
        break;
    case 'K':   /* EL */
        if (t->params[0] == 0) erase(t, row, col, LAST_COL);
        else if (t->params[0] == 1) erase(t, row, 0, col);
        else if (t->params[0] == 2) erase(t, row, 0, LAST_COL);
        break;
    case 'S':   /* SU */
        for (int n = param(t, 0, 1); n > 0; n--) scroll_up(t);
        break;
    case 'T':   /* SD */
        for (int n = param(t, 0, 1); n > 0; n--) scroll_down(t);
        break;
    case 'c':   /* DA */
        report(t, "\033[?1;c");
        break;
    case 'd':   /* VPA */
        t->row = (uint8_t)imin(param(t, 0, 1) - 1, MAX_ROW);
        break;
    case 'e':   /* VPR */
        t->row = (uint8_t)imin(row + param(t, 0, 1), MAX_ROW);
        break;
    case 'g':   /* TBC */
        if (t->params[0] == 3) memset(t->tab_stops, 0, sizeof t->tab_stops);
        else if (t->params[0] == 0) t->tab_stops[col] = false;
        break;
    case 'h':   /* SM, RM: no ANSI modes */
    case 'l':
        break;
    case 'm':
        sgr(t);
        break;
    case 'n':   /* DSR */
        if (t->params[0] == 5) {
            report(t, "\033[0n");
        } else if (t->params[0] == 6) {
            char buf[16];
            snprintf(buf, sizeof buf, "\033[%d;%dR", row + 1, col + 1);
            report(t, buf);
        }
        break;
    case 'q': { /* DECLL */
        uint8_t leds = t->leds;
        for (unsigned i = 0; i <= t->nparam; i++) {
            if (t->params[i] == 0) leds = 0;
            else if (t->params[i] <= 8) leds |= (uint8_t)(1u << (t->params[i] - 1));
        }
        update_leds(t, leds);
        break;
    }
    case 'r': { /* DECSTBM */
        int top = imin(param(t, 0, 1) - 1, MAX_ROW);
        int bottom = imin(param(t, 1, (int)TERM_ROWS) - 1, MAX_ROW);
        if (bottom > top) {
            t->top = (uint8_t)top;
            t->bottom = (uint8_t)bottom;
        } else {
            reset_region(t);
        }
        t->row = (uint8_t)top;
        t->col = 0;
        break;
    }
    case 's':   /* save cursor (ANSI) */
        t->save_col = t->col;
        t->save_row = t->row;
        break;
    case 't':   /* window operations: none */
        break;
    case 'u':   /* restore cursor (ANSI) */
        t->col = t->save_col;
        t->row = t->save_row;
        break;
    default:    /* unknown, and CAN or SUB inside the sequence */
        put_glyph(t, TERM_GLYPH_ERROR);
        break;
    }
}

static void dec_final(term_t *t, uint8_t ch) {
    t->state = ST_NORMAL;
    if (ch != 'h' && ch != 'l' && ch != 'm') {
        put_glyph(t, TERM_GLYPH_ERROR);
        return;
    }
    if (ch == 'm') return;
    bool set = ch == 'h';
    for (unsigned i = 0; i <= t->nparam; i++) {
        if (t->params[i] == 25) t->cursor_on = set;                    /* DECTCEM */
        else if (t->params[i] == 4264) set_cols(t, set ? TERM_COLS_5X10 : TERM_COLS_8X10);
    }
}

static uint8_t charset_for(uint8_t ch) {
    switch (ch) {
    case 'A': return CHARSET_UK;
    case '0': return CHARSET_DEC;
    case 'B': return CHARSET_ASCII;
    default:  return 0xFF;
    }
}

static void normal(term_t *t, uint8_t ch) {
    switch (ch) {
    case CHR_BS:
        if (t->col > 0) t->col--;
        break;
    case CHR_BEL:
        if (t->bell) t->bell(t->ctx);
        break;
    case CHR_HT: {
        int c = t->col + 1;
        while (c < t->cols - 1 && !t->tab_stops[c]) c++;
        t->col = (uint8_t)imin(c, t->cols - 1);
        break;
    }
    case CHR_LF:
    case CHR_VT:
    case CHR_FF:
        linefeed(t);
        break;
    case CHR_CR:
        t->col = 0;
        break;
    case CHR_SO:
        t->gl = 1;
        break;
    case CHR_SI:
        t->gl = 0;
        break;
    case CHR_ESC:
        t->state = ST_ESCAPE;
        break;
    default:
        if (ch >= 0x20 && ch < 0x7F) {
            uint8_t set = t->g[t->gl];
            if (set == CHARSET_UK && ch == '#') ch = 0x1E;              /* the pound sign */
            else if (set == CHARSET_DEC && ch >= 0x5F) ch -= 0x5F;      /* line drawing */
            put_glyph(t, ch);
        } else if (ch >= 0x80) {
            put_glyph(t, TERM_GLYPH_ERROR);   /* UTF-8 or Latin-1: no glyph */
        }
        break;
    }
}

void term_putc(term_t *t, uint8_t ch) {
    switch (t->state) {
    case ST_ESCAPE:
        escape(t, ch);
        break;
    case ST_CSI:
        if (ch == CHR_ESC) t->state = ST_ESCAPE;
        else if (ch == '?') t->state = ST_DEC;
        else if (ch == '!') t->state = ST_TMC;
        else if (!param_byte(t, ch)) csi_final(t, ch);
        break;
    case ST_DEC:
        if (ch == CHR_ESC) t->state = ST_ESCAPE;
        else if (!param_byte(t, ch)) dec_final(t, ch);
        break;
    case ST_TMC:
        if (ch == 'p') reset_terminal(t);   /* DECSTR, soft reset */
        t->state = ST_NORMAL;
        break;
    case ST_G0_SET:
    case ST_G1_SET: {
        uint8_t set = charset_for(ch);
        if (set != 0xFF) t->g[t->state == ST_G1_SET] = set;
        t->state = ST_NORMAL;
        break;
    }
    case ST_OSC:
        if (ch == CHR_ESC) t->state = ST_OSC_ESC;
        else if (ch == CHR_BEL || ch == 0x9C) t->state = ST_NORMAL;
        break;
    case ST_OSC_ESC:
        t->state = ch == '\\' ? ST_NORMAL : ST_OSC;
        break;
    default:
        normal(t, ch);
        break;
    }

    /* Wrap at once at the end of a line, as the starter does. */
    if (t->col >= t->cols) {
        t->col = 0;
        linefeed(t);
    }
}

void term_write(term_t *t, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) term_putc(t, (uint8_t)s[i]);
}

void term_init(term_t *t) {
    memset(t, 0, sizeof *t);
    t->cols = TERM_COLS_8X10;
    t->default_fg = TERM_FOREGROUND;
    for (unsigned c = 8; c < TERM_COLS_MAX; c += 8) t->tab_stops[c] = true;
    reset_terminal(t);
    t->dirty = ALL_ROWS;
}

void term_set_phosphor(term_t *t, uint16_t rgb565) {
    t->default_fg = rgb565;
}

uint32_t term_take(term_t *t, int *scroll) {
    uint32_t d = t->dirty;
    *scroll = t->scroll;
    t->dirty = 0;
    t->scroll = 0;
    return d;
}

size_t term_row_text(const term_t *t, unsigned row, char *buf) {
    size_t n = 0, end = 0;
    for (unsigned c = 0; c < t->cols; c++) {
        uint8_t ch = t->cells[row][c].ch;
        buf[n++] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
        if (ch != ' ') end = n;
    }
    buf[end] = '\0';
    return end;
}
