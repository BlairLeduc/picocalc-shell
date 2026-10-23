/* hardware/i2c.h on the host: the calls src/port/ds3231.c makes, over the
 * chip's model in fake_ds3231.c. */
#ifndef SHELL_FAKE_I2C_H
#define SHELL_FAKE_I2C_H

#include "pico/stdlib.h"

#define PICO_ERROR_GENERIC (-1)
#define PICO_ERROR_TIMEOUT (-2)

typedef struct i2c_inst i2c_inst_t;
#define i2c0 ((i2c_inst_t *)0)

unsigned i2c_init(i2c_inst_t *i2c, unsigned baud);
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len,
                         bool nostop, unsigned timeout_us);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop,
                        unsigned timeout_us);

#endif /* SHELL_FAKE_I2C_H */
