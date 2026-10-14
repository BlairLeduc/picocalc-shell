/* sd.c — SD card in SPI mode on spi0 (hardware-notes.md §7.1).
 *
 * The SD Physical Layer Simplified Specification's SPI-mode sequence:
 * CMD0, CMD8 to tell a v2 card from a v1, ACMD41 until ready, CMD58 for
 * the capacity bit, CMD16 on byte-addressed cards. Single-block reads
 * and writes with CMD17 and CMD24; several blocks with CMD18 and CMD12,
 * and CMD25 and its stop token.
 *
 * Copied from pico-oric's src/port/sd.c (THIRD-PARTY.md), unchanged but
 * for comments and two additions: sd_sectors, CMD9 and the CSD, for
 * usbdisk, with the wait for a data token shared with sd_read; and the
 * multi-block transfers, for usbdisk's speed (design.md §10, M6).
 */

#include "sd.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define SD_SPI      spi0
#define SD_PIN_MISO 16
#define SD_PIN_CS   17
#define SD_PIN_SCK  18
#define SD_PIN_MOSI 19
#define SD_PIN_CD   22

#define SD_INIT_HZ  400000u     /* §7.1 */
#define SD_RUN_HZ   25000000u   /* §7.1: requested, not measured */

/* Bounds, all generous against the specification's own limits. */
#define SD_READY_US     500000u  /* card busy before a command, or after a write */
#define SD_TOKEN_US     200000u  /* data token after CMD17 (spec: 100 ms)       */
#define SD_ACMD41_US   1000000u  /* initialisation (spec: 1 s)                  */
#define SD_BLOCKS_US   1000000u  /* all of a multi-block transfer, before its stop */

#define R1_IDLE     0x01u
#define R1_ILLEGAL  0x04u

#define TOKEN_START 0xFEu   /* a block of CMD17, CMD18 or CMD24 */
#define TOKEN_MULTI 0xFCu   /* a block of CMD25 */
#define TOKEN_STOP  0xFDu   /* the end of CMD25 */

static bool s_hc;   /* block addressing */

static uint8_t xfer(uint8_t out) {
    uint8_t in;
    spi_write_read_blocking(SD_SPI, &out, &in, 1);
    return in;
}

static void select(void)   { gpio_put(SD_PIN_CS, 0); }

static void deselect(void) {
    gpio_put(SD_PIN_CS, 1);
    xfer(0xFF);    /* one more clock byte so the card releases MISO */
}

static bool before(absolute_time_t until) {
    return absolute_time_diff_us(get_absolute_time(), until) > 0;
}

/* The earlier of us from now and until. */
static absolute_time_t bound(uint32_t us, absolute_time_t until) {
    absolute_time_t t = make_timeout_time_ms(us / 1000u);
    return absolute_time_diff_us(t, until) < 0 ? until : t;
}

/* The card holds MISO low while busy. */
static bool wait_until(absolute_time_t until) {
    do {
        if (xfer(0xFF) == 0xFF) return true;
    } while (before(until));
    return false;
}

static bool wait_ready(uint32_t us) {
    return wait_until(make_timeout_time_ms(us / 1000u));
}

/* Send a command frame and return R1, or 0xFF if nothing answered. */
static uint8_t send(uint8_t cmd, uint32_t arg) {
    /* CRC is checked only for CMD0 and CMD8 in SPI mode; these are the
     * specification's fixed values for their fixed arguments. */
    uint8_t crc = (cmd == 0) ? 0x95u : (cmd == 8) ? 0x87u : 0x01u;
    uint8_t frame[6] = {
        (uint8_t)(0x40u | cmd),
        (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg,
        crc,
    };
    spi_write_blocking(SD_SPI, frame, sizeof frame);
    /* CMD12's R1 comes after a stuff byte, which may be the last of the
     * data it stopped and so look like an R1. */
    if (cmd == 12) xfer(0xFF);

    /* R1 arrives within eight bytes; its bit 7 is always clear. */
    for (int i = 0; i < 8; i++) {
        uint8_t r1 = xfer(0xFF);
        if (!(r1 & 0x80u)) return r1;
    }
    return 0xFF;
}

/* Select the card, wait while it is busy, and send a command. Leaves the
 * card selected: the caller reads any trailing bytes and deselects. */
static uint8_t command(uint8_t cmd, uint32_t arg) {
    select();
    if (cmd != 0 && !wait_ready(SD_READY_US)) return 0xFF;
    return send(cmd, arg);
}

static uint8_t app_command(uint8_t cmd, uint32_t arg) {
    uint8_t r1 = command(55, 0);
    deselect();
    if (r1 > R1_IDLE) return r1;
    return command(cmd, arg);
}

static void pins_init(void) {
    static bool done;
    if (done) return;
    done = true;

    spi_init(SD_SPI, SD_INIT_HZ);
    spi_set_format(SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_pull_up(SD_PIN_MISO);     /* an absent card must read 0xFF */

    gpio_init(SD_PIN_CS);
    gpio_put(SD_PIN_CS, 1);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);

    gpio_init(SD_PIN_CD);
    gpio_set_dir(SD_PIN_CD, GPIO_IN);
    gpio_pull_up(SD_PIN_CD);       /* §7.1: active low, needs the pull-up */
    busy_wait_us_32(10);           /* let the pull-up charge the line */
}

/* The pins are set up on first use, so card detect can be polled before
 * a card is ever initialised. */
bool sd_present(void) {
    pins_init();
    return !gpio_get(SD_PIN_CD);
}

sd_status_t sd_init(uint32_t *hz, bool *high_capacity) {
    pins_init();
    if (!sd_present()) return SD_NO_CARD;

    /* Clock rate is re-applied every time: clk_peri may have moved
     * (hardware-notes.md §3). */
    spi_set_baudrate(SD_SPI, SD_INIT_HZ);

    /* At least 74 clocks with CS and MOSI high puts the card in its
     * native mode, ready for CMD0 to select SPI. */
    gpio_put(SD_PIN_CS, 1);
    for (int i = 0; i < 10; i++) xfer(0xFF);

    uint8_t r1 = 0xFF;
    for (int tries = 0; tries < 10 && r1 != R1_IDLE; tries++) {
        r1 = command(0, 0);
        deselect();
    }
    if (r1 != R1_IDLE) return SD_NO_REPLY;

    /* CMD8: a v2 card echoes the check pattern; a v1 card calls it
     * illegal. */
    bool v2 = false;
    r1 = command(8, 0x1AAu);
    if (r1 == R1_IDLE) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = xfer(0xFF);
        deselect();
        if ((r7[2] & 0x0Fu) != 0x01u || r7[3] != 0xAAu) return SD_UNUSABLE;
        v2 = true;
    } else {
        deselect();
        if (!(r1 & R1_ILLEGAL)) return SD_REJECTED;
    }

    /* ACMD41 until the card leaves idle. HCS says we can take SDHC. */
    absolute_time_t until = make_timeout_time_ms(SD_ACMD41_US / 1000u);
    do {
        r1 = app_command(41, v2 ? 0x40000000u : 0);
        deselect();
        if (r1 == 0) break;
        if (r1 != R1_IDLE) return SD_REJECTED;   /* includes MMC, not handled */
    } while (absolute_time_diff_us(get_absolute_time(), until) > 0);
    if (r1 != 0) return SD_NO_REPLY;

    s_hc = false;
    if (v2) {
        /* CMD58: OCR bit 30 is CCS, block addressing. */
        r1 = command(58, 0);
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
        deselect();
        if (r1 != 0) return SD_REJECTED;
        s_hc = (ocr[0] & 0x40u) != 0;
    }
    if (!s_hc) {
        /* Byte-addressed cards: make the block length the sector size. */
        r1 = command(16, SD_BLOCK);
        deselect();
        if (r1 != 0) return SD_REJECTED;
    }

    uint32_t got = spi_set_baudrate(SD_SPI, SD_RUN_HZ);
    if (hz) *hz = got;
    if (high_capacity) *high_capacity = s_hc;
    return SD_OK;
}

static uint32_t address(uint32_t block) {
    return s_hc ? block : block * SD_BLOCK;
}

/* After a read command: the data token, then n bytes and the CRC. The
 * card stays selected. */
static sd_status_t read_block(uint8_t *buf, size_t n, absolute_time_t until) {
    uint8_t tok;
    do {
        tok = xfer(0xFF);
    } while (tok == 0xFF && before(until));
    if (tok != TOKEN_START) return tok == 0xFF ? SD_NO_REPLY : SD_REJECTED;

    spi_read_blocking(SD_SPI, 0xFF, buf, n);
    xfer(0xFF);    /* CRC, not checked in SPI mode */
    xfer(0xFF);
    return SD_OK;
}

static sd_status_t read_data(uint8_t *buf, size_t n) {
    sd_status_t st = read_block(buf, n, make_timeout_time_ms(SD_TOKEN_US / 1000u));
    deselect();
    return st;
}

sd_status_t sd_read(uint32_t block, uint8_t *buf) {
    uint8_t r1 = command(17, address(block));
    if (r1 != 0) {
        deselect();
        return r1 == 0xFF ? SD_NO_REPLY : SD_REJECTED;
    }
    return read_data(buf, SD_BLOCK);
}

/* CMD18 streams blocks until CMD12 stops it. The command and the
 * transfer are bounded by SD_BLOCKS_US from entry, then the stop by
 * SD_READY_US: 1.5 s at worst, so a card that slows down mid-transfer
 * still comes back below the southbridge's 2.5 s (design.md §4.2). */
sd_status_t sd_read_blocks(uint32_t block, uint8_t *buf, uint32_t count) {
    if (count == 1) return sd_read(block, buf);
    absolute_time_t until = make_timeout_time_ms(SD_BLOCKS_US / 1000u);
    uint8_t r1 = command(18, address(block));
    if (r1 != 0) {
        deselect();
        return r1 == 0xFF ? SD_NO_REPLY : SD_REJECTED;
    }
    sd_status_t st = SD_OK;
    for (uint32_t i = 0; i < count && st == SD_OK; i++)
        st = read_block(buf + i * SD_BLOCK, SD_BLOCK, bound(SD_TOKEN_US, until));
    r1 = send(12, 0);
    bool ready = wait_ready(SD_READY_US);
    deselect();
    if (st != SD_OK) return st;
    if (r1 != 0) return r1 == 0xFF ? SD_NO_REPLY : SD_REJECTED;
    return ready ? SD_OK : SD_NO_REPLY;
}

sd_status_t sd_sectors(uint32_t *count) {
    uint8_t r1 = command(9, 0);
    if (r1 != 0) {
        deselect();
        return r1 == 0xFF ? SD_NO_REPLY : SD_REJECTED;
    }
    uint8_t csd[16];
    sd_status_t st = read_data(csd, sizeof csd);
    if (st != SD_OK) return st;
    if ((csd[0] >> 6) == 1u) {
        /* CSD 2.0 (SDHC, SDXC): C_SIZE + 1 units of 512 KB. */
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        *count = (c_size + 1u) * 1024u;
    } else if ((csd[0] >> 6) == 0u) {
        /* CSD 1.0: (C_SIZE + 1) << (C_SIZE_MULT + 2) blocks of
         * 2^READ_BL_LEN bytes. */
        uint32_t bl_len = csd[5] & 0x0Fu;
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        uint32_t mult = ((uint32_t)(csd[9] & 0x03u) << 1) | (csd[10] >> 7);
        *count = ((c_size + 1u) << (mult + 2u)) << bl_len >> 9;
    } else {
        return SD_UNUSABLE;
    }
    return SD_OK;
}

sd_status_t sd_write(uint32_t block, const uint8_t *buf) {
    uint8_t r1 = command(24, address(block));
    if (r1 != 0) {
        deselect();
        return r1 == 0xFF ? SD_NO_REPLY : SD_REJECTED;
    }

    xfer(0xFF);
    xfer(TOKEN_START);
    spi_write_blocking(SD_SPI, buf, SD_BLOCK);
    xfer(0xFF);    /* dummy CRC */
    xfer(0xFF);

    /* Data response: xxx0sss1, where sss = 010 is accepted. */
    uint8_t resp = xfer(0xFF);
    if ((resp & 0x1Fu) != 0x05u) {
        deselect();
        return SD_REJECTED;
    }
    bool ok = wait_ready(SD_READY_US);    /* programming */
    deselect();
    return ok ? SD_OK : SD_NO_REPLY;
}

/* CMD25: each block behind its own token, then the stop token, after
 * which the card is busy until everything is written. Bounded as
 * sd_read_blocks is; the stop is sent whatever happened, so a card that
 * answers is never left mid-transfer. */
sd_status_t sd_write_blocks(uint32_t block, const uint8_t *buf, uint32_t count) {
    if (count == 1) return sd_write(block, buf);
    absolute_time_t until = make_timeout_time_ms(SD_BLOCKS_US / 1000u);
    uint8_t r1 = command(25, address(block));
    if (r1 != 0) {
        deselect();
        return r1 == 0xFF ? SD_NO_REPLY : SD_REJECTED;
    }
    sd_status_t st = SD_OK;
    for (uint32_t i = 0; i < count; i++) {
        xfer(0xFF);
        xfer(TOKEN_MULTI);
        spi_write_blocking(SD_SPI, buf + i * SD_BLOCK, SD_BLOCK);
        xfer(0xFF);    /* dummy CRC */
        xfer(0xFF);
        if ((xfer(0xFF) & 0x1Fu) != 0x05u) {
            st = SD_REJECTED;
            break;
        }
        if (!wait_until(bound(SD_READY_US, until))) {
            st = SD_NO_REPLY;
            break;
        }
    }
    /* A busy card ignores the stop token. With the transfer's time gone
     * (its command's ready wait included), the card gets SD_READY_US
     * more, and the stop as much again: 2 s from entry at worst. */
    if (st != SD_OK) wait_ready(SD_READY_US);
    xfer(TOKEN_STOP);
    xfer(0xFF);    /* busy starts a byte after the stop token */
    bool ready = wait_ready(SD_READY_US);
    deselect();
    if (st != SD_OK) return st;
    return ready ? SD_OK : SD_NO_REPLY;
}
