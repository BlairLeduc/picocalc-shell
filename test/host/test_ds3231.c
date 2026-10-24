/* test_ds3231.c — the DS3231 (src/port/ds3231.c) and when the shell reads
 * and writes it (src/port/clock.c), against a model of the chip and of
 * POWMAN (fake_ds3231.c; design.md §3.8, M12 step 3): a chip present,
 * absent or with its bus stuck; OSF set, and said once per power-on;
 * written when the time is set, and at the next start after NTP set it;
 * its registers in BCD, in either form of the hours and either century;
 * and NTP's result in the record (M12 step 4). */

#include <string.h>

#include "aon.h"
#include "clock.h"
#include "ds3231.h"
#include "fake_ds3231.h"
#include "ntp.h"
#include "test_util.h"

/* A moment in UTC, in seconds. */
static int64_t utc(int y, int mo, int d, int h, int mi, int s) {
    return clock_days(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

/* Put a time in the chip's registers, 24-hour form, OSF clear. */
static void chip_holds(int yy, int mo, int d, int h, int mi, int s) {
    const uint8_t r[7] = { (uint8_t)(s / 10 << 4 | s % 10), (uint8_t)(mi / 10 << 4 | mi % 10),
                           (uint8_t)(h / 10 << 4 | h % 10), 1,
                           (uint8_t)(d / 10 << 4 | d % 10), (uint8_t)(mo / 10 << 4 | mo % 10),
                           (uint8_t)(yy / 10 << 4 | yy % 10) };
    memcpy(fake_rtc.reg, r, sizeof r);
    fake_rtc.reg[0x0F] &= 0x7F;
}

static bool is_set(int64_t *ms) {
    int64_t dummy;
    return aon_get(ms ? ms : &dummy);
}

static int test_absent(void) {
    fake_rtc_reset();
    fake_power_cycle();
    fake_rtc.present = false;
    CHECK(aon_start() == NULL && !is_set(NULL) && aon_source() == AON_SRC_NONE, "absent");
    unsigned n = fake_rtc.transfers;
    CHECK(aon_start() == NULL && fake_rtc.transfers == n, "not asked again: %u", fake_rtc.transfers - n);

    /* Setting the time with no chip is no trouble, and not tried again. */
    aon_set(utc(2026, 10, 22, 18, 30, 0) * 1000, AON_SRC_USER);
    aon_rtc_sync();
    n = fake_rtc.transfers;
    CHECK(aon_start() == NULL && fake_rtc.transfers == n, "no chip, not due");

    fake_rtc_reset();
    fake_power_cycle();
    fake_rtc.stuck = true;
    CHECK(aon_start() == NULL && !is_set(NULL), "stuck");
    return 0;
}

static int test_lost(void) {
    fake_rtc_reset();   /* a new chip: OSF set */
    fake_power_cycle();
    const char *note = aon_start();
    CHECK(note && strcmp(note, "the DS3231 lost its time") == 0, "OSF: %s", note ? note : "NULL");
    CHECK(!is_set(NULL), "not set");
    /* Back from a program, or reboot: the scratch registers kept. */
    unsigned n = fake_rtc.transfers;
    CHECK(aon_start() == NULL && fake_rtc.transfers == n, "said once");

    /* Registers that are not a time are as good as lost. */
    fake_rtc_reset();
    fake_power_cycle();
    chip_holds(26, 2, 30, 12, 0, 0);
    CHECK(aon_start() != NULL && !is_set(NULL), "30 February");
    fake_power_cycle();
    chip_holds(26, 10, 22, 12, 0, 0);
    fake_rtc.reg[0] = 0x5A;
    CHECK(aon_start() != NULL && !is_set(NULL), "BCD");
    return 0;
}

static int test_read(void) {
    fake_rtc_reset();
    fake_power_cycle();
    chip_holds(26, 10, 22, 18, 30, 5);
    int64_t ms;
    CHECK(aon_start() == NULL && is_set(&ms) && ms == utc(2026, 10, 22, 18, 30, 5) * 1000 + 500,
          "read: %lld", (long long)ms);
    CHECK(aon_source() == AON_SRC_RTC, "from the RTC: %d", aon_source());
    CHECK(fake_rtc.time_writes == 0, "not written back");

    /* With the clock set, a start does not read the chip. */
    unsigned n = fake_rtc.transfers;
    CHECK(aon_start() == NULL && fake_rtc.transfers == n, "set: %u", fake_rtc.transfers - n);

    /* The 12-hour form: 12 AM, 12 PM, 1 PM. */
    static const struct {
        uint8_t reg;
        int     hour;
    } twelve[] = { { 0x52, 0 }, { 0x72, 12 }, { 0x61, 13 }, { 0x49, 9 } };
    for (size_t i = 0; i < sizeof twelve / sizeof twelve[0]; i++) {
        int64_t s;
        fake_rtc.reg[2] = twelve[i].reg;
        CHECK(ds3231_read(&s) == DS3231_OK && s == utc(2026, 10, 22, twelve[i].hour, 30, 5),
              "0x%02x: %lld", twelve[i].reg, (long long)s);
    }
    fake_rtc.reg[2] = 0x40;   /* hour 0 in the 12-hour form */
    int64_t s;
    CHECK(ds3231_read(&s) == DS3231_BAD, "12-hour 0");
    return 0;
}

static int test_write(void) {
    fake_rtc_reset();
    fake_power_cycle();
    fake_rtc.reg[0x0E] |= 0x80;   /* EOSC: stopped on the battery */
    CHECK(aon_start() != NULL, "lost");

    /* date, a quarter of the way into a second: written at once, to the
     * nearest second. */
    int64_t t = utc(2026, 10, 22, 18, 30, 0);
    aon_set(t * 1000 + 250, AON_SRC_USER);
    aon_rtc_sync();
    CHECK(fake_rtc.time_writes == 1 && fake_rtc.written_at_ms == (uint64_t)t * 1000 + 250,
          "written at %llu", (unsigned long long)fake_rtc.written_at_ms);
    static const uint8_t want[7] = { 0x00, 0x30, 0x18, 5, 0x22, 0x10, 0x26 };   /* Thursday */
    CHECK(memcmp(fake_rtc.reg, want, sizeof want) == 0, "registers %02x %02x %02x %02x %02x %02x %02x",
          fake_rtc.reg[0], fake_rtc.reg[1], fake_rtc.reg[2], fake_rtc.reg[3], fake_rtc.reg[4],
          fake_rtc.reg[5], fake_rtc.reg[6]);
    CHECK(fake_rtc.reg[0x0F] == 0x08, "OSF cleared, EN32kHz kept: 0x%02x", fake_rtc.reg[0x0F]);
    CHECK(fake_rtc.reg[0x0E] == 0x1C, "EOSC cleared: 0x%02x", fake_rtc.reg[0x0E]);
    CHECK(aon_source() == AON_SRC_USER, "by hand");
    unsigned n = fake_rtc.time_writes;
    CHECK(aon_start() == NULL && fake_rtc.time_writes == n, "not written again");

    /* Power off and on: the time comes back from the chip. */
    fake_power_cycle();
    int64_t ms;
    CHECK(aon_start() == NULL && is_set(&ms) && ms == t * 1000 + 500 &&
              aon_source() == AON_SRC_RTC,
          "survives: %lld", (long long)ms);

    /* Either century, a leap day, and the years it cannot hold. */
    static const int64_t ok[] = { 946684800, 4107542399, 4107542400, 7258118399 };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        int64_t s;
        CHECK(ds3231_write(ok[i]) == DS3231_OK && ds3231_read(&s) == DS3231_OK && s == ok[i],
              "%lld: %lld", (long long)ok[i], (long long)s);
    }
    CHECK(fake_rtc.reg[5] == 0x80 + 0x12, "century bit: 0x%02x", fake_rtc.reg[5]);
    int64_t s;
    t = utc(2028, 2, 29, 23, 59, 59);
    CHECK(ds3231_write(t) == DS3231_OK && ds3231_read(&s) == DS3231_OK && s == t, "29 February");
    CHECK(ds3231_write(946684799) == DS3231_BAD && ds3231_write(7258118400) == DS3231_BAD,
          "1999 and 2200");
    return 0;
}

/* ntp.uf2 and programs set the clock with no I2C driver: the chip is
 * written when the shell next starts, once. */
static int test_marked(void) {
    fake_rtc_reset();
    fake_power_cycle();
    chip_holds(26, 10, 22, 18, 30, 5);
    CHECK(aon_start() == NULL && is_set(NULL), "from the chip");

    int64_t t = utc(2026, 10, 22, 18, 31, 0);
    aon_set(t * 1000 + 600, AON_SRC_NTP);   /* as ntp.uf2 would */
    CHECK(fake_rtc.time_writes == 0, "not by the program");
    CHECK(aon_start() == NULL && fake_rtc.time_writes == 1 &&
              fake_rtc.written_at_ms == (uint64_t)t * 1000 + 600 && fake_rtc.reg[0] == 0x01 &&
              fake_rtc.reg[1] == 0x31,
          "at the next start: %u", fake_rtc.time_writes);
    CHECK(aon_source() == AON_SRC_NTP, "still from NTP");
    CHECK(aon_start() == NULL && fake_rtc.time_writes == 1, "once");

    /* A stuck bus leaves it due, for the next start. */
    aon_set(t * 1000, AON_SRC_PROGRAM);
    fake_rtc.stuck = true;
    CHECK(aon_start() == NULL && fake_rtc.time_writes == 1, "stuck");
    fake_rtc.stuck = false;
    CHECK(aon_start() == NULL && fake_rtc.time_writes == 2, "then written");
    return 0;
}

/* What NTP did, kept beside the clock until power-off. */
static int test_ntp_record(void) {
    fake_rtc_reset();
    fake_rtc.present = false;
    fake_power_cycle();
    CHECK(aon_ntp() == -1, "not tried: %d", aon_ntp());
    aon_ntp_keep(NTP_UNFINISHED);
    CHECK(aon_ntp() == NTP_UNFINISHED && !is_set(NULL), "unfinished: %d", aon_ntp());

    /* As ntp.uf2 leaves it. */
    int64_t t = utc(2026, 10, 22, 18, 30, 0) * 1000;
    aon_set(t, AON_SRC_NTP);
    aon_ntp_keep(NTP_SET);
    int64_t ms;
    CHECK(aon_ntp() == NTP_SET && is_set(&ms) && ms == t && aon_source() == AON_SRC_NTP,
          "set: %d", aon_ntp());
    CHECK(aon_start() == NULL && aon_ntp() == NTP_SET && is_set(NULL), "kept by a start");
    aon_ntp_keep(NTP_NO_REPLY);
    CHECK(aon_ntp() == NTP_NO_REPLY && is_set(NULL) && aon_source() == AON_SRC_NTP,
          "the largest: %d", aon_ntp());
    aon_ntp_keep(-1);
    CHECK(aon_ntp() == -1 && is_set(NULL), "forgotten, the clock kept");
    aon_ntp_keep(NTP_STOPPED);
    fake_power_cycle();
    CHECK(aon_ntp() == -1, "power-off: %d", aon_ntp());
    return 0;
}

int main(void) {
    if (test_absent()) return 1;
    if (test_lost()) return 1;
    if (test_read()) return 1;
    if (test_write()) return 1;
    if (test_marked()) return 1;
    if (test_ntp_record()) return 1;
    TEST_DONE();
}
