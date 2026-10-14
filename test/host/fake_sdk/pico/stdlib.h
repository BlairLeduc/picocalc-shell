/* pico/stdlib.h, hardware/spi.h and hardware/gpio.h on the host: the
 * parts src/port/sd.c uses, over the card model in fake_sd.c. Time is
 * the model's: it moves only as bytes cross the bus and as sd.c waits,
 * so every bound sd.c keeps can be checked exactly. */
#ifndef SHELL_FAKE_SDK_H
#define SHELL_FAKE_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t absolute_time_t;
absolute_time_t get_absolute_time(void);
absolute_time_t make_timeout_time_ms(uint32_t ms);
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to);
void busy_wait_us_32(uint32_t us);

typedef struct spi_inst spi_inst_t;
#define spi0 ((spi_inst_t *)0)
enum { SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST };
unsigned spi_init(spi_inst_t *spi, unsigned baud);
unsigned spi_set_baudrate(spi_inst_t *spi, unsigned baud);
void spi_set_format(spi_inst_t *spi, unsigned bits, int cpol, int cpha, int order);
int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *src, uint8_t *dst, size_t len);
int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len);
int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx, uint8_t *dst, size_t len);

enum { GPIO_FUNC_SPI };
enum { GPIO_IN = 0, GPIO_OUT = 1 };
void gpio_set_function(unsigned gpio, int fn);
void gpio_pull_up(unsigned gpio);
void gpio_init(unsigned gpio);
void gpio_set_dir(unsigned gpio, int out);
void gpio_put(unsigned gpio, int value);
bool gpio_get(unsigned gpio);

#endif /* SHELL_FAKE_SDK_H */
