/* test_clock.c — the date and time (design.md §3.8, M12 step 2), in the
 * UNIX flavour: the calendar, TZ across the changes of daylight saving,
 * FAT's timestamps, `date` in each form and its mistakes, `ls -l`'s
 * six-month rule, and the times each command writes, over FAT16 and
 * FAT32, with the clock set and not set.
 *
 * The port's clock is a fake: a time in UTC milliseconds, set or not. */

#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "files.h"
#include "host_disk.h"
#include "shell.h"
#include "test_util.h"
#include "vars.h"

static shell_t sh;
static char    con[1 << 16];   /* the terminal's text, CR removed */
static size_t  con_len;

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

static int run(const char *line) {
    con_len = 0;
    con[0] = '\0';
    return shell_run(&sh, line);
}

/* ---- the fake clock --------------------------------------------------- */

static int64_t     fake_ms;
static bool        fake_is_set;
static const char *fake_refuse;   /* what set says, or NULL to take it */

static bool fake_get(void *ctx, int64_t *utc_ms) {
    (void)ctx;
    if (fake_is_set) *utc_ms = fake_ms;
    return fake_is_set;
}

static const char *fake_set(void *ctx, int64_t utc_ms) {
    (void)ctx;
    if (fake_refuse) return fake_refuse;
    fake_ms = utc_ms;
    fake_is_set = true;
    return NULL;
}

/* A moment in UTC, in seconds. */
static int64_t utc(int y, int mo, int d, int h, int mi, int s) {
    return clock_days(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

static void set_at(int64_t utc_s) {
    fake_ms = utc_s * 1000;
    fake_is_set = true;
}

static void tz(const char *value) {
    if (value) vars_set("TZ", 2, value);
    else vars_unset("TZ", 2);
}

/* `utc_s` in local time as "YYYY-MM-DD HH:MM:SS ZONE offset". */
static const char *local(int64_t utc_s) {
    static char buf[64];
    clock_tm_t tm;
    clock_local(utc_s, &tm);
    snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d %s %+d%s", tm.year, tm.mon, tm.mday,
             tm.hour, tm.min, tm.sec, tm.zone, tm.off_min, tm.dst ? " dst" : "");
    return buf;
}

/* ---- the calendar, TZ and FAT, with no shell ---------------------------- */

static int calendar_tests(void) {
    CHECK(clock_days(1970, 1, 1) == 0 && clock_days(2000, 3, 1) == 11017 &&
              clock_days(1969, 12, 31) == -1,
          "days");
    /* Every day from 1980 to 2108 there and back, with its weekday. */
    int wday = 2;   /* 1980-01-01 was a Tuesday */
    for (int64_t d = clock_days(1980, 1, 1); d < clock_days(2108, 1, 1); d++) {
        clock_tm_t tm;
        clock_gmt(d * 86400 + 86399, &tm);
        if (clock_days(tm.year, tm.mon, tm.mday) != d || tm.wday != wday || tm.hour != 23 ||
            tm.sec != 59 || !clock_valid(tm.year, tm.mon, tm.mday)) {
            CHECK(0, "day %lld: %d-%d-%d wday %d", (long long)d, tm.year, tm.mon, tm.mday, tm.wday);
            break;
        }
        wday = (wday + 1) % 7;
    }
    CHECK(clock_valid(2024, 2, 29) && !clock_valid(2026, 2, 29) && !clock_valid(2100, 2, 29) &&
              clock_valid(2000, 2, 29) && !clock_valid(2026, 4, 31) && !clock_valid(2026, 13, 1) &&
              !clock_valid(2026, 0, 1) && !clock_valid(2026, 1, 0),
          "valid");

    /* FAT's date and time, there and back; seconds go in twos. */
    clock_tm_t a = { .year = 2026, .mon = 10, .mday = 22, .hour = 14, .min = 30, .sec = 59 }, b;
    uint16_t fd, ft;
    clock_to_fat(&a, &fd, &ft);
    clock_from_fat(fd, ft, &b);
    CHECK(b.year == 2026 && b.mon == 10 && b.mday == 22 && b.hour == 14 && b.min == 30 &&
              b.sec == 58,
          "fat: %d-%d-%d %d:%d:%d", b.year, b.mon, b.mday, b.hour, b.min, b.sec);
    clock_from_fat(0, 0, &b);
    CHECK(b.year == 0, "fat date 0");
    return 0;
}

static int tz_tests(void) {
    /* None, empty, a zoneinfo name and nonsense: UTC. */
    int64_t t = utc(2026, 10, 22, 18, 30, 5);
    const char *none[] = { NULL, "", ":America/Toronto", "America/Toronto", "EST", "E5",
                           "EST5EDT,M3.2.0", "EST5EDT,M13.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0x",
                           "EST99" };
    for (size_t i = 0; i < sizeof none / sizeof none[0]; i++) {
        tz(none[i]);
        CHECK(strcmp(local(t), "2026-10-22 18:30:05 UTC +0") == 0, "TZ '%s': %s",
              none[i] ? none[i] : "(unset)", local(t));
    }

    /* The United States' rules, written out and by default: the second
     * Sunday of March at 2:00 EST, the first of November at 2:00 EDT. */
    const char *eastern[] = { "EST5EDT,M3.2.0,M11.1.0", "EST5EDT", "EST+5EDT4,M3.2.0/2,M11.1.0/02:00:00" };
    for (size_t i = 0; i < sizeof eastern / sizeof eastern[0]; i++) {
        tz(eastern[i]);
        struct { int64_t t; const char *want; } c[] = {
            { utc(2026, 3, 8, 6, 59, 59), "2026-03-08 01:59:59 EST -300" },
            { utc(2026, 3, 8, 7, 0, 0), "2026-03-08 03:00:00 EDT -240 dst" },
            { utc(2026, 11, 1, 5, 59, 59), "2026-11-01 01:59:59 EDT -240 dst" },
            { utc(2026, 11, 1, 6, 0, 0), "2026-11-01 01:00:00 EST -300" },
            { utc(2026, 10, 22, 18, 30, 5), "2026-10-22 14:30:05 EDT -240 dst" },
            { utc(2027, 1, 1, 4, 59, 59), "2026-12-31 23:59:59 EST -300" },
        };
        for (size_t k = 0; k < sizeof c / sizeof c[0]; k++)
            CHECK(strcmp(local(c[k].t), c[k].want) == 0, "%s: %s, not %s", eastern[i],
                  local(c[k].t), c[k].want);
    }

    /* Local time back to UTC: a time skipped in March is an hour on, one
     * that happens twice in November the first. */
    tz("EST5EDT,M3.2.0,M11.1.0");
    clock_tm_t tm = { .year = 2026, .mon = 3, .mday = 8, .hour = 2, .min = 30 };
    CHECK(clock_from_local(&tm) == utc(2026, 3, 8, 7, 30, 0), "the gap: %s",
          local(clock_from_local(&tm)));
    tm = (clock_tm_t){ .year = 2026, .mon = 11, .mday = 1, .hour = 1, .min = 30 };
    CHECK(clock_from_local(&tm) == utc(2026, 11, 1, 5, 30, 0), "twice: %s",
          local(clock_from_local(&tm)));
    tm = (clock_tm_t){ .year = 2026, .mon = 1, .mday = 15, .hour = 9 };
    CHECK(clock_from_local(&tm) == utc(2026, 1, 15, 14, 0, 0), "winter");

    /* The southern hemisphere: summer across the new year. */
    tz("AEST-10AEDT,M10.1.0,M4.1.0/3");
    CHECK(strcmp(local(utc(2026, 1, 15, 0, 0, 0)), "2026-01-15 11:00:00 AEDT +660 dst") == 0,
          "AEDT: %s", local(utc(2026, 1, 15, 0, 0, 0)));
    CHECK(strcmp(local(utc(2026, 7, 15, 0, 0, 0)), "2026-07-15 10:00:00 AEST +600") == 0,
          "AEST: %s", local(utc(2026, 7, 15, 0, 0, 0)));
    /* 3:00 AEDT on 5 April 2026 is 16:00 UTC the day before. */
    CHECK(strcmp(local(utc(2026, 4, 4, 15, 59, 59)), "2026-04-05 02:59:59 AEDT +660 dst") == 0 &&
              strcmp(local(utc(2026, 4, 4, 16, 0, 0)), "2026-04-05 02:00:00 AEST +600") == 0,
          "AEDT ends: %s", local(utc(2026, 4, 4, 16, 0, 0)));

    /* A name in <>, minutes, no daylight saving. */
    tz("<+0530>-5:30");
    CHECK(strcmp(local(utc(2026, 10, 22, 18, 30, 0)), "2026-10-23 00:00:00 +0530 +330") == 0,
          "India: %s", local(utc(2026, 10, 22, 18, 30, 0)));
    /* Jn and n: day 60 is 1 March, with or without a 29 February; n
     * counts it. Changes at 2:00 local time. */
    tz("AAA0BBB,J60,J300");
    CHECK(strcmp(local(utc(2028, 3, 1, 2, 0, 0)), "2028-03-01 03:00:00 BBB +60 dst") == 0 &&
              strcmp(local(utc(2028, 2, 29, 12, 0, 0)), "2028-02-29 12:00:00 AAA +0") == 0,
          "Jn: %s", local(utc(2028, 3, 1, 2, 0, 0)));
    tz("AAA0BBB,59,300");
    CHECK(strcmp(local(utc(2028, 2, 29, 2, 0, 0)), "2028-02-29 03:00:00 BBB +60 dst") == 0,
          "n: %s", local(utc(2028, 2, 29, 2, 0, 0)));
    tz(NULL);
    return 0;
}

/* ---- the commands ----------------------------------------------------- */

/* A file's time as ls shows it, from its entry. */
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

static int date_tests(void) {
    tz(NULL);
    fake_is_set = false;
    fake_refuse = NULL;

    /* Not set. */
    CHECK(run("date") == 1 && strcmp(con, "date: clock not set\n") == 0, "not set: '%s'", con);
    CHECK(run("date -u") == 1, "not set, -u");

    /* Set, in each form, in local time; the new time is shown. */
    tz("EST5EDT,M3.2.0,M11.1.0");
    CHECK(run("date 1022143026") == 0 && strcmp(con, "Thu Oct 22 14:30:00 EDT 2026\n") == 0 &&
              fake_ms == utc(2026, 10, 22, 18, 30, 0) * 1000,
          "mmddHHMMyy: '%s'", con);
    CHECK(run("date") == 0 && strcmp(con, "Thu Oct 22 14:30:00 EDT 2026\n") == 0, "show: '%s'", con);
    CHECK(run("date -u") == 0 && strcmp(con, "Thu Oct 22 18:30:00 UTC 2026\n") == 0, "-u: '%s'", con);
    fake_ms += 5000;
    CHECK(run("date") == 0 && strcmp(con, "Thu Oct 22 14:30:05 EDT 2026\n") == 0, "ticks: '%s'", con);
    CHECK(run("date 01020304") == 0 && strcmp(con, "Fri Jan  2 03:04:00 EST 2026\n") == 0,
          "mmddHHMM, this year: '%s'", con);
    CHECK(run("date 123123592099") == 0 && strcmp(con, "Thu Dec 31 23:59:00 EST 2099\n") == 0,
          "ccyy: '%s'", con);
    CHECK(run("date 0101000080") == 0 && strcmp(con, "Tue Jan  1 00:00:00 EST 1980\n") == 0,
          "80 is 1980: '%s'", con);
    CHECK(run("date 0229120024") == 0 && strstr(con, "Feb 29 12:00:00 EST 2024"), "leap: '%s'", con);
    CHECK(run("date -u 1022183026") == 0 && strcmp(con, "Thu Oct 22 18:30:00 UTC 2026\n") == 0 &&
              fake_ms == utc(2026, 10, 22, 18, 30, 0) * 1000,
          "-u sets UTC: '%s'", con);

    /* Without a year and with the clock not set: the fixed date's. */
    fake_is_set = false;
    CHECK(run("date 03150900") == 0 && strcmp(con, "Sun Mar 15 09:00:00 EDT 2026\n") == 0,
          "no year, not set: '%s'", con);

    /* Mistakes: nothing is set. */
    set_at(utc(2026, 10, 22, 18, 30, 0));
    const char *bad[] = { "1322143026", "0230000026", "1022246026", "1022140026x", "102214",
                          "10221430261", "1022143079", "abcdefgh", "1022143021000" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char line[64], want[96];
        snprintf(line, sizeof line, "date %s", bad[i]);
        snprintf(want, sizeof want, "date: %s: not a date: mmddHHMM[[cc]yy]\n", bad[i]);
        CHECK(run(line) == 1 && strcmp(con, want) == 0 &&
                  fake_ms == utc(2026, 10, 22, 18, 30, 0) * 1000,
              "%s: '%s'", line, con);
    }
    CHECK(run("date -x") == 1 && strcmp(con, "usage: date [-u] [mmddHHMM[[cc]yy]]\n") == 0,
          "-x: '%s'", con);
    CHECK(run("date 1022143026 1022143026") == 1 && strstr(con, "usage:"), "two: '%s'", con);
    fake_refuse = "the clock cannot be set";
    CHECK(run("date 1022143026") == 1 && strcmp(con, "date: the clock cannot be set\n") == 0,
          "refused: '%s'", con);
    fake_refuse = NULL;
    tz(NULL);
    CHECK(run("date") == 0 && strcmp(con, "Thu Oct 22 18:30:00 UTC 2026\n") == 0, "UTC: '%s'", con);
    return 0;
}

static int file_tests(bool fat32) {
    const char *kind = fat32 ? "FAT32" : "FAT16";
    CHECK(host_disk_create(NULL, fat32 ? 64u << 20 : 32u << 20, fat32), "%s image", kind);
    files_mount();
    shell_start(&sh);

    /* Not set: FatFs's fixed date, and touch leaves a file alone. */
    fake_is_set = false;
    tz("EST5EDT");
    CHECK(run("echo a > /old.txt") == 0 && strcmp(stamp("/old.txt"), "2026-01-01 00:00") == 0,
          "%s: not set: %s", kind, stamp("/old.txt"));
    CHECK(stamp_file("/old.txt", 2020, 5, 6, 7, 8) && run("touch /old.txt") == 0 &&
              strcmp(stamp("/old.txt"), "2020-05-06 07:08") == 0,
          "%s: touch, not set: %s", kind, stamp("/old.txt"));

    /* Set: each command that writes gives local time. */
    set_at(utc(2026, 10, 22, 18, 30, 0));
    CHECK(run("echo b > /a.txt") == 0 && strcmp(stamp("/a.txt"), "2026-10-22 14:30") == 0,
          "%s: >: %s", kind, stamp("/a.txt"));
    fake_ms += 60000;
    CHECK(run("echo c >> /a.txt") == 0 && strcmp(stamp("/a.txt"), "2026-10-22 14:31") == 0,
          "%s: >>: %s", kind, stamp("/a.txt"));
    CHECK(run("mkdir /d") == 0 && strcmp(stamp("/d"), "2026-10-22 14:31") == 0, "%s: mkdir: %s",
          kind, stamp("/d"));
    CHECK(run("touch /new.txt") == 0 && strcmp(stamp("/new.txt"), "2026-10-22 14:31") == 0,
          "%s: touch new: %s", kind, stamp("/new.txt"));
    fake_ms += 60000;
    CHECK(run("touch /old.txt") == 0 && strcmp(stamp("/old.txt"), "2026-10-22 14:32") == 0,
          "%s: touch sets: %s", kind, stamp("/old.txt"));
    CHECK(stamp_file("/old.txt", 2020, 5, 6, 7, 8) && run("cp /old.txt /copy.txt") == 0 &&
              strcmp(stamp("/copy.txt"), "2026-10-22 14:32") == 0 &&
              strcmp(stamp("/old.txt"), "2020-05-06 07:08") == 0,
          "%s: cp, the time now: %s", kind, stamp("/copy.txt"));
    CHECK(run("mv /copy.txt /moved.txt") == 0 && strcmp(stamp("/moved.txt"), "2026-10-22 14:32") == 0,
          "%s: mv keeps it: %s", kind, stamp("/moved.txt"));

    /* ls -l: the time within six months either way, the year beyond. */
    CHECK(run("mkdir /l") == 0 && run("echo x > /l/now") == 0 && run("echo x > /l/spring") == 0 &&
              run("echo x > /l/lastyear") == 0 && run("echo x > /l/future") == 0 &&
              run("echo x > /l/soon") == 0,
          "%s: files", kind);
    CHECK(stamp_file("/l/spring", 2026, 4, 25, 9, 5) && stamp_file("/l/lastyear", 2026, 4, 22, 9, 5) &&
              stamp_file("/l/future", 2027, 4, 23, 12, 0) && stamp_file("/l/soon", 2027, 4, 21, 23, 59),
          "%s: stamps", kind);
    CHECK(run("ls -l /l") == 0 &&
              strcmp(con, "-rw        2 Apr 23  2027 future\n"
                          "-rw        2 Apr 22  2026 lastyear\n"
                          "-rw        2 Oct 22 14:32 now\n"
                          "-rw        2 Apr 21 23:59 soon\n"
                          "-rw        2 Apr 25 09:05 spring\n") == 0,
          "%s: ls -l:\n%s", kind, con);
    CHECK(run("ls -l /d /a.txt") == 0 && strstr(con, "-rw        4 Oct 22 14:31 /a.txt\n"),
          "%s: ls -l named: '%s'", kind, con);
    /* No line reaches the 40th column with a 14-column name. */
    CHECK(run("echo x > /l/abcdefghijklmn") == 0 && run("cd /l") == 0 &&
              run("ls -l abcdefghijklmn") == 0 &&
              strlen(con) == 41,
          "%s: 40 columns: '%s'", kind, con);
    return 0;
}

/* The port's note, after the banner and before the startup file, once. */
static int note_tests(const char *want) {
    const shell_clock_t clock = { fake_get, fake_set, NULL, "the DS3231 lost its time", NULL, NULL };
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
    const shell_hooks_t hooks = { now_us, interrupted, NULL, NULL, NULL, NULL };
    shell_init(&sh, &out, &hooks, "test");

    if (calendar_tests()) return 1;
    if (tz_tests()) return 1;

    /* No clock at all. */
    CHECK(run("date") == 1 && strcmp(con, "date: clock not set\n") == 0, "no clock: '%s'", con);
    CHECK(run("date 1022143026") == 1 && strcmp(con, "date: no clock here\n") == 0,
          "no clock, set: '%s'", con);

    const shell_clock_t clock = { fake_get, fake_set, NULL, NULL, NULL, NULL };
    shell_set_clock(&sh, &clock);
    if (date_tests()) return 1;
    if (file_tests(false)) return 1;
    if (file_tests(true)) return 1;
    if (note_tests(" test\n\nshell: the DS3231 lost its time\n")) return 1;
    TEST_DONE();
}
