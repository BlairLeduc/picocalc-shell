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
 *              writing
 *   scratch 2  ~(scratch 0 ^ scratch 1), as a check
 *
 * The clock is set when the magic, the check and `set` agree and the
 * timer is running. */

#include "aon.h"

#include "hardware/powman.h"

#define AON_MAGIC      0x434c4b31u   /* "CLK1" */
#define REC_SOURCE_LSB 16u
#define REC_SOURCE     (7u << REC_SOURCE_LSB)
#define REC_SET        (1u << 19)

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
    write_record(bits | REC_SET | (uint32_t)source << REC_SOURCE_LSB);
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
