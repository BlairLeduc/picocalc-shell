/* diskio.c — FatFs's disk layer, over sd.c (design.md §4.2).
 *
 * Copied from pico-oric's src/port/diskio.c (THIRD-PARTY.md); card.h's
 * poll is added, since the shell keeps one mount for as long as the card
 * stays in, and FatFs's runs of sectors go to the card as one transfer.
 */

#include <stddef.h>

#include "ff.h"
#include "diskio.h"

#include "card.h"
#include "sd.h"

static DSTATUS s_status = STA_NOINIT;

/* A transfer that failed may be a card pulled mid-job: the drive is
 * uninitialised until disk_initialize runs sd_init again, so FatFs never
 * reuses a mount across it. */
static DRESULT failed(void) {
    s_status |= STA_NOINIT;
    return RES_ERROR;
}

/* Card detect bounces as a card goes in or out (hardware-notes.md §7.1),
 * so any reading of "out" marks the drive uninitialised; FatFs then
 * mounts afresh at the next access, and a mount that fails because the
 * card was not yet in is simply tried again at the one after. */
void card_poll(void) {
    if (!sd_present()) s_status |= STA_NODISK | STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    card_poll();
    return s_status;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    s_status = (sd_init(NULL, NULL) == SD_OK) ? 0 : STA_NOINIT;
    if (!sd_present()) s_status |= STA_NODISK;
    return s_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (s_status & STA_NOINIT) return RES_NOTRDY;
    if (sd_read_blocks((uint32_t)sector, buff, count) != SD_OK) return failed();
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (s_status & STA_NOINIT) return RES_NOTRDY;
    if (sd_write_blocks((uint32_t)sector, buff, count) != SD_OK) return failed();
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)buff;
    if (pdrv != 0) return RES_PARERR;
    if (s_status & STA_NOINIT) return RES_NOTRDY;
    /* Writes are on the card before sd_write_blocks returns, so there is
     * nothing to flush; mkfs and trim, which want the rest, are off. */
    return cmd == CTRL_SYNC ? RES_OK : RES_PARERR;
}
