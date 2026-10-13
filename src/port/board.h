/* board.h — clocks and board identification (hardware-notes.md §2.1, §3).
 *
 * From pico-oric's src/port/board.h (GPL-3.0, the owner's), trimmed: no
 * die temperature. Record physical board identity separately from the
 * SDK build target: a banner saying board=pico2 identifies compilation
 * settings, not the installed module.
 */
#ifndef SHELL_BOARD_H
#define SHELL_BOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *sdk_board;      /* what we were compiled for            */
    char        unique_id[17];  /* physical: the module's flash id      */
    const char *chip;           /* physical: RP2350A or B, by package   */
    uint8_t     chip_version;   /* physical: RP2350 revision            */
    uint32_t    clk_sys_hz;
    uint32_t    clk_peri_hz;
    unsigned    vreg_mv;        /* the core rail, as set                 */
} board_info_t;

/* Set clk_sys to 150 MHz with the core rail at its default, and put
 * clk_peri on it. Call it first in main(), before any peripheral is
 * brought up: every one derives its rate from the clock it finds. A
 * program run before the shell may have left either raised. Returns false
 * if the PLL would not take it. */
bool board_init_clocks(void);

void board_identify(board_info_t *info);

#endif /* SHELL_BOARD_H */
