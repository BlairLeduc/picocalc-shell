/* sd.h — the SD card on spi0, in SPI mode (hardware-notes.md §7.1).
 *
 * Copied from pico-oric's src/port/sd.h (THIRD-PARTY.md). Every wait is
 * bounded, the longest at 1 s, below the southbridge's 2.5 s (design.md
 * §4.2); a card that stops answering returns an error rather than
 * hanging the machine.
 */
#ifndef SHELL_SD_H
#define SHELL_SD_H

#include <stdbool.h>
#include <stdint.h>

#define SD_BLOCK 512u

typedef enum {
    SD_OK       = 0,
    SD_NO_CARD  = -1,   /* card detect (GP22) says the slot is empty      */
    SD_NO_REPLY = -2,   /* a command or data token never came             */
    SD_REJECTED = -3,   /* the card answered with an error                */
    SD_UNUSABLE = -4,   /* voltage or version this driver does not handle */
} sd_status_t;

/* Is a card in the slot? GP22, active low, internal pull-up. */
bool sd_present(void);

/* Initialise at 400 kHz, then switch to the operating rate. On success
 * *hz is the rate configured — a configured figure, not one measured on
 * SCK — and *high_capacity says SDHC/SDXC block addressing. */
sd_status_t sd_init(uint32_t *hz, bool *high_capacity);

sd_status_t sd_read(uint32_t block, uint8_t *buf);
sd_status_t sd_write(uint32_t block, const uint8_t *buf);

/* count blocks from block on, in one command (CMD18, CMD25) when there
 * is more than one. A transfer is given 1 s before it is stopped, so a
 * caller keeps each to tens of KB; a write is on the card when it
 * returns. */
sd_status_t sd_read_blocks(uint32_t block, uint8_t *buf, uint32_t count);
sd_status_t sd_write_blocks(uint32_t block, const uint8_t *buf, uint32_t count);

/* The card's size in blocks of SD_BLOCK, from its CSD (CMD9). */
sd_status_t sd_sectors(uint32_t *count);

#endif /* SHELL_SD_H */
