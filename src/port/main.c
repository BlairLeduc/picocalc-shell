/* main.c — bring-up, then the shell's loop (design.md §5, §10).
 *
 * One core. Bring-up follows hardware-notes.md §10: the southbridge, the
 * panel, the keyboard. The shell's output goes to the terminal's grid,
 * then, in the development build, to UART1 as the same bytes, so a
 * terminal on the other end shows what the panel does; then the panel is
 * brought up to date. Waiting for a key, at the prompt or in vi, polls
 * the keyboard and card detect at about 30 Hz, takes the UART's bytes as
 * keys, and blinks the cursor.
 *
 * Two UART bytes are not keys (EL §13.1): FS (0x1C) dumps the grid as
 * text, and GS (0x1D) logs the start time and the counters.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/uart.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "board.h"
#include "card.h"
#include "kbd.h"
#include "keycodes.h"
#include "keys.h"
#include "lcd.h"
#include "loader.h"
#include "screen.h"
#include "sd.h"
#include "shell.h"
#include "shell_version.h"
#include "southbridge.h"
#include "term.h"
#include "usbdisk.h"

#define KBD_POLL_US 33333u   /* about 30 Hz (hardware-notes.md §6.1) */

#define UART_DUMP_SCREEN 0x1C
#define UART_LOG_STATS   0x1D

static term_t       g_term;
static shell_t      g_shell;
static keys_t       g_keys;
static board_info_t g_board;
static shell_loader_t  g_loader;
static loader_status_t g_loader_st;
static sb_status_t     g_sb;           /* the version's read at bring-up */
static uint8_t         g_sb_ver[2];

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

static bool usb_stop(void *ctx) {
    return interrupted(ctx);
}

/* usbdisk: until the computer ejects the card, or Break, Esc or Ctrl-C. */
static const char *usbdisk(void *ctx) {
    return usbdisk_run(usb_stop, ctx);
}

/* ---- the PicoCalc, for system.h ---------------------------------------- */

static const char *sb_why(sb_status_t st) {
    return st == SB_TIMEOUT ? "the southbridge timed out" : "the southbridge did not answer";
}

static const char *battery(void *ctx, unsigned *percent, bool *charging) {
    (void)ctx;
    uint8_t r[2];
    sb_status_t st = sb_read(SB_REG_BAT, r);
    if (st != SB_OK) return sb_why(st);
    *percent = r[1] & 0x7Fu;
    *charging = (r[1] & 0x80u) != 0;   /* clear proves nothing (hardware-notes.md §6) */
    return NULL;
}

static const char *backlight(void *ctx, shell_light_t which, int level, unsigned *now) {
    (void)ctx;
    uint8_t reg = which == SHELL_LIGHT_LCD ? SB_REG_BKL : SB_REG_BK2;
    uint8_t r[2];
    sb_status_t st = level >= 0 ? sb_write(reg, (uint8_t)level, NULL) : SB_OK;
    /* The MCU may step and clamp the level (§4.11): read back what it kept. */
    if (st == SB_OK) st = sb_read(reg, r);
    if (st != SB_OK) return sb_why(st);
    *now = r[1];
    return NULL;
}

static void phosphor(void *ctx, uint16_t rgb565) {
    (void)ctx;
    term_set_phosphor(&g_term, rgb565);
}

static void info(void *ctx, const out_t *o) {
    (void)ctx;
    out_printf(o, "chip      %s rev %u, %lu MHz\n", g_board.chip, g_board.chip_version,
               (unsigned long)(g_board.clk_sys_hz / 1000000u));
    out_printf(o, "id        %s\n", g_board.unique_id);
    out_printf(o, "memory    %lu KB of %lu KB SRAM\n",
               (unsigned long)(((uintptr_t)&__end__ - SRAM_BASE) >> 10),
               (unsigned long)((SRAM_END - SRAM_BASE) >> 10));
    if (g_loader_st.part_size)
        out_printf(o, "loader    UF2 Loader %s, %lu KB for programs\n",
                   g_loader_st.version ? g_loader_st.version : "(no version)",
                   (unsigned long)(g_loader_st.part_size >> 10));
    else
        out_puts(o, "loader    none in flash\n");
    out_printf(o, "programs  %s\n", g_loader.problem ? g_loader.problem : "can be run");
    if (g_sb == SB_OK) out_printf(o, "keyboard  firmware 0x%02x\n", g_sb_ver[1]);
    else out_printf(o, "keyboard  %s\n", sb_why(g_sb));
}

/* The power-off delay (hardware-notes.md §6), and how long to wait past
 * it before saying it did not happen: on USB power the Pico stays up. */
#define POWEROFF_S      6u
#define POWEROFF_WAIT_S 10u

static const char *power(void *ctx, shell_power_t what) {
    (void)ctx;
    switch (what) {
    case SHELL_POWER_OFF: {
        sb_status_t st = sb_write(SB_REG_OFF, POWEROFF_S, NULL);
        if (st != SB_OK) return sb_why(st);
        uint64_t end = time_us_64() + POWEROFF_WAIT_S * 1000000u;
        while (time_us_64() < end) screen_tick(&g_term, time_us_64());
        return "still on after 10 s; on USB power, perhaps";
    }
    case SHELL_POWER_REBOOT:
        /* Without either, stage3 would drop to BOOTSEL (design.md §3.1). */
        if (!g_loader_st.part_size) return "no UF2 Loader in flash";
        if (!sd_present()) return "no card to load the shell from";
        loader_reboot_shell();
        break;
    case SHELL_POWER_BOOTSEL:
#if PICO_SHELL_UART
        uart_tx_wait_blocking(uart_default);
#endif
        reset_usb_boot(0, 0);
        break;
    }
    return NULL;
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

/* The next input byte, from the keyboard or, in the development build,
 * the UART, with the cursor blinking while it waits. FS and GS on the
 * UART are acted on here and never returned. */
static int getkey(void *ctx) {
    (void)ctx;
    for (;;) {
        poll_keys();
        uint8_t state, code;
        while (kbd_pop(&state, &code)) {
            int k = keys_event(&g_keys, state, code);
            if (k != KEYS_NONE) return k;
        }
#if PICO_SHELL_UART
        int c;
        while ((c = getchar_timeout_us(0)) >= 0) {
            if (c == UART_DUMP_SCREEN) dump_screen();
            else if (c == UART_LOG_STATS) log_stats();
            else return keys_uart((uint8_t)c);
        }
#endif
        screen_tick(&g_term, time_us_64());
    }
}

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
    if (g_loader_st.part_size)
        printf("  loader       : stage3 %s, app partition 0x%06lx + %lu KB, flash end 0x%08lx\n",
               g_loader_st.version ? g_loader_st.version : "(no version)",
               (unsigned long)g_loader_st.part_offset, (unsigned long)(g_loader_st.part_size >> 10),
               (unsigned long)g_loader_st.flash_end);
    printf("  programs     : %s\n", g_loader.problem ? g_loader.problem : "can be run");
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
#if PICO_SHELL_UART
    /* Bytes on UART1 are keys: with nothing on GP5, as on a PicoCalc with
     * no probe attached, a floating line could type them. */
    gpio_pull_up(PICO_DEFAULT_UART_RX_PIN);
#endif
    board_identify(&g_board);
    loader_init(&g_loader, &g_loader_st);

    uint32_t i2c_hz = sb_init();
    g_sb = sb_read(SB_REG_VER, g_sb_ver);

    uint32_t spi_hz = lcd_init();
    screen_init();
    s_lcd_us = time_us_64();

    log_banner(clocks_ok, i2c_hz, g_sb, g_sb_ver, spi_hz);

    term_init(&g_term);
    keys_init(&g_keys);
    const out_t out = { con_write, NULL, false };
    const shell_hooks_t hooks = { now_us, interrupted, getkey, usbdisk, NULL };
    shell_init(&g_shell, &out, &hooks, SHELL_VERSION);
    shell_set_loader(&g_shell, &g_loader);
    const shell_system_t sys = { battery, backlight, phosphor, info, power, NULL };
    shell_set_system(&g_shell, &sys);
    shell_start(&g_shell);
    s_prompt_us = time_us_64();

    for (;;) shell_input(&g_shell, getkey(NULL));
}
