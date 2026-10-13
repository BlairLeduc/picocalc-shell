/* usbdisk.c — the card over USB mass storage (usbdisk.h).
 *
 * The MSC callbacks follow UF2 Loader's ui/lib/usb_msc/usb_msc.c
 * (THIRD-PARTY.md). Two changes: an eject from the computer (START STOP
 * UNIT with LoEj) ends the run, and from then on the unit reports no
 * medium, so the computer does not mount it again before the shell
 * disconnects; and a card taken out ends it too. */

#include "usbdisk.h"

#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "sd.h"

static uint32_t s_sectors;
static volatile bool s_ejected;
static bool s_failed;     /* a block could not be read or written */

const char *usbdisk_run(bool (*stop)(void *ctx), void *ctx) {
    static bool started;
    if (!sd_present()) return "no card";
    if (sd_init(NULL, NULL) != SD_OK || sd_sectors(&s_sectors) != SD_OK)
        return "the card does not answer";
    s_ejected = false;
    s_failed = false;
    if (!started) {
        tud_init(BOARD_TUD_RHPORT);
        started = true;
    }
    tud_connect();
    const char *why = NULL;
    while (!s_ejected) {
        tud_task();
        if (stop(ctx)) break;
        if (!sd_present()) {
            why = "the card was taken out";
            break;
        }
    }
    tud_disconnect();
    /* Let the bus see the disconnect before anything else uses the card. */
    for (int i = 0; i < 10; i++) {
        tud_task();
        sleep_ms(1);
    }
    if (!why && s_failed) why = "the card failed a read or a write while on USB";
    return why;
}

/* ---- TinyUSB's MSC callbacks ------------------------------------------ */

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                        uint8_t product_rev[4]) {
    (void)lun;
    /* As the loader's, so the computer sees the same disk. */
    memcpy(vendor_id, "PICO    ", 8);
    memcpy(product_id, "UF2LOADER_MSC   ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (s_ejected || !sd_present()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);   /* medium not present */
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = s_sectors;
    *block_size = SD_BLOCK;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    (void)power_condition;
    if (load_eject && !start) s_ejected = true;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer,
                          uint32_t bufsize) {
    (void)lun;
    (void)offset;   /* bufsize is one block, so offset is always 0 */
    for (uint32_t i = 0; i < bufsize / SD_BLOCK; i++) {
        if (sd_read(lba + i, (uint8_t *)buffer + i * SD_BLOCK) != SD_OK) {
            s_failed = true;
            return -1;
        }
    }
    return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer,
                           uint32_t bufsize) {
    (void)lun;
    (void)offset;
    for (uint32_t i = 0; i < bufsize / SD_BLOCK; i++) {
        if (sd_write(lba + i, buffer + i * SD_BLOCK) != SD_OK) {
            s_failed = true;
            return -1;
        }
    }
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)scsi_cmd;
    (void)buffer;
    (void)bufsize;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);   /* invalid command */
    return -1;
}
