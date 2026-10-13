/* exit_test.c — a program that returns to the shell when a key is
 * pressed, to check include/picocalc_shell.h on the board (design.md
 * §3.4, §10 M2).
 *
 * Copy build/pico/exit-test.uf2 to /pico2-apps/ and type `exit-test`:
 * the shell flashes it and reboots, and it says what to do on the
 * panel. Any key calls picocalc_shell_exit(): with the card in, the
 * shell comes back; with it out, the call returns false, the panel says
 * so, and the program waits for the card and another key. A printable
 * character or CR on UART1 (GP4/GP5, 115200) counts as a key too, and
 * each step is logged there, so it can be driven over the Debug Probe.
 * GP5 is pulled up, and only those bytes count: with nothing on GP5, as
 * on a PicoCalc with no probe, the first build returned at once, most
 * likely on bytes read from the floating line. The panel says which key woke it, and from where.
 *
 * It uses the shell's own panel, terminal and keyboard code. */

#include <stdarg.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "kbd.h"
#include "keys.h"
#include "lcd.h"
#include "picocalc_shell.h"
#include "screen.h"
#include "southbridge.h"
#include "term.h"

#define KBD_POLL_US 33333u   /* about 30 Hz (hardware-notes.md §6.1) */

static term_t s_term;
static keys_t s_keys;

/* To the panel and the UART alike. */
static void say(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
    fputs(buf, stdout);
    /* The terminal's LF only moves down. */
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') term_write(&s_term, "\r", 1);
        term_write(&s_term, &buf[i], 1);
    }
    screen_flush(&s_term, time_us_64());
}

/* Whatever is waiting from the keyboard and the UART, dropped. */
static void drain(void) {
    kbd_poll();
    uint8_t state, code;
    while (kbd_pop(&state, &code)) (void)keys_event(&s_keys, state, code);
    while (getchar_timeout_us(0) >= 0) {}
}

/* Wait for a key press, or a printable character or CR on the UART,
 * and say which. */
static void wait_key(void) {
    for (;;) {
        kbd_poll();
        uint8_t state, code;
        while (kbd_pop(&state, &code)) {
            int k = keys_event(&s_keys, state, code);
            if (k != KEYS_NONE) {
                say("\nKey 0x%02x from the keyboard.\n", (unsigned)k);
                return;
            }
        }
        int c;
        while ((c = getchar_timeout_us(0)) >= 0)
            if ((c >= 0x20 && c < 0x7F) || c == '\r') {
                say("\nKey 0x%02x from the UART.\n", (unsigned)c);
                return;
            }
        screen_tick(&s_term, time_us_64());
        sleep_us(KBD_POLL_US);
    }
}

int main(void) {
    stdio_init_all();
    gpio_pull_up(PICO_DEFAULT_UART_RX_PIN);
    printf("\nexit-test: started %llu us after reset\n", (unsigned long long)time_us_64());
    sb_init();
    lcd_init();
    screen_init();
    term_init(&s_term);
    keys_init(&s_keys);

    say("\033[1mexit-test\033[0m\n\n"
        "Checks picocalc_shell_exit().\n\n"
        "Press any key to return to the\n"
        "shell. Take the card out first\n"
        "to see the call refuse.\n");
    drain();
    for (;;) {
        wait_key();
        bool card = picocalc_shell_card_present();
        say("The card is %s.\n", card ? "in" : "out");
        if (card) {
            say("Returning to the shell...\n");
            stdio_flush();
            sleep_ms(1500);   /* time to read the panel; the UART drains */
        }
        if (!picocalc_shell_exit())
            say("picocalc_shell_exit() returned\nfalse: no card. Put it back and\npress a key.\n");
    }
}
