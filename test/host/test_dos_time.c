/* test_dos_time.c — the DOS flavour's date and time (design.md §3.8, M12
 * step 2): DATE and TIME shown, set and asked for, and their mistakes;
 * the clock not set, as AUTOEXEC.BAT finds it; COUNTRY= in CONFIG.SYS;
 * DIR's dates; COPY and XCOPY keeping the source's; PROMPT's $D and $T.
 * Over FAT16 and FAT32, with a fake clock, as test_clock has. Linked
 * with shell_core_dos. */

#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "dos.h"
#include "files.h"
#include "flavour.h"
#include "host_disk.h"
#include "shell.h"
#include "test_util.h"
#include "vars.h"

static shell_t sh;
static char    con[1 << 16];   /* the terminal's text, CR removed */
static size_t  con_len;
static const char *keys = "";  /* what getkey types; -1 after it */

static void sink(void *ctx, const char *buf, size_t n) {
    (void)ctx;
    for (size_t i = 0; i < n; i++)
        if (buf[i] != '\r' && con_len + 1 < sizeof con) con[con_len++] = buf[i];
    con[con_len] = '\0';
}

static uint64_t now_us(void *ctx) {
    (void)ctx;
    return 0;
}

static bool interrupted(void *ctx) {
    (void)ctx;
    return false;
}

static int getkey(void *ctx) {
    (void)ctx;
    return *keys ? (unsigned char)*keys++ : -1;
}

static int run(const char *line) {
    con_len = 0;
    con[0] = '\0';
    return shell_run(&sh, line);
}

static bool has(const char *text) {
    return strstr(con, text) != NULL;
}

static bool put(const char *path, const char *text) {
    FIL f;
    UINT n;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    FRESULT r = f_write(&f, text, (UINT)strlen(text), &n);
    return f_close(&f) == FR_OK && r == FR_OK && n == strlen(text);
}

/* ---- the fake clock --------------------------------------------------- */

static int64_t fake_ms;
static bool    fake_is_set;

static bool fake_get(void *ctx, int64_t *utc_ms) {
    (void)ctx;
    if (fake_is_set) *utc_ms = fake_ms;
    return fake_is_set;
}

static const char *fake_set(void *ctx, int64_t utc_ms) {
    (void)ctx;
    fake_ms = utc_ms;
    fake_is_set = true;
    return NULL;
}

static int64_t utc(int y, int mo, int d, int h, int mi, int s) {
    return clock_days(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

static void set_at(int64_t utc_s) {
    fake_ms = utc_s * 1000;
    fake_is_set = true;
}

static const char *stamp(const char *path) {
    static char buf[32];
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return "(none)";
    clock_tm_t tm;
    clock_from_fat(fno.fdate, fno.ftime, &tm);
    snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", tm.year, tm.mon, tm.mday, tm.hour, tm.min);
    return buf;
}

static bool stamp_file(const char *path, int y, int mo, int d, int h, int mi) {
    clock_tm_t tm = { .year = y, .mon = mo, .mday = d, .hour = h, .min = mi };
    FILINFO fno;
    clock_to_fat(&tm, &fno.fdate, &fno.ftime);
    return f_utime(path, &fno) == FR_OK;
}

/* The fake clock's time, in local time: "YYYY-MM-DD HH:MM:SS". */
static const char *local_now(void) {
    static char buf[32];
    clock_tm_t tm;
    clock_local(fake_ms / 1000, &tm);
    snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.year, tm.mon, tm.mday, tm.hour,
             tm.min, tm.sec);
    return buf;
}

/* ---- DATE and TIME ---------------------------------------------------- */

static int date_time_tests(void) {
    dos_set_country(1);
    fake_is_set = false;

    /* Not set: /T says so with ERRORLEVEL 1; DATE asks, and Enter keeps
     * it not set. */
    CHECK(run("DATE /T") == 1 && strcmp(con, "Current date is not set\n") == 0, "/T: '%s'", con);
    CHECK(run("TIME /T") == 1 && strcmp(con, "Current time is not set\n") == 0, "/T: '%s'", con);
    keys = "\r";
    CHECK(run("DATE") == 1 &&
              strcmp(con, "Current date is not set\nEnter new date (mm-dd-yy): \n") == 0,
          "Enter, not set: '%s'", con);

    /* AUTOEXEC.BAT's way of asking only when needed (design.md §3.8). */
    CHECK(put("/ASK.BAT", "@ECHO OFF\r\nDATE /T > NUL\r\nIF NOT ERRORLEVEL 1 GOTO SET\r\nDATE\r\n"
                          "TIME\r\n:SET\r\nECHO done\r\n"),
          "ask.bat");
    keys = "10-22-26\r2:30p\r";
    CHECK(run("\\ASK") == 0 && has("Enter new date (mm-dd-yy): ") && has("Enter new time: ") &&
              has("done\n") && fake_is_set,
          "asked: '%s'", con);
    const char *said = strstr(con, "Current date is not set\n");
    CHECK(said && !strstr(said + 1, "Current date is not set"), "said once: '%s'", con);
    CHECK(fake_ms == utc(2026, 10, 22, 14, 30, 0) * 1000, "set in UTC with no TZ: %s",
          local_now());
    keys = "";
    CHECK(run("\\ASK") == 0 && !has("Enter new") && has("done\n"), "not asked again: '%s'", con);

    /* Shown and set in local time. */
    run("SET TZ=EST5EDT");
    set_at(utc(2026, 10, 22, 18, 30, 5));
    CHECK(run("DATE /T") == 0 && strcmp(con, "Current date is Thu 10-22-2026\n") == 0, "'%s'", con);
    CHECK(run("TIME /T") == 0 && strcmp(con, "Current time is  2:30:05.00p\n") == 0, "'%s'", con);
    keys = "\r";
    CHECK(run("DATE") == 0 &&
              strcmp(con, "Current date is Thu 10-22-2026\nEnter new date (mm-dd-yy): \n") == 0 &&
              strcmp(local_now(), "2026-10-22 14:30:05") == 0,
          "Enter keeps it: '%s'", con);
    keys = "\r";
    CHECK(run("TIME") == 0 && strcmp(con, "Current time is  2:30:05.00p\nEnter new time: \n") == 0,
          "TIME, Enter: '%s'", con);
    CHECK(run("DATE 10-23-26") == 0 && con_len == 0 && strcmp(local_now(), "2026-10-23 14:30:05") == 0,
          "DATE keeps the time: '%s' %s", con, local_now());
    CHECK(run("DATE 1/2/2027") == 0 && fake_ms == utc(2027, 1, 2, 19, 30, 5) * 1000,
          "/ and four digits, into EST: %s", local_now());
    CHECK(run("DATE 12.31.99") == 0 && strcmp(local_now(), "1999-12-31 14:30:05") == 0,
          "99 is 1999: %s", local_now());
    CHECK(run("DATE 2-29-2024") == 0 && strcmp(local_now(), "2024-02-29 14:30:05") == 0,
          "leap: %s", local_now());
    CHECK(run("DATE 01-01-80") == 0 && run("DATE 12-31-2099") == 0 &&
              strcmp(local_now(), "2099-12-31 14:30:05") == 0,
          "DOS's range: %s", local_now());
    CHECK(run("DATE 10-22-26") == 0, "back");

    /* A bad date: DOS's word, and the question; with no more keys,
     * nothing changes. */
    const char *bad_dates[] = { "2-30-26", "10-22-1979", "10-22", "10-22-26-1", "13-01-26",
                                "10-22-123", "10-22-2100", "a-b-c", "100-1-26" };
    for (size_t i = 0; i < sizeof bad_dates / sizeof bad_dates[0]; i++) {
        char line[40];
        snprintf(line, sizeof line, "DATE %s", bad_dates[i]);
        keys = "";
        CHECK(run(line) == 1 && strcmp(con, "Invalid date\nEnter new date (mm-dd-yy): \n") == 0 &&
                  strcmp(local_now(), "2026-10-22 14:30:05") == 0,
              "%s: '%s' %s", line, con, local_now());
    }
    keys = "x\r10-24-26\r";
    CHECK(run("DATE") == 0 && has("Invalid date\nEnter new date (mm-dd-yy): ") &&
              strcmp(local_now(), "2026-10-24 14:30:05") == 0,
          "asked again: '%s'", con);
    keys = "\x1b";
    CHECK(run("DATE") == 130 && strcmp(local_now(), "2026-10-24 14:30:05") == 0, "Esc: '%s'", con);
    keys = "";
    CHECK(run("DATE 1 2") == 1 && has("Invalid date"), "two words: '%s'", con);
    CHECK(run("DATE /T 10-22-26") == 1 && has("Too many parameters"), "/T and a date: '%s'", con);
    CHECK(run("DATE /X") == 1 && has("Invalid switch"), "/X: '%s'", con);
    CHECK(run("DATE 10-22-26") == 0, "back");

    /* TIME: 24 hours or a and p; seconds and hundredths optional. */
    struct { const char *arg, *want; } times[] = {
        { "14:30", "2026-10-22 14:30:00" },  { "2:30p", "2026-10-22 14:30:00" },
        { "2:30 pm", "2026-10-22 14:30:00" }, { "12a", "2026-10-22 00:00:00" },
        { "12:15p", "2026-10-22 12:15:00" }, { "9", "2026-10-22 09:00:00" },
        { "9.05.06", "2026-10-22 09:05:06" }, { "23:59:59.99", "2026-10-22 23:59:59" },
        { "0:00", "2026-10-22 00:00:00" },
    };
    for (size_t i = 0; i < sizeof times / sizeof times[0]; i++) {
        char line[40];
        snprintf(line, sizeof line, "TIME %s", times[i].arg);
        CHECK(run(line) == 0 && strcmp(local_now(), times[i].want) == 0, "%s: %s '%s'", line,
              local_now(), con);
    }
    CHECK(run("TIME /T") == 0 && strcmp(con, "Current time is 12:00:00.00a\n") == 0, "midnight: '%s'",
          con);
    const char *bad_times[] = { "14:30p", "25:00", "13:60", "0a", "13a", "1:2:3:4:5", "x", "1:", "123" };
    for (size_t i = 0; i < sizeof bad_times / sizeof bad_times[0]; i++) {
        char line[40];
        snprintf(line, sizeof line, "TIME %s", bad_times[i]);
        keys = "";
        CHECK(run(line) == 1 && strcmp(con, "Invalid time\nEnter new time: \n") == 0 &&
                  strcmp(local_now(), "2026-10-22 00:00:00") == 0,
              "%s: '%s'", line, con);
    }
    return 0;
}

/* ---- COUNTRY= --------------------------------------------------------- */

static int country_tests(void) {
    run("SET TZ=EST5EDT");
    set_at(utc(2026, 10, 22, 18, 30, 5));

    /* A CONFIG.SYS from a PC: only COUNTRY= means anything. */
    CHECK(put("/CONFIG.SYS", "REM from a 486\r\nFILES=30\r\nBUFFERS = 20\r\n"
                             "DEVICE=C:\\DOS\\HIMEM.SYS\r\ndevicehigh=C:\\DOS\\EMM386.EXE\r\n\r\n"
                             "; a comment\r\nCOUNTRY=049,850,C:\\DOS\\COUNTRY.SYS\r\n"
                             "DOS=HIGH,UMB\r\nLASTDRIVE=Z\r\nBREAK=ON\r\n"
                             "SHELL=C:\\COMMAND.COM /P\r\nSTACKS=9,256\r\nrem\r\n"),
          "config.sys");
    con_len = 0;
    con[0] = '\0';
    dos_config(&sh);
    CHECK(con_len == 0 && dos_country()->code == 49, "Germany: '%s'", con);
    CHECK(run("DATE /T") == 0 && strcmp(con, "Current date is Thu 22.10.2026\n") == 0, "'%s'", con);
    CHECK(run("TIME /T") == 0 && strcmp(con, "Current time is 14:30:05,00\n") == 0, "'%s'", con);
    keys = "23.10.26\r";
    CHECK(run("DATE") == 0 && has("Enter new date (dd.mm.yy): ") &&
              strcmp(local_now(), "2026-10-23 14:30:05") == 0,
          "dd.mm.yy: '%s'", con);
    CHECK(run("TIME 2:30p") == 0 && strcmp(local_now(), "2026-10-23 14:30:00") == 0,
          "a and p still read: %s", local_now());

    /* The others: their order and separators. */
    struct { unsigned code; const char *date, *time, *ask; } c[] = {
        { 1, "Fri 10-23-2026", " 2:30:00.00p", "(mm-dd-yy)" },
        { 2, "Fri 2026-10-23", "14:30:00,00", "(yy-mm-dd)" },
        { 33, "Fri 23.10.2026", "14:30:00,00", "(dd.mm.yy)" },
        { 44, "Fri 23/10/2026", "14:30:00.00", "(dd/mm/yy)" },
        { 81, "Fri 2026-10-23", "14:30:00.00", "(yy-mm-dd)" },
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        char want[64];
        CHECK(dos_set_country(c[i].code), "%u", c[i].code);
        snprintf(want, sizeof want, "Current date is %s\n", c[i].date);
        CHECK(run("DATE /T") == 0 && strcmp(con, want) == 0, "%u: '%s'", c[i].code, con);
        snprintf(want, sizeof want, "Current time is %s\n", c[i].time);
        CHECK(run("TIME /T") == 0 && strcmp(con, want) == 0, "%u: '%s'", c[i].code, con);
        keys = "\r";
        CHECK(run("DATE") == 0 && has(c[i].ask), "%u: '%s'", c[i].code, con);
    }
    dos_set_country(2);
    CHECK(run("DATE 2027-1-2") == 0 && run("DATE 26-10-23") == 0 &&
              strcmp(local_now(), "2026-10-23 14:30:00") == 0,
          "yy-mm-dd: %s", local_now());

    /* Mistakes, each with its line; the country stays as it was. */
    CHECK(put("/CONFIG.SYS", "FILES=30\r\nBOGUS\r\nCOUNTRY=999\r\nCOUNTRY=abc\r\nCOUNTRY\r\n"
                             "FILESX=1\r\n"),
          "bad config.sys");
    con_len = 0;
    con[0] = '\0';
    dos_config(&sh);
    CHECK(strcmp(con, "Unrecognized command in CONFIG.SYS\nError in CONFIG.SYS line 2\n"
                      "Invalid country code or code page\nError in CONFIG.SYS line 3\n"
                      "Invalid country code or code page\nError in CONFIG.SYS line 4\n"
                      "Invalid country code or code page\nError in CONFIG.SYS line 5\n"
                      "Unrecognized command in CONFIG.SYS\nError in CONFIG.SYS line 6\n") == 0 &&
              dos_country()->code == 2,
          "mistakes: '%s'", con);
    CHECK(f_unlink("/CONFIG.SYS") == FR_OK, "unlink");
    dos_set_country(1);
    con_len = 0;
    dos_config(&sh);
    CHECK(con_len == 0 && dos_country()->code == 1, "none");
    return 0;
}

/* ---- the files -------------------------------------------------------- */

static int file_tests(bool fat32) {
    const char *kind = fat32 ? "FAT32" : "FAT16";
    CHECK(host_disk_create(NULL, fat32 ? 64u << 20 : 32u << 20, fat32), "%s image", kind);
    files_mount();
    shell_start(&sh);
    dos_set_country(1);
    run("SET TZ=EST5EDT");
    set_at(utc(2026, 10, 22, 18, 30, 5));

    CHECK(run("MD \\D") == 0 && put("/D/A.TXT", "hello") && strcmp(stamp("/D/A.TXT"), "2026-10-22 14:30") == 0,
          "%s: written in local time: %s", kind, stamp("/D/A.TXT"));
    CHECK(run("MD \\D\\SUB") == 0 && strcmp(stamp("/D/SUB"), "2026-10-22 14:30") == 0,
          "%s: MD: %s", kind, stamp("/D/SUB"));
    CHECK(run("DIR \\D") == 0 && has("\nA        TXT          5 10-22-26  2:30p\n") &&
              has("\nSUB          <DIR>      10-22-26  2:30p\n"),
          "%s: DIR: '%s'", kind, con);
    static char big[1235];
    memset(big, 'x', sizeof big - 1);
    CHECK(put("/D/BIG.DAT", big) && stamp_file("/D/BIG.DAT", 1999, 12, 31, 23, 59), "big");
    CHECK(run("DIR \\D") == 0 && has("\nBIG      DAT      1,234 12-31-99 11:59p\n"), "%s: '%s'", kind, con);
    dos_set_country(49);
    CHECK(run("DIR \\D") == 0 && has("\nBIG      DAT      1.234 31.12.99 23:59\n") &&
              has("\nA        TXT          5 22.10.26 14:30\n"),
          "%s: 24 hours, a column less: '%s'", kind, con);
    dos_set_country(1);

    /* COPY and XCOPY keep the source's date; MD and new files get now. */
    CHECK(stamp_file("/D/A.TXT", 2020, 5, 6, 7, 8), "old");
    CHECK(run("COPY \\D\\A.TXT \\D\\B.TXT") == 0 && strcmp(stamp("/D/B.TXT"), "2020-05-06 07:08") == 0,
          "%s: COPY: %s", kind, stamp("/D/B.TXT"));
    CHECK(put("/D/C.TXT", "x") && run("COPY \\D\\A.TXT \\D\\C.TXT") == 0 &&
              strcmp(stamp("/D/C.TXT"), "2020-05-06 07:08") == 0,
          "%s: COPY over a file: %s", kind, stamp("/D/C.TXT"));
    CHECK(run("COPY \\D\\*.TXT \\D\\SUB") == 0 && strcmp(stamp("/D/SUB/A.TXT"), "2020-05-06 07:08") == 0,
          "%s: COPY *.TXT: %s", kind, stamp("/D/SUB/A.TXT"));
    CHECK(run("XCOPY \\D \\E /S") == 0 && strcmp(stamp("/E/SUB/A.TXT"), "2020-05-06 07:08") == 0 &&
              strcmp(stamp("/E/BIG.DAT"), "1999-12-31 23:59") == 0 && strcmp(stamp("/E"), "2026-10-22 14:30") == 0,
          "%s: XCOPY: %s '%s'", kind, stamp("/E/SUB/A.TXT"), con);

    /* COPY NAME+,, gives the time now, and leaves the contents. */
    FILINFO fno;
    CHECK(run("copy/b \\D\\A.TXT+,,") == 0 && strcmp(stamp("/D/A.TXT"), "2026-10-22 14:30") == 0 &&
              f_stat("/D/A.TXT", &fno) == FR_OK && fno.fsize == 5 && has("        1 file(s) copied\n"),
          "%s: COPY/B A.TXT+,,: %s '%s'", kind, stamp("/D/A.TXT"), con);
    CHECK(run("COPY /B \\D\\B.TXT +,,") == 0 && strcmp(stamp("/D/B.TXT"), "2026-10-22 14:30") == 0,
          "%s: B.TXT +,,: %s", kind, stamp("/D/B.TXT"));
    CHECK(run("COPY \\D\\SUB\\*.TXT+,,") == 0 && strcmp(stamp("/D/SUB/A.TXT"), "2026-10-22 14:30") == 0 &&
              strcmp(stamp("/D/SUB/C.TXT"), "2026-10-22 14:30") == 0 && has("        3 file(s) copied\n"),
          "%s: *.TXT+,,: '%s'", kind, con);
    CHECK(run("COPY NOPE.TXT+,,") == 1 && has("File not found - NOPE.TXT\n"), "%s: '%s'", kind, con);
    /* With the clock not set, left as it is. */
    CHECK(stamp_file("/D/A.TXT", 2020, 5, 6, 7, 8), "old again");
    fake_is_set = false;
    CHECK(run("COPY \\D\\A.TXT+,,") == 0 && strcmp(stamp("/D/A.TXT"), "2020-05-06 07:08") == 0,
          "%s: no clock: %s", kind, stamp("/D/A.TXT"));
    fake_is_set = true;
    return 0;
}

/* ---- PROMPT ----------------------------------------------------------- */

static int prompt_tests(void) {
    char buf[SHELL_PROMPT_MAX + 1];
    dos_set_country(1);
    run("SET TZ=EST5EDT");
    set_at(utc(2026, 10, 22, 18, 30, 5));
    CHECK(run("PROMPT $D $T$G") == 0, "prompt");
    flavour_prompt(&sh, buf, sizeof buf);
    CHECK(strcmp(buf, "Thu 10-22-2026  2:30:05.00p>") == 0, "$D $T: '%s'", buf);
    fake_is_set = false;
    flavour_prompt(&sh, buf, sizeof buf);
    CHECK(strcmp(buf, " >") == 0, "not set: '%s'", buf);
    run("PROMPT");
    return 0;
}

/* The port's note, after the banner and before the startup file, once. */
static int note_tests(const char *want) {
    const shell_clock_t clock = { fake_get, fake_set, NULL, "the DS3231 lost its time",
                                  NULL, NULL, NULL };
    shell_set_clock(&sh, &clock);
    con_len = 0;
    shell_start(&sh);
    CHECK(strstr(con, want), "note: '%s'", con);
    con_len = 0;
    shell_start(&sh);
    CHECK(!strstr(con, "DS3231"), "once: '%s'", con);
    return 0;
}

int main(void) {
    const out_t out = { sink, NULL, false };
    const shell_hooks_t hooks = { now_us, interrupted, getkey, NULL, NULL, NULL };
    shell_init(&sh, &out, &hooks, "test");
    const shell_clock_t clock = { fake_get, fake_set, NULL, NULL, NULL, NULL, NULL };
    shell_set_clock(&sh, &clock);

    CHECK(host_disk_create(NULL, 32u << 20, false), "image");
    files_mount();
    shell_start(&sh);
    if (date_time_tests()) return 1;
    if (country_tests()) return 1;
    if (prompt_tests()) return 1;
    if (file_tests(false)) return 1;
    if (file_tests(true)) return 1;
    if (note_tests(" test (DOS)\n\nWarning: the DS3231 lost its time\n")) return 1;
    TEST_DONE();
}
