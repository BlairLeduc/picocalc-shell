/* test_skeleton.c — M0 (design.md §10). Compiles every core header with
 * the system compiler and no Pico SDK, so an SDK #include in one of them
 * fails the host build, and checks config.h's sizes against each other
 * and against the fonts. */

#include "config.h"
#include "files.h"
#include "font.h"
#include "keycodes.h"
#include "keys.h"
#include "out.h"
#include "parse.h"
#include "path.h"
#include "shell.h"
#include "term.h"
#include "test_util.h"

int main(void) {
    CHECK(GLYPH_HEIGHT == SHELL_GLYPH_H, "font.h's glyph is %d rows, config.h's %u",
          GLYPH_HEIGHT, SHELL_GLYPH_H);
    CHECK(font_bbc_master_512.width * TERM_COLS == SHELL_PANEL_W, "font_bbc_master_512 is %u wide",
          font_bbc_master_512.width);
    CHECK(TERM_ROWS * SHELL_GLYPH_H == SHELL_PANEL_H, "%u rows of %u pixels", TERM_ROWS,
          SHELL_GLYPH_H);
    CHECK(SHELL_PANEL_MEM_H % SHELL_GLYPH_H == 0,
          "hardware scroll in whole rows needs frame memory in whole rows");
    CHECK((SHELL_KEY_RING & (SHELL_KEY_RING - 1u)) == 0 && SHELL_KEY_RING > 31u,
          "the key ring must be a power of two above the FIFO's 31");
    CHECK(sizeof(term_cell_t) == 6, "a cell is %zu bytes; design.md §7 budgets 6",
          sizeof(term_cell_t));

    TEST_DONE();
}
