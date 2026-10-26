/* restart_test.c — a program that restarts itself at every start, to
 * check M7's way out of a restart loop on the board (design.md §10 M7).
 *
 * Copy build/pico/restart-test.uf2 to /pico2-apps/ and type
 * `restart-test`. It shows how many times it has started since the
 * shell ran it, reads and drops the keyboard's events for a second, as
 * a program would, then reboots with watchdog_reboot(0, 0, 0) and no
 * command, which starts it again under either loader. Under M7's
 * patched stage3, holding Up, F1 or F5 through a restart gives the
 * shell, and a power cycle does too. The count is in watchdog scratch
 * 3, which neither stage3 nor the bootrom uses, and starts again from
 * nothing after a power-on. Each start is logged on UART0 (GP2/GP3,
 * 115200).
 *
 * It uses the shell's own panel, terminal and keyboard code. */

#include <stdio.h>

#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "kbd.h"
#include "lcd.h"
#include "screen.h"
#include "southbridge.h"
#include "term.h"

#define KBD_POLL_US 33333u   /* about 30 Hz (hardware-notes.md §6.1) */
#define RUN_US      1000000u
#define COUNT_MAGIC 0x52540000u   /* "RT" over the count in the low half */

static term_t s_term;

int main(void) {
    stdio_init_all();
    uint32_t r = watchdog_hw->scratch[3];
    uint32_t n = (r & 0xffff0000u) == COUNT_MAGIC ? (r & 0xffffu) + 1u : 1u;
    watchdog_hw->scratch[3] = COUNT_MAGIC | (n & 0xffffu);
    printf("\nrestart-test: start %lu, %llu us after reset\n", (unsigned long)n,
           (unsigned long long)time_us_64());

    sb_init();
    lcd_init();
    screen_init();
    term_init(&s_term);
    char buf[160];
    int len = snprintf(buf, sizeof buf,
                       "\033[1mrestart-test\033[0m\r\n\r\n"
                       "Start %lu. It restarts itself\r\n"
                       "every second. Hold Up, F1 or F5\r\n"
                       "for the shell.\r\n",
                       (unsigned long)n);
    term_write(&s_term, buf, (size_t)len);
    screen_flush(&s_term, time_us_64());

    uint64_t end = time_us_64() + RUN_US;
    while (time_us_64() < end) {
        kbd_poll();
        uint8_t state, code;
        while (kbd_pop(&state, &code)) {}
        sleep_us(KBD_POLL_US);
    }
    stdio_flush();
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}
