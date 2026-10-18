/* panel_model.h — a renderer's view of the panel, for host tests.
 *
 * Models what src/port/screen.c does with term_take(): the panel's 480
 * rows of frame memory as 48 text rows, a hardware scroll offset that
 * moves by whole rows, and only the dirty rows redrawn. After a flush,
 * the 32 rows the panel shows must equal the grid; if they do not, the
 * dirty and scroll accounting in term.c has lost a change. */
#ifndef SHELL_PANEL_MODEL_H
#define SHELL_PANEL_MODEL_H

#include <stdbool.h>
#include <string.h>

#include "term.h"

#define PANEL_MEM_ROWS (SHELL_PANEL_MEM_H / SHELL_GLYPH_H)

typedef struct {
    term_cell_t mem[PANEL_MEM_ROWS][TERM_COLS];
    unsigned    offset;            /* frame memory row shown at the top */
    bool        ignore_scroll;     /* the control: a renderer that forgets it */
    unsigned long rows_drawn;
} panel_model_t;

static void panel_model_init(panel_model_t *p) {
    memset(p, 0, sizeof *p);
}

static void panel_model_flush(panel_model_t *p, term_t *t) {
    int scroll;
    uint32_t dirty = term_take(t, &scroll);
    if (!p->ignore_scroll) {
        int m = (int)PANEL_MEM_ROWS;
        p->offset = (unsigned)((((int)p->offset + scroll) % m + m) % m);
    }
    for (unsigned r = 0; r < TERM_ROWS; r++) {
        if (dirty & (1u << r)) {
            memcpy(p->mem[(p->offset + r) % PANEL_MEM_ROWS], t->cells[r], sizeof t->cells[r]);
            p->rows_drawn++;
        }
    }
}

/* The first row where the panel differs from the grid, or -1. */
static int panel_model_diff(const panel_model_t *p, const term_t *t) {
    for (unsigned r = 0; r < TERM_ROWS; r++) {
        const term_cell_t *a = p->mem[(p->offset + r) % PANEL_MEM_ROWS];
        if (memcmp(a, t->cells[r], sizeof t->cells[r]) != 0) return (int)r;
    }
    return -1;
}

#endif /* SHELL_PANEL_MODEL_H */
