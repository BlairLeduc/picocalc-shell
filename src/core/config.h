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

/* One line typed at the prompt, without its terminator, and what the
 * parser makes of it (parse.h): words across all its commands, and
 * commands joined by ;, && and ||. */
#define SHELL_LINE_MAX     255u
#define SHELL_WORDS_MAX    32u
#define SHELL_CMDS_MAX     8u

/* A path on the card, as the shell resolves it (path.h): FAT's own limit
 * on a name, and room for a few levels above it. */
#define SHELL_PATH_MAX     255u

/* Files (files.c): the buffer cat, cp and redirection copy through; the
 * names ls sorts, packed, and how many; how deep cp -r and rm -r go. */
#define SHELL_COPY_BUF     4096u
#define SHELL_LS_NAMES     16384u
#define SHELL_LS_MAX       512u
#define SHELL_TREE_DEPTH   16u

/* Running a program (uf2.c): the most 4 KB flash sectors an app
 * partition can have, 16 MB, for the bitmap of those that differ. */
#define SHELL_FLASH_SECTORS_MAX 4096u

/* out_printf formats into this, and out_write translates LF to CR LF in
 * chunks of this, so neither needs an allocator. */
#define SHELL_OUT_CHUNK    128u
#define SHELL_PRINTF_MAX   256u

/* Southbridge key events held between polls: more than its FIFO's 31
 * (hardware-notes.md §6.1), a power of two. */
#define SHELL_KEY_RING     64u

/* vi (design.md §4.6, vi.c). The file is held whole in one buffer, its
 * NUL included, so a file of up to VI_BUF_SIZE - 1 bytes opens and a
 * larger one is refused. The copy buffer is the one register; a yank
 * larger than it is refused, not cut short. The undo journal drops its
 * oldest steps to make room (editor_undo.h). The screen is VI_TEXT_ROWS
 * of text and a status row. */
#define VI_BUF_SIZE        65536u
#define VI_COPY_SIZE       8192u
#define VI_UNDO_SIZE       16384u
#define VI_TEXT_ROWS       31
#define VI_TAB_STOP        8u     /* a tab's width on screen */
#define VI_SHIFT_WIDTH     4u     /* the spaces > and < add and take */

/* The vi key layer's own capacities, under Pico Logo's names (its
 * core/limits.h explains each). Its 40-column footer fits this status
 * row in either font. */
#define LOGO_VI_TEXT_MAX          32
#define LOGO_VI_CMDLINE_MAX       (LOGO_VI_TEXT_MAX * 2 + 8)
#define LOGO_VI_MSG_MAX           40
#define LOGO_VI_SUB_EXPAND_MAX    256
#define LOGO_VI_PATTERN_STEPS_MAX 200000
#define LOGO_VI_COUNT_MAX         999999
#define LOGO_VI_REPEAT_MAX        8
#define LOGO_VI_INSERT_MAX        80

#endif /* SHELL_CONFIG_H */
