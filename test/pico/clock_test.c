/* clock_test.c — a program that shows the time the shell gave it, sets
 * it forward a minute and returns to the shell, to check the clock in
 * include/picocalc_shell.h on the board (design.md §3.8, §10 M12).
 *
 * Copy build/pico/clock-test.uf2 to /pico2-apps/ and type `clock-test`:
 * the panel shows picocalc_shell_time()'s UTC and local time, and the
 * offset the shell's TZ gave. Any key calls picocalc_shell_set_time()
 * with a minute more, and picocalc_shell_exit(): `date` or `TIME /T`
 * then shows the minute added. With the clock not set, it says so, sets
 * nothing, and returns. As in exit-test, a printable character or CR on
 * UART0 (GP2/GP3, 115200) counts as a key, and each step is logged
 * there.
 *
 * It uses the shell's own panel, terminal and keyboard code. */

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

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

/* Wait for a key press, or a printable character or CR on the UART. */
static void wait_key(void) {
    for (;;) {
        kbd_poll();
        uint8_t state, code;
        while (kbd_pop(&state, &code))
            if (keys_event(&s_keys, state, code) != KEYS_NONE) return;
        int c;
        while ((c = getchar_timeout_us(0)) >= 0)
            if ((c >= 0x20 && c < 0x7F) || c == '\r') return;
        screen_tick(&s_term, time_us_64());
        sleep_us(KBD_POLL_US);
    }
}

/* Seconds since 1970 as "2026-10-23 14:30:05", 19 columns. */
static const char *stamp(int64_t s) {
    static char buf[24];
    time_t t = (time_t)s;
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

int main(void) {
    stdio_init_all();
    gpio_pull_up(PICO_DEFAULT_UART_RX_PIN);
    printf("\nclock-test: started %llu us after reset\n", (unsigned long long)time_us_64());
    sb_init();
    lcd_init();
    screen_init();
    term_init(&s_term);
    keys_init(&s_keys);

    say("\033[1mclock-test\033[0m\n\n"
        "Checks picocalc_shell_time() and\n"
        "picocalc_shell_set_time().\n\n");
    int64_t ms;
    int off;
    bool set = picocalc_shell_time(&ms, &off);
    if (set)
        say("UTC    %s\n"
            "local  %s\n"
            "offset %+d min\n\n"
            "Press any key to set it a minute\n"
            "forward and return to the shell.\n",
            stamp(ms / 1000), stamp(ms / 1000 + off * 60), off);
    else
        say("The clock is not set.\n\n"
            "Press any key to return to the\n"
            "shell.\n");
    drain();
    for (;;) {
        wait_key();
        if (set && picocalc_shell_time(&ms, &off)) {
            picocalc_shell_set_time(ms + 60000);
            say("\nSet to %s UTC.\n", stamp((ms + 60000) / 1000));
        }
        say("Returning to the shell...\n");
        stdio_flush();
        sleep_ms(1500);   /* time to read the panel; the UART drains */
        if (!picocalc_shell_exit())
            say("picocalc_shell_exit() returned\nfalse. Press a key to try again.\n");
    }
}
