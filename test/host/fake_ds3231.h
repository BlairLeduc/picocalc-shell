/* fake_ds3231.h — the host's model of a DS3231 on I2C and of POWMAN's AON
 * timer and scratch registers, behind the fake SDK in fake_sdk/, for
 * test_ds3231. Time is the model's: the timer moves only as the code
 * under test sleeps, or as the test moves it. The chip's registers do not
 * tick; the test reads what was written. */
#ifndef SHELL_FAKE_DS3231_H
#define SHELL_FAKE_DS3231_H

#include <stdbool.h>
#include <stdint.h>

#define FAKE_RTC_REGS 0x13u

typedef struct {
    bool     present;                /* answers at 0x68 */
    bool     stuck;                  /* every transfer times out */
    uint8_t  reg[FAKE_RTC_REGS];
    unsigned transfers;              /* reads and writes, answered or not */
    unsigned time_writes;            /* writes that set the seconds */
    uint64_t written_at_ms;          /* the timer then */
} fake_rtc_t;

extern fake_rtc_t fake_rtc;

/* A chip as it comes: present, its registers 0 but OSF set and the
 * control register's default, 0x1C. */
void fake_rtc_reset(void);

/* The timer: its count and whether it runs. */
extern uint64_t fake_aon_ms;
extern bool     fake_aon_running;

/* Power off and on: the scratch registers cleared and the timer stopped
 * at 0. The chip keeps its registers, on its battery. */
void fake_power_cycle(void);

#endif /* SHELL_FAKE_DS3231_H */
