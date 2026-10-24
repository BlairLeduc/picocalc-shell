/* fake_ds3231.c — the DS3231 and POWMAN models (fake_ds3231.h). */

#include "fake_ds3231.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/powman.h"

#define ADDR       0x68u
#define REG_STATUS 0x0Fu

fake_rtc_t  fake_rtc;
powman_hw_t fake_powman;
uint64_t    fake_aon_ms;
bool        fake_aon_running;

static uint8_t s_ptr;   /* the chip's register pointer */

void fake_rtc_reset(void) {
    memset(&fake_rtc, 0, sizeof fake_rtc);
    fake_rtc.present = true;
    fake_rtc.reg[0x0E] = 0x1C;
    fake_rtc.reg[REG_STATUS] = 0x88;   /* OSF, EN32kHz */
    s_ptr = 0;
}

void fake_power_cycle(void) {
    memset(&fake_powman, 0, sizeof fake_powman);
    fake_aon_ms = 0;
    fake_aon_running = false;
}

/* ---- I2C -------------------------------------------------------------- */

unsigned i2c_init(i2c_inst_t *i2c, unsigned baud) {
    (void)i2c;
    return baud;
}

static void put_reg(uint8_t r, uint8_t v) {
    if (r == REG_STATUS) {
        /* OSF, A2F and A1F are only cleared; BSY is read-only. */
        uint8_t old = fake_rtc.reg[r];
        fake_rtc.reg[r] = (uint8_t)((old & v & 0x83) | (v & 0x08) | (old & 0x04));
        return;
    }
    if (r == 0) {
        fake_rtc.time_writes++;
        fake_rtc.written_at_ms = fake_aon_ms;
    }
    fake_rtc.reg[r] = v;
}

int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len,
                         bool nostop, unsigned timeout_us) {
    (void)i2c;
    (void)nostop;
    (void)timeout_us;
    fake_rtc.transfers++;
    if (fake_rtc.stuck) return PICO_ERROR_TIMEOUT;
    if (addr != ADDR || !fake_rtc.present) return PICO_ERROR_GENERIC;
    if (len == 0) return 0;
    s_ptr = src[0] % FAKE_RTC_REGS;
    for (size_t i = 1; i < len; i++) {
        put_reg(s_ptr, src[i]);
        s_ptr = (uint8_t)((s_ptr + 1) % FAKE_RTC_REGS);
    }
    return (int)len;
}

int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop,
                        unsigned timeout_us) {
    (void)i2c;
    (void)nostop;
    (void)timeout_us;
    fake_rtc.transfers++;
    if (fake_rtc.stuck) return PICO_ERROR_TIMEOUT;
    if (addr != ADDR || !fake_rtc.present) return PICO_ERROR_GENERIC;
    for (size_t i = 0; i < len; i++) {
        dst[i] = fake_rtc.reg[s_ptr];
        s_ptr = (uint8_t)((s_ptr + 1) % FAKE_RTC_REGS);
    }
    return (int)len;
}

void gpio_set_function(unsigned gpio, int fn) {
    (void)gpio;
    (void)fn;
}

void gpio_pull_up(unsigned gpio) {
    (void)gpio;
}

/* ---- POWMAN and time -------------------------------------------------- */

void sleep_ms(uint32_t ms) {
    if (fake_aon_running) fake_aon_ms += ms;
}

bool powman_timer_is_running(void) {
    return fake_aon_running;
}

uint64_t powman_timer_get_ms(void) {
    return fake_aon_ms;
}

void powman_timer_set_ms(uint64_t ms) {
    fake_aon_ms = ms;
}

void powman_timer_start(void) {
    fake_aon_running = true;
}

void powman_timer_set_1khz_tick_source_xosc(void) {
}
