/* picocalc_shell.h — return from a program to the PicoCalc shell.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR MIT
 *
 * This one header is offered under the MIT licence as well as the GPL,
 * so that a program under any licence can include it (design.md §9).
 * Copyright (c) 2026 Blair Leduc.
 *
 * For a program built with the Pico SDK for an RP2350 PicoCalc under UF2
 * Loader 2.3 or later, with the shell installed as /BOOT2350.UF2. The
 * shell started the program by flashing it and rebooting; this reboots
 * the other way, telling the loader's stage3 to load the shell from the
 * card instead of starting the program again.
 *
 *     #include "picocalc_shell.h"
 *     ...
 *     save_settings();              // finish every card write first
 *     if (!picocalc_shell_exit())
 *         show_message("Cannot return to the shell");
 *
 * The program must close its files first: the reboot does not wait for
 * them. Holding Up, F1 or F5 at power-on also returns to the shell, for
 * any program.
 *
 * A program can also be installed without the loader, straight from
 * BOOTSEL; the call returns false there too, and the program can do
 * something else instead, such as power off through its own southbridge
 * driver. picocalc_shell_loader_present() and
 * picocalc_shell_card_present() tell it beforehand, to name its menu
 * item.
 *
 * The values below are UF2 Loader's (common/bootloader/proginfo.c); the
 * shell's src/port/loader.c checks it agrees with them.
 */
#ifndef PICOCALC_SHELL_H
#define PICOCALC_SHELL_H

#include <stdbool.h>

#include "hardware/gpio.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

/* The card's detect switch: low with a card in, so it needs a pull-up. */
#define PICOCALC_SHELL_CARD_DETECT_PIN 22u

/* A command to stage3 in the watchdog's scratch registers: the magic in
 * scratch[0], the mode in [1], an argument in [2]. They survive the
 * watchdog's reboot, and stage3 clears the magic once it has read them. */
#define PICOCALC_SHELL_LOADER_MAGIC 0xe98cc638u
#define PICOCALC_SHELL_BOOT_SD      1u    /* load /BOOT2350.UF2 into SRAM */

/* Whether a card is in the slot. */
static inline bool picocalc_shell_card_present(void) {
    gpio_init(PICOCALC_SHELL_CARD_DETECT_PIN);
    gpio_set_dir(PICOCALC_SHELL_CARD_DETECT_PIN, GPIO_IN);
    gpio_pull_up(PICOCALC_SHELL_CARD_DETECT_PIN);
    busy_wait_us(100);   /* the pull-up's settling */
    return !gpio_get(PICOCALC_SHELL_CARD_DETECT_PIN);
}

/* Whether the program was started by UF2 Loader. stage3 starts it from
 * the app partition, after the loader in flash, so the boot ROM maps a
 * flash offset other than 0 at 0x10000000; a program copied on in
 * BOOTSEL runs from offset 0. (It shows a partition boot, not stage3 by
 * name; without stage3, the reboot below would only start the program
 * again.) */
static inline bool picocalc_shell_loader_present(void) {
    return (qmi_hw->atrans[0] & QMI_ATRANS0_BASE_BITS) != 0;
}

/* Return to the shell: stage3 loads /BOOT2350.UF2 instead of the app.
 * Returns false, and does nothing, if UF2 Loader did not start the
 * program, or there is no card to load the shell from: without one,
 * stage3 would drop to BOOTSEL and the PicoCalc would look dead.
 * Otherwise it does not return. */
static inline bool picocalc_shell_exit(void) {
    if (!picocalc_shell_loader_present() || !picocalc_shell_card_present()) return false;
    watchdog_hw->scratch[1] = PICOCALC_SHELL_BOOT_SD;
    watchdog_hw->scratch[2] = 0;
    watchdog_hw->scratch[0] = PICOCALC_SHELL_LOADER_MAGIC;
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}

#endif /* PICOCALC_SHELL_H */
