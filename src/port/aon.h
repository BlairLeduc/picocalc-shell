/* aon.h — the time the shell keeps, in the RP2350's always-on timer, and
 * the record beside it (design.md §3.8). In clock.c, which is the only
 * place that knows the record.
 *
 * The timer counts UTC milliseconds since 1970 from the crystal, and
 * survives the watchdog reboots between the shell and a program, but not
 * a power-off. POWMAN's scratch registers 0-2 say whether it was set, by
 * whom, and the local offset a program is given. Power-on clears them,
 * so a timer left running from before is not taken as set. */
#ifndef SHELL_AON_H
#define SHELL_AON_H

#include <stdbool.h>
#include <stdint.h>

/* Who set the clock, as `info` says it. */
typedef enum {
    AON_SRC_NONE,
    AON_SRC_USER,      /* date, DATE or TIME */
    AON_SRC_RTC,       /* read from a DS3231 */
    AON_SRC_NTP,       /* ntp.uf2 */
    AON_SRC_PROGRAM,   /* picocalc_shell_set_time() */
} aon_source_t;

/* The time: false if the clock is not set. */
bool aon_get(int64_t *utc_ms);

/* Set it, starting the timer from the crystal if it was not running.
 * Unless the time came from the DS3231, the chip needs it: aon_rtc_sync
 * writes it, or the next aon_start does. */
void aon_set(int64_t utc_ms, aon_source_t source);

/* At each start of the shell. With the clock set, write the DS3231 if it
 * needs it. Otherwise, once per power-on, read the chip into the clock:
 * NULL, or what to tell the user (the chip lost its time). */
const char *aon_start(void);

/* Write the DS3231, if there is one and it needs it, to the nearest
 * second. */
void aon_rtc_sync(void);

/* Who set it, AON_SRC_NONE if it is not set; and in words. */
aon_source_t aon_source(void);
const char *aon_source_name(aon_source_t source);

#endif /* SHELL_AON_H */
