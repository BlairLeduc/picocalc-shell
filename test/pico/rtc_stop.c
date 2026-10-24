/* rtc_stop.c — a program that makes the DS3231 lose its time at the next
 * power-off, to check a PicoCalc with no clock source when the chip
 * cannot be taken out (design.md §3.8, §10 M12).
 *
 * Copy build/pico/rtc-stop.uf2 to /pico2-apps/ and type `rtc-stop`. It
 * sets EOSC, bit 7 of the chip's control register (0x0E), which stops
 * its oscillator while it runs on its battery; the data sheet says OSF
 * is then set. Any key returns to the shell, whose clock is still set.
 * After `poweroff` and a power-on, the shell says the DS3231 lost its
 * time, and the clock is not set. Setting the time writes the chip and
 * clears EOSC and OSF again (ds3231_write()). The panel shows the
 * control and status registers before and after; as in exit-test, a
 * printable character or CR on UART0 (GP2/GP3, 115200) counts as a key.
 *
 * It uses the shell's own panel, terminal, keyboard and I2C set-up. */

#include <stdarg.h>
#include <stdio.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "ds3231.h"
#include "kbd.h"
#include "keys.h"
#include "lcd.h"
#include "picocalc_shell.h"
#include "screen.h"
#include "southbridge.h"
#include "term.h"

#define KBD_POLL_US 33333u   /* about 30 Hz (hardware-notes.md §6.1) */

/* As in src/port/ds3231.c. */
#define RTC_ADDR       0x68u
#define RTC_TIMEOUT_US 20000u
#define REG_CONTROL    0x0Eu
#define CONTROL_EOSC   0x80u

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

/* The control and status registers, false if the chip did not answer. */
static bool regs(uint8_t r[2]) {
    uint8_t reg = REG_CONTROL;
    return i2c_write_timeout_us(i2c0, RTC_ADDR, &reg, 1, true, RTC_TIMEOUT_US) == 1 &&
           i2c_read_timeout_us(i2c0, RTC_ADDR, r, 2, false, RTC_TIMEOUT_US) == 2;
}

int main(void) {
    stdio_init_all();
    gpio_pull_up(PICO_DEFAULT_UART_RX_PIN);
    printf("\nrtc-stop: started %llu us after reset\n", (unsigned long long)time_us_64());
    sb_init();
    lcd_init();
    screen_init();
    term_init(&s_term);
    keys_init(&s_keys);
    ds3231_init();

    say("\033[1mrtc-stop\033[0m\n\n"
        "Stops the DS3231 at power-off.\n\n");
    uint8_t r[2];
    if (!regs(r)) {
        say("No DS3231 answered.\n");
    } else {
        say("control 0x%02x, status 0x%02x\n", r[0], r[1]);
        const uint8_t out[2] = { REG_CONTROL, (uint8_t)(r[0] | CONTROL_EOSC) };
        bool ok = i2c_write_timeout_us(i2c0, RTC_ADDR, out, 2, false, RTC_TIMEOUT_US) == 2 &&
                  regs(r) && (r[0] & CONTROL_EOSC);
        if (ok)
            say("control 0x%02x, status 0x%02x\n\n"
                "EOSC is set. After poweroff the\n"
                "chip loses its time. Setting the\n"
                "time starts it again.\n",
                r[0], r[1]);
        else
            say("\nSetting EOSC failed.\n");
    }
    say("\nPress any key to return to the\nshell.\n");
    drain();
    for (;;) {
        wait_key();
        say("Returning to the shell...\n");
        stdio_flush();
        sleep_ms(500);
        if (!picocalc_shell_exit())
            say("picocalc_shell_exit() returned\nfalse. Press a key to try again.\n");
    }
}
