/* main.c — bring-up, then the shell's loop (design.md §5, §10).
 *
 * One core. Bring-up follows hardware-notes.md §10: the southbridge, the
 * panel, the keyboard. The shell's output goes to the terminal's grid,
 * then, in the development build, to UART1 as the same bytes, so a
 * terminal on the other end shows what the panel does; then the panel is
 * brought up to date. The loop polls the keyboard and card detect at
 * about 30 Hz, takes the UART's bytes as keys, and blinks the cursor.
 *
 * Two UART bytes are not keys (EL §13.1): FS (0x1C) dumps the grid as
 * text, and GS (0x1D) logs the start time and the counters.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "board.h"
#include "card.h"
#include "kbd.h"
#include "keycodes.h"
#include "keys.h"
#include "lcd.h"
#include "screen.h"
#include "shell.h"
#include "shell_version.h"
#include "southbridge.h"
#include "term.h"

#define KBD_POLL_US 33333u   /* about 30 Hz (hardware-notes.md §6.1) */

#define UART_DUMP_SCREEN 0x1C
#define UART_LOG_STATS   0x1D

static term_t       g_term;
static shell_t      g_shell;
static keys_t       g_keys;
static board_info_t g_board;

static uint64_t s_next_poll;
/* volatile, so the release build keeps them for a read over SWD. */
static volatile uint64_t s_main_us, s_lcd_us, s_prompt_us;

extern char __end__;   /* the linker's: the end of .bss, where the heap starts */

static void poll_keys(void) {
    uint64_t now = time_us_64();
    if (now < s_next_poll) return;
    s_next_poll = now + KBD_POLL_US;
    kbd_poll();
    card_poll();
}

static void con_write(void *ctx, const char *buf, size_t n) {
    (void)ctx;
    term_write(&g_term, buf, n);
#if PICO_SHELL_UART
    uart_write_blocking(uart_default, (const uint8_t *)buf, n);
#endif
    screen_flush(&g_term, time_us_64());
}

static uint64_t now_us(void *ctx) {
    (void)ctx;
    return time_us_64();
}

/* For a long command: keep the keyboard polled, and report Break, Esc or
 * Ctrl-C. Other keys pressed meanwhile are dropped. */
static bool interrupted(void *ctx) {
    (void)ctx;
    bool hit = false;
    poll_keys();
    uint8_t state, code;
    while (kbd_pop(&state, &code)) {
        int k = keys_event(&g_keys, state, code);
        if (k == KEY_BREAK || k == 0x1B || k == 0x03) hit = true;
    }
#if PICO_SHELL_UART
    int c;
    while ((c = getchar_timeout_us(0)) >= 0) {
        int k = keys_uart((uint8_t)c);
        if (k == KEY_BREAK || k == 0x1B || k == 0x03) hit = true;
    }
#endif
    return hit;
}

#if PICO_SHELL_UART
static void dump_screen(void) {
    char text[TERM_COLS_MAX + 1];
    printf("\n--- screen %u x %u, cursor row %u col %u ---\n", g_term.cols, TERM_ROWS,
           g_term.row, g_term.col);
    for (unsigned r = 0; r < TERM_ROWS; r++) {
        term_row_text(&g_term, r, text);
        printf("%s\n", text);
    }
    printf("--- end of screen ---\n");
}

static void log_stats(void) {
    uint32_t rows, cells;
    screen_counts(&rows, &cells);
    printf("\nstats: main at %llu us, lcd up at %llu us, prompt at %llu us since reset; "
           "%lu rows and %lu cells drawn; %lu key overflows, %lu i2c errors\n",
           (unsigned long long)s_main_us, (unsigned long long)s_lcd_us,
           (unsigned long long)s_prompt_us, (unsigned long)rows, (unsigned long)cells,
           (unsigned long)kbd_overflows(), (unsigned long)sb_error_count());
}
#endif

static void log_banner(bool clocks_ok, uint32_t i2c_hz, sb_status_t sb, const uint8_t ver[2],
                       uint32_t spi_hz) {
#if PICO_SHELL_UART
    printf("\nshell %s, a no_flash RAM program (BOOT2350.UF2)\n", SHELL_VERSION);
    /* Build target and physical identity, labelled as such
     * (hardware-notes.md §2.1). */
    printf("  build target : board=%s platform=rp2350\n", g_board.sdk_board);
    printf("  module       : id=%s chip=%s rev=%u\n", g_board.unique_id, g_board.chip,
           g_board.chip_version);
    printf("  clocks       : clk_sys=%lu Hz clk_peri=%lu Hz, core rail ~%u mV%s\n",
           (unsigned long)g_board.clk_sys_hz, (unsigned long)g_board.clk_peri_hz,
           g_board.vreg_mv, clocks_ok ? "" : " (WARNING: not the 150 MHz this build assumes)");
    if (sb == SB_OK)
        printf("  southbridge  : i2c %lu Hz, version 0x%02x\n", (unsigned long)i2c_hz, ver[1]);
    else
        printf("  southbridge  : i2c %lu Hz, version read failed (%d)\n", (unsigned long)i2c_hz,
               (int)sb);
    printf("  lcd          : spi %lu Hz configured\n", (unsigned long)spi_hz);
    printf("  memory       : static data ends at %p, %lu bytes of SRAM\n", (void *)&__end__,
           (unsigned long)((uintptr_t)&__end__ - SRAM_BASE));
    printf("  start        : main at %llu us, lcd up at %llu us since reset\n",
           (unsigned long long)s_main_us, (unsigned long long)s_lcd_us);
#else
    (void)clocks_ok;
    (void)i2c_hz;
    (void)sb;
    (void)ver;
    (void)spi_hz;
#endif
}

int main(void) {
    s_main_us = time_us_64();

    /* The clock first, so every divider is worked out from the clock
     * that stays. */
    bool clocks_ok = board_init_clocks();
    stdio_init_all();
    board_identify(&g_board);

    uint32_t i2c_hz = sb_init();
    uint8_t ver[2] = { 0, 0 };
    sb_status_t sb = sb_read(SB_REG_VER, ver);

    uint32_t spi_hz = lcd_init();
    screen_init();
    s_lcd_us = time_us_64();

    log_banner(clocks_ok, i2c_hz, sb, ver, spi_hz);

    term_init(&g_term);
    keys_init(&g_keys);
    const out_t out = { con_write, NULL, false };
    const shell_hooks_t hooks = { now_us, interrupted, NULL };
    shell_init(&g_shell, &out, &hooks, SHELL_VERSION);
    shell_start(&g_shell);
    s_prompt_us = time_us_64();

    for (;;) {
        poll_keys();
        uint8_t state, code;
        while (kbd_pop(&state, &code)) {
            int k = keys_event(&g_keys, state, code);
            if (k != KEYS_NONE) shell_input(&g_shell, k);
        }
#if PICO_SHELL_UART
        int c;
        while ((c = getchar_timeout_us(0)) >= 0) {
            if (c == UART_DUMP_SCREEN) dump_screen();
            else if (c == UART_LOG_STATS) log_stats();
            else shell_input(&g_shell, keys_uart((uint8_t)c));
        }
#endif
        screen_tick(&g_term, time_us_64());
    }
}
