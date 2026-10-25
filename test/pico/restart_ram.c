/* restart_ram.c — restart-test as a RAM program: it restarts itself at
 * every start, to check a RAM program's restart loop under M7's patched
 * stage3 (design.md §10 M7).
 *
 * Copy build/pico/restart-ram.uf2 to /pico2-apps/ and type
 * `restart-ram`. It shows how many times it has started since the shell
 * ran it, reads and drops the keyboard's events for a second, then
 * reboots with UF2 Loader's BOOT_RAM command and its own path, which
 * loads it from the card again. A reboot with no command would start the
 * program in flash instead: a RAM program leaves nothing there.
 *
 * stage3 opens only 8.3 names, so the path is RESTART_RAM_PATH with each
 * name made its 8.3 entry, read from the card at each start, as the
 * shell makes it (short_path in src/core/run.c). stage3 reads the path
 * after the reboot, so it must be in SRAM that the reboot and stage3
 * leave alone: below stage3's own 8 KB, which is checked.
 *
 * A command skips the patched stage3's watch for a key, so Up held does
 * not break this loop; switching off and on does (the owner, 2026-10-25).
 * The count is in watchdog scratch 3, as restart-test's, under another
 * magic. Each start is logged on UART0 (GP2/GP3, 115200).
 *
 * It uses the shell's own panel, terminal, keyboard and card code. */

#include <stdio.h>
#include <string.h>

#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "ff.h"
#include "kbd.h"
#include "lcd.h"
#include "picocalc_shell.h"
#include "screen.h"
#include "sd.h"
#include "southbridge.h"
#include "term.h"

#define RESTART_RAM_PATH "/pico2-apps/restart-ram.uf2"
#define PATH_MAX_8_3     63
#define BOOT_RAM         3u                         /* stage3's mode */
#define STAGE3_RAM_BASE  (0x20080000u - 0x2000u)   /* stage3/memmap_2350.ld */

#define KBD_POLL_US 33333u   /* about 30 Hz (hardware-notes.md §6.1) */
#define RUN_US      1000000u
#define COUNT_MAGIC 0x52520000u   /* "RR" over the count in the low half */

static term_t s_term;
static FATFS s_fs;
static FILINFO s_fno;
static char s_sfn[PATH_MAX_8_3 + 1];

/* FatFs's clock, for writes; this only reads. */
DWORD get_fattime(void) {
    return 0;
}

static void say(const char *s) {
    fputs(s, stdout);
    for (; *s; s++) {
        if (*s == '\n') term_write(&s_term, "\r", 1);
        term_write(&s_term, s, 1);
    }
    screen_flush(&s_term, time_us_64());
}

/* The 8.3 form of the absolute `path`, a name at a time. */
static FRESULT short_path(const char *path, char out[PATH_MAX_8_3 + 1]) {
    char prefix[PATH_MAX_8_3 + 1];
    size_t olen = 0;
    const char *p = path;
    while (*p == '/') p++;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t end = slash ? (size_t)(slash - path) : strlen(path);
        if (end > PATH_MAX_8_3) return FR_INVALID_NAME;
        memcpy(prefix, path, end);
        prefix[end] = '\0';
        FRESULT r = f_stat(prefix, &s_fno);
        if (r != FR_OK) return r;
        const char *name = s_fno.altname[0] ? s_fno.altname : s_fno.fname;
        size_t n = strlen(name);
        if (olen + 1 + n > PATH_MAX_8_3) return FR_INVALID_NAME;
        out[olen++] = '/';
        memcpy(out + olen, name, n);
        olen += n;
        p = slash ? slash + 1 : path + end;
    }
    out[olen] = '\0';
    return FR_OK;
}

int main(void) {
    stdio_init_all();
    uint32_t r = watchdog_hw->scratch[3];
    uint32_t n = (r & 0xffff0000u) == COUNT_MAGIC ? (r & 0xffffu) + 1u : 1u;
    watchdog_hw->scratch[3] = COUNT_MAGIC | (n & 0xffffu);
    printf("\nrestart-ram: start %lu, %llu us after reset\n", (unsigned long)n,
           (unsigned long long)time_us_64());

    sb_init();
    lcd_init();
    screen_init();
    term_init(&s_term);
    char buf[200];
    snprintf(buf, sizeof buf,
             "\033[1mrestart-ram\033[0m\n\n"
             "Start %lu. A RAM program: it\n"
             "restarts itself every second.\n"
             "Switch off and on for the shell.\n\n",
             (unsigned long)n);
    say(buf);

    const char *problem = NULL;
    FRESULT fr = FR_NOT_READY;
    if ((uintptr_t)s_sfn + sizeof s_sfn > STAGE3_RAM_BASE) problem = "The path is in stage3's RAM.\n";
    else if (!sd_present() || (fr = f_mount(&s_fs, "", 1)) != FR_OK) problem = "The card cannot be read.\n";
    else if ((fr = short_path(RESTART_RAM_PATH, s_sfn)) != FR_OK) problem = "No " RESTART_RAM_PATH ".\n";
    f_unmount("");
    if (problem) {
        say(problem);
        say("Switch off and on.\n");
        for (;;) tight_loop_contents();
    }
    snprintf(buf, sizeof buf, "Next: %s\n", s_sfn);
    say(buf);

    uint64_t end = time_us_64() + RUN_US;
    while (time_us_64() < end) {
        kbd_poll();
        uint8_t state, code;
        while (kbd_pop(&state, &code)) {}
        sleep_us(KBD_POLL_US);
    }
    stdio_flush();
    watchdog_hw->scratch[1] = BOOT_RAM;
    watchdog_hw->scratch[2] = (uint32_t)s_sfn;
    watchdog_hw->scratch[0] = PICOCALC_SHELL_LOADER_MAGIC;
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}
