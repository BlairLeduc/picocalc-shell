/* fake_sd.c — an SD card in SPI mode, byte by byte, and the fake SDK
 * calls that reach it (fake_sdk/), for test_sd.
 *
 * Follows the SD Physical Layer Simplified Specification's SPI mode as
 * sd.c uses it: CMD0, CMD8, CMD55 and ACMD41, CMD58, CMD16; CMD17 and
 * CMD24 for one block; CMD18, streaming blocks until CMD12, whose R1
 * comes after a stuff byte; CMD25, a 0xFC token before each block and
 * 0xFD to stop. A written block's data response is followed by busy
 * (MISO low), as is CMD12's R1 and the stop token. What the card is
 * sent while it is busy is ignored and counted as a fault, as is a
 * command or token out of turn, or a deselect with a transfer open. */

#include "fake_sd.h"

#include <string.h>

#include "pico/stdlib.h"

fake_sd_card_t fake_sd;
uint8_t fake_sd_data[FAKE_SD_BLOCKS][512] = {{0}};
unsigned fake_sd_commands[64];
unsigned long fake_sd_faults;
uint64_t fake_sd_now;

#define PIN_CS 17u
#define PIN_CD 22u
#define FOREVER UINT64_MAX

enum mode { IDLE, READ, WRITE_ONE, WRITE_MANY, RECEIVE };

static struct {
    bool selected, ready, app;
    enum mode mode;
    uint8_t q[600];            /* what the card will put on MISO next */
    unsigned qh, qt;
    uint64_t busy_until;       /* MISO low until then, once q is empty */
    uint64_t busy_after;       /* busy to start when q empties         */
    uint8_t cmd[6];
    unsigned cmd_n;
    uint32_t lba;              /* the next block read or written */
    uint32_t read_left;        /* blocks still to stream          */
    uint64_t next_block_at;
    bool halted;               /* a read stopped by its error token */
    bool single;               /* READ: CMD17, which ends by itself */
    bool many;                 /* RECEIVE: a block of CMD25 */
    uint8_t block[514];
    unsigned block_n;
} c;

void fake_sd_reset(void) {
    memset(&c, 0, sizeof c);
    memset(&fake_sd, 0, sizeof fake_sd);
    fake_sd.busy_us = 5;
    fake_sd.read_us = 5;
    fake_sd.stop_busy_us = 200;
    fake_sd.read_error = FAKE_SD_NONE;
    fake_sd.write_error = FAKE_SD_NONE;
    memset(fake_sd_data, 0, sizeof fake_sd_data);
    memset(fake_sd_commands, 0, sizeof fake_sd_commands);
    fake_sd_faults = 0;
}

void fake_sd_busy_for(uint32_t us) {
    c.busy_until = fake_sd_now + us;
}

bool fake_sd_idle(void) {
    return c.mode == IDLE && c.cmd_n == 0 && c.qh == c.qt && c.busy_after == 0 &&
           fake_sd_now >= c.busy_until;
}

static void put(uint8_t b) {
    c.q[c.qt++] = b;
}

static bool busy(void) {
    return c.qh == c.qt && fake_sd_now < c.busy_until;
}

static void queue_block(void) {
    if (c.lba == fake_sd.read_error || c.lba >= FAKE_SD_BLOCKS) {
        put(0x08);    /* error token: out of range or a card error */
        c.halted = true;
        return;
    }
    put(0xFE);
    for (unsigned i = 0; i < 512; i++) put(fake_sd_data[c.lba][i]);
    put(0xAA);    /* CRC, which SPI mode does not check */
    put(0x55);
    c.lba++;
    c.read_left--;
}

static uint8_t out(void) {
    if (c.qh == c.qt && c.mode == READ && !c.halted && c.read_left > 0 &&
        fake_sd_now >= c.next_block_at) {
        c.qh = c.qt = 0;
        queue_block();
    }
    if (c.qh != c.qt) {
        uint8_t b = c.q[c.qh++];
        if (c.qh == c.qt) {
            c.qh = c.qt = 0;
            if (c.busy_after) {
                c.busy_until = c.busy_after == FOREVER ? FOREVER : fake_sd_now + c.busy_after;
                c.busy_after = 0;
            }
            if (c.mode == READ) {
                c.next_block_at = fake_sd_now + fake_sd.read_us;
                if (c.single && (c.read_left == 0 || c.halted)) c.mode = IDLE;
            }
        }
        return b;
    }
    return busy() ? 0x00 : 0xFF;
}

static uint32_t address(uint32_t arg) {
    if (!fake_sd.sdsc) return arg;
    if (arg % 512u) fake_sd_faults++;
    return arg / 512u;
}

static void r1(uint8_t v) {
    put(0xFF);    /* NCR: a byte before the response */
    put(v);
}

static void command(uint8_t cmd, uint32_t arg) {
    if (busy()) fake_sd_faults++;
    fake_sd_commands[cmd]++;
    bool app = c.app;
    c.app = false;
    uint8_t idle = c.ready ? 0x00 : 0x01;

    if (c.mode == READ) {
        if (cmd != 12) fake_sd_faults++;
        c.qh = c.qt = 0;
        put(0x3C);    /* stuff byte: data cut short, high bit clear */
        put(0x00);
        c.busy_after = fake_sd.stop_busy_us;
        c.mode = IDLE;
        return;
    }
    switch (cmd) {
    case 0:
        c.ready = false;
        r1(0x01);
        break;
    case 8:
        if (fake_sd.sdsc) {
            r1(0x05);    /* idle, illegal command: a v1 card */
        } else {
            r1(idle);
            put(0x00); put(0x00); put(0x01); put(0xAA);
        }
        break;
    case 55:
        c.app = true;
        r1(idle);
        break;
    case 41:
        if (!app) { r1(idle | 0x04); break; }
        c.ready = true;
        r1(0x00);
        break;
    case 58:
        r1(idle);
        put(fake_sd.sdsc ? 0x80 : 0xC0); put(0xFF); put(0x80); put(0x00);
        break;
    case 16:
        r1(idle);
        break;
    case 17:
    case 18:
        r1(idle);
        c.mode = READ;
        c.lba = address(arg);
        c.single = cmd == 17;
        c.read_left = c.single ? 1u : UINT32_MAX;
        c.halted = false;
        c.next_block_at = fake_sd_now + fake_sd.read_us;
        break;
    case 24:
    case 25:
        r1(idle);
        c.mode = cmd == 24 ? WRITE_ONE : WRITE_MANY;
        c.lba = address(arg);
        break;
    case 12:
        fake_sd_faults++;    /* no transfer to stop */
        r1(0x04);
        break;
    default:
        r1(idle | 0x04);
        break;
    }
}

static void written(void) {
    if (c.lba == fake_sd.write_error || c.lba >= FAKE_SD_BLOCKS) {
        put(0x0D);    /* data response: write error */
    } else {
        memcpy(fake_sd_data[c.lba], c.block, 512);
        put(0x05);    /* data accepted */
    }
    c.lba++;
    c.busy_after = fake_sd.stuck ? FOREVER : fake_sd.busy_us;
    c.mode = c.many ? WRITE_MANY : IDLE;
}

static void take(uint8_t in) {
    switch (c.mode) {
    case RECEIVE:
        c.block[c.block_n++] = in;
        if (c.block_n == sizeof c.block) written();
        return;
    case WRITE_ONE:
    case WRITE_MANY:
        if (in == 0xFF) return;
        if (busy()) {
            fake_sd_faults++;
            return;
        }
        if (in == (c.mode == WRITE_ONE ? 0xFE : 0xFC)) {
            c.many = c.mode == WRITE_MANY;
            c.mode = RECEIVE;
            c.block_n = 0;
        } else if (c.mode == WRITE_MANY && in == 0xFD) {
            c.mode = IDLE;
            put(0xFF);    /* busy starts a byte after the stop token */
            c.busy_after = fake_sd.stop_busy_us;
        } else {
            fake_sd_faults++;
        }
        return;
    default:
        if (c.cmd_n == 0 && (in & 0xC0u) != 0x40u) return;
        c.cmd[c.cmd_n++] = in;
        if (c.cmd_n == 6) {
            c.cmd_n = 0;
            command(c.cmd[0] & 0x3Fu, (uint32_t)c.cmd[1] << 24 | (uint32_t)c.cmd[2] << 16 |
                                           (uint32_t)c.cmd[3] << 8 | c.cmd[4]);
        }
        return;
    }
}

static uint8_t exchange(uint8_t in) {
    fake_sd_now++;
    if (!c.selected) return 0xFF;
    uint8_t b = out();
    take(in);
    return b;
}

/* ---- the fake SDK -------------------------------------------------- */

absolute_time_t get_absolute_time(void) { return fake_sd_now; }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return fake_sd_now + (uint64_t)ms * 1000u; }
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)(to - from);
}
void busy_wait_us_32(uint32_t us) { fake_sd_now += us; }

unsigned spi_init(spi_inst_t *spi, unsigned baud) { (void)spi; return baud; }
unsigned spi_set_baudrate(spi_inst_t *spi, unsigned baud) { (void)spi; return baud; }
void spi_set_format(spi_inst_t *spi, unsigned bits, int cpol, int cpha, int order) {
    (void)spi; (void)bits; (void)cpol; (void)cpha; (void)order;
}

int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *src, uint8_t *dst, size_t len) {
    (void)spi;
    for (size_t i = 0; i < len; i++) dst[i] = exchange(src[i]);
    return (int)len;
}

int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len) {
    (void)spi;
    for (size_t i = 0; i < len; i++) exchange(src[i]);
    return (int)len;
}

int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx, uint8_t *dst, size_t len) {
    (void)spi;
    for (size_t i = 0; i < len; i++) dst[i] = exchange(repeated_tx);
    return (int)len;
}

void gpio_set_function(unsigned gpio, int fn) { (void)gpio; (void)fn; }
void gpio_pull_up(unsigned gpio) { (void)gpio; }
void gpio_init(unsigned gpio) { (void)gpio; }
void gpio_set_dir(unsigned gpio, int out) { (void)gpio; (void)out; }

void gpio_put(unsigned gpio, int value) {
    if (gpio != PIN_CS) return;
    bool selected = !value;
    if (c.selected && !selected && (c.mode != IDLE || c.cmd_n != 0)) fake_sd_faults++;
    c.selected = selected;
}

bool gpio_get(unsigned gpio) {
    return gpio == PIN_CD ? false : true;    /* card detect: active low, a card in */
}
