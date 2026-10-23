/* ds3231.h — a DS3231 real-time clock on i2c0, GP4 (SDA) and GP5 (SCL),
 * at 100 kHz (design.md §3.8). It holds UTC. Only clock.c uses it.
 *
 * Every transfer is bounded by a timeout, as the southbridge's are
 * (hardware-notes.md §6.1), so a bus with nothing on it, or held low,
 * costs milliseconds. */
#ifndef SHELL_DS3231_H
#define SHELL_DS3231_H

#include <stdint.h>

typedef enum {
    DS3231_OK,
    DS3231_ABSENT,    /* nothing answered at 0x68 */
    DS3231_TIMEOUT,   /* the bus is stuck */
    DS3231_STOPPED,   /* OSF: its oscillator stopped, and the time is lost */
    DS3231_BAD,       /* its registers are not a time; or a time it cannot hold */
} ds3231_status_t;

/* The bus and its pins. */
void ds3231_init(void);

/* The time, UTC seconds since 1970, to the second. */
ds3231_status_t ds3231_read(int64_t *utc_s);

/* Set it, starting a new second now; clears OSF, and EOSC, so the
 * oscillator runs on the battery. 2000 to 2199 only. */
ds3231_status_t ds3231_write(int64_t utc_s);

#endif /* SHELL_DS3231_H */
