/* config.h — every fixed capacity in the shell (design.md §5, §7; EL §2.1).
 *
 * Portable: no SDK header. Nothing elsewhere duplicates one of these, so
 * the SRAM budget in design.md §7 is a link-time fact. */
#ifndef SHELL_CONFIG_H
#define SHELL_CONFIG_H

/* The panel (hardware-notes.md §4.1) and the fonts' cell (design.md §4.1). */
#define SHELL_PANEL_W      320u
#define SHELL_PANEL_H      320u
#define SHELL_PANEL_MEM_H  480u   /* frame memory rows, for hardware scroll (§4.8) */
#define SHELL_GLYPH_H      10u

/* The terminal's grid: 32 rows, 40 columns in font_8x10, 64 in font_5x10.
 * The grid is always 64 wide; 40-column mode uses its left part. */
#define TERM_ROWS          32u
#define TERM_COLS_8X10     40u
#define TERM_COLS_5X10     64u
#define TERM_COLS_MAX      TERM_COLS_5X10
#define TERM_PARAMS        16u    /* CSI parameters kept; more are dropped */

/* One line typed at the prompt, without its terminator. */
#define SHELL_LINE_MAX     255u
#define SHELL_WORDS_MAX    16u

/* out_printf formats into this, and out_write translates LF to CR LF in
 * chunks of this, so neither needs an allocator. */
#define SHELL_OUT_CHUNK    128u
#define SHELL_PRINTF_MAX   256u

/* Southbridge key events held between polls: more than its FIFO's 31
 * (hardware-notes.md §6.1), a power of two. */
#define SHELL_KEY_RING     64u

#endif /* SHELL_CONFIG_H */
