/* displaytest.h — the starter's display stress test (design.md §10, M0).
 *
 * picocalc-text-starter's displaytest() in tests.c (MIT, the owner's),
 * split into its two phases so a host test can read the grid after each.
 * The bytes sent are the starter's, byte for byte, through out_write's
 * LF to CR LF, as the starter's printf did. */
#ifndef SHELL_DISPLAYTEST_H
#define SHELL_DISPLAYTEST_H

#include <stdbool.h>

#include "out.h"

#define DISPLAYTEST_ROWS  2000
#define DISPLAYTEST_CHARS 60000

/* Polled once a row or character; true stops the phase early. May be NULL. */
typedef bool (*displaytest_stop_fn)(void *ctx);

/* Phase 1: DISPLAYTEST_ROWS rows of exactly one screen width (40 or 64
 * columns), each in the next xterm colour, scrolling. Hides the cursor.
 * Returns the rows sent. */
int displaytest_rows(const out_t *o, bool wide, displaytest_stop_fn stop, void *ctx);

/* Phase 2: clear, draw a DEC line-drawing box at rows 3-5, then rewrite
 * the cell inside it DISPLAYTEST_CHARS times. Returns the characters
 * written; `*bytes` gets the bytes sent for them. */
int displaytest_chars(const out_t *o, displaytest_stop_fn stop, void *ctx, unsigned long *bytes);

/* The end: back to ASCII, attributes off, the cursor shown. */
void displaytest_finish(const out_t *o);

#endif /* SHELL_DISPLAYTEST_H */
