/* board.c — clocks and board identification (board.h). From pico-oric's
 * src/port/board.c (GPL-3.0, the owner's), trimmed. */

#include "board.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

/* The stock 150 MHz, the RP2350's rated clock and one of the two points
 * where the SPI divider gives the panel 25 MHz or 75 MHz exactly
 * (hardware-notes.md §3). */
#define BOARD_MHZ 150u

bool board_init_clocks(void) {
    if (clock_get_hz(clk_sys) != BOARD_MHZ * 1000000u &&
        !set_sys_clock_khz(BOARD_MHZ * 1000u, false)) {
        return false;
    }
    /* The rail is not reset with the chip: after a program that ran
     * overclocked, a watchdog reboot comes back still at its raised
     * voltage. Put it back once the clock is down, never before
     * (hardware-notes.md §3). */
    vreg_set_voltage(VREG_VOLTAGE_DEFAULT);

    /* set_sys_clock_pll otherwise parks clk_peri on the 48 MHz USB PLL,
     * and clk_peri does not follow clk_sys by default (§3). */
    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clock_get_hz(clk_sys),
                    clock_get_hz(clk_sys));
    return true;
}

void board_identify(board_info_t *info) {
    memset(info, 0, sizeof(*info));
    info->sdk_board = PICO_BOARD;
    info->chip = (sysinfo_hw->package_sel & SYSINFO_PACKAGE_SEL_BITS) ? "RP2350A" : "RP2350B";
    info->chip_version = (uint8_t)((sysinfo_hw->chip_id >> 28) & 0xFu);
    pico_get_unique_board_id_string(info->unique_id, sizeof(info->unique_id));
    info->clk_sys_hz = clock_get_hz(clk_sys);
    info->clk_peri_hz = clock_get_hz(clk_peri);
    info->vreg_mv = 550u + 50u * (unsigned)vreg_get_voltage();
}
