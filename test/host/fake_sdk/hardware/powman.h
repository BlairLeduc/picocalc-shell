/* hardware/powman.h on the host: the AON timer and the scratch registers
 * src/port/clock.c uses, kept in fake_ds3231.c. */
#ifndef SHELL_FAKE_POWMAN_H
#define SHELL_FAKE_POWMAN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t scratch[8];
} powman_hw_t;

extern powman_hw_t fake_powman;
#define powman_hw (&fake_powman)

bool powman_timer_is_running(void);
uint64_t powman_timer_get_ms(void);
void powman_timer_set_ms(uint64_t ms);
void powman_timer_start(void);
void powman_timer_set_1khz_tick_source_xosc(void);

#endif /* SHELL_FAKE_POWMAN_H */
