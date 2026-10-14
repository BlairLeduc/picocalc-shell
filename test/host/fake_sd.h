/* fake_sd.h — the host's model of an SD card in SPI mode, behind the
 * fake SDK in fake_sdk/, for test_sd. */
#ifndef SHELL_FAKE_SD_H
#define SHELL_FAKE_SD_H

#include <stdbool.h>
#include <stdint.h>

#define FAKE_SD_BLOCKS 256u
#define FAKE_SD_NONE   0xFFFFFFFFu

typedef struct {
    bool sdsc;               /* a v1 card with byte addresses, not SDHC        */
    uint32_t busy_us;        /* busy after each block written                   */
    uint32_t read_us;        /* before each block's data token                  */
    uint32_t stop_busy_us;   /* busy after CMD12 or CMD25's stop token          */
    uint32_t read_error;     /* this block's read gives an error token          */
    uint32_t write_error;    /* this block's write gives a write-error response */
    bool stuck;              /* busy for ever after the next block written      */
} fake_sd_card_t;

/* What the card does, set by the test; fake_sd_reset puts in defaults
 * (an SDHC card, a few µs busy, 200 µs after a stop, no errors) and blanks the blocks. */
extern fake_sd_card_t fake_sd;

void fake_sd_reset(void);

/* The card's blocks. */
extern uint8_t fake_sd_data[FAKE_SD_BLOCKS][512];

/* Commands the card took, by index; faults are breaks of the protocol:
 * a command, token or deselect at a moment the card does not allow. */
extern unsigned fake_sd_commands[64];
extern unsigned long fake_sd_faults;

/* Is the card between commands, with nothing streaming, awaited or
 * still being written? */
bool fake_sd_idle(void);

/* Time, in µs. Each byte on the bus takes one. */
extern uint64_t fake_sd_now;

#endif /* SHELL_FAKE_SD_H */
