/* clock.c — the AON timer and its record in POWMAN scratch 0-2 (aon.h;
 * design.md §3.8).
 *
 * POWMAN's scratch registers are their own bank (powman_hw->scratch[],
 * at 0x401000b0), not the watchdog's that carry the loader's commands.
 * The SDK's pico_low_power uses scratch 6 and 7; the shell uses 0-2:
 *
 *   scratch 0  AON_MAGIC
 *   scratch 1  bits 0-15 the local offset from UTC in minutes, signed;
 *              16-18 who set it (aon_source_t); 19 set; 20 NTP tried
 *              since power-on; 21 NTP set the clock; 22 the RTC needs
 *              writing; 23 the RTC was read since power-on
 *   scratch 2  ~(scratch 0 ^ scratch 1), as a check
 *
 * The clock is set when the magic, the check and `set` agree and the
 * timer is running.
 *
 * The DS3231 (ds3231.c) is read when the shell starts with the clock not
 * set, once per power-on, so a chip that lost its time is reported once
 * and not again after each program; and it is written whenever something
 * else set the clock: the shell at once, and after ntp.uf2 or a program,
 * which have no I2C driver, when the shell next starts. */

#include "aon.h"

#include <stddef.h>

#include "hardware/powman.h"

#include "ds3231.h"

#define AON_MAGIC      0x434c4b31u   /* "CLK1" */
#define REC_SOURCE_LSB 16u
#define REC_SOURCE     (7u << REC_SOURCE_LSB)
#define REC_SET        (1u << 19)
#define REC_RTC_DUE    (1u << 22)
#define REC_RTC_READ   (1u << 23)

/* Scratch 1, or 0 if the record is not whole. */
static uint32_t record(void) {
    uint32_t magic = powman_hw->scratch[0], bits = powman_hw->scratch[1];
    if (magic != AON_MAGIC || powman_hw->scratch[2] != ~(magic ^ bits)) return 0;
    return bits;
}

static void write_record(uint32_t bits) {
    powman_hw->scratch[0] = AON_MAGIC;
    powman_hw->scratch[1] = bits;
    powman_hw->scratch[2] = ~(AON_MAGIC ^ bits);
}

bool aon_get(int64_t *utc_ms) {
    if (!(record() & REC_SET) || !powman_timer_is_running()) return false;
    *utc_ms = (int64_t)powman_timer_get_ms();
    return true;
}

void aon_set(int64_t utc_ms, aon_source_t source) {
    /* The crystal, as the SDK's aon_timer_start() sets it; the low-power
     * oscillator is much less accurate. */
    powman_timer_set_1khz_tick_source_xosc();
    powman_timer_set_ms((uint64_t)utc_ms);
    powman_timer_start();
    uint32_t bits = record() & ~REC_SOURCE;
    if (source != AON_SRC_RTC) bits |= REC_RTC_DUE;
    write_record(bits | REC_SET | (uint32_t)source << REC_SOURCE_LSB);
}

const char *aon_start(void) {
    int64_t ms, s;
    if (aon_get(&ms)) {
        aon_rtc_sync();
        return NULL;
    }
    uint32_t bits = record();
    if (bits & REC_RTC_READ) return NULL;
    write_record(bits | REC_RTC_READ);
    switch (ds3231_read(&s)) {
    case DS3231_OK:
        /* The chip gives whole seconds: the middle of one is at most half
         * a second out. */
        aon_set(s * 1000 + 500, AON_SRC_RTC);
        return NULL;
    case DS3231_STOPPED:
    case DS3231_BAD:
        return "the DS3231 lost its time";
    default:
        return NULL;
    }
}

void aon_rtc_sync(void) {
    int64_t ms;
    if (!(record() & REC_RTC_DUE) || !aon_get(&ms)) return;
    /* Writing the seconds starts the chip's second, so the nearest one is
     * at most half a second out; waiting for the next would be exact, but
     * holds up the prompt for up to a second, chip or not. */
    ds3231_status_t st = ds3231_write((ms + 500) / 1000);
    /* A stuck bus may clear; anything else will not be better next time. */
    if (st != DS3231_TIMEOUT) write_record(record() & ~REC_RTC_DUE);
}

aon_source_t aon_source(void) {
    int64_t ms;
    if (!aon_get(&ms)) return AON_SRC_NONE;
    return (aon_source_t)((record() & REC_SOURCE) >> REC_SOURCE_LSB);
}

const char *aon_source_name(aon_source_t source) {
    switch (source) {
    case AON_SRC_USER:    return "set by hand";
    case AON_SRC_RTC:     return "from the RTC";
    case AON_SRC_NTP:     return "from NTP";
    case AON_SRC_PROGRAM: return "set by a program";
    default:              return "not set";
    }
}
