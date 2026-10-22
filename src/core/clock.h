/* clock.h — the date and time (design.md §3.8).
 *
 * The port keeps the time as UTC (the AON timer on the board); this is
 * everything above it, portable: the civil calendar, local time by a
 * POSIX TZ string, and FAT's timestamps. The time zone is the shell's
 * variable TZ, such as EST5EDT,M3.2.0,M11.1.0: a standard zone and its
 * offset west of UTC, and optionally a daylight-saving zone, its offset
 * (an hour less by default) and the rules for when it starts and ends
 * (M3.2.0,M11.1.0, the United States', by default). Without TZ, or with
 * one that cannot be read, local time is UTC.
 *
 * With the clock not set, the shell still has a date to give FAT: the
 * fixed one FatFs used before (ffconf.h's FF_NORTC_*), at midnight. */
#ifndef SHELL_CLOCK_H
#define SHELL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "shell.h"

/* A moment in local time, or in UTC. */
typedef struct {
    int  year, mon, mday;   /* mon 1-12 */
    int  hour, min, sec;
    int  wday;              /* 0 is Sunday */
    int  off_min;           /* local time less UTC, in minutes */
    bool dst;
    char zone[SHELL_ZONE_MAX + 1];
} clock_tm_t;

/* The time now, in seconds since 1970 (UTC): false if the clock is not
 * set, or there is no clock. */
bool clock_utc(int64_t *utc_s);

/* Set the clock: NULL, or why not. */
const char *clock_set(int64_t utc_s);

/* `utc_s` broken down in local time, by TZ; or in UTC, its zone "UTC". */
void clock_local(int64_t utc_s, clock_tm_t *tm);
void clock_gmt(int64_t utc_s, clock_tm_t *tm);

/* The moment a local time names, by TZ: year to sec are read, the rest
 * ignored. A time skipped when daylight saving starts is taken as an
 * hour later; one that happens twice, the first. */
int64_t clock_from_local(const clock_tm_t *tm);

/* The local time now, true; or false and the fixed date, if the clock
 * is not set. */
bool clock_now(clock_tm_t *tm);

/* Days from 1970-01-01 to a date; whether a date is one; the days in a
 * month. */
int64_t clock_days(int year, int mon, int mday);
bool clock_valid(int year, int mon, int mday);
int clock_month_days(int year, int mon);

/* "Sun".."Sat" and "Jan".."Dec". */
const char *clock_wday_name(int wday);
const char *clock_mon_name(int mon);

/* A local time as FAT keeps it, and back. A FAT date of 0, which some
 * entries have, gives year 0. */
void clock_to_fat(const clock_tm_t *tm, uint16_t *fdate, uint16_t *ftime);
void clock_from_fat(uint16_t fdate, uint16_t ftime, clock_tm_t *tm);

/* A local time in seconds, as if it were UTC: for comparing two of them. */
int64_t clock_seconds(const clock_tm_t *tm);

#endif /* SHELL_CLOCK_H */
